#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#ifdef _WIN32
#define FAKE_EXPORT extern "C" __declspec(dllexport)
#else
#define FAKE_EXPORT extern "C" __attribute__((visibility("default")))
#endif

namespace {

struct SendCreate {
    const char* name;
    const char* groups;
    bool clockVideo;
    bool clockAudio;
};

struct VideoFrame {
    int xres;
    int yres;
    uint32_t fourCC;
    int rateN;
    int rateD;
    float aspect;
    int formatType;
    int64_t timecode;
    const uint8_t* data;
    int stride;
    const char* metadata;
    int64_t timestamp;
};

struct State {
    bool initialized = false;
    int created = 0;
    int destroyed = 0;
    int frames = 0;
    int width = 0;
    int height = 0;
    uint32_t fourCC = 0;
    int stride = 0;
    int formatType = 0;
    int rateN = 0;
    int rateD = 0;
    int64_t timecode = 0;
    int connections = 1;
    std::string name;
    std::vector<uint8_t> last;
};

State state;
int token = 0;

}

FAKE_EXPORT bool NDIlib_initialize() {
    state.initialized = true;
    return true;
}

FAKE_EXPORT void NDIlib_destroy() {
    state.initialized = false;
}

FAKE_EXPORT void* NDIlib_send_create(const SendCreate* create) {
    if (!state.initialized || !create || !create->name) {
        return nullptr;
    }
    state.name = create->name;
    ++state.created;
    return &token;
}

FAKE_EXPORT void NDIlib_send_destroy(void* sender) {
    if (sender == &token) {
        ++state.destroyed;
        const char* path = std::getenv("FAKE_NDI_DUMP");
        if (path && *path && !state.last.empty()) {
            if (FILE* file = std::fopen(path, "wb")) {
                std::fprintf(file, "P6\n%d %d\n255\n", state.width, state.height);
                for (size_t i = 0; i + 3 < state.last.size(); i += 4) {
                    std::fwrite(&state.last[i], 1, 3, file);
                }
                std::fclose(file);
            }
        }
    }
}

FAKE_EXPORT void NDIlib_send_send_video_v2(void* sender, const VideoFrame* frame) {
    if (sender != &token || !frame || !frame->data) {
        return;
    }
    ++state.frames;
    state.width = frame->xres;
    state.height = frame->yres;
    state.fourCC = frame->fourCC;
    state.stride = frame->stride;
    state.formatType = frame->formatType;
    state.rateN = frame->rateN;
    state.rateD = frame->rateD;
    state.timecode = frame->timecode;
    state.last.assign(frame->data, frame->data + static_cast<size_t>(frame->stride) * static_cast<size_t>(frame->yres));
}

FAKE_EXPORT int NDIlib_send_get_no_connections(void* sender, uint32_t) {
    return sender == &token ? state.connections : 0;
}

FAKE_EXPORT void fake_ndi_set_connections(int value) {
    state.connections = value;
}

FAKE_EXPORT int fake_ndi_frames() {
    return state.frames;
}

FAKE_EXPORT int fake_ndi_created() {
    return state.created;
}

FAKE_EXPORT int fake_ndi_destroyed() {
    return state.destroyed;
}

FAKE_EXPORT int fake_ndi_width() {
    return state.width;
}

FAKE_EXPORT int fake_ndi_height() {
    return state.height;
}

FAKE_EXPORT int fake_ndi_stride() {
    return state.stride;
}

FAKE_EXPORT uint32_t fake_ndi_fourcc() {
    return state.fourCC;
}

FAKE_EXPORT int fake_ndi_format() {
    return state.formatType;
}

FAKE_EXPORT int fake_ndi_rate_n() {
    return state.rateN;
}

FAKE_EXPORT int fake_ndi_rate_d() {
    return state.rateD;
}

FAKE_EXPORT int64_t fake_ndi_timecode() {
    return state.timecode;
}

FAKE_EXPORT const char* fake_ndi_name() {
    return state.name.c_str();
}

FAKE_EXPORT const uint8_t* fake_ndi_last() {
    return state.last.data();
}

FAKE_EXPORT int fake_ndi_last_size() {
    return static_cast<int>(state.last.size());
}
