#include "media/camera_capture.hpp"

#include "core/log.hpp"
#include "media/ffmpeg_api.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <thread>

namespace nvj {

namespace {

constexpr const char* kTestPatternPrefix = "lavfi:";

}

bool isTestPatternDevice(const std::string& id) {
    return id.rfind(kTestPatternPrefix, 0) == 0;
}

#ifdef NYREXVJ_HAVE_FFMPEG

namespace {

namespace fs = std::filesystem;

std::string errorText(const FfmpegApi& api, int code) {
    char buffer[AV_ERROR_MAX_STRING_SIZE] = {};
    api.av_strerror(code, buffer, sizeof(buffer));
    return buffer;
}

std::string trimmed(std::string text) {
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r' || text.back() == ' ')) {
        text.pop_back();
    }
    return text;
}

#ifdef _WIN32
const char* kDeviceFormat = "dshow";
const char* kFormatOption = "vcodec";
#else
const char* kDeviceFormat = "video4linux2";
const char* kFormatOption = "input_format";
#endif

std::vector<CameraDevice> systemCameras(const FfmpegDeviceApi* devices, const FfmpegApi* api) {
    std::vector<CameraDevice> list;
#ifdef _WIN32
    if (devices != nullptr && api != nullptr) {
        const AVInputFormat* format = api->av_find_input_format("dshow");
        FfmpegDeviceInfoList* found = nullptr;
        if (format != nullptr && devices->listInputSources(format, nullptr, nullptr, &found) >= 0 && found != nullptr) {
            for (int i = 0; i < found->count; ++i) {
                const FfmpegDeviceInfo* info = found->devices[i];
                if (info == nullptr || info->name == nullptr) {
                    continue;
                }
                bool video = info->mediaTypeCount == 0;
                for (int t = 0; t < info->mediaTypeCount; ++t) {
                    video = video || info->mediaTypes[t] == 0;
                }
                if (!video) {
                    continue;
                }
                CameraDevice device;
                device.id = std::string("video=") + (info->description != nullptr && *info->description != '\0' ? info->description : info->name);
                device.name = info->description != nullptr && *info->description != '\0' ? info->description : info->name;
                list.push_back(std::move(device));
            }
        }
        if (found != nullptr) {
            devices->freeList(&found);
        }
    }
#else
    (void)devices;
    (void)api;
    std::error_code ec;
    std::vector<std::pair<int, CameraDevice>> nodes;
    for (const fs::directory_entry& entry : fs::directory_iterator("/sys/class/video4linux", ec)) {
        const std::string name = entry.path().filename().string();
        if (name.rfind("video", 0) != 0) {
            continue;
        }
        int number = -1;
        try {
            number = std::stoi(name.substr(5));
        } catch (...) {
            continue;
        }
        std::string index = "0";
        {
            std::ifstream file(entry.path() / "index");
            if (file) {
                std::getline(file, index);
            }
        }
        if (trimmed(index) != "0") {
            continue;
        }
        std::string label = name;
        {
            std::ifstream file(entry.path() / "name");
            std::string text;
            if (file && std::getline(file, text) && !trimmed(text).empty()) {
                label = trimmed(text);
            }
        }
        CameraDevice device;
        device.id = "/dev/" + name;
        device.name = label + " (" + device.id + ")";
        nodes.emplace_back(number, std::move(device));
    }
    std::sort(nodes.begin(), nodes.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    for (auto& node : nodes) {
        list.push_back(std::move(node.second));
    }
#endif
    return list;
}

}

CameraSupport cameraSupport() {
    CameraSupport support;
    support.compiled = true;
    std::string problem;
    support.ffmpeg = ffmpegApi(problem) != nullptr;
    if (!support.ffmpeg) {
        support.problem = problem;
        return support;
    }
    support.devices = ffmpegDeviceApi(problem) != nullptr;
    support.problem = problem;
    return support;
}

std::vector<CameraDevice> listCameras() {
    std::string problem;
    const FfmpegApi* api = ffmpegApi(problem);
    if (api == nullptr) {
        return {};
    }
    const FfmpegDeviceApi* devices = ffmpegDeviceApi(problem);
    std::vector<CameraDevice> list = systemCameras(devices, api);
    if (devices != nullptr && api->av_find_input_format("lavfi") != nullptr) {
        CameraDevice pattern;
        pattern.id = std::string(kTestPatternPrefix) + "testsrc2=size=1280x720:rate=30";
        pattern.name = "Test pattern (lavfi, 1280x720)";
        list.push_back(std::move(pattern));
    }
    return list;
}

struct CameraCapture::Impl {
    const FfmpegApi* api = nullptr;
    ReconnectPolicy policy;
    std::string device;
    CameraMode mode;
    std::string error;
    std::thread thread;
    std::mutex wakeMutex;
    std::condition_variable wake;
    std::atomic<bool> stop{false};
    std::atomic<double> deadline{0.0};
    std::atomic<unsigned long long> skipped{0};
    LiveFrameBox box;
    mutable std::mutex statusMutex;
    LiveStatus status;

