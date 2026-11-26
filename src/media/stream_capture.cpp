#include "media/stream_capture.hpp"

#include "core/log.hpp"
#include "core/stoppable_thread.hpp"
#include "core/stream_clock.hpp"
#include "media/ffmpeg_api.hpp"
#include "media/live_convert.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <thread>

namespace nvj {

#ifdef NYREXVJ_HAVE_FFMPEG

namespace {

constexpr double kProbeSeconds = 6.0;
constexpr double kListenSeconds = 3600.0;
constexpr double kKeyWaitSeconds = 2.0;
constexpr double kStopWaitSeconds = 3.0;

}

StreamSupport streamSupport() {
    StreamSupport support;
    support.compiled = true;
    std::string problem;
    support.ffmpeg = ffmpegApi(problem) != nullptr;
    support.problem = problem;
    return support;
}

struct StreamCapture::Impl {
    const FfmpegApi* api = nullptr;
    ReconnectPolicy policy;
    stream::Profile profile = stream::Profile::Balanced;
    stream::OpenPlan plan;
    std::string shown;
    std::string error;
    std::mutex wakeMutex;
    std::condition_variable wake;
    std::atomic<bool> stop{false};
    std::atomic<const char*> phase{"idle"};
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
        bool waitKey = false;
        double waitKeySince = 0.0;
        double fps = 0.0;
        double windowStart = 0.0;
        double windowFirst = 0.0;
        double windowLast = 0.0;
        bool windowStamped = true;
        unsigned long long windowFrames = 0;
        unsigned long long frames = 0;
        unsigned long long unsupported = 0;
        unsigned long long serial = 0;
        unsigned long long resyncs = 0;
        int width = 0;
        int height = 0;
        std::string codecName;
        stream::LagTracker lag;
        stream::JitterMeter jitter;
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
        s.waitKey = false;
        s.codecName.clear();
    }

