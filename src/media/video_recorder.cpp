#include "media/video_recorder.hpp"

#ifdef NYREXVJ_HAVE_FFMPEG

#include "media/ffmpeg_api.hpp"
#include "platform/platform.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cerrno>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <filesystem>
#include <mutex>
#include <thread>
#include <vector>

namespace nvj {

namespace {

namespace fs = std::filesystem;

constexpr size_t kAudioChunkSamples = 4096;
constexpr uint32_t kAudioChunkCount = 128;
constexpr int kAudioBlock = 1024;
constexpr uint64_t kDefaultMinFreeBytes = 256ull * 1024ull * 1024ull;

struct Candidate {
    const char* encoder;
    AVPixelFormat pixelFormat;
};

struct AudioChunk {
    double wall = 0.0;
    uint32_t count = 0;
    float data[kAudioChunkSamples];
};

std::string describeError(const FfmpegApi& api, int code) {
    char text[256] = {0};
    if (api.av_strerror(code, text, sizeof(text)) < 0) {
        return "error " + std::to_string(code);
    }
    return text;
}

int encoderThreads() {
    const unsigned cores = std::thread::hardware_concurrency();
    return static_cast<int>(std::clamp<unsigned>(cores / 2u, 1u, 4u));
}

std::vector<Candidate> videoCandidates(const RecordSettings& settings) {
    switch (settings.profile) {
    case RecordProfile::H264Software:
        return {{"libopenh264", AV_PIX_FMT_YUV420P}};
    case RecordProfile::H264Hardware:
        return {{"h264_nvenc", AV_PIX_FMT_YUV420P}, {"h264_qsv", AV_PIX_FMT_NV12}, {"h264_amf", AV_PIX_FMT_NV12}};
    case RecordProfile::HevcHardware:
        return {{"hevc_nvenc", AV_PIX_FMT_YUV420P}, {"hevc_qsv", AV_PIX_FMT_NV12}, {"hevc_amf", AV_PIX_FMT_NV12}};
    case RecordProfile::ProResHq:
        return {{"prores_ks", AV_PIX_FMT_YUV422P10LE}};
    case RecordProfile::ProRes4444:
        return {{"prores_ks", settings.alpha ? AV_PIX_FMT_YUVA444P10LE : AV_PIX_FMT_YUV444P10LE}};
    case RecordProfile::Hap:
    case RecordProfile::HapAlpha:
    case RecordProfile::HapQ:
        return {{"hap", AV_PIX_FMT_RGBA}};
    case RecordProfile::MotionJpeg:
        return {{"mjpeg", AV_PIX_FMT_YUVJ420P}};
    case RecordProfile::Ffv1:
        if (settings.alpha) {
            return {{"ffv1", AV_PIX_FMT_BGRA}};
        }
        return {{"ffv1", AV_PIX_FMT_BGR0}, {"ffv1", AV_PIX_FMT_BGRA}};
    case RecordProfile::Vp9:
        return {{"libvpx-vp9", settings.alpha ? AV_PIX_FMT_YUVA420P : AV_PIX_FMT_YUV420P}};
    }
    return {};
}

bool isRgbFormat(AVPixelFormat format) {
    return format == AV_PIX_FMT_RGBA || format == AV_PIX_FMT_BGRA || format == AV_PIX_FMT_BGR0;
}

void tagColor(AVCodecContext* context, AVFrame* frame, const RecordSettings& settings, AVPixelFormat format) {
    if (isRgbFormat(format)) {
        return;
    }
    const bool jfif = settings.profile == RecordProfile::MotionJpeg;
    const AVColorPrimaries primaries = jfif ? AVCOL_PRI_BT470BG : AVCOL_PRI_BT709;
    const AVColorTransferCharacteristic transfer = jfif ? AVCOL_TRC_SMPTE170M : AVCOL_TRC_BT709;
    const AVColorSpace space = jfif ? AVCOL_SPC_BT470BG : AVCOL_SPC_BT709;
    const AVColorRange range = format == AV_PIX_FMT_YUVJ420P ? AVCOL_RANGE_JPEG : AVCOL_RANGE_MPEG;
    if (context != nullptr) {
        context->color_primaries = primaries;
        context->color_trc = transfer;
        context->colorspace = space;
        context->color_range = range;
    }
    if (frame != nullptr) {
        frame->color_primaries = primaries;
        frame->color_trc = transfer;
        frame->colorspace = space;
        frame->color_range = range;
    }
}

int64_t targetBitrate(const RecordSettings& settings, double factor) {
    const double pixelsPerSecond = static_cast<double>(settings.width) * static_cast<double>(settings.height) * static_cast<double>(settings.fps);
    const double bitsPerPixel = 0.02 + 0.20 * static_cast<double>(settings.quality) / 100.0;
    const double bits = pixelsPerSecond * bitsPerPixel * factor;
    return static_cast<int64_t>(std::clamp(bits, 1.0e6, 4.0e8));
}

void setOption(const FfmpegApi& api, AVCodecContext* context, const char* name, const char* value) {
    if (context->priv_data != nullptr) {
        api.av_opt_set(context->priv_data, name, value, 0);
    }
}

void configureVideo(const FfmpegApi& api, AVCodecContext* context, const RecordSettings& settings, const Candidate& candidate, bool globalHeader) {
    context->width = settings.width;
    context->height = settings.height;
    context->pix_fmt = candidate.pixelFormat;
    context->time_base = AVRational{1, settings.fps};
    context->framerate = AVRational{settings.fps, 1};
    context->sample_aspect_ratio = AVRational{1, 1};
    context->gop_size = settings.fps * 2;
    context->max_b_frames = 0;
    context->thread_count = encoderThreads();
    if (globalHeader) {
        context->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    }
    tagColor(context, nullptr, settings, candidate.pixelFormat);
    const std::string name = candidate.encoder;
    switch (settings.profile) {
    case RecordProfile::H264Software:
        context->bit_rate = static_cast<int64_t>(targetBitrate(settings, 1.0));
        setOption(api, context, "rc_mode", "bitrate");
        setOption(api, context, "profile", "high");
        setOption(api, context, "allow_skip_frames", "0");
        context->qmax = 51 - static_cast<int>(std::lround(12.0 * static_cast<double>(settings.quality) / 100.0));
        break;
    case RecordProfile::H264Hardware:
    case RecordProfile::HevcHardware:
        context->bit_rate = static_cast<int64_t>(targetBitrate(settings, settings.profile == RecordProfile::HevcHardware ? 0.7 : 1.0));
        context->rc_max_rate = context->bit_rate + context->bit_rate / 2;
        context->rc_buffer_size = static_cast<int>(std::min<int64_t>(context->bit_rate, 2000000000));
        if (name.find("nvenc") != std::string::npos) {
            setOption(api, context, "preset", "p4");
            setOption(api, context, "tune", "ll");
            setOption(api, context, "rc", "vbr");
        } else if (name.find("amf") != std::string::npos) {
            setOption(api, context, "usage", "lowlatency");
            setOption(api, context, "rc", "vbr_peak");
        } else if (name.find("qsv") != std::string::npos) {
            setOption(api, context, "preset", "veryfast");
        }
        break;
    case RecordProfile::ProResHq:
        setOption(api, context, "profile", "hq");
        setOption(api, context, "vendor", "apl0");
        break;
    case RecordProfile::ProRes4444:
        setOption(api, context, "profile", "4444");
        setOption(api, context, "vendor", "apl0");
        break;
    case RecordProfile::Hap:
        setOption(api, context, "format", "hap");
        setOption(api, context, "chunks", "4");
        break;
    case RecordProfile::HapAlpha:
        setOption(api, context, "format", "hap_alpha");
        setOption(api, context, "chunks", "4");
        break;
    case RecordProfile::HapQ:
        setOption(api, context, "format", "hap_q");
        setOption(api, context, "chunks", "4");
        break;
    case RecordProfile::MotionJpeg: {
        const int scale = 31 - static_cast<int>(std::lround(29.0 * static_cast<double>(settings.quality) / 100.0));
        context->flags |= AV_CODEC_FLAG_QSCALE;
        context->global_quality = FF_QP2LAMBDA * std::clamp(scale, 2, 31);
        break;
    }
    case RecordProfile::Ffv1:
        setOption(api, context, "coder", "1");
        setOption(api, context, "context", "1");
        setOption(api, context, "slicecrc", "1");
        setOption(api, context, "slices", "4");
        break;
    case RecordProfile::Vp9:
        context->bit_rate = static_cast<int64_t>(targetBitrate(settings, 0.8));
        setOption(api, context, "deadline", "realtime");
        setOption(api, context, "cpu-used", "8");
        setOption(api, context, "row-mt", "1");
        setOption(api, context, "lag-in-frames", "0");
        setOption(api, context, "auto-alt-ref", "0");
        break;
    }
}

struct OpenedVideo {
    AVCodecContext* context = nullptr;
    std::string encoder;
    AVPixelFormat pixelFormat = AV_PIX_FMT_NONE;
    std::string detail;
};

OpenedVideo openVideoEncoder(const FfmpegApi& api, const RecordSettings& settings, bool globalHeader) {
    OpenedVideo opened;
    for (const Candidate& candidate : videoCandidates(settings)) {
        const AVCodec* codec = api.avcodec_find_encoder_by_name(candidate.encoder);
        if (codec == nullptr) {
            opened.detail = std::string(candidate.encoder) + " is not part of this FFmpeg";
            continue;
        }
        AVCodecContext* context = api.avcodec_alloc_context3(codec);
        if (context == nullptr) {
            opened.detail = "Out of memory";
            continue;
        }
        configureVideo(api, context, settings, candidate, globalHeader);
        const int result = api.avcodec_open2(context, codec, nullptr);
        if (result < 0) {
            opened.detail = std::string(candidate.encoder) + ": " + describeError(api, result);
            api.avcodec_free_context(&context);
            continue;
        }
        opened.context = context;
        opened.encoder = candidate.encoder;
        opened.pixelFormat = candidate.pixelFormat;
        opened.detail.clear();
        return opened;
    }
    return opened;
}

}

struct VideoRecorder::Impl {
    struct Job {
        uint8_t* data = nullptr;
        int64_t index = 0;
    };

