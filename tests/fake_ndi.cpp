#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
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

struct Source {
    const char* name;
    const char* url;
};

struct FindCreate {
    bool showLocalSources;
    const char* groups;
    const char* extraIps;
};

struct RecvCreate {
    Source source;
    int colorFormat;
    int bandwidth;
    bool allowVideoFields;
    const char* receiverName;
};

struct Receive {
    std::mutex mutex;
    std::vector<std::string> sources;
    bool sourcesChanged = false;
    std::vector<std::string> snapshotNames;
    std::vector<std::string> snapshotUrls;
    std::vector<Source> snapshot;
    int finderCreated = 0;
    int finderDestroyed = 0;
    std::set<void*> finders;
    int recvCreated = 0;
    int recvDestroyed = 0;
    std::set<void*> receivers;
    bool refuseCreate = false;
    bool injectError = false;
    std::string lastName;
    int lastColorFormat = 0;
    int lastBandwidth = 0;
    bool lastAllowFields = true;
    std::string lastReceiverName;
    int connections = 1;
    bool streaming = false;
    int width = 0;
    int height = 0;
    uint32_t fourCC = 0;
    int fps = 30;
    int padding = 0;
    int pending = 0;
    int counter = 0;
    std::chrono::steady_clock::time_point nextFrame{};
    int captured = 0;
    int freed = 0;
    int badFrees = 0;
    std::map<const uint8_t*, std::unique_ptr<std::vector<uint8_t>>> buffers;
};

State state;
int token = 0;
Receive rx;

uint8_t pattern(int counter, int y, int index) {
    return static_cast<uint8_t>(counter * 7 + y * 3 + index * 5);
}

}

#ifndef FAKE_NDI_V3_ONLY
FAKE_EXPORT const char* NDIlib_version() {
    return "NDI SDK FAKE 6.0.1 (test library)";
}
#endif

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

FAKE_EXPORT void* NDIlib_find_create_v2(const FindCreate* create) {
    if (!state.initialized || !create) {
        return nullptr;
    }
    std::lock_guard<std::mutex> lock(rx.mutex);
    void* handle = new int(0);
    rx.finders.insert(handle);
    ++rx.finderCreated;
    rx.sourcesChanged = true;
    return handle;
}

FAKE_EXPORT void NDIlib_find_destroy(void* find) {
    std::lock_guard<std::mutex> lock(rx.mutex);
    if (rx.finders.erase(find) > 0) {
        delete static_cast<int*>(find);
        ++rx.finderDestroyed;
    }
}

