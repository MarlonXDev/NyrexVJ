#include "media/video_decoder.hpp"

#include "core/log.hpp"
#include "media/ffmpeg_api.hpp"

#include <filesystem>

namespace nvj {

std::string ffmpegDirectory() {
    return (std::filesystem::path(userDataDir()) / "ffmpeg").string();
}

}

#ifdef NYREXVJ_HAVE_FFMPEG

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

namespace nvj {

namespace {

constexpr size_t kQueueLimit = 5;
constexpr double kSeekBehind = 0.35;
constexpr double kSeekAhead = 1.5;
constexpr int kMaxFailures = 200;

bool hasAlphaFormat(const FfmpegApi& api, int format) {
    const AVPixFmtDescriptor* descriptor = api.av_pix_fmt_desc_get(static_cast<AVPixelFormat>(format));
    return descriptor != nullptr && (descriptor->flags & AV_PIX_FMT_FLAG_ALPHA) != 0;
}

}

struct VideoDecoder::Impl {
    const FfmpegApi* api = nullptr;
    AVFormatContext* format = nullptr;
    AVCodecContext* codec = nullptr;
    SwsContext* scaler = nullptr;
    AVPacket* packet = nullptr;
    AVFrame* frame = nullptr;
    int stream = -1;
    double timeBase = 0.0;
    double startTime = 0.0;
    double duration = 0.0;
    double framePeriod = 1.0 / 30.0;
    int outWidth = 0;
    int outHeight = 0;
    VideoInfo info;
    bool opened = false;

    std::thread worker;
    std::mutex mutex;
    std::condition_variable wake;
    std::deque<std::shared_ptr<VideoFrame>> queue;
    std::shared_ptr<VideoFrame> shown;
    std::vector<std::shared_ptr<VideoFrame>> pool;
    std::atomic<double> want{0.0};
    std::atomic<bool> stop{false};
    std::atomic<bool> broken{false};
    std::string reason;
    unsigned long long nextId = 1;

    long long loops = 0;
    double windowStart = 0.0;
    double decodedUntil = 0.0;
    double skipUntil = -1.0;
    bool draining = false;
    bool ended = false;
    int failures = 0;

    ~Impl() { shutdown(); }

    void fail(const std::string& text) {
        std::lock_guard<std::mutex> lock(mutex);
        if (!broken.load()) {
            reason = text;
            broken.store(true);
        }
    }

    void shutdown() {
        stop.store(true);
        wake.notify_all();
        if (worker.joinable()) {
            worker.join();
        }
        if (api != nullptr) {
            if (scaler != nullptr) {
                api->sws_freeContext(scaler);
                scaler = nullptr;
            }
            if (frame != nullptr) {
                api->av_frame_free(&frame);
            }
            if (packet != nullptr) {
                api->av_packet_free(&packet);
            }
            if (codec != nullptr) {
                api->avcodec_free_context(&codec);
            }
            if (format != nullptr) {
                api->avformat_close_input(&format);
            }
        }
        opened = false;
    }

    std::shared_ptr<VideoFrame> takeBuffer() {
        for (const std::shared_ptr<VideoFrame>& candidate : pool) {
            if (candidate.use_count() == 1) {
                return candidate;
            }
        }
        if (pool.size() >= kQueueLimit + 4) {
            return nullptr;
        }
        std::shared_ptr<VideoFrame> created = std::make_shared<VideoFrame>();
        created->width = outWidth;
        created->height = outHeight;
        created->rgba.resize(static_cast<size_t>(outWidth) * static_cast<size_t>(outHeight) * 4);
        pool.push_back(created);
        return created;
    }

    void seekTo(long long wraps, double inside) {
        loops = wraps;
        const double absolute = static_cast<double>(wraps) * duration + inside;
        const double target = (inside + startTime) / timeBase;
        api->av_seek_frame(format, stream, static_cast<int64_t>(target), AVSEEK_FLAG_BACKWARD);
        api->avcodec_flush_buffers(codec);
        draining = false;
        ended = false;
        skipUntil = absolute;
        windowStart = absolute;
        decodedUntil = absolute;
        std::lock_guard<std::mutex> lock(mutex);
        queue.clear();
    }

    void seekAbsolute(double absolute) {
        absolute = std::max(absolute, 0.0);
        if (duration <= 0.0) {
            seekTo(0, absolute);
            return;
        }
        const long long wraps = static_cast<long long>(std::floor(absolute / duration));
        seekTo(wraps, std::clamp(absolute - static_cast<double>(wraps) * duration, 0.0, duration));
    }