    const FfmpegApi* api = nullptr;
    RecordSettings settings;
    RecordProfileInfo info{};
    RecorderTestHooks hooks;
    std::string path;
    int audioRate = 0;

    AVFormatContext* format = nullptr;
    AVCodecContext* videoContext = nullptr;
    AVStream* videoStream = nullptr;
    AVCodecContext* audioContext = nullptr;
    AVStream* audioStream = nullptr;
    AVFrame* videoFrame = nullptr;
    AVFrame* audioFrame = nullptr;
    AVPacket* packet = nullptr;
    SwsContext* scaler = nullptr;
    AVPixelFormat videoPixelFormat = AV_PIX_FMT_NONE;
    bool headerWritten = false;
    bool ioOpened = false;

    std::vector<std::unique_ptr<uint8_t[]>> pool;
    std::vector<uint8_t*> freeFrames;
    std::deque<Job> queue;
    std::unique_ptr<AudioChunk[]> audioRing;
    std::atomic<uint32_t> audioHead{0};
    std::atomic<uint32_t> audioTail{0};
    std::atomic<uint64_t> audioOverruns{0};

    mutable std::mutex mutex;
    std::condition_variable wake;
    RecordStats stats;
    std::atomic<bool> finishRequested{false};
    std::atomic<bool> cancelRequested{false};
    double finishWall = 0.0;
    std::thread worker;

