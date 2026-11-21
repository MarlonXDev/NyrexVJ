#include "media/video_decoder.hpp"

#include "core/log.hpp"
#include "media/ffmpeg_api.hpp"

#include <filesystem>

namespace nvj {

std::string ffmpegDirectory() {
    return (std::filesystem::path(userDataDir()) / "ffmpeg").string();
}

const char* decodePathName(DecodePath path) {
    switch (path) {
    case DecodePath::Software:
        return "software";
    case DecodePath::Hardware:
        return "hardware";
    case DecodePath::HapGpu:
        return "hap-gpu";
    case DecodePath::HapCpu:
        return "hap-cpu";
    default:
        return "none";
    }
}

const char* videoDecodeModeName(VideoDecodeMode mode) {
    switch (mode) {
    case VideoDecodeMode::PreferHardware:
        return "hardware";
    case VideoDecodeMode::ForceSoftware:
        return "software";
    default:
        return "auto";
    }
}

VideoDecodeMode videoDecodeModeFromName(const std::string& name) {
    if (name == "hardware") {
        return VideoDecodeMode::PreferHardware;
    }
    if (name == "software") {
        return VideoDecodeMode::ForceSoftware;
    }
    return VideoDecodeMode::Auto;
}

}

#ifdef NYREXVJ_HAVE_FFMPEG

#include "media/hap.hpp"
#include "media/video_hardware.hpp"
#include "media/video_hardware_internal.hpp"
#include "platform/platform.hpp"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <mutex>
#include <thread>

