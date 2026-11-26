#include "media/person_worker.hpp"

#include "platform/platform.hpp"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <utility>

namespace nvj {

namespace {

constexpr double kStopWaitSeconds = 3.0;
constexpr double kMaxLostStep = 2.0;

double elapsedMs(std::chrono::steady_clock::time_point begin) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count();
}

}

struct PersonWorker::Impl {
    mutable std::mutex mutex;
    std::condition_variable wake;
    mutable std::condition_variable done;
    bool stop = false;
    bool hasPicture = false;
    bool working = false;
    bool learnRequested = false;
    bool configChanged = false;
    double lostSeconds = 0.0;
    person::Picture pending;
    person::Settings settings;
    std::shared_ptr<const person::Result> published;
    PersonStats stats;
    person::Governor governor;
    std::unique_ptr<person::Analyzer> analyzer;
    person::Mode activeMode = person::Mode::Off;
    person::Result work;

    void applyConfig() {
        const person::Settings wanted = person::sanitised(settings);
        if (!analyzer || wanted.mode != activeMode) {
            activeMode = wanted.mode;
            analyzer = std::make_unique<person::Analyzer>(person::makeBackend(wanted.mode));
            work = person::Result();
            stats.provenance = analyzer->provenance().id;
            stats.disabled = false;
        }
        analyzer->configure(wanted);
        governor.request(wanted.tier, wanted.rate);
        stats.requestedTier = wanted.tier;
        stats.requestedRate = wanted.rate;
        stats.tier = governor.tier();
        stats.rate = governor.rate();
        stats.disabled = governor.disabled();
    }

    void publish() {
        work.tier = governor.tier();
        work.rate = governor.rate();
        published = std::make_shared<const person::Result>(work);
        stats.state = work.state;
        stats.tier = governor.tier();
        stats.rate = governor.rate();
        stats.disabled = governor.disabled();
        stats.lowered = governor.lowered();
        stats.averageMs = governor.average();
        stats.p95Ms = governor.p95();
        stats.budgetMs = governor.budgetMs();
        stats.width = work.mask.width;
        stats.height = work.mask.height;
    }

    void run() {
        platform::setCurrentThreadBackground(true);
        std::unique_lock<std::mutex> lock(mutex);
        while (!stop) {
            wake.wait(lock, [&] { return stop || hasPicture || lostSeconds > 0.0 || learnRequested || configChanged; });
            if (stop) {
                break;
            }
            if (configChanged) {
                configChanged = false;
                applyConfig();
            }
            if (!analyzer) {
                hasPicture = false;
                lostSeconds = 0.0;
                learnRequested = false;
                continue;
            }
            const bool learn = learnRequested;
            learnRequested = false;
            const bool haveFrame = hasPicture;
            person::Picture picture;
            if (haveFrame) {
                picture = std::move(pending);
                hasPicture = false;
            }
            const double lostFor = lostSeconds;
            lostSeconds = 0.0;
            working = true;
            lock.unlock();
            bool ok = true;
            double milliseconds = 0.0;
            std::string problem;
            if (learn) {
                analyzer->learn();
            }
            if (haveFrame) {
                const auto begin = std::chrono::steady_clock::now();
                ok = analyzer->process(picture, work);
                milliseconds = elapsedMs(begin);
                problem = work.note;
            } else if (lostFor > 0.0) {
                analyzer->sourceLost(lostFor, work);
            }
            lock.lock();
            if (haveFrame) {
                if (ok) {
                    ++stats.completed;
                    work.milliseconds = milliseconds;
                    stats.lastMs = milliseconds;
                    if (!governor.disabled()) {
                        governor.record(milliseconds);
                    }
                    if (governor.disabled()) {
                        work.state = person::State::Failed;
                        work.note = "the analysis is too slow for this computer and was switched off";
                        stats.lastError = work.note;
                    }
                } else {
                    ++stats.failed;
                    stats.lastError = problem;
                }
            } else if (lostFor > 0.0) {
                ++stats.lostTicks;
            }
            publish();
            working = false;
            done.notify_all();
        }
    }
};

PersonWorker::PersonWorker()
    : impl_(std::make_shared<Impl>()) {
}

PersonWorker::~PersonWorker() {
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        impl_->stop = true;
    }
    impl_->wake.notify_all();
    thread_.stop(kStopWaitSeconds);
}

void PersonWorker::configure(const person::Settings& settings) {
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        impl_->settings = settings;
        impl_->configChanged = true;
    }
    if (!thread_.running()) {
        thread_.start([keep = impl_]() { keep->run(); });
    }
    impl_->wake.notify_all();
}

bool PersonWorker::submit(person::Picture&& picture) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (impl_->stats.disabled || impl_->stop) {
        return false;
    }
    if (impl_->hasPicture) {
        ++impl_->stats.superseded;
    }
    impl_->pending = std::move(picture);
    impl_->hasPicture = true;
    ++impl_->stats.submitted;
    impl_->wake.notify_one();
    return true;
}

void PersonWorker::lost(double seconds) {
    if (!(seconds > 0.0)) {
        return;
    }
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->lostSeconds = std::min(impl_->lostSeconds + seconds, kMaxLostStep);
    impl_->wake.notify_one();
}

void PersonWorker::learn() {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->learnRequested = true;
    impl_->wake.notify_one();
}

std::shared_ptr<const person::Result> PersonWorker::latest() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->published;
}

PersonStats PersonWorker::stats() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->stats;
}

person::Tier PersonWorker::tier() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->governor.tier();
}

int PersonWorker::rate() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->governor.rate();
}

bool PersonWorker::idle() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return !impl_->hasPicture && !impl_->working && impl_->lostSeconds <= 0.0 && !impl_->learnRequested && !impl_->configChanged;
}

bool PersonWorker::waitForGeneration(unsigned long long generation, int milliseconds) const {
    std::unique_lock<std::mutex> lock(impl_->mutex);
    return impl_->done.wait_for(lock, std::chrono::milliseconds(milliseconds), [&] { return impl_->published && impl_->published->generation >= generation; });
}

}