    std::unique_ptr<AudioSync> sync;
    std::vector<float> audioBuffer;
    int64_t audioPts = 0;
    int packetsWritten = 0;
    double encodeAverage = 0.0;
    uint64_t encodeCount = 0;

    ~Impl() {
        release();
    }

    void release() {
        if (scaler != nullptr) {
            api->sws_freeContext(scaler);
            scaler = nullptr;
        }
        if (videoFrame != nullptr) {
            api->av_frame_free(&videoFrame);
        }
        if (audioFrame != nullptr) {
            api->av_frame_free(&audioFrame);
        }
        if (packet != nullptr) {
            api->av_packet_free(&packet);
        }
        if (videoContext != nullptr) {
            api->avcodec_free_context(&videoContext);
        }
        if (audioContext != nullptr) {
            api->avcodec_free_context(&audioContext);
        }
        if (format != nullptr) {
            if (ioOpened) {
                api->avio_closep(&format->pb);
                ioOpened = false;
            }
            api->avformat_free_context(format);
            format = nullptr;
        }
    }

    void setState(RecordState state) {
        std::lock_guard<std::mutex> lock(mutex);
        stats.state = state;
    }

    void fail(const char* error, const std::string& detail) {
        std::lock_guard<std::mutex> lock(mutex);
        if (stats.state == RecordState::Failed) {
            return;
        }
        stats.state = RecordState::Failed;
        stats.stop = RecordStop::Error;
        stats.error = error;
        stats.detail = detail;
    }

    bool failed() const {
        std::lock_guard<std::mutex> lock(mutex);
        return stats.state == RecordState::Failed;
    }

    bool writePacket(AVCodecContext* context, AVStream* stream) {
        packet->stream_index = stream->index;
        api->av_packet_rescale_ts(packet, context->time_base, stream->time_base);
        const int size = packet->size;
        int written = 0;
        if (hooks.failWritesAfter >= 0 && packetsWritten >= hooks.failWritesAfter) {
            api->av_packet_unref(packet);
            written = AVERROR(ENOSPC);
        } else {
            written = api->av_interleaved_write_frame(format, packet);
        }
        if (written < 0) {
            fail("The file cannot be written", describeError(*api, written));
            return false;
        }
        ++packetsWritten;
        std::lock_guard<std::mutex> lock(mutex);
        stats.bytes += static_cast<uint64_t>(std::max(size, 0));
        return true;
    }

    bool receivePackets(AVCodecContext* context, AVStream* stream, const char* failure) {
        for (;;) {
            const int received = api->avcodec_receive_packet(context, packet);
            if (received == AVERROR(EAGAIN) || received == AVERROR_EOF) {
                return true;
            }
            if (received < 0) {
                fail(failure, describeError(*api, received));
                return false;
            }
            if (!writePacket(context, stream)) {
                return false;
            }
        }
    }

    bool convertVideo(const uint8_t* source) {
        const int width = settings.width;
        const int height = settings.height;
        if (videoPixelFormat == AV_PIX_FMT_RGBA) {
            const size_t rowBytes = static_cast<size_t>(width) * 4u;
            for (int y = 0; y < height; ++y) {
                std::memcpy(videoFrame->data[0] + static_cast<size_t>(y) * static_cast<size_t>(videoFrame->linesize[0]), source + static_cast<size_t>(y) * rowBytes, rowBytes);
            }
            return true;
        }
        scaler = api->sws_getCachedContext(scaler, width, height, AV_PIX_FMT_RGBA, width, height, videoPixelFormat, SWS_BILINEAR, nullptr, nullptr, nullptr);
        if (scaler == nullptr) {
            fail("The picture cannot be converted", "swscale");
            return false;
        }
        const int* coefficients = api->sws_getCoefficients(settings.profile == RecordProfile::MotionJpeg ? SWS_CS_DEFAULT : SWS_CS_ITU709);
        const int destinationRange = videoPixelFormat == AV_PIX_FMT_YUVJ420P ? 1 : 0;
        api->sws_setColorspaceDetails(scaler, coefficients, 1, coefficients, destinationRange, 0, 1 << 16, 1 << 16);
        const uint8_t* planes[4] = {source, nullptr, nullptr, nullptr};
        const int strides[4] = {width * 4, 0, 0, 0};
        const int scaled = api->sws_scale(scaler, planes, strides, 0, height, videoFrame->data, videoFrame->linesize);
        if (scaled != height) {
            fail("The picture cannot be converted", "swscale returned " + std::to_string(scaled));
            return false;
        }
        return true;
    }