namespace nvj {

namespace {

constexpr size_t kQueueLimit = 5;
constexpr double kSeekBehind = 0.35;
constexpr double kSeekAhead = 1.5;
constexpr int kMaxFailures = 200;
constexpr int kMaxHardwareFailures = 24;
constexpr int kProbePackets = 160;
constexpr unsigned long long kReverseBudget = 96ull * 1024ull * 1024ull;
constexpr int kMinReverseFrames = 6;
constexpr int kMaxReverseFrames = 16;

using Buffer = std::vector<uint8_t>;

bool hasAlphaFormat(const FfmpegApi& api, int format) {
    const AVPixFmtDescriptor* descriptor = api.av_pix_fmt_desc_get(static_cast<AVPixelFormat>(format));
    return descriptor != nullptr && (descriptor->flags & AV_PIX_FMT_FLAG_ALPHA) != 0;
}

int ceilShift(int value, int shift) {
    return -((-value) >> shift);
}

int decoderThreadCount(AVCodecID codec, int width, int height) {
    const int cores = static_cast<int>(std::max(1u, std::thread::hardware_concurrency()));
    int threads = std::clamp(cores / 4, 1, 4);
    if (static_cast<long long>(width) * static_cast<long long>(height) > 2600000ll) {
        threads = std::clamp(cores / 3, 2, 6);
    } else if (codec != AV_CODEC_ID_H264 && cores >= 4) {
        threads = std::max(threads, 2);
    }
    return std::min(threads, cores);
}

bool worthHardware(AVCodecID codec, int width, int height) {
    const bool heavyCodec = codec == AV_CODEC_ID_H264 || codec == AV_CODEC_ID_HEVC || codec == AV_CODEC_ID_AV1 || codec == AV_CODEC_ID_VP9;
    return heavyCodec && static_cast<long long>(width) * static_cast<long long>(height) >= 1280ll * 720ll;
}

int colorMatrix(const AVFrame& frame) {
    if (frame.colorspace == AVCOL_SPC_YCGCO) {
        return 3;
    }
    if (frame.colorspace == AVCOL_SPC_BT2020_NCL || frame.colorspace == AVCOL_SPC_BT2020_CL) {
        return 2;
    }
    if (frame.colorspace == AVCOL_SPC_BT709) {
        return 1;
    }
    if (frame.colorspace == AVCOL_SPC_SMPTE170M || frame.colorspace == AVCOL_SPC_BT470BG) {
        return 0;
    }
    return frame.height >= 720 ? 1 : 0;
}

bool describeFrame(const FfmpegApi& api, const AVFrame& frame, VideoFrame& out) {
    if (frame.width <= 0 || frame.height <= 0) {
        return false;
    }
    const AVPixelFormat format = static_cast<AVPixelFormat>(frame.format);
    const AVPixFmtDescriptor* d = api.av_pix_fmt_desc_get(format);
    if (d == nullptr) {
        return false;
    }
    constexpr uint64_t kRejected = AV_PIX_FMT_FLAG_RGB | AV_PIX_FMT_FLAG_PAL | AV_PIX_FMT_FLAG_BITSTREAM | AV_PIX_FMT_FLAG_BE | AV_PIX_FMT_FLAG_HWACCEL | AV_PIX_FMT_FLAG_FLOAT;
    if ((d->flags & kRejected) != 0 || (d->flags & AV_PIX_FMT_FLAG_PLANAR) == 0) {
        return false;
    }
    const bool alpha = (d->flags & AV_PIX_FMT_FLAG_ALPHA) != 0;
    if (d->nb_components != (alpha ? 4 : 3)) {
        return false;
    }
    const int depth = d->comp[0].depth;
    if (depth < 8 || depth > 16) {
        return false;
    }
    for (int i = 1; i < d->nb_components; ++i) {
        if (d->comp[i].depth != depth) {
            return false;
        }
    }
    const int bytes = depth > 8 ? 2 : 1;
    const int shift = d->comp[0].shift;
    if (shift != 0 && shift != 16 - depth) {
        return false;
    }
    const bool semi = d->comp[1].plane == d->comp[2].plane;
    int planeCount = 0;
    if (semi) {
        if (alpha || d->comp[1].plane != 1 || d->comp[1].step != 2 * bytes || d->comp[2].step != 2 * bytes || d->comp[1].offset != 0 || d->comp[2].offset != bytes || d->comp[0].plane != 0 || d->comp[0].step != bytes) {
            return false;
        }
        planeCount = 2;
    } else {
        for (int i = 0; i < d->nb_components; ++i) {
            if (d->comp[i].plane != i || d->comp[i].step != bytes || d->comp[i].offset != 0) {
                return false;
            }
        }
        planeCount = d->nb_components;
    }
    for (int i = 0; i < planeCount; ++i) {
        if (frame.data[i] == nullptr || frame.linesize[i] <= 0) {
            return false;
        }
    }
    const int chromaWidth = ceilShift(frame.width, d->log2_chroma_w);
    const int chromaHeight = ceilShift(frame.height, d->log2_chroma_h);
    out.layout = semi ? VideoLayout::SemiPlanar : VideoLayout::Planar;
    out.compressed = CompressedFormat::None;
    out.planeCount = planeCount;
    out.planes = {};
    out.planes[0] = {frame.data[0], frame.width, frame.height, frame.linesize[0], 1, bytes};
    if (semi) {
        out.planes[1] = {frame.data[1], chromaWidth, chromaHeight, frame.linesize[1], 2, bytes};
    } else {
        out.planes[1] = {frame.data[1], chromaWidth, chromaHeight, frame.linesize[1], 1, bytes};
        out.planes[2] = {frame.data[2], chromaWidth, chromaHeight, frame.linesize[2], 1, bytes};
        if (alpha) {
            out.planes[3] = {frame.data[3], frame.width, frame.height, frame.linesize[3], 1, bytes};
        }
    }
    out.width = frame.width;
    out.height = frame.height;
    out.bitDepth = depth;
    out.msbAligned = shift != 0;
    const char* name = api.av_get_pix_fmt_name(format);
    out.matrix = colorMatrix(frame);
    out.fullRange = out.matrix == 3 || frame.color_range == AVCOL_RANGE_JPEG || (name != nullptr && std::strncmp(name, "yuvj", 4) == 0);
    out.hasAlpha = alpha;
    return true;
}

bool convertYcocgToRgba(const FfmpegApi& api, const AVFrame& frame, std::vector<uint8_t>& rgba) {
    const AVPixFmtDescriptor* d = api.av_pix_fmt_desc_get(static_cast<AVPixelFormat>(frame.format));
    if (d == nullptr || (d->flags & (AV_PIX_FMT_FLAG_PLANAR | AV_PIX_FMT_FLAG_RGB | AV_PIX_FMT_FLAG_BE)) != AV_PIX_FMT_FLAG_PLANAR || d->nb_components < 3 || d->nb_components > 4) {
        return false;
    }
    for (int i = 0; i < d->nb_components; ++i) {
        if (d->comp[i].plane != i || d->comp[i].step != 1 || d->comp[i].offset != 0 || d->comp[i].depth != 8 || frame.data[i] == nullptr || frame.linesize[i] <= 0) {
            return false;
        }
    }
    const bool alpha = d->nb_components == 4;
    rgba.resize(static_cast<size_t>(frame.width) * static_cast<size_t>(frame.height) * 4u);
    uint8_t* out = rgba.data();
    for (int y = 0; y < frame.height; ++y) {
        const int cy = y >> d->log2_chroma_h;
        const uint8_t* luma = frame.data[0] + static_cast<ptrdiff_t>(y) * frame.linesize[0];
        const uint8_t* cg = frame.data[1] + static_cast<ptrdiff_t>(cy) * frame.linesize[1];
        const uint8_t* co = frame.data[2] + static_cast<ptrdiff_t>(cy) * frame.linesize[2];
        const uint8_t* opacity = alpha ? frame.data[3] + static_cast<ptrdiff_t>(y) * frame.linesize[3] : nullptr;
        for (int x = 0; x < frame.width; ++x) {
            const int cx = x >> d->log2_chroma_w;
            const int lumaValue = luma[x];
            const int g = cg[cx] - 128;
            const int o = co[cx] - 128;
            out[0] = static_cast<uint8_t>(std::clamp(lumaValue + o - g, 0, 255));
            out[1] = static_cast<uint8_t>(std::clamp(lumaValue + g, 0, 255));
            out[2] = static_cast<uint8_t>(std::clamp(lumaValue - g - o, 0, 255));
            out[3] = opacity != nullptr ? opacity[x] : 255;
            out += 4;
        }
    }
    return true;
}

}

struct VideoDecoder::Impl {
    const FfmpegApi* api = nullptr;
    VideoOptions options;
    AVFormatContext* format = nullptr;
    AVCodecContext* codec = nullptr;
    SwsContext* scaler = nullptr;
    AVPacket* packet = nullptr;
    AVFrame* frame = nullptr;
    HardwareSetup hardware;
    bool hardwareActive = false;
    bool hapDirect = false;
    CompressedFormat hapFormat = CompressedFormat::None;
    size_t hapBytes = 0;
    int stream = -1;
    double timeBase = 0.0;
    double startTime = 0.0;
    double duration = 0.0;
    double framePeriod = 1.0 / 30.0;
    int sourceWidth = 0;
    int sourceHeight = 0;
    int outWidth = 0;
    int outHeight = 0;
    VideoInfo info;
    VideoStatus status;
    bool opened = false;

