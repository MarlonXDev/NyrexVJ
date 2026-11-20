#include "core/ndi_receiver.hpp"

#include "core/ndi_runtime.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <thread>

namespace nvj {

namespace {

std::string fourCCText(uint32_t value) {
    std::string text;
    for (int i = 0; i < 4; ++i) {
        const char c = static_cast<char>((value >> (8 * i)) & 0xff);
        text += (c >= 32 && c < 127) ? c : '?';
    }
    return text;
}

bool containsOwnSender(const std::string& name, const std::string& excluded) {
    if (excluded.empty()) {
        return false;
    }
    return name.find("(" + excluded + ")") != std::string::npos;
}

}

bool pickNdiSource(const std::vector<NdiSourceInfo>& list, const std::string& wanted, const std::string& excluded, NdiSourceInfo& out) {
    if (!wanted.empty()) {
        for (const NdiSourceInfo& source : list) {
            if (source.name == wanted) {
                out = source;
                return true;
            }
        }
        return false;
    }
    for (const NdiSourceInfo& source : list) {
        if (source.name.empty() || containsOwnSender(source.name, excluded)) {
            continue;
        }
        out = source;
        return true;
    }
    return false;
}

struct NdiFinder::Impl {
    mutable std::mutex mutex;
    std::mutex lifecycle;
    ndi::Api api;
    std::thread thread;
    std::vector<NdiSourceInfo> list;
    std::string error;
    int users = 0;
    std::atomic<bool> stop{false};
    std::atomic<bool> alive{false};

    void run() {
        ndi::FindCreate create{};
        create.showLocalSources = true;
        create.groups = nullptr;
        create.extraIps = nullptr;
        void* find = api.findCreate(&create);
        if (find == nullptr) {
            std::lock_guard<std::mutex> lock(mutex);
            error = "NDI source search could not be started";
            alive = false;
            return;
        }
        while (!stop.load()) {
            api.findWait(find, 250);
            uint32_t count = 0;
            const ndi::Source* found = api.findSources(find, &count);
            std::vector<NdiSourceInfo> next;
            for (uint32_t i = 0; found != nullptr && i < count && i < 256; ++i) {
                if (found[i].name == nullptr) {
                    continue;
                }
                NdiSourceInfo info;
                info.name = found[i].name;
                info.url = found[i].url != nullptr ? found[i].url : "";
                next.push_back(std::move(info));
            }
            std::sort(next.begin(), next.end(), [](const NdiSourceInfo& a, const NdiSourceInfo& b) { return a.name < b.name; });
            std::lock_guard<std::mutex> lock(mutex);
            list = std::move(next);
        }
        api.findDestroy(find);
        alive = false;
    }
};

NdiFinder::NdiFinder() : impl_(std::make_unique<Impl>()) {}

NdiFinder::~NdiFinder() {
    impl_->stop = true;
    if (impl_->thread.joinable()) {
        impl_->thread.join();
    }
}

NdiFinder& NdiFinder::instance() {
    static NdiFinder finder;
    return finder;
}

bool NdiFinder::acquire() {
    std::lock_guard<std::mutex> life(impl_->lifecycle);
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (impl_->users > 0) {
        ++impl_->users;
        return true;
    }
    std::string problem;
    if (!ndi::loadForReceiving(impl_->api, problem)) {
        impl_->error = problem;
        return false;
    }
    impl_->error.clear();
    impl_->stop = false;
    impl_->alive = true;
    impl_->list.clear();
    impl_->users = 1;
    impl_->thread = std::thread([this]() { impl_->run(); });
    return true;
}

void NdiFinder::release() {
    std::lock_guard<std::mutex> life(impl_->lifecycle);
    std::thread worker;
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        if (impl_->users <= 0) {
            return;
        }
        --impl_->users;
        if (impl_->users > 0) {
            return;
        }
        impl_->stop = true;
        worker = std::move(impl_->thread);
    }
    if (worker.joinable()) {
        worker.join();
    }
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->list.clear();
}