    bool encodeVideo(const Job& job) {
        const auto began = std::chrono::steady_clock::now();
        if (hooks.encodeDelayMs > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(hooks.encodeDelayMs));
        }
        if (hooks.frameTap != nullptr) {
            hooks.frameTap(hooks.tapUser, job.data, settings.width, settings.height, job.index);
        }
        int result = api->av_frame_make_writable(videoFrame);
        if (result < 0) {
            fail("The encoder failed", describeError(*api, result));
            return false;
        }
        if (!convertVideo(job.data)) {
            return false;
        }
        videoFrame->pts = job.index;
        result = api->avcodec_send_frame(videoContext, videoFrame);
        if (result < 0) {
            fail("The encoder failed", describeError(*api, result));
            return false;
        }
        if (!receivePackets(videoContext, videoStream, "The encoder failed")) {
            return false;
        }
        const double elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - began).count();
        ++encodeCount;
        encodeAverage = encodeCount == 1 ? elapsed : encodeAverage * 0.95 + elapsed * 0.05;
        std::lock_guard<std::mutex> lock(mutex);
        ++stats.framesEncoded;
        stats.videoSeconds = static_cast<double>(job.index + 1) / static_cast<double>(settings.fps);
        stats.encodeMsAverage = encodeAverage;
        stats.encodeMsMax = std::max(stats.encodeMsMax, elapsed);
        return true;
    }

    bool encodeAudioBlock(const float* samples, int count) {
        int result = api->av_frame_make_writable(audioFrame);
        if (result < 0) {
            fail("The audio encoder failed", describeError(*api, result));
            return false;
        }
        audioFrame->nb_samples = count;
        if (audioContext->sample_fmt == AV_SAMPLE_FMT_FLTP) {
            std::memcpy(audioFrame->data[0], samples, static_cast<size_t>(count) * sizeof(float));
        } else {
            int16_t* target = reinterpret_cast<int16_t*>(audioFrame->data[0]);
            for (int i = 0; i < count; ++i) {
                const float clipped = std::clamp(samples[i], -1.0f, 1.0f);
                target[i] = static_cast<int16_t>(std::lround(clipped * 32767.0f));
            }
        }
        audioFrame->pts = audioPts;
        audioPts += count;
        result = api->avcodec_send_frame(audioContext, audioFrame);
        if (result < 0) {
            fail("The audio encoder failed", describeError(*api, result));
            return false;
        }
        return receivePackets(audioContext, audioStream, "The audio encoder failed");
    }

    bool appendAudio(const float* samples, size_t count) {
        audioBuffer.insert(audioBuffer.end(), samples, samples + count);
        size_t offset = 0;
        while (audioBuffer.size() - offset >= static_cast<size_t>(kAudioBlock)) {
            if (!encodeAudioBlock(audioBuffer.data() + offset, kAudioBlock)) {
                return false;
            }
            offset += static_cast<size_t>(kAudioBlock);
        }
        audioBuffer.erase(audioBuffer.begin(), audioBuffer.begin() + static_cast<std::ptrdiff_t>(offset));
        return true;
    }

    bool appendSilence(int64_t count) {
        static const std::vector<float> zeros(8192, 0.0f);
        while (count > 0) {
            const size_t step = static_cast<size_t>(std::min<int64_t>(count, static_cast<int64_t>(zeros.size())));
            if (!appendAudio(zeros.data(), step)) {
                return false;
            }
            count -= static_cast<int64_t>(step);
        }
        return true;
    }

    void publishAudioStats() {
        std::lock_guard<std::mutex> lock(mutex);
        if (sync != nullptr) {
            const double rate = static_cast<double>(sync->sampleRate());
            stats.audioSeconds = static_cast<double>(sync->writtenSamples()) / rate;
            stats.audioInsertedSeconds = static_cast<double>(sync->insertedSamples()) / rate;
            stats.audioDroppedSeconds = static_cast<double>(sync->droppedSamples()) / rate;
        }
        stats.audioOverruns = audioOverruns.load();
    }

    bool drainAudio() {
        if (!audioRing) {
            return true;
        }
        for (;;) {
            const uint32_t tail = audioTail.load(std::memory_order_relaxed);
            if (tail == audioHead.load(std::memory_order_acquire)) {
                break;
            }
            const AudioChunk& chunk = audioRing[tail % kAudioChunkCount];
            const int64_t delta = sync->adjust(chunk.wall, chunk.count);
            bool ok = true;
            if (delta > 0) {
                ok = appendSilence(delta);
            }
            const size_t skip = delta < 0 ? static_cast<size_t>(-delta) : 0u;
            if (ok && chunk.count > skip) {
                ok = appendAudio(chunk.data + skip, chunk.count - skip);
            }
            audioTail.store(tail + 1, std::memory_order_release);
            if (!ok) {
                return false;
            }
        }
        publishAudioStats();
        return true;
    }

    bool closeAudio() {
        if (!audioRing) {
            return true;
        }
        if (!drainAudio()) {
            return false;
        }
        if (!appendSilence(sync->finish(finishWall))) {
            return false;
        }
        if (!audioBuffer.empty()) {
            const size_t remaining = audioBuffer.size();
            const bool fixedBlock = audioContext->frame_size > 0;
            if (fixedBlock) {
                audioBuffer.resize(static_cast<size_t>(kAudioBlock), 0.0f);
            }
            if (!encodeAudioBlock(audioBuffer.data(), fixedBlock ? kAudioBlock : static_cast<int>(remaining))) {
                return false;
            }
            audioBuffer.clear();
        }
        int result = api->avcodec_send_frame(audioContext, nullptr);
        if (result < 0 && result != AVERROR_EOF) {
            fail("The audio encoder failed", describeError(*api, result));
            return false;
        }
        publishAudioStats();
        return receivePackets(audioContext, audioStream, "The audio encoder failed");
    }

    bool closeVideo() {
        const int result = api->avcodec_send_frame(videoContext, nullptr);
        if (result < 0 && result != AVERROR_EOF) {
            fail("The encoder failed", describeError(*api, result));
            return false;
        }
        return receivePackets(videoContext, videoStream, "The encoder failed");
    }

    void finalizeFile() {
        bool ok = !failed();
        if (ok) {
            ok = closeVideo();
        }
        if (ok) {
            ok = closeAudio();
        }
        if (headerWritten) {
            const int trailer = api->av_write_trailer(format);
            if (trailer < 0 && ok) {
                fail("The file cannot be finished", describeError(*api, trailer));
            }
            headerWritten = false;
        }
        if (ioOpened) {
            api->avio_closep(&format->pb);
            ioOpened = false;
        }
        std::lock_guard<std::mutex> lock(mutex);
        if (stats.state != RecordState::Failed) {
            stats.state = RecordState::Finished;
            if (stats.stop == RecordStop::None) {
                stats.stop = RecordStop::User;
            }
        }
    }

    void discardFile() {
        if (headerWritten) {
            headerWritten = false;
        }
        if (ioOpened) {
            api->avio_closep(&format->pb);
            ioOpened = false;
        }
        std::error_code error;
        fs::remove(fs::path(std::u8string(path.begin(), path.end())), error);
        std::lock_guard<std::mutex> lock(mutex);
        stats.state = RecordState::Cancelled;
        stats.bytes = 0;
    }

    bool lowOnDisk(uint64_t minimum) {
        std::error_code error;
        const fs::path file(std::u8string(path.begin(), path.end()));
        const fs::space_info space = fs::space(file.parent_path(), error);
        if (error) {
            return false;
        }
        return static_cast<uint64_t>(space.available) < minimum;
    }

    void run() {
        platform::setCurrentThreadBackground(true);
        const uint64_t minimumFree = hooks.minFreeBytes > 0 ? hooks.minFreeBytes : kDefaultMinFreeBytes;
        auto lastDiskCheck = std::chrono::steady_clock::now();
        for (;;) {
            if (cancelRequested.load()) {
                discardFile();
                return;
            }
            Job job;
            bool haveJob = false;
            {
                std::unique_lock<std::mutex> lock(mutex);
                if (queue.empty() && !finishRequested.load() && audioTail.load() == audioHead.load()) {
                    wake.wait_for(lock, std::chrono::milliseconds(20));
                }
                if (!queue.empty()) {
                    job = queue.front();
                    queue.pop_front();
                    stats.queueDepth = static_cast<int>(queue.size());
                    haveJob = true;
                }
            }
            if (cancelRequested.load()) {
                discardFile();
                return;
            }
            bool ok = true;
            if (haveJob) {
                ok = encodeVideo(job);
                std::lock_guard<std::mutex> lock(mutex);
                freeFrames.push_back(job.data);
            }
            if (ok) {
                ok = drainAudio();
            }
            if (!ok || failed()) {
                finalizeFile();
                return;
            }
            const auto now = std::chrono::steady_clock::now();
            if (now - lastDiskCheck >= std::chrono::seconds(1)) {
                lastDiskCheck = now;
                if (lowOnDisk(minimumFree)) {
                    {
                        std::lock_guard<std::mutex> lock(mutex);
                        stats.stop = RecordStop::DiskFull;
                        stats.error = "The disk is almost full, the recording was stopped";
                        stats.state = RecordState::Finishing;
                    }
                    finishRequested = true;
                }
            }
            if (finishRequested.load()) {
                bool empty = false;
                {
                    std::lock_guard<std::mutex> lock(mutex);
                    empty = queue.empty();
                }
                if (empty && audioTail.load() == audioHead.load()) {
                    finalizeFile();
                    return;
                }
            }
        }
    }

    bool openAudio(std::string& detail) {
        if (audioRate <= 0 || !settings.audio) {
            return true;
        }
        if (!recordAudioRateSupported(info.audio, audioRate)) {
            std::lock_guard<std::mutex> lock(mutex);
            stats.audioNote = "The sample rate of the audio source cannot be recorded in this format";
            return true;
        }
        const char* name = info.audio == RecordAudioCodec::Aac ? "aac" : "pcm_s16le";
        const AVCodec* codec = api->avcodec_find_encoder_by_name(name);
        if (codec == nullptr) {
            std::lock_guard<std::mutex> lock(mutex);
            stats.audioNote = "This FFmpeg has no audio encoder for this format";
            return true;
        }
        audioContext = api->avcodec_alloc_context3(codec);
        if (audioContext == nullptr) {
            detail = "Out of memory";
            return false;
        }
        audioContext->sample_fmt = info.audio == RecordAudioCodec::Aac ? AV_SAMPLE_FMT_FLTP : AV_SAMPLE_FMT_S16;
        audioContext->sample_rate = audioRate;
        audioContext->time_base = AVRational{1, audioRate};
        audioContext->ch_layout.order = AV_CHANNEL_ORDER_NATIVE;
        audioContext->ch_layout.nb_channels = 1;
        audioContext->ch_layout.u.mask = AV_CH_LAYOUT_MONO;
        if (info.audio == RecordAudioCodec::Aac) {
            audioContext->bit_rate = 160000;
        }
        if (format->oformat->flags & AVFMT_GLOBALHEADER) {
            audioContext->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
        }
        const int opened = api->avcodec_open2(audioContext, codec, nullptr);
        if (opened < 0) {
            detail = describeError(*api, opened);
            api->avcodec_free_context(&audioContext);
            std::lock_guard<std::mutex> lock(mutex);
            stats.audioNote = "The audio encoder could not be opened";
            return true;
        }
        audioStream = api->avformat_new_stream(format, nullptr);
        if (audioStream == nullptr) {
            detail = "Out of memory";
            return false;
        }
        audioStream->time_base = AVRational{1, audioRate};
        api->avcodec_parameters_from_context(audioStream->codecpar, audioContext);
        audioFrame = api->av_frame_alloc();
        if (audioFrame == nullptr) {
            detail = "Out of memory";
            return false;
        }
        audioFrame->format = audioContext->sample_fmt;
        audioFrame->sample_rate = audioRate;
        audioFrame->nb_samples = kAudioBlock;
        audioFrame->ch_layout = audioContext->ch_layout;
        if (api->av_frame_get_buffer(audioFrame, 0) < 0) {
            detail = "Out of memory";
            return false;
        }
        audioRing.reset(new AudioChunk[kAudioChunkCount]);
        sync = std::make_unique<AudioSync>(audioRate);
        std::lock_guard<std::mutex> lock(mutex);
        stats.audioActive = true;
        return true;
    }
};