    std::thread worker;
    std::mutex mutex;
    std::condition_variable wake;
    std::deque<std::shared_ptr<VideoFrame>> queue;
    std::shared_ptr<VideoFrame> shown;
    std::vector<std::shared_ptr<Buffer>> pool;
    std::vector<uint8_t> ycocgScratch;
    std::atomic<double> want{0.0};
    std::atomic<bool> stop{false};
    std::atomic<bool> broken{false};
    std::atomic<double> decodeMs{0.0};
    std::atomic<unsigned long long> decoded{0};
    std::atomic<unsigned long long> dropped{0};
    std::atomic<unsigned long long> underruns{0};
    std::string reason;
    std::string pixelFormat;
    unsigned long long nextId = 1;

    double rangeIn = 0.0;
    double rangeLen = 0.0;
    bool rangeCustom = false;
    double pendingIn = 0.0;
    double pendingLen = 0.0;
    bool pendingCustom = false;
    bool configDirty = false;
    bool jumpPending = false;
    bool seekPending = false;
    bool fallbackShown = false;
    bool reverseMode = false;
    bool collecting = false;
    bool segmentDone = false;
    double reverseLimit = 0.0;
    double skipTolerance = 0.5;
    int stepEpoch = 0;
    size_t frameBytes = 0;
    size_t poolLimit = kQueueLimit + 4;
    std::vector<std::shared_ptr<VideoFrame>> collected;
    std::atomic<int> epoch{0};
    std::atomic<bool> reverseWanted{false};
    std::atomic<int> reverseWindow{8};
    std::atomic<unsigned long long> seeks{0};

    long long loops = 0;
    double windowStart = 0.0;
    double decodedUntil = 0.0;
    double skipUntil = -1.0;
    bool draining = false;
    bool ended = false;
    bool starving = false;
    int failures = 0;
    double workSeconds = 0.0;
    bool announcedFormat = false;

    ~Impl() { shutdown(); }