    bool open(Session& s, std::string& failure) {
        AVDictionary* options = nullptr;
        for (const auto& option : plan.options) {
            api->av_dict_set(&options, option.first.c_str(), option.second.c_str(), 0);
        }
        AVFormatContext* context = api->avformat_alloc_context();
        if (context == nullptr) {
            api->av_dict_free(&options);
            failure = "Out of memory";
            return false;
        }
        context->interrupt_callback.callback = &Impl::interrupted;
        context->interrupt_callback.opaque = this;
        deadline = steadySeconds() + (plan.waitsForPeer ? kListenSeconds : std::max(policy.connectTimeout, 4.0));
        phase = "open";
        int result = api->avformat_open_input(&context, plan.url.c_str(), nullptr, &options);
        api->av_dict_free(&options);
        if (result < 0) {
            deadline = 0.0;
            failure = stop.load() ? std::string("stopped") : ffmpegErrorText(*api, result);
            return false;
        }
        s.format = context;
        deadline = steadySeconds() + kProbeSeconds;
        phase = "probe";
        result = api->avformat_find_stream_info(s.format, nullptr);
        deadline = 0.0;
        if (result < 0) {
            failure = stop.load() ? std::string("stopped") : ffmpegErrorText(*api, result);
            close(s);
            return false;
        }
        if (s.format->nb_streams > static_cast<unsigned>(stream::kMaxStreams)) {
            failure = "The stream has too many parts";
            close(s);
            return false;
        }
        s.stream = api->av_find_best_stream(s.format, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
        if (s.stream < 0) {
            failure = "The stream has no video";
            close(s);
            return false;
        }
        const AVCodecParameters* parameters = s.format->streams[s.stream]->codecpar;
        const std::string name = api->avcodec_get_name(parameters->codec_id);
        if (!stream::codecAllowed(name)) {
            failure = "The video codec is not allowed for network streams: " + name.substr(0, 24);
            close(s);
            return false;
        }
        if ((parameters->width != 0 || parameters->height != 0) && !stream::pictureSizeAllowed(parameters->width, parameters->height)) {
            failure = "The picture size is not allowed for network streams: " + std::to_string(parameters->width) + "x" + std::to_string(parameters->height);
            close(s);
            return false;
        }
        for (unsigned i = 0; i < s.format->nb_streams; ++i) {
            if (static_cast<int>(i) != s.stream) {
                s.format->streams[i]->discard = AVDISCARD_ALL;
            }
        }
        const AVCodec* decoder = api->avcodec_find_decoder(parameters->codec_id);
        if (decoder == nullptr) {
            failure = "No decoder for the video codec: " + name.substr(0, 24);
            close(s);
            return false;
        }
        s.codec = api->avcodec_alloc_context3(decoder);
        if (s.codec == nullptr || api->avcodec_parameters_to_context(s.codec, parameters) < 0) {
            failure = "The decoder could not be prepared";
            close(s);
            return false;
        }
        if (stream::profileSpec(profile).noBuffer) {
            s.codec->flags |= AV_CODEC_FLAG_LOW_DELAY;
        }
        s.codec->thread_count = 2;
        s.codec->thread_type = FF_THREAD_SLICE;
        const int opened = api->avcodec_open2(s.codec, decoder, nullptr);
        if (opened < 0) {
            failure = ffmpegErrorText(*api, opened);
            close(s);
            return false;
        }
        s.packet = api->av_packet_alloc();
        s.frame = api->av_frame_alloc();
        if (s.packet == nullptr || s.frame == nullptr) {
            failure = "Out of memory";
            close(s);
            return false;
        }
        s.codecName = name;
        s.lag = stream::LagTracker(stream::profileSpec(profile).lagLimitSeconds);
        s.jitter.reset();
        s.fps = 0.0;
        s.windowStart = 0.0;
        s.windowFirst = 0.0;
        s.windowLast = 0.0;
        s.windowStamped = true;
        s.windowFrames = 0;
        return true;
    }

    void publish(const LiveStateMachine& machine, const std::string& problem, const Session& s) {
        std::lock_guard<std::mutex> lock(statusMutex);
        status.state = machine.state();
        status.source = shown;
        status.error = problem;
        status.width = s.width;
        status.height = s.height;
        status.connections = 0;
        status.fps = s.fps;
        status.frames = s.frames;
        status.dropped = box.dropped() + skipped.load();
        status.unsupported = s.unsupported;
        status.attempts = machine.attempts();
        status.jitterMs = s.jitter.jitterSeconds() * 1000.0;
        status.resyncs = s.resyncs;
        status.detail = s.codecName;
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
                    publish(machine, problem, session);
                    std::string failure;
                    if (open(session, failure)) {
                        problem.clear();
                        machine.attemptSucceeded(steadySeconds());
                        logLine("Stream input connected: " + shown + " (" + session.codecName + ")");
                    } else {
                        if (!stop.load()) {
                            problem = failure;
                            logLine("Stream input could not connect: " + shown + ": " + failure);
                        }
                        machine.attemptFailed(steadySeconds());
                    }
                } else {
                    std::unique_lock<std::mutex> lock(wakeMutex);
                    wake.wait_for(lock, std::chrono::milliseconds(20), [this]() { return stop.load(); });
                }
                continue;
            }
            deadline = steadySeconds() + readLimit;
            phase = "read";
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
                problem = ffmpegErrorText(*api, read);
                close(session);
                machine.connectionLost(steadySeconds());
                continue;
            }
            if (session.packet->stream_index != session.stream || (session.packet->flags & AV_PKT_FLAG_CORRUPT) != 0) {
                api->av_packet_unref(session.packet);
                continue;
            }
            const double arrived = steadySeconds();
            const AVStream* video = session.format->streams[session.stream];
            const int64_t stamp = session.packet->dts != AV_NOPTS_VALUE ? session.packet->dts : session.packet->pts;
            if (stamp != AV_NOPTS_VALUE && video->time_base.num > 0 && video->time_base.den > 0) {
                const double seconds = static_cast<double>(stamp) * static_cast<double>(video->time_base.num) / static_cast<double>(video->time_base.den);
                session.jitter.observe(arrived, seconds);
                if (session.lag.observe(arrived, seconds)) {
                    api->avcodec_flush_buffers(session.codec);
                    session.waitKey = true;
                    session.waitKeySince = arrived;
                    ++session.resyncs;
                }
            }
            if (session.waitKey) {
                if ((session.packet->flags & AV_PKT_FLAG_KEY) != 0 || arrived - session.waitKeySince > kKeyWaitSeconds) {
                    session.waitKey = false;
                } else {
                    skipped.fetch_add(1);
                    api->av_packet_unref(session.packet);
                    continue;
                }
            }
            phase = "decode";
            const int sent = api->avcodec_send_packet(session.codec, session.packet);
            api->av_packet_unref(session.packet);
            if (sent < 0 && sent != AVERROR(EAGAIN)) {
                continue;
            }
            while (api->avcodec_receive_frame(session.codec, session.frame) == 0) {
                phase = "convert";
                if (!stream::pictureSizeAllowed(session.frame->width, session.frame->height)) {
                    problem = "The picture size is not allowed for network streams: " + std::to_string(session.frame->width) + "x" + std::to_string(session.frame->height);
                    ++session.unsupported;
                } else if (convertToUyvy(*api, session.sws, *session.frame, arrived, stream::kMaxPictureSide, "stream", frame, problem)) {
                    frame.id = ++session.serial;
                    session.width = frame.width;
                    session.height = frame.height;
                    box.publish(frame);
                    ++session.frames;
                    if (session.windowStart <= 0.0) {
                        session.windowStart = arrived;
                        session.windowFrames = 0;
                        session.windowStamped = true;
                    }
                    const int64_t shown = session.frame->best_effort_timestamp;
                    if (shown != AV_NOPTS_VALUE && video->time_base.num > 0 && video->time_base.den > 0) {
                        const double seconds = static_cast<double>(shown) * static_cast<double>(video->time_base.num) / static_cast<double>(video->time_base.den);
                        if (session.windowFrames == 0) {
                            session.windowFirst = seconds;
                        }
                        session.windowLast = seconds;
                    } else {
                        session.windowStamped = false;
                    }
                    ++session.windowFrames;
                    if (arrived - session.windowStart >= 1.0) {
                        double measured = 0.0;
                        if (session.windowStamped && session.windowFrames >= 2 && session.windowLast > session.windowFirst) {
                            measured = static_cast<double>(session.windowFrames - 1) / (session.windowLast - session.windowFirst);
                        } else {
                            measured = static_cast<double>(session.windowFrames) / (arrived - session.windowStart);
                        }
                        session.fps = session.fps <= 0.0 ? measured : session.fps * 0.5 + measured * 0.5;
                        session.windowStart = arrived;
                        session.windowFrames = 0;
                    }
                    machine.frame(arrived);
                    problem.clear();
                } else {
                    ++session.unsupported;
                }
                api->av_frame_unref(session.frame);
            }
        }
        phase = "close";
        close(session);
        phase = "ended";
        std::lock_guard<std::mutex> lock(statusMutex);
        status.state = LiveState::Idle;
    }
};

