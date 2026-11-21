#include "media/video_hardware.hpp"

#include "media/ffmpeg_api.hpp"
#include "media/video_decoder.hpp"
#include "media/video_hardware_internal.hpp"
#include "media/video_probe_data.hpp"

#ifndef _WIN32
#include <dlfcn.h>
#endif

namespace nvj {

const char* probeCodecLabel(size_t index) {
    static const char* const labels[kProbeCodecCount] = {"H.264", "HEVC", "AV1"};
    return index < kProbeCodecCount ? labels[index] : "";
}

std::vector<std::string> hardwareBackendOrder() {
#ifdef _WIN32
    return {"d3d11va", "dxva2", "cuda", "qsv"};
#else
    return {"cuda", "vaapi", "qsv", "vdpau"};
#endif
}

std::vector<std::string> hardwareBackendLibraries(const std::string& backend) {
#ifdef _WIN32
    (void)backend;
    return {};
#else
    if (backend == "vaapi") {
        return {"libva.so.2", "libva-drm.so.2", "libva-x11.so.2"};
    }
    if (backend == "qsv") {
        return {"libva.so.2", "libva-drm.so.2", "libvpl.so.2"};
    }
    if (backend == "vdpau") {
        return {"libvdpau.so.1"};
    }
    return {};
#endif
}

namespace {

bool systemCanLoad(const char* file) {
#ifdef _WIN32
    (void)file;
    return true;
#else
    void* handle = dlopen(file, RTLD_LAZY | RTLD_LOCAL);
    if (handle == nullptr) {
        return false;
    }
    dlclose(handle);
    return true;
#endif
}

}

std::string missingHardwareLibrary(const std::string& backend, bool (*canLoad)(const char*)) {
    if (canLoad == nullptr) {
        canLoad = systemCanLoad;
    }
    for (const std::string& file : hardwareBackendLibraries(backend)) {
        if (!canLoad(file.c_str())) {
            return file;
        }
    }
    return std::string();
}

std::string hardwareBackendLabel(const std::string& backend) {
    if (backend == "d3d11va") {
        return "D3D11VA";
    }
    if (backend == "dxva2") {
        return "DXVA2";
    }
    if (backend == "cuda") {
        return "NVDEC (CUDA)";
    }
    if (backend == "vaapi") {
        return "VA-API";
    }
    if (backend == "qsv") {
        return "Intel QSV";
    }
    if (backend == "vdpau") {
        return "VDPAU";
    }
    return backend;
}

std::string softwareOnlyCodecLabel(const std::string& codecName) {
    if (codecName == "dxv") {
        return "DXV";
    }
    if (codecName == "prores") {
        return "ProRes";
    }
    if (codecName == "dnxhd") {
        return "DNxHD/DNxHR";
    }
    if (codecName == "cfhd") {
        return "CineForm";
    }
    return std::string();
}

std::string softwareOnlyReason(const std::string& codecName) {
    if (codecName == "dxv") {
        return "DXV is decoded by the processor, graphics cards cannot decode this codec (this is normal)";
    }
    if (softwareOnlyCodecLabel(codecName).empty()) {
        return std::string();
    }
    return "this codec is decoded by the processor, graphics cards cannot decode it (this is normal)";
}

}

#ifdef NYREXVJ_HAVE_FFMPEG

#include <cstdint>
#include <cstring>
#include <vector>

