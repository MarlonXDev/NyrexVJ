#include "media/flow_worker.hpp"

#include "media/frame_luma.hpp"
#include "platform/platform.hpp"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <map>
#include <mutex>
#include <thread>
#include <utility>

namespace nvj {

namespace {

constexpr std::size_t kQueueLimit = 3;
constexpr std::size_t kCacheLimit = 8;
constexpr std::size_t kLumaLimit = 4;
constexpr double kMinPeriod = 1.0 / 240.0;

double elapsedMs(std::chrono::steady_clock::time_point begin) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count();
}

}

bool analyseFrames(const VideoFrame& a, const VideoFrame& b, flow::Tier tier, flow::Field& field, double* milliseconds, std::string* error) {
    const auto begin = std::chrono::steady_clock::now();
    std::string reason;
    if (!flowCanAnalyse(a, &reason) || !flowCanAnalyse(b, &reason)) {
        if (error != nullptr) {
            *error = reason;
        }
        return false;
    }
    const VideoPlane& plane = a.planes[0];
    int width = 0;
    int height = 0;
    const int sourceWidth = a.layout == VideoLayout::PackedUyvy ? a.width : plane.width;
    const int sourceHeight = a.layout == VideoLayout::PackedUyvy ? a.height : plane.height;
    flow::analysisSize(sourceWidth, sourceHeight, tier, width, height);
    const flow::Luma lumaA = lumaFromVideoFrame(a, width, height);
    const flow::Luma lumaB = lumaFromVideoFrame(b, width, height);
    if (!lumaA.valid() || !lumaB.valid()) {
        if (error != nullptr) {
            *error = "the frames could not be reduced to analysis pictures";
        }
        return false;
    }
    const bool ok = flow::estimate(lumaA, lumaB, tier, field, error);
    if (milliseconds != nullptr) {
        *milliseconds = elapsedMs(begin);
    }
    return ok;
}

struct FlowWorker::Impl {
    struct Job {
        std::shared_ptr<const VideoFrame> a;
        std::shared_ptr<const VideoFrame> b;
    };
    using Key = std::pair<unsigned long long, unsigned long long>;
    struct LumaEntry {
        unsigned long long id = 0;
        int width = 0;
        int height = 0;
        flow::Luma luma;
    };

    mutable std::mutex mutex;
    std::condition_variable wake;
    std::deque<Job> queue;
    std::map<Key, FlowResult> cache;
    std::deque<Key> order;
    std::deque<LumaEntry> lumas;
    flow::Governor governor;
    FlowWorkerStats counters;
    bool stop = false;
    bool busy = false;
    Key active{0, 0};
    std::thread thread;

    Impl() {
        thread = std::thread([this] { run(); });
    }

    ~Impl() {
        {
            std::lock_guard<std::mutex> lock(mutex);
            stop = true;
        }
        wake.notify_all();
        if (thread.joinable()) {
            thread.join();
        }
    }

    bool known(const Key& key) const {
        if (cache.count(key) != 0 || (busy && active == key)) {
            return true;
        }
        for (const Job& job : queue) {
            if (job.a->id == key.first && job.b->id == key.second) {
                return true;
            }
        }
        return false;
    }

    flow::Luma lumaFor(const VideoFrame& frame, int width, int height) {
        {
            std::lock_guard<std::mutex> lock(mutex);
            for (const LumaEntry& entry : lumas) {
                if (entry.id == frame.id && entry.width == width && entry.height == height) {
                    return entry.luma;
                }
            }
        }
        LumaEntry entry;
        entry.id = frame.id;
        entry.width = width;
        entry.height = height;
        entry.luma = lumaFromVideoFrame(frame, width, height);
        flow::Luma copy = entry.luma;
        std::lock_guard<std::mutex> lock(mutex);
        lumas.push_back(std::move(entry));
        while (lumas.size() > kLumaLimit) {
            lumas.pop_front();
        }
        return copy;
    }

    void store(const Key& key, FlowResult result) {
        cache[key] = std::move(result);
        order.push_back(key);
        while (order.size() > kCacheLimit) {
            cache.erase(order.front());
            order.pop_front();
        }
    }