    void convertAndPush(double absolute) {
        std::shared_ptr<VideoFrame> out;
        {
            std::lock_guard<std::mutex> lock(mutex);
            out = takeBuffer();
        }
        if (!out) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            return;
        }
        scaler = api->sws_getCachedContext(scaler, frame->width, frame->height, static_cast<AVPixelFormat>(frame->format), outWidth, outHeight, AV_PIX_FMT_RGBA, SWS_BILINEAR, nullptr, nullptr, nullptr);
        if (scaler == nullptr) {
            fail("the picture format of this video cannot be converted");
            return;
        }
        int space = SWS_CS_DEFAULT;
        if (frame->colorspace == AVCOL_SPC_BT709 || (frame->colorspace == AVCOL_SPC_UNSPECIFIED && frame->height >= 720)) {
            space = SWS_CS_ITU709;
        } else if (frame->colorspace == AVCOL_SPC_BT2020_NCL || frame->colorspace == AVCOL_SPC_BT2020_CL) {
            space = SWS_CS_BT2020;
        }
        const int sourceRange = frame->color_range == AVCOL_RANGE_JPEG ? 1 : 0;
        api->sws_setColorspaceDetails(scaler, api->sws_getCoefficients(space), sourceRange, api->sws_getCoefficients(SWS_CS_DEFAULT), 1, 0, 1 << 16, 1 << 16);
        uint8_t* destination[4] = {out->rgba.data(), nullptr, nullptr, nullptr};
        int stride[4] = {outWidth * 4, 0, 0, 0};
        api->sws_scale(scaler, frame->data, frame->linesize, 0, frame->height, destination, stride);
        out->time = absolute;
        std::lock_guard<std::mutex> lock(mutex);
        out->id = nextId++;
        queue.push_back(out);
        decodedUntil = absolute;
    }

    void step() {
        int result = api->avcodec_receive_frame(codec, frame);
        if (result >= 0) {
            int64_t stamp = frame->best_effort_timestamp;
            if (stamp == AV_NOPTS_VALUE) {
                stamp = frame->pts;
            }
            double absolute;
            if (stamp == AV_NOPTS_VALUE) {
                absolute = decodedUntil + framePeriod;
            } else {
                const double inside = static_cast<double>(stamp) * timeBase - startTime;
                absolute = static_cast<double>(loops) * duration + std::max(inside, 0.0);
            }
            if (absolute >= skipUntil - framePeriod * 0.5) {
                convertAndPush(absolute);
            }
            api->av_frame_unref(frame);
            failures = 0;
            return;
        }
        if (result == AVERROR_EOF) {
            onEnd();
            return;
        }
        if (result != AVERROR(EAGAIN)) {
            if (++failures > kMaxFailures) {
                fail("the video could not be decoded");
            }
            return;
        }
        if (draining) {
            onEnd();
            return;
        }
        const int read = api->av_read_frame(format, packet);
        if (read < 0) {
            api->avcodec_send_packet(codec, nullptr);
            draining = true;
            return;
        }
        if (packet->stream_index == stream) {
            const int sent = api->avcodec_send_packet(codec, packet);
            if (sent < 0 && sent != AVERROR(EAGAIN) && ++failures > kMaxFailures) {
                fail("the video could not be decoded");
            }
        }
        api->av_packet_unref(packet);
    }

    void onEnd() {
        if (duration > 0.0) {
            seekTo(loops + 1, 0.0);
        } else {
            ended = true;
        }
    }

    void run() {
        while (!stop.load() && !broken.load()) {
            const double target = want.load();
            bool seek = false;
            bool full = false;
            {
                std::lock_guard<std::mutex> lock(mutex);
                if (target < windowStart - kSeekBehind || target > decodedUntil + kSeekAhead) {
                    seek = true;
                }
                full = queue.size() >= kQueueLimit;
            }
            if (seek) {
                seekAbsolute(target);
                continue;
            }
            if (full || ended) {
                std::unique_lock<std::mutex> lock(mutex);
                wake.wait_for(lock, std::chrono::milliseconds(4));
                continue;
            }
            step();
        }
    }
};

VideoDecoder::VideoDecoder() : impl_(std::make_unique<Impl>()) {}

VideoDecoder::~VideoDecoder() = default;