    struct Session {
        AVFormatContext* format = nullptr;
        AVCodecContext* codec = nullptr;
        AVPacket* packet = nullptr;
        AVFrame* frame = nullptr;
        SwsContext* sws = nullptr;
        int stream = -1;
        bool paced = false;
        bool baseSet = false;
        double base = 0.0;
        double fps = 0.0;
        double previous = 0.0;
        unsigned long long frames = 0;
        unsigned long long unsupported = 0;
        unsigned long long serial = 0;
        int width = 0;
        int height = 0;
    };

    static int interrupted(void* opaque) {
        const Impl* impl = static_cast<const Impl*>(opaque);
        if (impl->stop.load()) {
            return 1;
        }
        const double limit = impl->deadline.load();
        return limit > 0.0 && steadySeconds() > limit ? 1 : 0;
    }

    void close(Session& s) {
        if (s.sws != nullptr) {
            api->sws_freeContext(s.sws);
            s.sws = nullptr;
        }
        if (s.frame != nullptr) {
            api->av_frame_free(&s.frame);
        }
        if (s.packet != nullptr) {
            api->av_packet_free(&s.packet);
        }
        if (s.codec != nullptr) {
            api->avcodec_free_context(&s.codec);
        }
        if (s.format != nullptr) {
            api->avformat_close_input(&s.format);
        }
        s.stream = -1;
        s.baseSet = false;
    }