    void run() {
        platform::setCurrentThreadBackground(true);
        for (;;) {
            Job job;
            flow::Tier tier = flow::Tier::Balanced;
            {
                std::unique_lock<std::mutex> lock(mutex);
                wake.wait(lock, [this] { return stop || !queue.empty(); });
                if (stop) {
                    return;
                }
                job = std::move(queue.front());
                queue.pop_front();
                busy = true;
                active = {job.a->id, job.b->id};
                tier = governor.current();
            }
            FlowResult result;
            result.tier = tier;
            const auto begin = std::chrono::steady_clock::now();
            std::string error;
            bool ok = flowCanAnalyse(*job.a, &error) && flowCanAnalyse(*job.b, &error);
            flow::Field field;
            int width = 0;
            int height = 0;
            if (ok) {
                const VideoPlane& plane = job.a->planes[0];
                const int sourceWidth = job.a->layout == VideoLayout::PackedUyvy ? job.a->width : plane.width;
                const int sourceHeight = job.a->layout == VideoLayout::PackedUyvy ? job.a->height : plane.height;
                flow::analysisSize(sourceWidth, sourceHeight, tier, width, height);
                const flow::Luma lumaA = lumaFor(*job.a, width, height);
                const flow::Luma lumaB = lumaFor(*job.b, width, height);
                if (!lumaA.valid() || !lumaB.valid()) {
                    ok = false;
                    error = "the frames could not be reduced to analysis pictures";
                } else {
                    ok = flow::estimate(lumaA, lumaB, tier, field, &error);
                }
            }
            const double ms = elapsedMs(begin);
            const double period = std::max(job.b->time - job.a->time, kMinPeriod);
            job.a.reset();
            const unsigned long long idB = job.b->id;
            const unsigned long long idA = active.first;
            job.b.reset();
            {
                std::lock_guard<std::mutex> lock(mutex);
                result.ready = true;
                result.milliseconds = ms;
                if (ok) {
                    counters.analysisWidth = width;
                    counters.analysisHeight = height;
                    counters.cols = field.cols;
                    counters.rows = field.rows;
                    if (field.sceneCut) {
                        ++counters.cuts;
                    }
                    result.field = std::make_shared<const flow::Field>(std::move(field));
                    ++counters.completed;
                    const flow::Governor::Decision decision = governor.record(ms, period);
                    if (decision == flow::Governor::Decision::Lower) {
                        counters.lastError = "the analysis was too slow, the quality tier was lowered";
                    } else if (decision == flow::Governor::Decision::Disable) {
                        counters.lastError = "the analysis is too slow even at the lowest tier";
                    }
                } else {
                    result.error = error;
                    counters.lastError = error;
                    ++counters.failed;
                }
                counters.lastMs = ms;
                store({idA, idB}, std::move(result));
                busy = false;
            }
        }
    }
};

FlowWorker::FlowWorker() : impl_(std::make_unique<Impl>()) {}

FlowWorker::~FlowWorker() = default;

void FlowWorker::setTier(flow::Tier tier) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (impl_->governor.requested() == tier) {
        return;
    }
    impl_->governor.setRequested(tier);
    impl_->cache.clear();
    impl_->order.clear();
}

bool FlowWorker::submit(const std::shared_ptr<const VideoFrame>& a, const std::shared_ptr<const VideoFrame>& b) {
    if (!a || !b || a->id == b->id) {
        return false;
    }
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        if (impl_->governor.disabled()) {
            return false;
        }
        const Impl::Key key{a->id, b->id};
        if (impl_->known(key)) {
            return true;
        }
        while (impl_->queue.size() >= kQueueLimit) {
            impl_->queue.pop_front();
            ++impl_->counters.superseded;
        }
        impl_->queue.push_back({a, b});
        ++impl_->counters.submitted;
    }
    impl_->wake.notify_one();
    return true;
}

FlowResult FlowWorker::lookup(unsigned long long idA, unsigned long long idB) const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    const auto found = impl_->cache.find({idA, idB});
    if (found == impl_->cache.end()) {
        return FlowResult();
    }
    return found->second;
}

bool FlowWorker::disabled() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->governor.disabled();
}

FlowWorkerStats FlowWorker::stats() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    FlowWorkerStats out = impl_->counters;
    out.requested = impl_->governor.requested();
    out.current = impl_->governor.current();
    out.disabled = impl_->governor.disabled();
    out.lowered = impl_->governor.lowered();
    out.averageMs = impl_->governor.average();
    out.p95Ms = impl_->governor.p95();
    return out;
}

void FlowWorker::reset() {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->queue.clear();
    impl_->cache.clear();
    impl_->order.clear();
    impl_->lumas.clear();
    impl_->governor.reset();
    impl_->counters = FlowWorkerStats();
}

bool FlowWorker::idle() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->queue.empty() && !impl_->busy;
}

}