bool VideoDecoder::open(const std::string& path, std::string& error, int maxDimension) {
    close();
    impl_ = std::make_unique<Impl>();
    Impl& d = *impl_;
    std::string problem;
    d.api = ffmpegApi(problem);
    if (d.api == nullptr) {
        error = "FFmpeg is not available";
        return false;
    }
    const FfmpegApi& api = *d.api;
    if (api.avformat_open_input(&d.format, path.c_str(), nullptr, nullptr) < 0) {
        error = "the video file cannot be opened";
        return false;
    }
    if (api.avformat_find_stream_info(d.format, nullptr) < 0) {
        error = "the video has no readable streams";
        d.shutdown();
        return false;
    }
    d.stream = api.av_find_best_stream(d.format, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (d.stream < 0) {
        error = "the file contains no video picture";
        d.shutdown();
        return false;
    }
    AVStream* stream = d.format->streams[d.stream];
    const AVCodec* decoder = api.avcodec_find_decoder(stream->codecpar->codec_id);
    if (decoder == nullptr) {
        error = std::string("this FFmpeg has no decoder for ") + api.avcodec_get_name(stream->codecpar->codec_id);
        d.shutdown();
        return false;
    }
    d.codec = api.avcodec_alloc_context3(decoder);
    if (d.codec == nullptr || api.avcodec_parameters_to_context(d.codec, stream->codecpar) < 0) {
        error = "the decoder could not be prepared";
        d.shutdown();
        return false;
    }
    d.codec->thread_count = 0;
    if (api.avcodec_open2(d.codec, decoder, nullptr) < 0) {
        error = "the decoder could not be opened";
        d.shutdown();
        return false;
    }
    const int width = stream->codecpar->width > 0 ? stream->codecpar->width : d.codec->width;
    const int height = stream->codecpar->height > 0 ? stream->codecpar->height : d.codec->height;
    if (width <= 0 || height <= 0 || width > 16384 || height > 16384) {
        error = "the video size is not supported";
        d.shutdown();
        return false;
    }
    const int limit = std::clamp(maxDimension, 64, 4096);
    const double shrink = std::min(1.0, static_cast<double>(limit) / static_cast<double>(std::max(width, height)));
    d.outWidth = std::max(2, static_cast<int>(std::lround(width * shrink)) & ~1);
    d.outHeight = std::max(2, static_cast<int>(std::lround(height * shrink)) & ~1);
    d.timeBase = stream->time_base.den > 0 ? static_cast<double>(stream->time_base.num) / static_cast<double>(stream->time_base.den) : 0.0;
    if (d.timeBase <= 0.0) {
        error = "the video has no usable clock";
        d.shutdown();
        return false;
    }
    d.startTime = stream->start_time != AV_NOPTS_VALUE ? static_cast<double>(stream->start_time) * d.timeBase : 0.0;
    double fps = 0.0;
    if (stream->avg_frame_rate.den > 0 && stream->avg_frame_rate.num > 0) {
        fps = static_cast<double>(stream->avg_frame_rate.num) / static_cast<double>(stream->avg_frame_rate.den);
    } else if (stream->r_frame_rate.den > 0 && stream->r_frame_rate.num > 0) {
        fps = static_cast<double>(stream->r_frame_rate.num) / static_cast<double>(stream->r_frame_rate.den);
    }
    d.framePeriod = fps > 1.0 ? 1.0 / fps : 1.0 / 30.0;
    double length = 0.0;
    if (d.format->duration != AV_NOPTS_VALUE && d.format->duration > 0) {
        length = static_cast<double>(d.format->duration) / static_cast<double>(AV_TIME_BASE);
    } else if (stream->duration != AV_NOPTS_VALUE && stream->duration > 0) {
        length = static_cast<double>(stream->duration) * d.timeBase;
    }
    d.duration = length;
    d.info.width = width;
    d.info.height = height;
    d.info.decodedWidth = d.outWidth;
    d.info.decodedHeight = d.outHeight;
    d.info.fps = fps;
    d.info.duration = length;
    d.info.hasAlpha = hasAlphaFormat(api, stream->codecpar->format);
    d.info.codec = api.avcodec_get_name(stream->codecpar->codec_id);
    d.packet = api.av_packet_alloc();
    d.frame = api.av_frame_alloc();
    if (d.packet == nullptr || d.frame == nullptr) {
        error = "out of memory";
        d.shutdown();
        return false;
    }
    d.opened = true;
    Impl* impl = &d;
    d.worker = std::thread([impl] { impl->run(); });
    return true;
}

void VideoDecoder::close() {
    if (impl_) {
        impl_->shutdown();
    }
    impl_ = std::make_unique<Impl>();
}

bool VideoDecoder::isOpen() const {
    return impl_ && impl_->opened;
}

bool VideoDecoder::failed() const {
    return impl_ && impl_->broken.load();
}

std::string VideoDecoder::failure() const {
    if (!impl_) {
        return std::string();
    }
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->reason;
}

const VideoInfo& VideoDecoder::info() const {
    return impl_->info;
}

std::shared_ptr<const VideoFrame> VideoDecoder::frameAt(double seconds) {
    if (!impl_ || !impl_->opened) {
        return nullptr;
    }
    Impl& d = *impl_;
    d.want.store(std::max(seconds, 0.0));
    std::lock_guard<std::mutex> lock(d.mutex);
    while (!d.queue.empty() && d.queue.front()->time <= seconds) {
        d.shown = d.queue.front();
        d.queue.pop_front();
    }
    if (!d.shown && !d.queue.empty()) {
        d.shown = d.queue.front();
        d.queue.pop_front();
    }
    return d.shown;
}

}

#else

namespace nvj {

VideoSupport videoSupport() {
    VideoSupport support;
    support.folder = ffmpegDirectory();
    support.problem = "this build was made without the FFmpeg headers";
    return support;
}

void retryVideoSupport() {}

VideoDecoder::VideoDecoder() = default;

VideoDecoder::~VideoDecoder() = default;

bool VideoDecoder::open(const std::string&, std::string& error, int) {
    error = "this build was made without video support";
    return false;
}

void VideoDecoder::close() {}

bool VideoDecoder::isOpen() const {
    return false;
}

bool VideoDecoder::failed() const {
    return false;
}

std::string VideoDecoder::failure() const {
    return std::string();
}

const VideoInfo& VideoDecoder::info() const {
    static const VideoInfo none;
    return none;
}

std::shared_ptr<const VideoFrame> VideoDecoder::frameAt(double) {
    return nullptr;
}

}

#endif
