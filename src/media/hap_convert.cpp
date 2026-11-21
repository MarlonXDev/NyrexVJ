#include "media/hap_convert.hpp"

#include "media/ffmpeg_api.hpp"
#include "media/hap.hpp"

#ifdef NYREXVJ_HAVE_FFMPEG
#include "media/frame_convert.hpp"
#endif

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <vector>

namespace nvj::hapconv {

Variant variantFromTag(std::uint32_t tag) {
    switch (tag) {
    case 0x31545844u:
        return Variant::Dxt1;
    case 0x35545844u:
        return Variant::Dxt5;
    case 0x36474359u:
        return Variant::Ycg6;
    case 0x30314759u:
        return Variant::Yg10;
    default:
        return Variant::Unknown;
    }
}

Variant variantFromPacket(const std::uint8_t* data, std::size_t size) {
    if (data == nullptr || size < 4) {
        return Variant::Unknown;
    }
    const std::uint32_t tag = (static_cast<std::uint32_t>(data[0]) << 24) | (static_cast<std::uint32_t>(data[1]) << 16) | (static_cast<std::uint32_t>(data[2]) << 8) | static_cast<std::uint32_t>(data[3]);
    return variantFromTag(tag);
}

const char* variantName(Variant variant) {
    switch (variant) {
    case Variant::Dxt1:
        return "DXV Normal Quality";
    case Variant::Dxt5:
        return "DXV Normal Quality with alpha";
    case Variant::Ycg6:
        return "DXV High Quality";
    case Variant::Yg10:
        return "DXV High Quality with alpha";
    default:
        return "DXV";
    }
}

bool variantHasAlpha(Variant variant) {
    return variant == Variant::Dxt5 || variant == Variant::Yg10 || variant == Variant::Unknown;
}

bool variantIsHighQuality(Variant variant) {
    return variant == Variant::Ycg6 || variant == Variant::Yg10;
}

TargetFormat chooseTarget(Variant variant, TargetFormat preference, bool& alphaKept) {
    alphaKept = variantHasAlpha(variant);
    TargetFormat target = preference;
    if (target == TargetFormat::Auto) {
        switch (variant) {
        case Variant::Dxt1:
            target = TargetFormat::Hap;
            break;
        case Variant::Ycg6:
            target = TargetFormat::HapQ;
            break;
        default:
            target = TargetFormat::HapAlpha;
            break;
        }
    }
    if (alphaKept && target != TargetFormat::HapAlpha) {
        target = TargetFormat::HapAlpha;
    }
    return target;
}

const char* targetName(TargetFormat format) {
    switch (format) {
    case TargetFormat::Hap:
        return "HAP";
    case TargetFormat::HapAlpha:
        return "HAP Alpha";
    case TargetFormat::HapQ:
        return "HAP Q";
    default:
        return "Automatic";
    }
}

const char* encoderFormat(TargetFormat format) {
    switch (format) {
    case TargetFormat::HapAlpha:
        return "hap_alpha";
    case TargetFormat::HapQ:
        return "hap_q";
    default:
        return "hap";
    }
}

TargetFormat targetFromName(const std::string& name) {
    if (name == "hap") {
        return TargetFormat::Hap;
    }
    if (name == "hap_alpha") {
        return TargetFormat::HapAlpha;
    }
    if (name == "hap_q") {
        return TargetFormat::HapQ;
    }
    return TargetFormat::Auto;
}

std::string targetKey(TargetFormat format) {
    switch (format) {
    case TargetFormat::Hap:
        return "hap";
    case TargetFormat::HapAlpha:
        return "hap_alpha";
    case TargetFormat::HapQ:
        return "hap_q";
    default:
        return "auto";
    }
}

#ifdef NYREXVJ_HAVE_FFMPEG

namespace {

namespace fs = std::filesystem;

std::string describe(const FfmpegApi& api, int code) {
    char text[128] = {0};
    api.av_strerror(code, text, sizeof(text));
    return text;
}

double framesPerSecond(const AVStream* stream) {
    AVRational rate = stream->avg_frame_rate;
    if (rate.num <= 0 || rate.den <= 0) {
        rate = stream->r_frame_rate;
    }
    if (rate.num <= 0 || rate.den <= 0) {
        return 0.0;
    }
    return static_cast<double>(rate.num) / static_cast<double>(rate.den);
}

AVRational frameRateOf(const AVStream* stream) {
    AVRational rate = stream->avg_frame_rate;
    if (rate.num <= 0 || rate.den <= 0) {
        rate = stream->r_frame_rate;
    }
    if (rate.num <= 0 || rate.den <= 0) {
        rate = AVRational{30, 1};
    }
    return rate;
}

Variant variantFromFrame(const FfmpegApi& api, const AVFrame& frame) {
    const AVPixFmtDescriptor* descriptor = api.av_pix_fmt_desc_get(static_cast<AVPixelFormat>(frame.format));
    const bool alpha = descriptor != nullptr && (descriptor->flags & AV_PIX_FMT_FLAG_ALPHA) != 0;
    if (frame.colorspace == AVCOL_SPC_YCGCO) {
        return alpha ? Variant::Yg10 : Variant::Ycg6;
    }
    return alpha ? Variant::Dxt5 : Variant::Dxt1;
}

struct Input {
    const FfmpegApi* api = nullptr;
    AVFormatContext* format = nullptr;
    AVCodecContext* codec = nullptr;
    int stream = -1;