VideoRecorder::VideoRecorder() : impl_(std::make_unique<Impl>()) {}

VideoRecorder::~VideoRecorder() {
    if (impl_ == nullptr || !impl_->worker.joinable()) {
        return;
    }
    if (!impl_->finishRequested.load() && !impl_->cancelRequested.load()) {
        impl_->finishRequested = true;
    }
    impl_->wake.notify_all();
    impl_->worker.join();
}

void VideoRecorder::setTestHooks(const RecorderTestHooks& hooks) {
    impl_->hooks = hooks;
}

bool VideoRecorder::open(const RecordSettings& checked, const std::string& pathUtf8, int audioRate, std::string& error, std::string& detail) {
    Impl& state = *impl_;
    if (state.worker.joinable() || state.format != nullptr) {
        error = "A recording is already running";
        return false;
    }
    std::string problem;
    state.api = ffmpegApi(problem);
    if (state.api == nullptr) {
        error = "FFmpeg is not available";
        detail = problem;
        return false;
    }
    const FfmpegApi& api = *state.api;
    state.settings = checked;
    state.info = recordProfileInfo(checked.profile);
    state.path = pathUtf8;
    state.audioRate = audioRate;
    {
        std::lock_guard<std::mutex> lock(state.mutex);
        state.stats = RecordStats();
        state.stats.path = pathUtf8;
    }
    state.finishRequested = false;
    state.cancelRequested = false;
    state.packetsWritten = 0;
    state.audioPts = 0;
    state.encodeAverage = 0.0;
    state.encodeCount = 0;
    state.audioHead = 0;
    state.audioTail = 0;
    state.audioOverruns = 0;
    state.audioBuffer.clear();

    std::error_code directoryError;
    const fs::path file(std::u8string(pathUtf8.begin(), pathUtf8.end()));
    if (file.has_parent_path()) {
        fs::create_directories(file.parent_path(), directoryError);
    }

    const int created = api.avformat_alloc_output_context2(&state.format, nullptr, state.info.container, pathUtf8.c_str());
    if (created < 0 || state.format == nullptr) {
        error = "The file format is not available in this FFmpeg";
        detail = describeError(api, created);
        state.release();
        return false;
    }
    const bool globalHeader = (state.format->oformat->flags & AVFMT_GLOBALHEADER) != 0;
    OpenedVideo video = openVideoEncoder(api, checked, globalHeader);
    if (video.context == nullptr) {
        error = "No encoder for this format works on this computer";
        detail = video.detail;
        state.release();
        return false;
    }
    state.videoContext = video.context;
    state.videoPixelFormat = video.pixelFormat;
    state.videoStream = api.avformat_new_stream(state.format, nullptr);
    state.packet = api.av_packet_alloc();
    state.videoFrame = api.av_frame_alloc();
    if (state.videoStream == nullptr || state.packet == nullptr || state.videoFrame == nullptr) {
        error = "Out of memory";
        state.release();
        return false;
    }
    state.videoStream->time_base = state.videoContext->time_base;
    state.videoStream->avg_frame_rate = AVRational{checked.fps, 1};
    api.avcodec_parameters_from_context(state.videoStream->codecpar, state.videoContext);
    if (state.videoContext->codec_id == AV_CODEC_ID_HEVC && std::string(state.info.container) == "mp4") {
        state.videoStream->codecpar->codec_tag = MKTAG('h', 'v', 'c', '1');
    }
    state.videoFrame->format = video.pixelFormat;
    state.videoFrame->width = checked.width;
    state.videoFrame->height = checked.height;
    if (api.av_frame_get_buffer(state.videoFrame, 0) < 0) {
        error = "Out of memory";
        state.release();
        return false;
    }
    tagColor(nullptr, state.videoFrame, checked, video.pixelFormat);
    if (checked.crashSafe) {
        state.format->flags |= AVFMT_FLAG_FLUSH_PACKETS;
    }
    {
        std::lock_guard<std::mutex> lock(state.mutex);
        state.stats.encoder = video.encoder;
    }
    std::string audioDetail;
    if (!state.openAudio(audioDetail)) {
        error = "The audio encoder could not be set up";
        detail = audioDetail;
        state.release();
        return false;
    }

    const int io = api.avio_open(&state.format->pb, pathUtf8.c_str(), AVIO_FLAG_WRITE);
    if (io < 0) {
        error = "The file cannot be created";
        detail = describeError(api, io);
        state.release();
        return false;
    }
    state.ioOpened = true;
    AVDictionary* muxerOptions = nullptr;
    if (checked.crashSafe && state.info.fragmentable) {
        if (state.info.intraOnly) {
            api.av_dict_set(&muxerOptions, "movflags", "empty_moov", 0);
            api.av_dict_set(&muxerOptions, "frag_duration", "2000000", 0);
        } else {
            api.av_dict_set(&muxerOptions, "movflags", "frag_keyframe+empty_moov+default_base_moof", 0);
        }
    }
    const int header = api.avformat_write_header(state.format, &muxerOptions);
    api.av_dict_free(&muxerOptions);
    if (header < 0) {
        error = "The file header cannot be written";
        detail = describeError(api, header);
        state.release();
        std::error_code removeError;
        fs::remove(file, removeError);
        return false;
    }
    state.headerWritten = true;

    const size_t bytes = recordFrameBytes(checked);
    const int count = recordPoolSize(bytes);
    state.pool.clear();
    state.freeFrames.clear();
    state.queue.clear();
    for (int i = 0; i < count; ++i) {
        state.pool.emplace_back(new uint8_t[bytes]);
        state.freeFrames.push_back(state.pool.back().get());
    }
    {
        std::lock_guard<std::mutex> lock(state.mutex);
        state.stats.poolSize = count;
        state.stats.state = RecordState::Recording;
    }
    state.worker = std::thread([this]() { impl_->run(); });
    return true;
}