std::vector<NdiSourceInfo> NdiFinder::sources() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->list;
}

bool NdiFinder::running() const {
    return impl_->alive.load();
}

std::string NdiFinder::error() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->error;
}

struct NdiReceiver::Impl {
    ndi::Api api;
    ReconnectPolicy policy;
    std::string wanted;
    std::string excluded;
    std::string error;
    std::thread thread;
    std::mutex wakeMutex;
    std::condition_variable wake;
    std::atomic<bool> stop{false};
    bool holdsFinder = false;
    LiveFrameBox box;
    std::atomic<unsigned long long> skipped{0};
    mutable std::mutex statusMutex;
    NdiReceiverStatus status;

    void publish(const LiveStateMachine& machine, const std::string& source, void* recv, double fps, unsigned long long frames, unsigned long long unsupported, const std::string& problem, int width, int height) {
        const int connections = recv != nullptr && api.recvConnections ? api.recvConnections(recv) : 0;
        std::lock_guard<std::mutex> lock(statusMutex);
        status.state = machine.state();
        status.source = source;
        status.error = problem;
        status.width = width;
        status.height = height;
        status.connections = connections;
        status.fps = fps;
        status.frames = frames;
        status.dropped = box.dropped() + skipped.load();
        status.unsupported = unsupported;
        status.attempts = machine.attempts();
    }

    bool convert(const ndi::VideoFrame& video, LiveFrame& out, double now) const {
        const int width = video.xres;
        const int height = video.yres;
        if (width <= 0 || height <= 0 || video.data == nullptr || video.stride <= 0 || width > 16384 || height > 16384) {
            return false;
        }
        size_t rowBytes = 0;
        if (video.fourCC == ndi::kFourCCUyvy || video.fourCC == ndi::kFourCCUyva) {
            if ((width & 1) != 0) {
                return false;
            }
            out.format = LiveFormat::Uyvy;
            rowBytes = static_cast<size_t>(width) * 2u;
        } else if (video.fourCC == ndi::kFourCCRgba) {
            out.format = LiveFormat::Rgba;
            rowBytes = static_cast<size_t>(width) * 4u;
        } else {
            return false;
        }
        if (static_cast<size_t>(video.stride) < rowBytes) {
            return false;
        }
        out.width = width;
        out.height = height;
        out.stride = static_cast<int>(rowBytes);
        out.capturedAt = now;
        out.data.resize(rowBytes * static_cast<size_t>(height));
        for (int y = 0; y < height; ++y) {
            std::memcpy(out.data.data() + static_cast<size_t>(y) * rowBytes, video.data + static_cast<size_t>(y) * static_cast<size_t>(video.stride), rowBytes);
        }
        return true;
    }