    ~Input() {
        if (codec != nullptr) {
            api->avcodec_free_context(&codec);
        }
        if (format != nullptr) {
            api->avformat_close_input(&format);
        }
    }

    bool open(const FfmpegApi& a, const std::string& path, bool allowAny, bool withDecoder, std::string& error) {
        api = &a;
        const int opened = api->avformat_open_input(&format, path.c_str(), nullptr, nullptr);
        if (opened < 0) {
            format = nullptr;
            error = "The video cannot be opened: " + describe(*api, opened);
            return false;
        }
        const int info = api->avformat_find_stream_info(format, nullptr);
        if (info < 0) {
            error = "The video cannot be read: " + describe(*api, info);
            return false;
        }
        stream = api->av_find_best_stream(format, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
        if (stream < 0) {
            error = "The file has no video stream";
            return false;
        }
        const AVCodecParameters* parameters = format->streams[stream]->codecpar;
        if (parameters->codec_id != AV_CODEC_ID_DXV && !allowAny) {
            error = "The video is not DXV";
            return false;
        }
        if (parameters->width <= 0 || parameters->height <= 0) {
            error = "The video has no picture size";
            return false;
        }
        if (!withDecoder) {
            return true;
        }
        const AVCodec* decoder = api->avcodec_find_decoder(parameters->codec_id);
        if (decoder == nullptr) {
            error = "FFmpeg has no decoder for this video";
            return false;
        }
        codec = api->avcodec_alloc_context3(decoder);
        if (codec == nullptr) {
            error = "The decoder could not be created";
            return false;
        }
        const int copied = api->avcodec_parameters_to_context(codec, parameters);
        if (copied < 0) {
            error = "The decoder could not be set up: " + describe(*api, copied);
            return false;
        }
        codec->thread_count = 1;
        const int started = api->avcodec_open2(codec, decoder, nullptr);
        if (started < 0) {
            error = "The decoder could not be started: " + describe(*api, started);
            return false;
        }
        return true;
    }
};

struct Output {
    const FfmpegApi* api = nullptr;
    AVFormatContext* format = nullptr;
    AVCodecContext* codec = nullptr;
    AVStream* stream = nullptr;
    AVFrame* frame = nullptr;
    AVPacket* packet = nullptr;
    bool headerWritten = false;
    bool trailerWritten = false;
    bool ioOpen = false;

    ~Output() {
        if (frame != nullptr) {
            api->av_frame_free(&frame);
        }
        if (packet != nullptr) {
            api->av_packet_free(&packet);
        }
        if (codec != nullptr) {
            api->avcodec_free_context(&codec);
        }
        closeIo();
        if (format != nullptr) {
            api->avformat_free_context(format);
            format = nullptr;
        }
    }