uint8_t* VideoRecorder::acquireFrame() {
    Impl& state = *impl_;
    std::lock_guard<std::mutex> lock(state.mutex);
    if (state.stats.state != RecordState::Recording || state.freeFrames.empty()) {
        if (state.stats.state == RecordState::Recording) {
            ++state.stats.framesRejected;
        }
        return nullptr;
    }
    uint8_t* frame = state.freeFrames.back();
    state.freeFrames.pop_back();
    return frame;
}

void VideoRecorder::submitFrame(uint8_t* frame, int64_t index) {
    Impl& state = *impl_;
    {
        std::lock_guard<std::mutex> lock(state.mutex);
        if (state.stats.state != RecordState::Recording) {
            state.freeFrames.push_back(frame);
            return;
        }
        Impl::Job job;
        job.data = frame;
        job.index = index;
        state.queue.push_back(job);
        ++state.stats.framesSubmitted;
        state.stats.queueDepth = static_cast<int>(state.queue.size());
        state.stats.queueDepthMax = std::max(state.stats.queueDepthMax, state.stats.queueDepth);
    }
    state.wake.notify_one();
}

void VideoRecorder::discardFrame(uint8_t* frame) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->freeFrames.push_back(frame);
}

void VideoRecorder::submitAudio(const float* samples, size_t count, double wallSeconds) {
    Impl& state = *impl_;
    if (!state.audioRing || samples == nullptr || count == 0 || state.finishRequested.load() || state.cancelRequested.load()) {
        return;
    }
    size_t offset = 0;
    while (offset < count) {
        const size_t step = std::min(count - offset, kAudioChunkSamples);
        const uint32_t head = state.audioHead.load(std::memory_order_relaxed);
        if (head - state.audioTail.load(std::memory_order_acquire) >= kAudioChunkCount) {
            state.audioOverruns.fetch_add(1);
            return;
        }
        AudioChunk& chunk = state.audioRing[head % kAudioChunkCount];
        chunk.wall = wallSeconds;
        chunk.count = static_cast<uint32_t>(step);
        std::memcpy(chunk.data, samples + offset, step * sizeof(float));
        state.audioHead.store(head + 1, std::memory_order_release);
        offset += step;
    }
    state.wake.notify_one();
}