    void run() {
        LiveStateMachine machine(policy);
        machine.start(steadySeconds());
        void* recv = nullptr;
        std::string connected;
        std::string problem;
        LiveFrame frame;
        double fps = 0.0;
        double previousFrame = 0.0;
        unsigned long long frames = 0;
        unsigned long long unsupported = 0;
        unsigned long long serial = 0;
        int width = 0;
        int height = 0;
        const auto drop = [&]() {
            if (recv != nullptr) {
                api.recvDestroy(recv);
                recv = nullptr;
            }
        };
        while (!stop.load()) {
            const double now = steadySeconds();
            if (machine.update(now) == LiveStateMachine::Action::Reset) {
                drop();
            }
            publish(machine, connected.empty() ? wanted : connected, recv, fps, frames, unsupported, problem, width, height);
            if (recv == nullptr) {
                if (machine.attemptDue(now)) {
                    machine.beginAttempt(now);
                    NdiSourceInfo found;
                    if (!pickNdiSource(NdiFinder::instance().sources(), wanted, excluded, found)) {
                        machine.attemptFailed(now);
                    } else {
                        ndi::RecvCreate create{};
                        create.source.name = found.name.c_str();
                        create.source.url = found.url.empty() ? nullptr : found.url.c_str();
                        create.colorFormat = ndi::kColorUyvyRgba;
                        create.bandwidth = ndi::kBandwidthHighest;
                        create.allowVideoFields = false;
                        create.receiverName = "NyrexVJ";
                        recv = api.recvCreate(&create);
                        if (recv == nullptr) {
                            problem = "NDI receiver could not be created";
                            machine.attemptFailed(now);
                        } else {
                            connected = found.name;
                            problem.clear();
                            machine.attemptSucceeded(now);
                        }
                    }
                } else {
                    std::unique_lock<std::mutex> lock(wakeMutex);
                    wake.wait_for(lock, std::chrono::milliseconds(20), [this]() { return stop.load(); });
                }
                continue;
            }
            ndi::VideoFrame video{};
            const int type = api.recvCapture(recv, &video, nullptr, nullptr, 100);
            if (type == ndi::kFrameVideo) {
                const double arrived = steadySeconds();
                ndi::VideoFrame newest = video;
                for (;;) {
                    ndi::VideoFrame next{};
                    if (api.recvCapture(recv, &next, nullptr, nullptr, 0) != ndi::kFrameVideo) {
                        break;
                    }
                    api.recvFreeVideo(recv, &newest);
                    ++skipped;
                    newest = next;
                }
                if (convert(newest, frame, arrived)) {
                    frame.id = ++serial;
                    width = frame.width;
                    height = frame.height;
                    box.publish(frame);
                    ++frames;
                    if (previousFrame > 0.0 && arrived > previousFrame) {
                        const double instant = 1.0 / (arrived - previousFrame);
                        fps = fps <= 0.0 ? instant : fps * 0.9 + instant * 0.1;
                    }
                    previousFrame = arrived;
                    machine.frame(arrived);
                    problem.clear();
                } else {
                    ++unsupported;
                    problem = "Unsupported NDI pixel format " + fourCCText(newest.fourCC);
                }
                api.recvFreeVideo(recv, &newest);
            } else if (type == ndi::kFrameError) {
                drop();
                machine.connectionLost(steadySeconds());
                problem = "NDI connection error";
            }
        }
        drop();
        std::lock_guard<std::mutex> lock(statusMutex);
        status.state = LiveState::Idle;
    }
};

NdiReceiver::NdiReceiver() : impl_(std::make_unique<Impl>()) {}

NdiReceiver::~NdiReceiver() {
    stop();
}

bool NdiReceiver::start(const std::string& wanted, const std::string& excluded, const ReconnectPolicy& policy) {
    stop();
    impl_->error.clear();
    if (!ndi::loadForReceiving(impl_->api, impl_->error)) {
        return false;
    }
    if (!NdiFinder::instance().acquire()) {
        impl_->error = NdiFinder::instance().error();
        return false;
    }
    impl_->holdsFinder = true;
    impl_->policy = policy;
    impl_->wanted = wanted;
    impl_->excluded = excluded;
    impl_->stop = false;
    impl_->box.reset();
    impl_->skipped = 0;
    {
        std::lock_guard<std::mutex> lock(impl_->statusMutex);
        impl_->status = NdiReceiverStatus{};
        impl_->status.state = LiveState::Searching;
        impl_->status.source = wanted;
    }
    impl_->thread = std::thread([this]() { impl_->run(); });
    return true;
}

void NdiReceiver::stop() {
    if (impl_->thread.joinable()) {
        impl_->stop = true;
        impl_->wake.notify_all();
        impl_->thread.join();
    }
    if (impl_->holdsFinder) {
        impl_->holdsFinder = false;
        NdiFinder::instance().release();
    }
    std::lock_guard<std::mutex> lock(impl_->statusMutex);
    impl_->status.state = LiveState::Idle;
}

bool NdiReceiver::running() const {
    return impl_->thread.joinable();
}

bool NdiReceiver::take(LiveFrame& out) {
    return impl_->box.take(out);
}

LiveStatus NdiReceiver::status() const {
    std::lock_guard<std::mutex> lock(impl_->statusMutex);
    return impl_->status;
}

const std::string& NdiReceiver::error() const {
    return impl_->error;
}

}