FAKE_EXPORT bool NDIlib_find_wait_for_sources(void* find, uint32_t timeout) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout);
    for (;;) {
        {
            std::lock_guard<std::mutex> lock(rx.mutex);
            if (rx.finders.count(find) == 0) {
                return false;
            }
            if (rx.sourcesChanged) {
                rx.sourcesChanged = false;
                return true;
            }
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
}

FAKE_EXPORT const Source* NDIlib_find_get_current_sources(void* find, uint32_t* count) {
    std::lock_guard<std::mutex> lock(rx.mutex);
    if (rx.finders.count(find) == 0 || count == nullptr) {
        return nullptr;
    }
    rx.snapshotNames = rx.sources;
    rx.snapshotUrls.clear();
    rx.snapshot.clear();
    for (const std::string& name : rx.snapshotNames) {
        rx.snapshotUrls.push_back("ndi://" + name);
    }
    for (size_t i = 0; i < rx.snapshotNames.size(); ++i) {
        rx.snapshot.push_back(Source{rx.snapshotNames[i].c_str(), rx.snapshotUrls[i].c_str()});
    }
    *count = static_cast<uint32_t>(rx.snapshot.size());
    return rx.snapshot.empty() ? nullptr : rx.snapshot.data();
}

FAKE_EXPORT void* NDIlib_recv_create_v3(const RecvCreate* create) {
    if (!state.initialized || !create) {
        return nullptr;
    }
    std::lock_guard<std::mutex> lock(rx.mutex);
    if (rx.refuseCreate) {
        return nullptr;
    }
    rx.lastName = create->source.name ? create->source.name : "";
    rx.lastColorFormat = create->colorFormat;
    rx.lastBandwidth = create->bandwidth;
    rx.lastAllowFields = create->allowVideoFields;
    rx.lastReceiverName = create->receiverName ? create->receiverName : "";
    void* handle = new int(0);
    rx.receivers.insert(handle);
    ++rx.recvCreated;
    return handle;
}

FAKE_EXPORT void NDIlib_recv_destroy(void* recv) {
    std::lock_guard<std::mutex> lock(rx.mutex);
    if (rx.receivers.erase(recv) > 0) {
        delete static_cast<int*>(recv);
        ++rx.recvDestroyed;
    }
}

FAKE_EXPORT int NDIlib_recv_get_no_connections(void* recv) {
    std::lock_guard<std::mutex> lock(rx.mutex);
    return rx.receivers.count(recv) > 0 ? rx.connections : 0;
}

#ifdef FAKE_NDI_V3_ONLY
FAKE_EXPORT int NDIlib_recv_capture_v3(void* recv, VideoFrame* video, void*, void*, uint32_t timeout) {
#else
FAKE_EXPORT int NDIlib_recv_capture_v2(void* recv, VideoFrame* video, void*, void*, uint32_t timeout) {
#endif
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout);
    for (;;) {
        {
            std::lock_guard<std::mutex> lock(rx.mutex);
            if (rx.receivers.count(recv) == 0 || video == nullptr) {
                return 0;
            }
            if (rx.injectError) {
                rx.injectError = false;
                return 4;
            }
            const auto now = std::chrono::steady_clock::now();
            bool produce = false;
            if (rx.pending > 0) {
                --rx.pending;
                produce = true;
            } else if (rx.streaming && now >= rx.nextFrame) {
                produce = true;
                rx.nextFrame = now + std::chrono::microseconds(1000000 / (rx.fps > 0 ? rx.fps : 30));
            }
            if (produce) {
                ++rx.counter;
                const int width = rx.width;
                const int height = rx.height;
                const bool rgba = rx.fourCC == (static_cast<uint32_t>('R') | (static_cast<uint32_t>('G') << 8) | (static_cast<uint32_t>('B') << 16) | (static_cast<uint32_t>('A') << 24));
                const bool uyva = rx.fourCC == (static_cast<uint32_t>('U') | (static_cast<uint32_t>('Y') << 8) | (static_cast<uint32_t>('V') << 16) | (static_cast<uint32_t>('A') << 24));
                const int rowBytes = rgba ? width * 4 : width * 2;
                const int stride = rowBytes + rx.padding;
                const size_t plane = static_cast<size_t>(stride) * static_cast<size_t>(height);
                const size_t total = plane + (uyva ? static_cast<size_t>(width) * static_cast<size_t>(height) : 0u);
                auto buffer = std::make_unique<std::vector<uint8_t>>(total, 0xEE);
                for (int y = 0; y < height; ++y) {
                    for (int i = 0; i < rowBytes; ++i) {
                        (*buffer)[static_cast<size_t>(y) * static_cast<size_t>(stride) + static_cast<size_t>(i)] = pattern(rx.counter, y, i);
                    }
                }
                std::memset(video, 0, sizeof(VideoFrame));
                video->xres = width;
                video->yres = height;
                video->fourCC = rx.fourCC;
                video->rateN = rx.fps;
                video->rateD = 1;
                video->aspect = height > 0 ? static_cast<float>(width) / static_cast<float>(height) : 1.0f;
                video->formatType = 1;
                video->timecode = INT64_MAX;
                video->data = buffer->data();
                video->stride = stride;
                video->timestamp = INT64_MIN;
                rx.buffers[buffer->data()] = std::move(buffer);
                ++rx.captured;
                return 1;
            }
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            return 0;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

FAKE_EXPORT void NDIlib_recv_free_video_v2(void*, VideoFrame* video) {
    std::lock_guard<std::mutex> lock(rx.mutex);
    if (video == nullptr) {
        return;
    }
    const auto found = rx.buffers.find(video->data);
    if (found == rx.buffers.end()) {
        ++rx.badFrees;
        return;
    }
    rx.buffers.erase(found);
    ++rx.freed;
    video->data = nullptr;
}

FAKE_EXPORT void fake_ndi_set_sources(const char* names) {
    std::lock_guard<std::mutex> lock(rx.mutex);
    rx.sources.clear();
    std::string text = names ? names : "";
    size_t start = 0;
    while (start <= text.size() && !text.empty()) {
        size_t end = text.find('\n', start);
        if (end == std::string::npos) {
            end = text.size();
        }
        if (end > start) {
            rx.sources.push_back(text.substr(start, end - start));
        }
        start = end + 1;
    }
    rx.sourcesChanged = true;
}

FAKE_EXPORT void fake_ndi_set_stream(int on, int width, int height, uint32_t fourCC, int fps, int padding) {
    std::lock_guard<std::mutex> lock(rx.mutex);
    rx.streaming = on != 0;
    rx.width = width;
    rx.height = height;
    rx.fourCC = fourCC;
    rx.fps = fps;
    rx.padding = padding;
    rx.nextFrame = std::chrono::steady_clock::now();
}

FAKE_EXPORT void fake_ndi_push_burst(int count) {
    std::lock_guard<std::mutex> lock(rx.mutex);
    rx.pending += count;
}

FAKE_EXPORT void fake_ndi_set_counter(int value) {
    std::lock_guard<std::mutex> lock(rx.mutex);
    rx.counter = value;
}

FAKE_EXPORT void fake_ndi_inject_error() {
    std::lock_guard<std::mutex> lock(rx.mutex);
    rx.injectError = true;
}

FAKE_EXPORT void fake_ndi_set_refuse_create(int value) {
    std::lock_guard<std::mutex> lock(rx.mutex);
    rx.refuseCreate = value != 0;
}

FAKE_EXPORT void fake_ndi_set_recv_connections(int value) {
    std::lock_guard<std::mutex> lock(rx.mutex);
    rx.connections = value;
}

FAKE_EXPORT int fake_ndi_finder_created() {
    std::lock_guard<std::mutex> lock(rx.mutex);
    return rx.finderCreated;
}

FAKE_EXPORT int fake_ndi_finder_destroyed() {
    std::lock_guard<std::mutex> lock(rx.mutex);
    return rx.finderDestroyed;
}

FAKE_EXPORT int fake_ndi_recv_created() {
    std::lock_guard<std::mutex> lock(rx.mutex);
    return rx.recvCreated;
}

FAKE_EXPORT int fake_ndi_recv_destroyed() {
    std::lock_guard<std::mutex> lock(rx.mutex);
    return rx.recvDestroyed;
}

FAKE_EXPORT int fake_ndi_recv_alive() {
    std::lock_guard<std::mutex> lock(rx.mutex);
    return static_cast<int>(rx.receivers.size());
}

FAKE_EXPORT int fake_ndi_captured() {
    std::lock_guard<std::mutex> lock(rx.mutex);
    return rx.captured;
}

FAKE_EXPORT int fake_ndi_freed() {
    std::lock_guard<std::mutex> lock(rx.mutex);
    return rx.freed;
}

FAKE_EXPORT int fake_ndi_bad_frees() {
    std::lock_guard<std::mutex> lock(rx.mutex);
    return rx.badFrees;
}

FAKE_EXPORT int fake_ndi_outstanding() {
    std::lock_guard<std::mutex> lock(rx.mutex);
    return static_cast<int>(rx.buffers.size());
}

FAKE_EXPORT int fake_ndi_counter() {
    std::lock_guard<std::mutex> lock(rx.mutex);
    return rx.counter;
}

FAKE_EXPORT int fake_ndi_last_color_format() {
    std::lock_guard<std::mutex> lock(rx.mutex);
    return rx.lastColorFormat;
}

FAKE_EXPORT int fake_ndi_last_bandwidth() {
    std::lock_guard<std::mutex> lock(rx.mutex);
    return rx.lastBandwidth;
}

FAKE_EXPORT int fake_ndi_last_allow_fields() {
    std::lock_guard<std::mutex> lock(rx.mutex);
    return rx.lastAllowFields ? 1 : 0;
}

FAKE_EXPORT int fake_ndi_last_recv_name(char* out, int size) {
    std::lock_guard<std::mutex> lock(rx.mutex);
    if (out == nullptr || size <= 0) {
        return 0;
    }
    std::snprintf(out, static_cast<size_t>(size), "%s", rx.lastName.c_str());
    return static_cast<int>(rx.lastName.size());
}

FAKE_EXPORT int fake_ndi_last_receiver_name(char* out, int size) {
    std::lock_guard<std::mutex> lock(rx.mutex);
    if (out == nullptr || size <= 0) {
        return 0;
    }
    std::snprintf(out, static_cast<size_t>(size), "%s", rx.lastReceiverName.c_str());
    return static_cast<int>(rx.lastReceiverName.size());
}