void VideoRecorder::finish(double wallSeconds) {
    Impl& state = *impl_;
    {
        std::lock_guard<std::mutex> lock(state.mutex);
        if (state.stats.state != RecordState::Recording) {
            return;
        }
        state.stats.state = RecordState::Finishing;
        state.finishWall = wallSeconds;
        if (state.stats.stop == RecordStop::None) {
            state.stats.stop = RecordStop::User;
        }
    }
    state.finishRequested = true;
    state.wake.notify_all();
}

void VideoRecorder::cancel() {
    Impl& state = *impl_;
    if (!state.worker.joinable()) {
        return;
    }
    state.cancelRequested = true;
    state.wake.notify_all();
}

bool VideoRecorder::waitUntilDone(double timeoutSeconds) {
    const auto limit = std::chrono::steady_clock::now() + std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(timeoutSeconds));
    for (;;) {
        const RecordState state = stats().state;
        if (state == RecordState::Finished || state == RecordState::Failed || state == RecordState::Cancelled || state == RecordState::Idle) {
            if (impl_->worker.joinable() && state != RecordState::Idle) {
                impl_->worker.join();
            }
            return true;
        }
        if (std::chrono::steady_clock::now() >= limit) {
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}

RecordStats VideoRecorder::stats() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->stats;
}