namespace nvj {

namespace {

AVPixelFormat chooseHardwareFormat(AVCodecContext* context, const AVPixelFormat* formats) {
    const AVPixelFormat wanted = static_cast<AVPixelFormat>(reinterpret_cast<intptr_t>(context->opaque));
    for (const AVPixelFormat* format = formats; *format != AV_PIX_FMT_NONE; ++format) {
        if (*format == wanted) {
            return *format;
        }
    }
    return AV_PIX_FMT_NONE;
}

const char* qsvDecoderName(AVCodecID codec) {
    switch (codec) {
    case AV_CODEC_ID_H264:
        return "h264_qsv";
    case AV_CODEC_ID_HEVC:
        return "hevc_qsv";
    case AV_CODEC_ID_AV1:
        return "av1_qsv";
    case AV_CODEC_ID_VP9:
        return "vp9_qsv";
    case AV_CODEC_ID_MPEG2VIDEO:
        return "mpeg2_qsv";
    case AV_CODEC_ID_VC1:
        return "vc1_qsv";
    default:
        return nullptr;
    }
}

}

std::string ffmpegError(const FfmpegApi& api, int code) {
    char text[128] = {};
    if (api.av_strerror(code, text, sizeof(text)) < 0) {
        return "error " + std::to_string(code);
    }
    return text;
}

const AVCodec* hardwareDecoder(const FfmpegApi& api, AVCodecID codec, const std::string& backend) {
    if (backend == "qsv") {
        const char* name = qsvDecoderName(codec);
        return name != nullptr ? api.avcodec_find_decoder_by_name(name) : nullptr;
    }
    if (codec == AV_CODEC_ID_AV1) {
        return api.avcodec_find_decoder_by_name("av1");
    }
    return api.avcodec_find_decoder(codec);
}

bool prepareHardware(const FfmpegApi& api, AVCodecID codec, const std::string& backend, HardwareSetup& setup, std::string& reason) {
    setup = HardwareSetup{};
    setup.backend = backend;
    const AVHWDeviceType type = api.av_hwdevice_find_type_by_name(backend.c_str());
    if (type == AV_HWDEVICE_TYPE_NONE) {
        reason = "this FFmpeg has no " + hardwareBackendLabel(backend) + " support";
        return false;
    }
    const AVCodec* decoder = hardwareDecoder(api, codec, backend);
    if (decoder == nullptr) {
        reason = hardwareBackendLabel(backend) + " has no decoder for " + api.avcodec_get_name(codec) + " in this FFmpeg";
        return false;
    }
    AVPixelFormat format = AV_PIX_FMT_NONE;
    for (int i = 0;; ++i) {
        const AVCodecHWConfig* config = api.avcodec_get_hw_config(decoder, i);
        if (config == nullptr) {
            break;
        }
        if ((config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX) != 0 && config->device_type == type) {
            format = config->pix_fmt;
            break;
        }
    }
    if (format == AV_PIX_FMT_NONE) {
        reason = std::string(api.avcodec_get_name(codec)) + " is not supported by " + hardwareBackendLabel(backend) + " in this FFmpeg";
        return false;
    }
    const std::string missing = missingHardwareLibrary(backend);
    if (!missing.empty()) {
        reason = hardwareBackendLabel(backend) + " is not available on this computer (" + missing + " is not installed)";
        return false;
    }
    AVBufferRef* device = nullptr;
    const int created = api.av_hwdevice_ctx_create(&device, type, nullptr, nullptr, 0);
    if (created < 0 || device == nullptr) {
        reason = hardwareBackendLabel(backend) + " is not available on this computer (" + ffmpegError(api, created) + ")";
        return false;
    }
    setup.decoder = decoder;
    setup.type = type;
    setup.format = format;
    setup.device = device;
    return true;
}

void releaseHardware(const FfmpegApi& api, HardwareSetup& setup) {
    if (setup.device != nullptr) {
        api.av_buffer_unref(&setup.device);
    }
    setup.device = nullptr;
}

bool openHardwareContext(const FfmpegApi& api, const AVCodecParameters* parameters, HardwareSetup& setup, AVCodecContext*& context, std::string& reason) {
    context = api.avcodec_alloc_context3(setup.decoder);
    if (context == nullptr || (parameters != nullptr && api.avcodec_parameters_to_context(context, parameters) < 0)) {
        reason = "the hardware decoder could not be prepared";
        if (context != nullptr) {
            api.avcodec_free_context(&context);
        }
        return false;
    }
    context->hw_device_ctx = api.av_buffer_ref(setup.device);
    context->opaque = reinterpret_cast<void*>(static_cast<intptr_t>(setup.format));
    context->get_format = chooseHardwareFormat;
    context->thread_count = 1;
    const int opened = api.avcodec_open2(context, setup.decoder, nullptr);
    if (opened < 0) {
        reason = "the hardware decoder was refused (" + ffmpegError(api, opened) + ")";
        api.avcodec_free_context(&context);
        return false;
    }
    return true;
}

namespace {

ProbeState probeStream(const FfmpegApi& api, AVCodecID codec, const std::string& backend, const unsigned char* data, size_t size, std::string& detail) {
    HardwareSetup setup;
    AVCodecContext* context = nullptr;
    const bool software = backend == "software";
    std::string reason;
    if (software) {
        const AVCodec* decoder = api.avcodec_find_decoder(codec);
        if (decoder == nullptr) {
            detail = "no software decoder in this FFmpeg";
            return ProbeState::Unsupported;
        }
        context = api.avcodec_alloc_context3(decoder);
        if (context == nullptr || api.avcodec_open2(context, decoder, nullptr) < 0) {
            detail = "the decoder could not be opened";
            if (context != nullptr) {
                api.avcodec_free_context(&context);
            }
            return ProbeState::Failed;
        }
    } else {
        if (!prepareHardware(api, codec, backend, setup, reason)) {
            detail = reason;
            return ProbeState::Unsupported;
        }
        if (!openHardwareContext(api, nullptr, setup, context, reason)) {
            releaseHardware(api, setup);
            detail = reason;
            return ProbeState::Unsupported;
        }
    }
    AVPacket* packet = api.av_packet_alloc();
    AVFrame* frame = api.av_frame_alloc();
    ProbeState state = ProbeState::Failed;
    std::vector<uint8_t> padded(size + AV_INPUT_BUFFER_PADDING_SIZE, 0);
    std::memcpy(padded.data(), data, size);
    if (packet != nullptr && frame != nullptr) {
        packet->data = padded.data();
        packet->size = static_cast<int>(size);
        const int sent = api.avcodec_send_packet(context, packet);
        api.avcodec_send_packet(context, nullptr);
        const int received = sent < 0 ? sent : api.avcodec_receive_frame(context, frame);
        if (received < 0) {
            state = ProbeState::Unsupported;
            detail = "the graphics card cannot decode this stream (" + ffmpegError(api, received) + ")";
        } else if (software) {
            state = ProbeState::Supported;
        } else if (frame->format != static_cast<int>(setup.format)) {
            state = ProbeState::Unsupported;
            detail = "decoded in software instead";
        } else {
            AVFrame* download = api.av_frame_alloc();
            if (download != nullptr && api.av_hwframe_transfer_data(download, frame, 0) >= 0) {
                state = ProbeState::Supported;
            } else {
                state = ProbeState::Failed;
                detail = "the picture could not be read back from the graphics card";
            }
            if (download != nullptr) {
                api.av_frame_free(&download);
            }
        }
        packet->data = nullptr;
        packet->size = 0;
    }
    if (frame != nullptr) {
        api.av_frame_free(&frame);
    }
    if (packet != nullptr) {
        api.av_packet_free(&packet);
    }
    api.avcodec_free_context(&context);
    releaseHardware(api, setup);
    return state;
}

}

HardwareProbeReport probeHardwareDecoding() {
    HardwareProbeReport report;
    std::string problem;
    const FfmpegApi* api = ffmpegApi(problem);
    if (api == nullptr) {
        report.problem = problem;
        return report;
    }
    report.ffmpegAvailable = true;
    struct Stream {
        AVCodecID codec;
        const unsigned char* data;
        size_t size;
    };
    const Stream streams[kProbeCodecCount] = {
        {AV_CODEC_ID_H264, kProbeH264, kProbeH264Size},
        {AV_CODEC_ID_HEVC, kProbeHevc, kProbeHevcSize},
        {AV_CODEC_ID_AV1, kProbeAv1, kProbeAv1Size},
    };
    std::vector<std::string> backends = {"software"};
    for (const std::string& name : hardwareBackendOrder()) {
        backends.push_back(name);
    }
    for (const std::string& backend : backends) {
        HardwareProbeRow row;
        row.backend = backend;
        row.label = backend == "software" ? "Processor" : hardwareBackendLabel(backend);
        row.deviceFound = backend == "software";
        if (backend != "software") {
            const AVHWDeviceType type = api->av_hwdevice_find_type_by_name(backend.c_str());
            const std::string missing = missingHardwareLibrary(backend);
            if (type == AV_HWDEVICE_TYPE_NONE) {
                row.detail = "not part of this FFmpeg";
            } else if (!missing.empty()) {
                row.detail = "no device (" + missing + " is not installed)";
            } else {
                AVBufferRef* device = nullptr;
                const int created = api->av_hwdevice_ctx_create(&device, type, nullptr, nullptr, 0);
                if (created >= 0 && device != nullptr) {
                    row.deviceFound = true;
                    api->av_buffer_unref(&device);
                } else {
                    row.detail = "no device (" + ffmpegError(*api, created) + ")";
                }
            }
        }
        for (size_t i = 0; i < kProbeCodecCount; ++i) {
            if (!row.deviceFound) {
                row.codecs[i].state = ProbeState::Unsupported;
                row.codecs[i].detail = row.detail;
                continue;
            }
            row.codecs[i].state = probeStream(*api, streams[i].codec, backend, streams[i].data, streams[i].size, row.codecs[i].detail);
        }
        report.rows.push_back(std::move(row));
    }
    return report;
}

}

#else

namespace nvj {

HardwareProbeReport probeHardwareDecoding() {
    HardwareProbeReport report;
    report.problem = "this build was made without video support";
    return report;
}

}

#endif