    bool open(Session& s, std::string& failure) {
        const bool pattern = isTestPatternDevice(device);
        const char* formatName = pattern ? "lavfi" : kDeviceFormat;
        const std::string url = pattern ? device.substr(std::strlen(kTestPatternPrefix)) : device;
        const AVInputFormat* input = api->av_find_input_format(formatName);
        if (input == nullptr) {
            failure = std::string("input format ") + formatName + " is not available";
            return false;
        }
        std::vector<std::string> preferred;
        const long long demand = static_cast<long long>(mode.width) * mode.height * mode.fps;
        if (!pattern && demand > 640LL * 480LL * 30LL) {
            preferred = {"mjpeg", ""};
        } else {
            preferred = {""};
        }
        for (const std::string& format : preferred) {
            AVDictionary* options = nullptr;
            if (!pattern) {
                if (mode.width > 0 && mode.height > 0) {
                    api->av_dict_set(&options, "video_size", (std::to_string(mode.width) + "x" + std::to_string(mode.height)).c_str(), 0);
                }
                if (mode.fps > 0) {
                    api->av_dict_set(&options, "framerate", std::to_string(mode.fps).c_str(), 0);
                }
                if (!format.empty()) {
                    api->av_dict_set(&options, kFormatOption, format.c_str(), 0);
                }
            }
            api->av_dict_set(&options, "fflags", "nobuffer", 0);
            AVFormatContext* context = api->avformat_alloc_context();
            if (context == nullptr) {
                api->av_dict_free(&options);
                failure = "out of memory";
                return false;
            }
            context->interrupt_callback.callback = &Impl::interrupted;
            context->interrupt_callback.opaque = this;
            deadline = steadySeconds() + 8.0;
            const int result = api->avformat_open_input(&context, url.c_str(), input, &options);
            api->av_dict_free(&options);
            deadline = 0.0;
            if (result < 0) {
                failure = errorText(*api, result);
                continue;
            }
            s.format = context;
            break;
        }
        if (s.format == nullptr) {
            return false;
        }
        s.stream = api->av_find_best_stream(s.format, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
        if (s.stream < 0) {
            failure = "the device has no video stream";
            close(s);
            return false;
        }
        const AVCodecParameters* parameters = s.format->streams[s.stream]->codecpar;
        const AVCodec* decoder = api->avcodec_find_decoder(parameters->codec_id);
        if (decoder == nullptr) {
            failure = std::string("no decoder for ") + api->avcodec_get_name(parameters->codec_id);
            close(s);
            return false;
        }
        s.codec = api->avcodec_alloc_context3(decoder);
        if (s.codec == nullptr || api->avcodec_parameters_to_context(s.codec, parameters) < 0) {
            failure = "the decoder could not be prepared";
            close(s);
            return false;
        }
        s.codec->flags |= AV_CODEC_FLAG_LOW_DELAY;
        s.codec->thread_count = 1;
        const int opened = api->avcodec_open2(s.codec, decoder, nullptr);
        if (opened < 0) {
            failure = errorText(*api, opened);
            close(s);
            return false;
        }
        s.packet = api->av_packet_alloc();
        s.frame = api->av_frame_alloc();
        if (s.packet == nullptr || s.frame == nullptr) {
            failure = "out of memory";
            close(s);
            return false;
        }
        s.paced = pattern;
        return true;
    }

    bool convert(Session& s, const AVFrame& picture, double arrived, LiveFrame& out, std::string& problem) {
        const int width = picture.width & ~1;
        const int height = picture.height;
        if (width <= 0 || height <= 0 || width > 16384 || height > 16384) {
            problem = "Unsupported camera picture size";
            return false;
        }
        const size_t rowBytes = static_cast<size_t>(width) * 2u;
        out.format = LiveFormat::Uyvy;
        out.width = width;
        out.height = height;
        out.stride = static_cast<int>(rowBytes);
        out.capturedAt = arrived;
        out.data.resize(rowBytes * static_cast<size_t>(height));
        const AVPixelFormat source = static_cast<AVPixelFormat>(picture.format);
        if (source == AV_PIX_FMT_UYVY422 && picture.data[0] != nullptr) {
            for (int y = 0; y < height; ++y) {
                std::memcpy(out.data.data() + static_cast<size_t>(y) * rowBytes, picture.data[0] + static_cast<ptrdiff_t>(y) * picture.linesize[0], rowBytes);
            }
            return true;
        }
        s.sws = api->sws_getCachedContext(s.sws, picture.width, picture.height, source, width, height, AV_PIX_FMT_UYVY422, SWS_FAST_BILINEAR, nullptr, nullptr, nullptr);
        if (s.sws == nullptr) {
            const char* name = api->av_get_pix_fmt_name(source);
            problem = std::string("Unsupported camera pixel format ") + (name != nullptr ? name : "unknown");
            return false;
        }
        const char* sourceName = api->av_get_pix_fmt_name(source);
        const bool jpegRange = picture.color_range == AVCOL_RANGE_JPEG || (sourceName != nullptr && std::strncmp(sourceName, "yuvj", 4) == 0);
        int sourceMatrix = height >= 720 ? SWS_CS_ITU709 : SWS_CS_ITU601;
        if (picture.colorspace == AVCOL_SPC_BT709) {
            sourceMatrix = SWS_CS_ITU709;
        } else if (picture.colorspace == AVCOL_SPC_BT470BG || picture.colorspace == AVCOL_SPC_SMPTE170M) {
            sourceMatrix = SWS_CS_ITU601;
        }
        const int targetMatrix = height >= 720 ? SWS_CS_ITU709 : SWS_CS_ITU601;
        api->sws_setColorspaceDetails(s.sws, api->sws_getCoefficients(sourceMatrix), jpegRange ? 1 : 0, api->sws_getCoefficients(targetMatrix), 0, 0, 1 << 16, 1 << 16);
        uint8_t* destination[4] = {out.data.data(), nullptr, nullptr, nullptr};
        int destinationStride[4] = {static_cast<int>(rowBytes), 0, 0, 0};
        const int scaled = api->sws_scale(s.sws, picture.data, picture.linesize, 0, picture.height, destination, destinationStride);
        if (scaled <= 0) {
            problem = "The camera picture could not be converted";
            return false;
        }
        return true;
    }

    void publish(const LiveStateMachine& machine, const std::string& problem, const Session& s) {
        std::lock_guard<std::mutex> lock(statusMutex);
        status.state = machine.state();
        status.source = device;
        status.error = problem;
        status.width = s.width;
        status.height = s.height;
        status.connections = 0;
        status.fps = s.fps;
        status.frames = s.frames;
        status.dropped = box.dropped() + skipped.load();
        status.unsupported = s.unsupported;
        status.attempts = machine.attempts();
    }

    void run() {
        LiveStateMachine machine(policy);
        machine.start(steadySeconds());
        Session session;
        std::string problem;
        LiveFrame frame;
        const double readLimit = std::max(2.0, policy.lostAfter);
        while (!stop.load()) {
            const double now = steadySeconds();
            if (machine.update(now) == LiveStateMachine::Action::Reset) {
                close(session);
            }
            publish(machine, problem, session);
            if (session.format == nullptr) {
                if (machine.attemptDue(now)) {
                    machine.beginAttempt(now);
                    std::string failure;
                    if (open(session, failure)) {
                        problem.clear();
                        machine.attemptSucceeded(now);
                    } else {
                        problem = failure;
                        machine.attemptFailed(now);
                    }
                } else {
                    std::unique_lock<std::mutex> lock(wakeMutex);
                    wake.wait_for(lock, std::chrono::milliseconds(20), [this]() { return stop.load(); });
                }
                continue;
            }
            deadline = steadySeconds() + readLimit;
            const int read = api->av_read_frame(session.format, session.packet);
            deadline = 0.0;
            if (read == AVERROR(EAGAIN)) {
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
                continue;
            }
            if (read == AVERROR_EXIT) {
                continue;
            }
            if (read < 0) {
                problem = errorText(*api, read);
                close(session);
                machine.connectionLost(steadySeconds());
                continue;
            }
            if (session.packet->stream_index != session.stream || (session.packet->flags & AV_PKT_FLAG_CORRUPT) != 0) {
                api->av_packet_unref(session.packet);
                continue;
            }
            if (session.paced && session.packet->pts != AV_NOPTS_VALUE) {
                const AVRational base = session.format->streams[session.stream]->time_base;
                const double stamp = static_cast<double>(session.packet->pts) * static_cast<double>(base.num) / static_cast<double>(base.den);
                const double current = steadySeconds();
                if (!session.baseSet || current - (session.base + stamp) > 1.0) {
                    session.base = current - stamp;
                    session.baseSet = true;
                }
                const double wait = session.base + stamp - current;
                if (wait > 0.0005) {
                    std::unique_lock<std::mutex> lock(wakeMutex);
                    wake.wait_for(lock, std::chrono::duration<double>(wait), [this]() { return stop.load(); });
                    if (stop.load()) {
                        api->av_packet_unref(session.packet);
                        break;
                    }
                }
            }
            const double arrived = steadySeconds();
            const int sent = api->avcodec_send_packet(session.codec, session.packet);
            api->av_packet_unref(session.packet);
            if (sent < 0 && sent != AVERROR(EAGAIN)) {
                continue;
            }
            while (api->avcodec_receive_frame(session.codec, session.frame) == 0) {
                if (convert(session, *session.frame, arrived, frame, problem)) {
                    frame.id = ++session.serial;
                    session.width = frame.width;
                    session.height = frame.height;
                    box.publish(frame);
                    ++session.frames;
                    if (session.previous > 0.0 && arrived > session.previous) {
                        const double instant = 1.0 / (arrived - session.previous);
                        session.fps = session.fps <= 0.0 ? instant : session.fps * 0.9 + instant * 0.1;
                    }
                    session.previous = arrived;
                    machine.frame(arrived);
                    problem.clear();
                } else {
                    ++session.unsupported;
                }
                api->av_frame_unref(session.frame);
            }
        }
        close(session);
        std::lock_guard<std::mutex> lock(statusMutex);
        status.state = LiveState::Idle;
    }
};

CameraCapture::CameraCapture() : impl_(std::make_unique<Impl>()) {}

CameraCapture::~CameraCapture() {
    stop();
}

bool CameraCapture::start(const std::string& device, const CameraMode& mode, const ReconnectPolicy& policy) {
    stop();
    impl_->error.clear();
    std::string problem;
    impl_->api = ffmpegApi(problem);
    if (impl_->api == nullptr) {
        impl_->error = "FFmpeg is not available (" + problem + ")";
        return false;
    }
    if (ffmpegDeviceApi(problem) == nullptr) {
        impl_->error = "Camera support is not available (" + problem + ")";
        return false;
    }
    if (device.empty()) {
        impl_->error = "No camera is selected";
        return false;
    }
    impl_->device = device;
    impl_->mode = mode;
    impl_->policy = policy;
    impl_->stop = false;
    impl_->deadline = 0.0;
    impl_->skipped = 0;
    impl_->box.reset();
    {
        std::lock_guard<std::mutex> lock(impl_->statusMutex);
        impl_->status = LiveStatus{};
        impl_->status.state = LiveState::Searching;
        impl_->status.source = device;
    }
    impl_->thread = std::thread([this]() { impl_->run(); });
    return true;
}

void CameraCapture::stop() {
    if (impl_->thread.joinable()) {
        impl_->stop = true;
        impl_->wake.notify_all();
        impl_->thread.join();
    }
    std::lock_guard<std::mutex> lock(impl_->statusMutex);
    impl_->status.state = LiveState::Idle;
}

bool CameraCapture::running() const {
    return impl_->thread.joinable();
}

bool CameraCapture::take(LiveFrame& out) {
    return impl_->box.take(out);
}

LiveStatus CameraCapture::status() const {
    std::lock_guard<std::mutex> lock(impl_->statusMutex);
    return impl_->status;
}

const std::string& CameraCapture::error() const {
    return impl_->error;
}

#else

CameraSupport cameraSupport() {
    CameraSupport support;
    support.problem = "this build has no FFmpeg support";
    return support;
}

std::vector<CameraDevice> listCameras() {
    return {};
}

struct CameraCapture::Impl {
    std::string error = "This build has no FFmpeg support, cameras are not available";
};

CameraCapture::CameraCapture() : impl_(std::make_unique<Impl>()) {}

CameraCapture::~CameraCapture() = default;

bool CameraCapture::start(const std::string&, const CameraMode&, const ReconnectPolicy&) {
    return false;
}

void CameraCapture::stop() {}

bool CameraCapture::running() const {
    return false;
}

bool CameraCapture::take(LiveFrame&) {
    return false;
}

LiveStatus CameraCapture::status() const {
    return LiveStatus{};
}

const std::string& CameraCapture::error() const {
    return impl_->error;
}

#endif

}