StreamCapture::StreamCapture() : impl_(std::make_shared<Impl>()) {}

StreamCapture::~StreamCapture() {
    stop();
}

bool StreamCapture::start(const std::string& url, stream::Profile profile, const ReconnectPolicy& policy) {
    stop();
    impl_->error.clear();
    std::string problem;
    impl_->api = ffmpegApi(problem);
    if (impl_->api == nullptr) {
        impl_->error = "FFmpeg is not available: " + problem;
        return false;
    }
    const stream::ParseResult parsed = stream::parseUrl(url);
    if (!parsed.ok) {
        impl_->error = parsed.error;
        return false;
    }
    impl_->plan = stream::makeOpenPlan(parsed.url, profile);
    impl_->profile = profile;
    impl_->policy = policy;
    impl_->shown = stream::maskUrl(url);
    impl_->stop = false;
    impl_->deadline = 0.0;
    impl_->skipped = 0;
    impl_->box.reset();
    {
        std::lock_guard<std::mutex> lock(impl_->statusMutex);
        impl_->status = LiveStatus{};
        impl_->status.state = LiveState::Searching;
        impl_->status.source = impl_->shown;
    }
    impl_->phase = "start";
    worker_.start([keep = impl_]() { keep->run(); });
    return true;
}

void StreamCapture::stop() {
    if (worker_.running()) {
        impl_->stop = true;
        impl_->wake.notify_all();
        if (!worker_.stop(kStopWaitSeconds)) {
            logLine("Stream input did not stop within " + std::to_string(static_cast<int>(kStopWaitSeconds)) + " s and was left behind (last step: " + std::string(impl_->phase.load()) + "): " + impl_->shown);
            impl_ = std::make_shared<Impl>();
        }
    }
    std::lock_guard<std::mutex> lock(impl_->statusMutex);
    impl_->status.state = LiveState::Idle;
}

bool StreamCapture::running() const {
    return worker_.running();
}

bool StreamCapture::take(LiveFrame& out) {
    return impl_->box.take(out);
}

LiveStatus StreamCapture::status() const {
    std::lock_guard<std::mutex> lock(impl_->statusMutex);
    return impl_->status;
}

const std::string& StreamCapture::error() const {
    return impl_->error;
}

#else

StreamSupport streamSupport() {
    StreamSupport support;
    support.problem = "This build has no FFmpeg support";
    return support;
}

struct StreamCapture::Impl {
    std::string error = "This build has no FFmpeg support, network streams are not available";
};

StreamCapture::StreamCapture() : impl_(std::make_shared<Impl>()) {}

StreamCapture::~StreamCapture() = default;

bool StreamCapture::start(const std::string&, stream::Profile, const ReconnectPolicy&) {
    return false;
}

void StreamCapture::stop() {}

bool StreamCapture::running() const {
    return false;
}

bool StreamCapture::take(LiveFrame&) {
    return false;
}

LiveStatus StreamCapture::status() const {
    return LiveStatus{};
}

const std::string& StreamCapture::error() const {
    return impl_->error;
}

#endif

}