size_t VideoRecorder::frameBytes() const {
    return recordFrameBytes(impl_->settings);
}

RecordCapabilities probeRecordCapabilities() {
    RecordCapabilities caps;
    std::string problem;
    const FfmpegApi* api = ffmpegApi(problem);
    if (api == nullptr) {
        caps.problem = problem;
        return caps;
    }
    caps.ffmpegLoaded = true;
    for (int i = 0; i < kRecordProfileCount; ++i) {
        RecordSettings settings;
        settings.profile = static_cast<RecordProfile>(i);
        settings.width = 256;
        settings.height = 144;
        settings.fps = 30;
        settings.quality = 70;
        const RecordProfileInfo& info = recordProfileInfo(settings.profile);
        RecordProbe& probe = caps.profiles[static_cast<size_t>(i)];
        AVFormatContext* format = nullptr;
        if (api->avformat_alloc_output_context2(&format, nullptr, info.container, nullptr) < 0 || format == nullptr) {
            probe.detail = "The file format is not available in this FFmpeg";
            continue;
        }
        const bool globalHeader = (format->oformat->flags & AVFMT_GLOBALHEADER) != 0;
        api->avformat_free_context(format);
        OpenedVideo opened = openVideoEncoder(*api, settings, globalHeader);
        if (opened.context == nullptr) {
            probe.detail = opened.detail;
            continue;
        }
        AVFrame* frame = api->av_frame_alloc();
        AVPacket* packet = api->av_packet_alloc();
        bool ok = frame != nullptr && packet != nullptr;
        if (ok) {
            frame->format = opened.pixelFormat;
            frame->width = settings.width;
            frame->height = settings.height;
            ok = api->av_frame_get_buffer(frame, 0) >= 0;
        }
        if (ok) {
            for (int plane = 0; plane < AV_NUM_DATA_POINTERS && frame->buf[plane] != nullptr; ++plane) {
                std::memset(frame->data[plane], plane == 0 ? 90 : 128, frame->buf[plane]->size);
            }
            frame->pts = 0;
            const int sent = api->avcodec_send_frame(opened.context, frame);
            const int flushed = sent >= 0 ? api->avcodec_send_frame(opened.context, nullptr) : sent;
            ok = sent >= 0 && flushed >= 0;
            if (!ok) {
                probe.detail = opened.encoder + ": " + describeError(*api, sent < 0 ? sent : flushed);
            }
        }
        if (ok) {
            bool gotPacket = false;
            for (int attempt = 0; attempt < 8 && !gotPacket; ++attempt) {
                const int received = api->avcodec_receive_packet(opened.context, packet);
                if (received >= 0) {
                    gotPacket = true;
                } else if (received != AVERROR(EAGAIN)) {
                    break;
                }
            }
            ok = gotPacket;
            if (!ok) {
                probe.detail = opened.encoder + " produced no picture";
            }
        }
        if (frame != nullptr) {
            api->av_frame_free(&frame);
        }
        if (packet != nullptr) {
            api->av_packet_free(&packet);
        }
        api->avcodec_free_context(&opened.context);
        probe.works = ok;
        if (ok) {
            probe.encoder = opened.encoder;
            probe.detail.clear();
        }
    }
    return caps;
}

}

#else

namespace nvj {

struct VideoRecorder::Impl {
    RecordStats stats;
};

VideoRecorder::VideoRecorder() : impl_(std::make_unique<Impl>()) {}
VideoRecorder::~VideoRecorder() = default;
void VideoRecorder::setTestHooks(const RecorderTestHooks&) {}

bool VideoRecorder::open(const RecordSettings&, const std::string&, int, std::string& error, std::string& detail) {
    error = "FFmpeg is not available";
    detail = "This build of NyrexVJ was made without video support";
    return false;
}

uint8_t* VideoRecorder::acquireFrame() {
    return nullptr;
}

void VideoRecorder::submitFrame(uint8_t*, int64_t) {}
void VideoRecorder::discardFrame(uint8_t*) {}
void VideoRecorder::submitAudio(const float*, size_t, double) {}
void VideoRecorder::finish(double) {}
void VideoRecorder::cancel() {}

bool VideoRecorder::waitUntilDone(double) {
    return true;
}

RecordStats VideoRecorder::stats() const {
    return impl_->stats;
}

size_t VideoRecorder::frameBytes() const {
    return 0;
}

RecordCapabilities probeRecordCapabilities() {
    RecordCapabilities caps;
    caps.problem = "This build of NyrexVJ was made without video support";
    return caps;
}

}

#endif