    void closeIo() {
        if (ioOpen && format != nullptr) {
            api->avio_closep(&format->pb);
            ioOpen = false;
        }
    }
};

class Clock {
public:
    Clock() : start_(std::chrono::steady_clock::now()) {}
    double seconds() const { return std::chrono::duration<double>(std::chrono::steady_clock::now() - start_).count(); }

private:
    std::chrono::steady_clock::time_point start_;
};

}

SourceInfo inspectSource(const std::string& pathUtf8, bool allowAnyCodec) {
    SourceInfo info;
    std::string problem;
    const FfmpegApi* api = ffmpegApi(problem);
    if (api == nullptr) {
        info.error = "FFmpeg is not available: " + problem;
        return info;
    }
    Input input;
    if (!input.open(*api, pathUtf8, allowAnyCodec, false, info.error)) {
        info.isDxv = input.format != nullptr && input.stream >= 0 && input.format->streams[input.stream]->codecpar->codec_id == AV_CODEC_ID_DXV;
        return info;
    }
    const AVStream* stream = input.format->streams[input.stream];
    info.isDxv = stream->codecpar->codec_id == AV_CODEC_ID_DXV;
    info.width = stream->codecpar->width;
    info.height = stream->codecpar->height;
    info.fps = framesPerSecond(stream);
    if (stream->nb_frames > 0) {
        info.frames = static_cast<int>(stream->nb_frames);
    }
    if (stream->duration > 0) {
        info.duration = static_cast<double>(stream->duration) * static_cast<double>(stream->time_base.num) / static_cast<double>(stream->time_base.den);
    } else if (input.format->duration > 0) {
        info.duration = static_cast<double>(input.format->duration) / static_cast<double>(AV_TIME_BASE);
    }
    if (info.frames == 0 && info.duration > 0.0 && info.fps > 0.0) {
        info.frames = static_cast<int>(info.duration * info.fps + 0.5);
    }
    if (info.isDxv) {
        AVPacket* packet = api->av_packet_alloc();
        if (packet != nullptr) {
            for (int guard = 0; guard < 64; ++guard) {
                if (api->av_read_frame(input.format, packet) < 0) {
                    break;
                }
                const bool mine = packet->stream_index == input.stream;
                if (mine) {
                    info.variant = variantFromPacket(packet->data, static_cast<size_t>(packet->size));
                }
                api->av_packet_unref(packet);
                if (mine) {
                    break;
                }
            }
            api->av_packet_free(&packet);
        }
    }
    info.ok = true;
    return info;
}

bool verifyOutput(const std::string& pathUtf8, const Result& expected, std::string& error) {
    std::string problem;
    const FfmpegApi* api = ffmpegApi(problem);
    if (api == nullptr) {
        error = "FFmpeg is not available";
        return false;
    }
    AVFormatContext* format = nullptr;
    if (api->avformat_open_input(&format, pathUtf8.c_str(), nullptr, nullptr) < 0) {
        error = "The converted file cannot be opened";
        return false;
    }
    struct Closer {
        const FfmpegApi* api;
        AVFormatContext** format;
        ~Closer() { api->avformat_close_input(format); }
    } closer{api, &format};
    if (api->avformat_find_stream_info(format, nullptr) < 0) {
        error = "The converted file cannot be read";
        return false;
    }
    const int index = api->av_find_best_stream(format, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (index < 0) {
        error = "The converted file has no video";
        return false;
    }
    const AVStream* stream = format->streams[index];
    if (stream->codecpar->codec_id != AV_CODEC_ID_HAP) {
        error = "The converted file is not HAP";
        return false;
    }
    if (stream->codecpar->width != expected.width || stream->codecpar->height != expected.height) {
        error = "The converted file has a different picture size";
        return false;
    }
    if (stream->nb_frames > 0 && stream->nb_frames != expected.frames) {
        error = "The converted file has a different number of pictures";
        return false;
    }
    const CompressedFormat wanted = expected.format == TargetFormat::HapQ ? CompressedFormat::YcoCgDxt5 : (expected.format == TargetFormat::HapAlpha ? CompressedFormat::Dxt5 : CompressedFormat::Dxt1);
    AVPacket* packet = api->av_packet_alloc();
    if (packet == nullptr) {
        error = "Out of memory";
        return false;
    }
    bool ok = true;
    int checked = 0;
    while (ok && checked < 2) {
        if (checked == 1) {
            const int64_t target = stream->duration > 0 ? stream->duration - 1 : 0;
            if (api->av_seek_frame(format, index, target, AVSEEK_FLAG_BACKWARD) < 0) {
                break;
            }
        }
        bool got = false;
        while (api->av_read_frame(format, packet) >= 0) {
            if (packet->stream_index == index) {
                HapFrameInfo frameInfo;
                const HapResult inspected = hapInspect(packet->data, static_cast<size_t>(packet->size), frameInfo);
                if (inspected != HapResult::Ok || frameInfo.format != wanted) {
                    error = "A picture of the converted file is not valid";
                    ok = false;
                }
                got = true;
            }
            api->av_packet_unref(packet);
            if (got) {
                break;
            }
        }
        if (!got && ok) {
            error = "The converted file contains no pictures";
            ok = false;
        }
        ++checked;
    }
    api->av_packet_free(&packet);
    return ok;
}

Result convertFile(const std::string& sourceUtf8, const std::string& outputUtf8, const Options& options, const ProgressFn& progress) {
    Result result;
    Clock total;
    std::string problem;
    const FfmpegApi* api = ffmpegApi(problem);
    if (api == nullptr) {
        result.error = "FFmpeg is not available: " + problem;
        return result;
    }
    std::error_code sizeEc;
    result.sourceBytes = static_cast<unsigned long long>(fs::file_size(fs::path(std::u8string(sourceUtf8.begin(), sourceUtf8.end())), sizeEc));
    Input input;
    if (!input.open(*api, sourceUtf8, options.allowAnyCodec, true, result.error)) {
        return result;
    }
    AVStream* inStream = input.format->streams[input.stream];
    const int width = inStream->codecpar->width;
    const int height = inStream->codecpar->height;
    if (width % 4 != 0 || height % 4 != 0) {
        result.error = "The picture size is not a multiple of 4, HAP cannot store it";
        return result;
    }
    if (width > 8192 || height > 8192) {
        result.error = "The picture is larger than 8192 pixels";
        return result;
    }
    const AVCodec* encoder = api->avcodec_find_encoder_by_name("hap");
    if (encoder == nullptr) {
        result.error = "This FFmpeg has no HAP encoder";
        return result;
    }
    const AVRational frameRate = frameRateOf(inStream);
    result.width = width;
    result.height = height;
    result.fps = static_cast<double>(frameRate.num) / static_cast<double>(frameRate.den);
    int expectedFrames = inStream->nb_frames > 0 ? static_cast<int>(inStream->nb_frames) : 0;
    if (expectedFrames == 0 && inStream->duration > 0) {
        expectedFrames = static_cast<int>(static_cast<double>(inStream->duration) * inStream->time_base.num / inStream->time_base.den * result.fps + 0.5);
    }

    Output output;
    output.api = api;
    output.packet = api->av_packet_alloc();
    AVPacket* inPacket = api->av_packet_alloc();
    AVFrame* decoded = api->av_frame_alloc();
    struct Releaser {
        const FfmpegApi* api;
        AVPacket** packet;
        AVFrame** frame;
        ~Releaser() {
            api->av_packet_free(packet);
            api->av_frame_free(frame);
        }
    } releaser{api, &inPacket, &decoded};
    if (output.packet == nullptr || inPacket == nullptr || decoded == nullptr) {
        result.error = "Out of memory";
        return result;
    }

    Variant tagVariant = Variant::Unknown;
    bool tagSeen = false;
    bool opened = false;
    bool cancelled = false;
    bool failed = false;
    int framesIn = 0;
    int framesOut = 0;
    int64_t firstPts = 0;
    bool firstPtsSet = false;
    RgbaConverter converter;
    double decodeSeconds = 0.0;
    double encodeSeconds = 0.0;
    const std::string partPath = outputUtf8;

    auto fail = [&](const std::string& message) {
        if (!failed && !cancelled) {
            result.error = message;
        }
        failed = true;
    };

    auto writePackets = [&]() {
        for (;;) {
            const int received = api->avcodec_receive_packet(output.codec, output.packet);
            if (received == AVERROR(EAGAIN) || received == AVERROR_EOF) {
                return;
            }
            if (received < 0) {
                fail("The HAP encoder failed: " + describe(*api, received));
                return;
            }
            api->av_packet_rescale_ts(output.packet, output.codec->time_base, output.stream->time_base);
            output.packet->stream_index = output.stream->index;
            result.bytes += static_cast<unsigned long long>(output.packet->size);
            const int written = api->av_interleaved_write_frame(output.format, output.packet);
            if (written < 0) {
                fail("The converted file cannot be written: " + describe(*api, written));
                return;
            }
        }
    };

    auto openEncoder = [&](const AVFrame& first) {
        Variant variant = tagSeen ? tagVariant : Variant::Unknown;
        if (variant == Variant::Unknown) {
            if (first.colorspace == AVCOL_SPC_YCGCO) {
                variant = variantFromFrame(*api, first);
            } else if (inStream->codecpar->codec_id == AV_CODEC_ID_DXV) {
                variant = Variant::Dxt5;
            } else {
                variant = variantFromFrame(*api, first);
            }
        }
        result.variant = variant;
        result.format = chooseTarget(variant, options.preference, result.alphaKept);
        const int created = api->avformat_alloc_output_context2(&output.format, nullptr, "mov", partPath.c_str());
        if (created < 0 || output.format == nullptr) {
            fail("The output file cannot be created: " + describe(*api, created));
            return;
        }
        output.stream = api->avformat_new_stream(output.format, encoder);
        output.codec = api->avcodec_alloc_context3(encoder);
        if (output.stream == nullptr || output.codec == nullptr) {
            fail("Out of memory");
            return;
        }
        output.codec->width = width;
        output.codec->height = height;
        output.codec->pix_fmt = AV_PIX_FMT_RGBA;
        output.codec->time_base = inStream->time_base.num > 0 && inStream->time_base.den > 0 ? inStream->time_base : AVRational{frameRate.den, frameRate.num};
        output.codec->framerate = frameRate;
        output.codec->thread_count = std::max(1, options.threads);
        if ((output.format->oformat->flags & AVFMT_GLOBALHEADER) != 0) {
            output.codec->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
        }
        if (output.codec->priv_data != nullptr) {
            api->av_opt_set(output.codec->priv_data, "format", encoderFormat(result.format), 0);
            result.snappy = options.snappy;
            api->av_opt_set(output.codec->priv_data, "compressor", options.snappy ? "snappy" : "none", 0);
        }
        int code = api->avcodec_open2(output.codec, encoder, nullptr);
        if (code < 0 && options.snappy) {
            api->avcodec_free_context(&output.codec);
            output.codec = api->avcodec_alloc_context3(encoder);
            if (output.codec == nullptr) {
                fail("Out of memory");
                return;
            }
            output.codec->width = width;
            output.codec->height = height;
            output.codec->pix_fmt = AV_PIX_FMT_RGBA;
            output.codec->time_base = inStream->time_base.num > 0 && inStream->time_base.den > 0 ? inStream->time_base : AVRational{frameRate.den, frameRate.num};
            output.codec->framerate = frameRate;
            output.codec->thread_count = std::max(1, options.threads);
            if ((output.format->oformat->flags & AVFMT_GLOBALHEADER) != 0) {
                output.codec->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
            }
            api->av_opt_set(output.codec->priv_data, "format", encoderFormat(result.format), 0);
            api->av_opt_set(output.codec->priv_data, "compressor", "none", 0);
            result.snappy = false;
            code = api->avcodec_open2(output.codec, encoder, nullptr);
        }
        if (code < 0) {
            fail("The HAP encoder could not be started: " + describe(*api, code));
            return;
        }
        const int copied = api->avcodec_parameters_from_context(output.stream->codecpar, output.codec);
        if (copied < 0) {
            fail("The output stream could not be set up: " + describe(*api, copied));
            return;
        }
        output.stream->time_base = output.codec->time_base;
        output.stream->avg_frame_rate = frameRate;
        const int io = api->avio_open(&output.format->pb, partPath.c_str(), AVIO_FLAG_WRITE);
        if (io < 0) {
            fail("The output file cannot be opened: " + describe(*api, io));
            return;
        }
        output.ioOpen = true;
        const int header = api->avformat_write_header(output.format, nullptr);
        if (header < 0) {
            fail("The output file header cannot be written: " + describe(*api, header));
            return;
        }
        output.headerWritten = true;
        output.frame = api->av_frame_alloc();
        if (output.frame == nullptr) {
            fail("Out of memory");
            return;
        }
        output.frame->format = AV_PIX_FMT_RGBA;
        output.frame->width = width;
        output.frame->height = height;
        if (api->av_frame_get_buffer(output.frame, 0) < 0) {
            fail("Out of memory");
            return;
        }
        opened = true;
    };

    auto handleFrame = [&](AVFrame& frame) {
        if (frame.width != width || frame.height != height) {
            fail("The picture size changes inside the video");
            return;
        }
        if (!opened) {
            openEncoder(frame);
            if (failed) {
                return;
            }
        }
        if (api->av_frame_make_writable(output.frame) < 0) {
            fail("Out of memory");
            return;
        }
        Clock encodeClock;
        if (!converter.convert(*api, frame, output.frame->data[0], output.frame->linesize[0])) {
            fail("The picture format of this video cannot be converted");
            return;
        }
        int64_t pts = frame.best_effort_timestamp;
        if (pts == AV_NOPTS_VALUE) {
            pts = frame.pts;
        }
        if (pts == AV_NOPTS_VALUE) {
            pts = api->av_rescale_q(framesIn, AVRational{frameRate.den, frameRate.num}, output.codec->time_base);
        }
        if (!firstPtsSet) {
            firstPts = pts;
            firstPtsSet = true;
        }
        output.frame->pts = pts - firstPts;
        const int sent = api->avcodec_send_frame(output.codec, output.frame);
        if (sent < 0) {
            fail("The HAP encoder rejected a picture: " + describe(*api, sent));
            return;
        }
        writePackets();
        encodeSeconds += encodeClock.seconds();
        ++framesIn;
        ++framesOut;
        if (progress && !progress(framesOut, expectedFrames > 0 ? std::max(expectedFrames, framesOut) : 0)) {
            cancelled = true;
        }
    };

    auto drainDecoder = [&]() {
        for (;;) {
            Clock decodeClock;
            const int received = api->avcodec_receive_frame(input.codec, decoded);
            decodeSeconds += decodeClock.seconds();
            if (received == AVERROR(EAGAIN) || received == AVERROR_EOF) {
                return;
            }
            if (received < 0) {
                fail("The video cannot be decoded: " + describe(*api, received));
                return;
            }
            handleFrame(*decoded);
            api->av_frame_unref(decoded);
            if (failed || cancelled) {
                return;
            }
        }
    };

    while (!failed && !cancelled) {
        const int read = api->av_read_frame(input.format, inPacket);
        if (read < 0) {
            break;
        }
        if (inPacket->stream_index != input.stream) {
            api->av_packet_unref(inPacket);
            continue;
        }
        if (!tagSeen) {
            tagVariant = variantFromPacket(inPacket->data, static_cast<size_t>(inPacket->size));
            tagSeen = true;
        }
        Clock decodeClock;
        const int sent = api->avcodec_send_packet(input.codec, inPacket);
        decodeSeconds += decodeClock.seconds();
        api->av_packet_unref(inPacket);
        if (sent < 0 && sent != AVERROR(EAGAIN)) {
            fail("The video cannot be decoded: " + describe(*api, sent));
            break;
        }
        drainDecoder();
    }
    if (!failed && !cancelled) {
        api->avcodec_send_packet(input.codec, nullptr);
        drainDecoder();
    }
    if (!failed && !cancelled && opened) {
        api->avcodec_send_frame(output.codec, nullptr);
        writePackets();
    }
    if (!failed && !cancelled && !opened) {
        fail("The video contains no pictures");
    }
    if (!failed && !cancelled) {
        const int trailer = api->av_write_trailer(output.format);
        if (trailer < 0) {
            fail("The converted file cannot be finished: " + describe(*api, trailer));
        } else {
            output.trailerWritten = true;
        }
    }
    output.closeIo();
    result.decodeSeconds = decodeSeconds;
    result.encodeSeconds = encodeSeconds;
    result.frames = framesOut;
    if (cancelled || failed) {
        result.cancelled = cancelled && !failed;
        if (result.cancelled) {
            result.error = "Cancelled";
        }
        std::error_code ec;
        fs::remove(fs::path(std::u8string(partPath.begin(), partPath.end())), ec);
        result.seconds = total.seconds();
        return result;
    }
    std::error_code ec;
    const auto size = fs::file_size(fs::path(std::u8string(partPath.begin(), partPath.end())), ec);
    if (!ec) {
        result.bytes = static_cast<unsigned long long>(size);
    }
    if (options.verify) {
        std::string verifyError;
        if (!verifyOutput(partPath, result, verifyError)) {
            result.error = verifyError;
            fs::remove(fs::path(std::u8string(partPath.begin(), partPath.end())), ec);
            result.seconds = total.seconds();
            return result;
        }
    }
    result.ok = true;
    result.seconds = total.seconds();
    return result;
}

#else

SourceInfo inspectSource(const std::string&, bool) {
    SourceInfo info;
    info.error = "This build of NyrexVJ was made without video support";
    return info;
}

bool verifyOutput(const std::string&, const Result&, std::string& error) {
    error = "This build of NyrexVJ was made without video support";
    return false;
}

Result convertFile(const std::string&, const std::string&, const Options&, const ProgressFn&) {
    Result result;
    result.error = "This build of NyrexVJ was made without video support";
    return result;
}

#endif

}
