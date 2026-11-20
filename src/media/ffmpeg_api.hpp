#pragma once

#ifdef NYREXVJ_HAVE_FFMPEG

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/buffer.h>
#include <libavutil/dict.h>
#include <libavutil/error.h>
#include <libavutil/hwcontext.h>
#include <libavutil/pixdesc.h>
#include <libswscale/swscale.h>
}

#include <string>

#define NYX_FFMPEG_FUNCTIONS(X) \
    X(avformat_alloc_context) \
    X(avformat_open_input) \
    X(av_find_input_format) \
    X(av_dict_set) \
    X(av_dict_free) \
    X(avformat_find_stream_info) \
    X(av_find_best_stream) \
    X(av_read_frame) \
    X(av_seek_frame) \
    X(avformat_close_input) \
    X(avcodec_find_decoder) \
    X(avcodec_find_decoder_by_name) \
    X(avcodec_get_hw_config) \
    X(avcodec_alloc_context3) \
    X(avcodec_parameters_to_context) \
    X(avcodec_open2) \
    X(avcodec_send_packet) \
    X(avcodec_receive_frame) \
    X(avcodec_flush_buffers) \
    X(avcodec_free_context) \
    X(avcodec_get_name) \
    X(av_packet_alloc) \
    X(av_packet_free) \
    X(av_packet_unref) \
    X(av_frame_alloc) \
    X(av_frame_free) \
    X(av_frame_unref) \
    X(av_frame_move_ref) \
    X(av_frame_copy_props) \
    X(av_hwdevice_ctx_create) \
    X(av_hwdevice_find_type_by_name) \
    X(av_hwdevice_get_type_name) \
    X(av_hwframe_transfer_data) \
    X(av_buffer_ref) \
    X(av_buffer_unref) \
    X(av_strerror) \
    X(av_get_pix_fmt_name) \
    X(av_log_set_level) \
    X(av_pix_fmt_desc_get) \
    X(sws_getCachedContext) \
    X(sws_scale) \
    X(sws_freeContext) \
    X(sws_setColorspaceDetails) \
    X(sws_getCoefficients)

namespace nvj {

#define NYX_FFMPEG_TYPE(name) using name##_fn = decltype(&::name);
NYX_FFMPEG_FUNCTIONS(NYX_FFMPEG_TYPE)
#undef NYX_FFMPEG_TYPE

struct FfmpegApi {
#define NYX_FFMPEG_MEMBER(name) name##_fn name = nullptr;
    NYX_FFMPEG_FUNCTIONS(NYX_FFMPEG_MEMBER)
#undef NYX_FFMPEG_MEMBER
};

const FfmpegApi* ffmpegApi(std::string& problem);

struct FfmpegDeviceInfo {
    char* name;
    char* description;
    int* mediaTypes;
    int mediaTypeCount;
};

struct FfmpegDeviceInfoList {
    FfmpegDeviceInfo** devices;
    int count;
    int defaultDevice;
};

struct FfmpegDeviceApi {
    void (*registerAll)() = nullptr;
    int (*listInputSources)(const AVInputFormat*, const char*, AVDictionary*, FfmpegDeviceInfoList**) = nullptr;
    void (*freeList)(FfmpegDeviceInfoList**) = nullptr;
};

const FfmpegDeviceApi* ffmpegDeviceApi(std::string& problem);

}

#endif