    int failureLimit() const {
        return hardwareActive ? kMaxHardwareFailures : kMaxFailures;
    }

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
            releaseHardware(*api, hardware);
            if (format != nullptr) {
                api->avformat_close_input(&format);
            }
        }
        opened = false;
    }

    std::shared_ptr<Buffer> takeBuffer(size_t size) {
        for (const std::shared_ptr<Buffer>& candidate : pool) {
            if (candidate.use_count() == 1) {
                if (candidate->size() != size) {
                    candidate->assign(size, 0);
                }
                return candidate;
            }
        }
        if (pool.size() >= poolLimit) {
            return nullptr;
        }
        std::shared_ptr<Buffer> created = std::make_shared<Buffer>(size, 0);
        pool.push_back(created);
        return created;
    }

    void rewindTo(double inside) {
        const double target = (inside + rangeIn + startTime) / timeBase;
        api->av_seek_frame(format, stream, static_cast<int64_t>(target), AVSEEK_FLAG_BACKWARD);
        if (codec != nullptr) {
            api->avcodec_flush_buffers(codec);
        }
        draining = false;
        ended = false;
        workSeconds = 0.0;
        seeks.fetch_add(1);
    }

    void seekTo(long long wraps, double inside, bool keepQueue = false) {
        loops = wraps;
        const double absolute = static_cast<double>(wraps) * rangeLen + inside;
        rewindTo(inside);
        skipTolerance = keepQueue ? 0.5 : 1.001;
        skipUntil = absolute;
        std::lock_guard<std::mutex> lock(mutex);
        decodedUntil = absolute;
        if (!keepQueue) {
            queue.clear();
        }
        windowStart = queue.empty() ? absolute : std::min(absolute, queue.front()->time);
    }

    void seekAbsolute(double absolute) {
        absolute = std::max(absolute, 0.0);
        if (rangeLen <= 0.0) {
            seekTo(0, absolute);
            return;
        }
        const long long wraps = static_cast<long long>(std::floor(absolute / rangeLen));
        seekTo(wraps, std::clamp(absolute - static_cast<double>(wraps) * rangeLen, 0.0, rangeLen));
    }

    void measureFrame(const VideoFrame& out) {
        size_t total = 0;
        if (hapDirect) {
            total = hapBytes;
        } else {
            for (int i = 0; i < out.planeCount; ++i) {
                total += out.planes[static_cast<size_t>(i)].size();
            }
        }
        frameBytes = std::max<size_t>(total, 1);
        const unsigned long long fit = kReverseBudget / (static_cast<unsigned long long>(frameBytes) * 3ull);
        reverseWindow.store(static_cast<int>(std::clamp<unsigned long long>(fit, static_cast<unsigned long long>(kMinReverseFrames), static_cast<unsigned long long>(kMaxReverseFrames))));
        poolLimit = std::max<size_t>(kQueueLimit + 4, static_cast<size_t>(reverseWindow.load()) * 3 + 4);
    }

    void recordWork(double seconds) {
        workSeconds += seconds;
    }

    void push(std::shared_ptr<VideoFrame> out, double absolute) {
        out->time = absolute;
        out->outWidth = outWidth;
        out->outHeight = outHeight;
        if (frameBytes == 0) {
            measureFrame(*out);
        }
        if (collecting) {
            collected.push_back(std::move(out));
            if (collected.size() > static_cast<size_t>(reverseWindow.load())) {
                collected.erase(collected.begin());
            }
            return;
        }
        const double ms = workSeconds * 1000.0;
        workSeconds = 0.0;
        const double previous = decodeMs.load();
        decodeMs.store(previous <= 0.0 ? ms : previous * 0.9 + ms * 0.1);
        std::lock_guard<std::mutex> lock(mutex);
        if (epoch.load() != stepEpoch) {
            return;
        }
        decoded.fetch_add(1);
        out->id = nextId++;
        queue.push_back(std::move(out));
        decodedUntil = absolute;
    }

    void announce(const std::string& text) {
        if (announcedFormat) {
            return;
        }
        announcedFormat = true;
        std::lock_guard<std::mutex> lock(mutex);
        pixelFormat = text;
    }

    void convertToRgba(const AVFrame& source, double absolute) {
        std::shared_ptr<Buffer> buffer;
        {
            std::lock_guard<std::mutex> lock(mutex);
            buffer = takeBuffer(static_cast<size_t>(outWidth) * static_cast<size_t>(outHeight) * 4);
        }
        if (!buffer) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            return;
        }
        const bool ycocg = source.colorspace == AVCOL_SPC_YCGCO && convertYcocgToRgba(*api, source, ycocgScratch);
        const AVPixelFormat inputFormat = ycocg ? AV_PIX_FMT_RGBA : static_cast<AVPixelFormat>(source.format);
        scaler = api->sws_getCachedContext(scaler, source.width, source.height, inputFormat, outWidth, outHeight, AV_PIX_FMT_RGBA, SWS_BILINEAR, nullptr, nullptr, nullptr);
        if (scaler == nullptr) {
            fail("the picture format of this video cannot be converted");
            return;
        }
        uint8_t* destination[4] = {buffer->data(), nullptr, nullptr, nullptr};
        int stride[4] = {outWidth * 4, 0, 0, 0};
        if (ycocg) {
            const uint8_t* planes[4] = {ycocgScratch.data(), nullptr, nullptr, nullptr};
            const int planeStride[4] = {source.width * 4, 0, 0, 0};
            api->sws_scale(scaler, planes, planeStride, 0, source.height, destination, stride);
        } else {
            int space = SWS_CS_DEFAULT;
            if (source.colorspace == AVCOL_SPC_BT709 || (source.colorspace == AVCOL_SPC_UNSPECIFIED && source.height >= 720)) {
                space = SWS_CS_ITU709;
            } else if (source.colorspace == AVCOL_SPC_BT2020_NCL || source.colorspace == AVCOL_SPC_BT2020_CL) {
                space = SWS_CS_BT2020;
            }
            const int sourceRange = source.color_range == AVCOL_RANGE_JPEG ? 1 : 0;
            api->sws_setColorspaceDetails(scaler, api->sws_getCoefficients(space), sourceRange, api->sws_getCoefficients(SWS_CS_DEFAULT), 1, 0, 1 << 16, 1 << 16);
            api->sws_scale(scaler, source.data, source.linesize, 0, source.height, destination, stride);
        }
        std::shared_ptr<VideoFrame> out = std::make_shared<VideoFrame>();
        out->width = outWidth;
        out->height = outHeight;
        out->layout = VideoLayout::Rgba;
        out->planeCount = 1;
        out->planes[0] = {buffer->data(), outWidth, outHeight, outWidth * 4, 4, 1};
        out->hasAlpha = hasAlphaFormat(*api, source.format);
        out->owner = buffer;
        const char* name = api->av_get_pix_fmt_name(static_cast<AVPixelFormat>(source.format));
        announce(std::string(name != nullptr ? name : "unknown") + " (processor conversion)");
        push(std::move(out), absolute);
    }

    void produce(double absolute) {
        AVFrame* source = frame;
        AVFrame* download = nullptr;
        if (hardwareActive && frame->format == static_cast<int>(hardware.format)) {
            download = api->av_frame_alloc();
            if (download == nullptr) {
                return;
            }
            const int moved = api->av_hwframe_transfer_data(download, frame, 0);
            if (moved < 0) {
                api->av_frame_free(&download);
                if (++failures > failureLimit()) {
                    fail("the picture could not be read back from the graphics card");
                }
                return;
            }
            api->av_frame_copy_props(download, frame);
            download->width = frame->width;
            download->height = frame->height;
            source = download;
        }
        VideoFrame description;
        bool handled = false;
        if (options.gpuConversion && describeFrame(*api, *source, description)) {
            AVFrame* keep = download;
            if (keep == nullptr) {
                keep = api->av_frame_alloc();
                if (keep != nullptr) {
                    api->av_frame_move_ref(keep, source);
                }
            }
            if (keep != nullptr) {
                std::shared_ptr<VideoFrame> out = std::make_shared<VideoFrame>(description);
                const FfmpegApi* shared = api;
                out->owner = std::shared_ptr<void>(keep, [shared](void* raw) {
                    AVFrame* released = static_cast<AVFrame*>(raw);
                    shared->av_frame_free(&released);
                });
                const char* name = api->av_get_pix_fmt_name(static_cast<AVPixelFormat>(keep->format));
                announce(name != nullptr ? name : "unknown");
                push(std::move(out), absolute);
                handled = true;
                download = nullptr;
            }
        }
        if (!handled) {
            convertToRgba(*source, absolute);
        }
        if (download != nullptr) {
            api->av_frame_free(&download);
        }
    }

    void produceHap(const AVPacket& input, double absolute) {
        std::shared_ptr<Buffer> buffer;
        {
            std::lock_guard<std::mutex> lock(mutex);
            buffer = takeBuffer(hapBytes);
        }
        if (!buffer) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            return;
        }
        HapFrameInfo frameInfo;
        std::string problem;
        const HapResult result = hapDecode(input.data, static_cast<size_t>(input.size), sourceWidth, sourceHeight, buffer->data(), buffer->size(), frameInfo, &problem);
        if (result != HapResult::Ok || frameInfo.format != hapFormat) {
            if (++failures > failureLimit()) {
                fail(problem.empty() ? "the HAP video could not be read" : problem);
            }
            return;
        }
        failures = 0;
        const int codedWidth = hapCodedSize(sourceWidth);
        const int codedHeight = hapCodedSize(sourceHeight);
        const int blockBytes = hapFormat == CompressedFormat::Dxt1 ? 8 : 16;
        std::shared_ptr<VideoFrame> out = std::make_shared<VideoFrame>();
        out->width = sourceWidth;
        out->height = sourceHeight;
        out->layout = VideoLayout::Compressed;
        out->compressed = hapFormat;
        out->planeCount = 1;
        out->planes[0] = {buffer->data(), codedWidth, codedHeight, codedWidth / 4 * blockBytes, 1, 1};
        out->hasAlpha = hapFormat == CompressedFormat::Dxt5;
        out->owner = buffer;
        announce(hapVariantName(hapFormat));
        push(std::move(out), absolute);
    }

    double timestampOf(int64_t stamp) const {
        const double inside = static_cast<double>(stamp) * timeBase - startTime - rangeIn;
        return static_cast<double>(loops) * rangeLen + (rangeIn > 0.0 ? inside : std::max(inside, 0.0));
    }

    bool admit(double absolute) {
        if (collecting && absolute >= reverseLimit) {
            segmentDone = true;
            return false;
        }
        if (rangeCustom && absolute - static_cast<double>(loops) * rangeLen >= rangeLen - framePeriod * 0.05) {
            onEnd();
            return false;
        }
        return absolute >= skipUntil - framePeriod * skipTolerance;
    }

    void step() {
        const auto begin = std::chrono::steady_clock::now();
        stepInner();
        recordWork(std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count());
    }

    void stepInner() {
        if (hapDirect) {
            stepHap();
            return;
        }
        int result = api->avcodec_receive_frame(codec, frame);
        if (result >= 0) {
            int64_t stamp = frame->best_effort_timestamp;
            if (stamp == AV_NOPTS_VALUE) {
                stamp = frame->pts;
            }
            const double absolute = stamp == AV_NOPTS_VALUE ? decodedUntil + framePeriod : timestampOf(stamp);
            if (admit(absolute)) {
                produce(absolute);
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
            if (++failures > failureLimit()) {
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
            if (sent < 0 && sent != AVERROR(EAGAIN) && ++failures > failureLimit()) {
                fail("the video could not be decoded");
            }
        }
        api->av_packet_unref(packet);
    }

    void stepHap() {
        const int read = api->av_read_frame(format, packet);
        if (read < 0) {
            onEnd();
            return;
        }
        if (packet->stream_index == stream && packet->size > 0) {
            int64_t stamp = packet->pts;
            if (stamp == AV_NOPTS_VALUE) {
                stamp = packet->dts;
            }
            const double absolute = stamp == AV_NOPTS_VALUE ? decodedUntil + framePeriod : timestampOf(stamp);
            if (admit(absolute)) {
                produceHap(*packet, absolute);
            }
        }
        api->av_packet_unref(packet);
    }

    void onEnd() {
        if (collecting) {
            segmentDone = true;
        } else if (rangeLen > 0.0) {
            seekTo(loops + 1, 0.0, true);
        } else {
            ended = true;
        }
    }

    bool syncConfig() {
        bool changed = false;
        std::lock_guard<std::mutex> lock(mutex);
        stepEpoch = epoch.load();
        if (configDirty) {
            configDirty = false;
            rangeIn = pendingIn;
            rangeLen = pendingLen;
            rangeCustom = pendingCustom;
            changed = true;
        }
        if (reverseWanted.load() != reverseMode) {
            reverseMode = reverseWanted.load();
            changed = true;
        }
        if (jumpPending) {
            jumpPending = false;
            changed = true;
        }
        return changed;
    }

    void pause() {
        std::unique_lock<std::mutex> lock(mutex);
        wake.wait_for(lock, std::chrono::milliseconds(4));
    }

    void decodeSegment(double limit) {
        const double lowest = std::max(limit - framePeriod * static_cast<double>(reverseWindow.load()), 0.0);
        const auto begin = std::chrono::steady_clock::now();
        loops = 0;
        reverseLimit = limit;
        segmentDone = false;
        collected.clear();
        collecting = true;
        rewindTo(lowest);
        skipTolerance = 1.001;
        skipUntil = lowest;
        while (!stop.load() && !broken.load() && !segmentDone && epoch.load() == stepEpoch) {
            step();
        }
        collecting = false;
        size_t inserted = 0;
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (epoch.load() == stepEpoch) {
                for (auto it = collected.rbegin(); it != collected.rend(); ++it) {
                    if (!queue.empty() && (*it)->time >= queue.back()->time - 1.0e-9) {
                        continue;
                    }
                    (*it)->id = nextId++;
                    queue.push_back(*it);
                    ++inserted;
                }
            }
        }
        collected.clear();
        workSeconds = 0.0;
        if (inserted > 0) {
            decoded.fetch_add(inserted);
            const double ms = std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count() * 1000.0 / static_cast<double>(inserted);
            const double previous = decodeMs.load();
            decodeMs.store(previous <= 0.0 ? ms : previous * 0.9 + ms * 0.1);
        } else {
            pause();
        }
    }

    void reverseStep() {
        if (rangeLen <= 0.0) {
            pause();
            return;
        }
        const double target = std::clamp(want.load(), 0.0, rangeLen);
        size_t count = 0;
        double high = 0.0;
        double low = 0.0;
        {
            std::lock_guard<std::mutex> lock(mutex);
            count = queue.size();
            if (count > 0) {
                high = queue.front()->time;
                low = queue.back()->time;
            }
        }
        const bool restart = count == 0 || target > high + framePeriod * 1.5 || low > target + framePeriod * 0.75;
        double limit = 0.0;
        if (restart) {
            limit = std::min(target + framePeriod * 0.5, rangeLen);
            std::lock_guard<std::mutex> lock(mutex);
            queue.clear();
        } else {
            const double lookahead = framePeriod * static_cast<double>(std::max(1, reverseWindow.load() / 2));
            if (low <= framePeriod * 0.5 || target - low >= lookahead) {
                pause();
                return;
            }
            limit = low - framePeriod * 0.5;
        }
        decodeSegment(limit);
    }

    void run() {
        platform::setCurrentThreadBackground(true);
        while (!stop.load() && !broken.load()) {
            if (syncConfig()) {
                if (!reverseMode) {
                    seekAbsolute(want.load());
                }
                continue;
            }
            if (reverseMode) {
                reverseStep();
                continue;
            }
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

    bool probeHardware(AVCodecContext* context, std::string& problem) {
        AVPacket* probe = api->av_packet_alloc();
        AVFrame* picture = api->av_frame_alloc();
        bool success = false;
        if (probe == nullptr || picture == nullptr) {
            problem = "out of memory";
        } else {
            for (int i = 0; i < kProbePackets && !success; ++i) {
                const int read = api->av_read_frame(format, probe);
                if (read < 0) {
                    problem = "the stream ended before a picture was decoded";
                    break;
                }
                if (probe->stream_index == stream) {
                    const int sent = api->avcodec_send_packet(context, probe);
                    if (sent < 0 && sent != AVERROR(EAGAIN)) {
                        problem = "the graphics card refused the stream (" + ffmpegError(*api, sent) + ")";
                        api->av_packet_unref(probe);
                        break;
                    }
                    const int received = api->avcodec_receive_frame(context, picture);
                    if (received >= 0) {
                        if (picture->format != static_cast<int>(hardware.format)) {
                            problem = "the graphics card did not take over the decoding";
                        } else {
                            AVFrame* check = api->av_frame_alloc();
                            if (check != nullptr && api->av_hwframe_transfer_data(check, picture, 0) >= 0) {
                                success = true;
                            } else {
                                problem = "the picture could not be read back from the graphics card";
                            }
                            if (check != nullptr) {
                                api->av_frame_free(&check);
                            }
                        }
                        api->av_frame_unref(picture);
                        api->av_packet_unref(probe);
                        break;
                    }
                    if (received != AVERROR(EAGAIN)) {
                        problem = "the graphics card could not decode the stream (" + ffmpegError(*api, received) + ")";
                        api->av_packet_unref(probe);
                        break;
                    }
                }
                api->av_packet_unref(probe);
            }
            if (!success && problem.empty()) {
                problem = "no picture came out of the graphics card";
            }
        }
        if (picture != nullptr) {
            api->av_frame_free(&picture);
        }
        if (probe != nullptr) {
            api->av_packet_free(&probe);
        }
        return success;
    }

    std::shared_ptr<const VideoFrame> pickForward(double seconds, bool& wake) {
        if (shown && !seekPending && !fallbackShown && seconds < shown->time - framePeriod * 0.001) {
            seekPending = true;
            jumpPending = true;
            epoch.fetch_add(1);
            queue.clear();
            wake = true;
            return shown;
        }
        int popped = 0;
        const double tolerance = framePeriod * 0.001;
        while (!queue.empty() && queue.front()->time <= seconds + tolerance) {
            shown = queue.front();
            queue.pop_front();
            ++popped;
            fallbackShown = false;
        }
        const bool resumed = seekPending;
        if ((!shown || seekPending) && popped == 0 && !queue.empty()) {
            shown = queue.front();
            queue.pop_front();
            popped = 1;
            fallbackShown = true;
        }
        if (popped > 0) {
            seekPending = false;
        }
        if (popped > 1 && !resumed) {
            dropped.fetch_add(static_cast<unsigned long long>(popped - 1));
        }
        if (shown) {
            const bool late = queue.empty() && seconds - shown->time > framePeriod * 2.0;
            if (late && !starving) {
                underruns.fetch_add(1);
            }
            starving = late;
        }
        return shown;
    }

    std::shared_ptr<const VideoFrame> pickReverse(double seconds) {
        const double tolerance = framePeriod * 0.001;
        while (queue.size() > 1 && queue.front()->time > seconds + tolerance) {
            if (queue.front() != shown) {
                dropped.fetch_add(1);
            }
            queue.pop_front();
        }
        bool covered = false;
        if (!queue.empty()) {
            if (queue.front()->time <= seconds + tolerance) {
                shown = queue.front();
                covered = true;
            } else if (!shown) {
                shown = queue.front();
            }
        }
        if (!covered && !starving) {
            underruns.fetch_add(1);
        }
        starving = !covered;
        return shown;
    }

    void rewind() {
        api->av_seek_frame(format, stream, static_cast<int64_t>(startTime / timeBase), AVSEEK_FLAG_BACKWARD);
    }
};

VideoDecoder::VideoDecoder() : impl_(std::make_unique<Impl>()) {}

VideoDecoder::~VideoDecoder() = default;

bool VideoDecoder::open(const std::string& path, std::string& error, const VideoOptions& options) {
    close();
    impl_ = std::make_unique<Impl>();
    Impl& d = *impl_;
    d.options = options;
    std::string problem;
    d.api = ffmpegApi(problem);
    if (d.api == nullptr) {
        error = "FFmpeg is not available";
        return false;
    }
    platform::setCurrentThreadBackground(true);
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
    const AVCodecID codecId = stream->codecpar->codec_id;
    const AVCodec* decoder = api.avcodec_find_decoder(codecId);
    if (decoder == nullptr) {
        error = std::string("this FFmpeg has no decoder for ") + api.avcodec_get_name(codecId);
        d.shutdown();
        return false;
    }
    const int width = stream->codecpar->width;
    const int height = stream->codecpar->height;
    if (width <= 0 || height <= 0 || width > 16384 || height > 16384) {
        error = "the video size is not supported";
        d.shutdown();
        return false;
    }
    d.sourceWidth = width;
    d.sourceHeight = height;
    const int limit = std::clamp(options.maxDimension, 64, 4096);
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
    d.rangeLen = length;
    d.pendingLen = length;
    d.info.width = width;
    d.info.height = height;
    d.info.decodedWidth = d.outWidth;
    d.info.decodedHeight = d.outHeight;
    d.info.fps = fps;
    d.info.duration = length;
    d.info.hasAlpha = hasAlphaFormat(api, stream->codecpar->format);
    d.info.codec = api.avcodec_get_name(codecId);
    d.status.codec = d.info.codec;
    d.packet = api.av_packet_alloc();
    d.frame = api.av_frame_alloc();
    if (d.packet == nullptr || d.frame == nullptr) {
        error = "out of memory";
        d.shutdown();
        return false;
    }

    if (d.info.codec == "hap") {
        CompressedFormat tagged = CompressedFormat::None;
        std::string hapReason;
        bool direct = false;
        if (!options.hapDirect) {
            hapReason = "HAP direct upload is turned off";
        } else if (!options.compressedTextures) {
            hapReason = "this graphics driver cannot show compressed textures";
        } else if (!hapFormatFromFourcc(stream->codecpar->codec_tag, tagged)) {
            hapReason = "this HAP variant is not handled by the direct path";
        } else {
            HapFrameInfo inspected;
            HapResult inspectResult = HapResult::Corrupt;
            for (int i = 0; i < kProbePackets; ++i) {
                if (api.av_read_frame(d.format, d.packet) < 0) {
                    break;
                }
                if (d.packet->stream_index == d.stream && d.packet->size > 0) {
                    inspectResult = hapInspect(d.packet->data, static_cast<size_t>(d.packet->size), inspected);
                    api.av_packet_unref(d.packet);
                    break;
                }
                api.av_packet_unref(d.packet);
            }
            d.rewind();
            if (inspectResult == HapResult::Ok && inspected.format == tagged) {
                direct = true;
            } else {
                hapReason = "the frames of this HAP file need the processor path";
            }
        }
        if (direct) {
            d.hapDirect = true;
            d.hapFormat = tagged;
            d.hapBytes = hapTextureBytes(tagged, width, height);
            d.info.hasAlpha = tagged == CompressedFormat::Dxt5;
            d.status.path = DecodePath::HapGpu;
            d.status.backend = "gpu";
            d.status.decoder = "HAP direct upload";
            d.status.variant = hapVariantName(tagged);
            d.status.threads = 1;
        } else {
            d.status.fallbackReason = hapReason;
            d.status.variant = "HAP";
        }
    }

    if (!d.hapDirect) {
        const bool hapSoftware = d.info.codec == "hap";
        const bool softwareOnly = !softwareOnlyCodecLabel(d.info.codec).empty();
        const bool wantHardware = !hapSoftware && !softwareOnly && options.mode != VideoDecodeMode::ForceSoftware && (options.mode == VideoDecodeMode::PreferHardware || worthHardware(codecId, width, height));
        d.status.hardwareWanted = wantHardware;
        std::string failures;
        if (wantHardware) {
            for (const std::string& backend : hardwareBackendOrder()) {
                HardwareSetup setup;
                std::string why;
                if (!prepareHardware(api, codecId, backend, setup, why)) {
                    failures += (failures.empty() ? "" : "; ") + hardwareBackendLabel(backend) + ": " + why;
                    continue;
                }
                AVCodecContext* context = nullptr;
                if (!openHardwareContext(api, stream->codecpar, setup, context, why)) {
                    releaseHardware(api, setup);
                    failures += (failures.empty() ? "" : "; ") + hardwareBackendLabel(backend) + ": " + why;
                    continue;
                }
                if (!d.probeHardware(context, why)) {
                    api.avcodec_free_context(&context);
                    releaseHardware(api, setup);
                    d.rewind();
                    failures += (failures.empty() ? "" : "; ") + hardwareBackendLabel(backend) + ": " + why;
                    continue;
                }
                d.codec = context;
                d.hardware = setup;
                d.hardwareActive = true;
                api.avcodec_flush_buffers(d.codec);
                d.seekTo(0, 0.0);
                d.status.path = DecodePath::Hardware;
                d.status.backend = backend;
                d.status.decoder = setup.decoder->name;
                d.status.threads = 1;
                break;
            }
        }
        if (!d.hardwareActive) {
            d.codec = api.avcodec_alloc_context3(decoder);
            if (d.codec == nullptr || api.avcodec_parameters_to_context(d.codec, stream->codecpar) < 0) {
                error = "the decoder could not be prepared";
                d.shutdown();
                return false;
            }
            const int threads = decoderThreadCount(codecId, width, height);
            d.codec->thread_count = threads;
            if (api.avcodec_open2(d.codec, decoder, nullptr) < 0) {
                error = "the decoder could not be opened";
                d.shutdown();
                return false;
            }
            d.status.path = hapSoftware ? DecodePath::HapCpu : DecodePath::Software;
            d.status.backend = "software";
            d.status.decoder = decoder->name;
            d.status.threads = threads;
            if (softwareOnly) {
                d.status.softwareOnly = true;
                if (options.mode == VideoDecodeMode::PreferHardware) {
                    d.status.fallbackReason = softwareOnlyReason(d.info.codec);
                }
            } else if (!options.hardwareFailure.empty()) {
                d.status.fallbackReason = "the hardware decoder stopped working during playback";
                d.status.fallbackDetail = options.hardwareFailure;
            } else if (!failures.empty()) {
                d.status.fallbackReason = "no hardware decoder worked on this computer";
                d.status.fallbackDetail = failures;
            }
        }
    }
    if (!d.status.fallbackReason.empty()) {
        logLine("Video '" + path + "': " + d.status.fallbackReason + (d.status.fallbackDetail.empty() ? std::string() : " (" + d.status.fallbackDetail + ")"));
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

VideoStatus VideoDecoder::status() const {
    VideoStatus result;
    if (!impl_) {
        return result;
    }
    Impl& d = *impl_;
    {
        std::lock_guard<std::mutex> lock(d.mutex);
        result = d.status;
        result.pixelFormat = d.pixelFormat;
    }
    result.decodeMs = d.decodeMs.load();
    result.decoded = d.decoded.load();
    result.dropped = d.dropped.load();
    result.underruns = d.underruns.load();
    result.seeks = d.seeks.load();
    result.reverse = d.reverseWanted.load();
    result.reverseFrames = d.reverseWindow.load();
    {
        std::lock_guard<std::mutex> lock(d.mutex);
        result.rangeIn = d.pendingIn;
        result.rangeOut = d.pendingIn + d.pendingLen;
    }
    return result;
}

std::shared_ptr<const VideoFrame> VideoDecoder::frameAt(double seconds) {
    if (!impl_ || !impl_->opened) {
        return nullptr;
    }
    Impl& d = *impl_;
    seconds = std::max(seconds, 0.0);
    d.want.store(seconds);
    bool wake = false;
    std::shared_ptr<const VideoFrame> result;
    {
        std::lock_guard<std::mutex> lock(d.mutex);
        if (d.reverseWanted.load()) {
            result = d.pickReverse(seconds);
        } else {
            result = d.pickForward(seconds, wake);
        }
    }
    if (wake) {
        d.wake.notify_all();
    }
    return result;
}

void VideoDecoder::setRange(double in, double out) {
    if (!impl_ || !impl_->opened || impl_->duration <= 0.0) {
        return;
    }
    Impl& d = *impl_;
    double low = std::clamp(in, 0.0, d.duration);
    double high = out > 0.0 ? std::min(out, d.duration) : d.duration;
    if (high < low + d.framePeriod) {
        high = std::min(low + d.framePeriod, d.duration);
        low = std::max(high - d.framePeriod, 0.0);
    }
    bool custom = low > d.framePeriod * 0.01 || high < d.duration - d.framePeriod * 0.01;
    if (!custom) {
        low = 0.0;
        high = d.duration;
    }
    {
        std::lock_guard<std::mutex> lock(d.mutex);
        if (d.pendingIn == low && d.pendingLen == high - low && d.pendingCustom == custom) {
            return;
        }
        d.pendingIn = low;
        d.pendingLen = high - low;
        d.pendingCustom = custom;
        d.configDirty = true;
        d.epoch.fetch_add(1);
        d.queue.clear();
        d.shown.reset();
        d.seekPending = false;
        d.fallbackShown = false;
    }
    d.wake.notify_all();
}

void VideoDecoder::setDirection(bool reverse) {
    if (!impl_ || !impl_->opened) {
        return;
    }
    Impl& d = *impl_;
    if (d.reverseWanted.load() == reverse || (reverse && d.duration <= 0.0)) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(d.mutex);
        d.reverseWanted.store(reverse);
        d.epoch.fetch_add(1);
        d.queue.clear();
        d.shown.reset();
        d.seekPending = false;
        d.fallbackShown = false;
    }
    d.wake.notify_all();
}

bool VideoDecoder::reversing() const {
    return impl_ && impl_->reverseWanted.load();
}

double VideoDecoder::framePeriod() const {
    return impl_ ? impl_->framePeriod : 1.0 / 30.0;
}

}

#else

namespace nvj {

struct VideoDecoder::Impl {};

VideoSupport videoSupport() {
    VideoSupport support;
    support.folder = ffmpegDirectory();
    support.problem = "this build was made without the FFmpeg headers";
    return support;
}

void retryVideoSupport() {}

VideoDecoder::VideoDecoder() = default;

VideoDecoder::~VideoDecoder() = default;

bool VideoDecoder::open(const std::string&, std::string& error, const VideoOptions&) {
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

VideoStatus VideoDecoder::status() const {
    return VideoStatus{};
}

std::shared_ptr<const VideoFrame> VideoDecoder::frameAt(double) {
    return nullptr;
}

void VideoDecoder::setRange(double, double) {}

void VideoDecoder::setDirection(bool) {}

bool VideoDecoder::reversing() const {
    return false;
}

double VideoDecoder::framePeriod() const {
    return 1.0 / 30.0;
}

}

#endif
