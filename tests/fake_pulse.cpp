#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>

#define FAKE_EXPORT extern "C" __attribute__((visibility("default")))

namespace {

struct SampleSpec {
    int format;
    uint32_t rate;
    uint8_t channels;
};

struct BufferAttr {
    uint32_t maxlength;
    uint32_t tlength;
    uint32_t prebuf;
    uint32_t minreq;
    uint32_t fragsize;
};

struct SourceInfo {
    const char* name;
    uint32_t index;
    const char* description;
};

struct Stream {
    double phase = 0.0;
    int reads = 0;
};

std::mutex lockState;
std::string lastDevice;
bool lastDeviceNull = false;
int lastRate = 0;
int lastChannels = 0;
int lastFormat = 0;
uint32_t lastFragsize = 0;
int opened = 0;
int freed = 0;
bool failOpen = false;
int failReadAfter = -1;
bool failContext = false;

struct Context {
    int polls = 0;
};

}

FAKE_EXPORT void fake_pulse_reset() {
    std::lock_guard<std::mutex> lock(lockState);
    lastDevice.clear();
    lastDeviceNull = false;
    lastRate = 0;
    lastChannels = 0;
    lastFormat = 0;
    lastFragsize = 0;
    opened = 0;
    freed = 0;
    failOpen = false;
    failReadAfter = -1;
    failContext = false;
}

FAKE_EXPORT void fake_pulse_fail_open(int value) {
    std::lock_guard<std::mutex> lock(lockState);
    failOpen = value != 0;
}

FAKE_EXPORT void fake_pulse_fail_read_after(int reads) {
    std::lock_guard<std::mutex> lock(lockState);
    failReadAfter = reads;
}

FAKE_EXPORT void fake_pulse_fail_context(int value) {
    std::lock_guard<std::mutex> lock(lockState);
    failContext = value != 0;
}

FAKE_EXPORT int fake_pulse_opened() {
    std::lock_guard<std::mutex> lock(lockState);
    return opened;
}

FAKE_EXPORT int fake_pulse_freed() {
    std::lock_guard<std::mutex> lock(lockState);
    return freed;
}

FAKE_EXPORT int fake_pulse_device_is_null() {
    std::lock_guard<std::mutex> lock(lockState);
    return lastDeviceNull ? 1 : 0;
}

FAKE_EXPORT const char* fake_pulse_device() {
    std::lock_guard<std::mutex> lock(lockState);
    return lastDevice.c_str();
}

FAKE_EXPORT int fake_pulse_rate() {
    std::lock_guard<std::mutex> lock(lockState);
    return lastRate;
}

FAKE_EXPORT int fake_pulse_channels() {
    std::lock_guard<std::mutex> lock(lockState);
    return lastChannels;
}

FAKE_EXPORT int fake_pulse_format() {
    std::lock_guard<std::mutex> lock(lockState);
    return lastFormat;
}

FAKE_EXPORT uint32_t fake_pulse_fragsize() {
    std::lock_guard<std::mutex> lock(lockState);
    return lastFragsize;
}

FAKE_EXPORT const char* pa_strerror(int code) {
    return code == 7 ? "Connection refused" : (code == 9 ? "Connection terminated" : "Unknown error");
}

FAKE_EXPORT void* pa_simple_new(const char*, const char*, int, const char* device, const char*, const SampleSpec* spec, const void*, const BufferAttr* attr, int* error) {
    std::lock_guard<std::mutex> lock(lockState);
    if (failOpen) {
        if (error) {
            *error = 7;
        }
        return nullptr;
    }
    lastDeviceNull = device == nullptr;
    lastDevice = device ? device : "";
    lastRate = spec ? static_cast<int>(spec->rate) : 0;
    lastChannels = spec ? spec->channels : 0;
    lastFormat = spec ? spec->format : 0;
    lastFragsize = attr ? attr->fragsize : 0;
    ++opened;
    return new Stream();
}

FAKE_EXPORT int pa_simple_read(void* handle, void* data, size_t bytes, int* error) {
    Stream* stream = static_cast<Stream*>(handle);
    int limit;
    {
        std::lock_guard<std::mutex> lock(lockState);
        limit = failReadAfter;
    }
    if (limit >= 0 && stream->reads >= limit) {
        if (error) {
            *error = 9;
        }
        return -1;
    }
    ++stream->reads;
    float* out = static_cast<float*>(data);
    const size_t count = bytes / sizeof(float);
    for (size_t i = 0; i < count; ++i) {
        out[i] = static_cast<float>(0.5 * std::sin(stream->phase));
        stream->phase += 2.0 * 3.14159265358979 * 440.0 / 48000.0;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    return 0;
}

FAKE_EXPORT void pa_simple_free(void* handle) {
    delete static_cast<Stream*>(handle);
    std::lock_guard<std::mutex> lock(lockState);
    ++freed;
}

FAKE_EXPORT void* pa_mainloop_new() {
    return new Context();
}

FAKE_EXPORT void pa_mainloop_free(void* loop) {
    delete static_cast<Context*>(loop);
}

FAKE_EXPORT void* pa_mainloop_get_api(void* loop) {
    return loop;
}

FAKE_EXPORT int pa_mainloop_iterate(void* loop, int, int*) {
    ++static_cast<Context*>(loop)->polls;
    return 1;
}

FAKE_EXPORT void* pa_context_new(void* api, const char*) {
    return api;
}

FAKE_EXPORT int pa_context_connect(void*, const char*, int, const void*) {
    return 0;
}

FAKE_EXPORT int pa_context_get_state(void* context) {
    std::lock_guard<std::mutex> lock(lockState);
    if (failContext) {
        return 5;
    }
    return static_cast<Context*>(context)->polls >= 1 ? 4 : 1;
}

FAKE_EXPORT void pa_context_disconnect(void*) {}

FAKE_EXPORT void pa_context_unref(void*) {}

FAKE_EXPORT void* pa_context_get_source_info_list(void* context, void (*callback)(void*, const SourceInfo*, int, void*), void* user) {
    static const SourceInfo sources[] = {
        {"alsa_input.pci-0000_00_1f.3.analog-stereo", 1, "Built-in Audio Analog Stereo"},
        {"alsa_output.pci-0000_00_1f.3.analog-stereo.monitor", 2, "Monitor of Built-in Audio Analog Stereo"},
        {"alsa_input.usb-Test_Interface.analog-stereo", 3, nullptr},
    };
    for (const SourceInfo& info : sources) {
        callback(context, &info, 0, user);
    }
    callback(context, nullptr, 1, user);
    static int operation = 0;
    return &operation;
}

FAKE_EXPORT int pa_operation_get_state(void*) {
    return 1;
}

FAKE_EXPORT void pa_operation_unref(void*) {}
