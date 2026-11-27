#include "audio/audio_input.hpp"

#include "core/log.hpp"

#include <dlfcn.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace nvj {

namespace {

constexpr int kSampleRate = 48000;
constexpr size_t kChunkFrames = 480;
constexpr size_t kMaxBuffered = static_cast<size_t>(kSampleRate) * 2;
constexpr int kSampleFloat32 = 5;
constexpr int kStreamRecord = 2;
constexpr int kContextReady = 4;
constexpr int kContextFailed = 5;
constexpr int kContextTerminated = 6;
constexpr int kOperationRunning = 0;
constexpr int kListTimeoutMs = 2500;
constexpr const char* kDefaultSource = "@DEFAULT_SOURCE@";
constexpr const char* kDefaultMonitor = "@DEFAULT_MONITOR@";

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

using SimpleNewFn = void* (*)(const char*, const char*, int, const char*, const char*, const SampleSpec*, const void*, const BufferAttr*, int*);
using SimpleReadFn = int (*)(void*, void*, size_t, int*);
using SimpleFreeFn = void (*)(void*);
using StrErrorFn = const char* (*)(int);
using MainloopNewFn = void* (*)();
using MainloopFreeFn = void (*)(void*);
using MainloopApiFn = void* (*)(void*);
using MainloopIterateFn = int (*)(void*, int, int*);
using ContextNewFn = void* (*)(void*, const char*);
using ContextConnectFn = int (*)(void*, const char*, int, const void*);
using ContextStateFn = int (*)(void*);
using ContextDisconnectFn = void (*)(void*);
using ContextUnrefFn = void (*)(void*);
using SourceListCallback = void (*)(void*, const SourceInfo*, int, void*);
using SourceListFn = void* (*)(void*, SourceListCallback, void*);
using OperationStateFn = int (*)(void*);
using OperationUnrefFn = void (*)(void*);

struct PulseApi {
    void* simple = nullptr;
    void* core = nullptr;
    SimpleNewFn simpleNew = nullptr;
    SimpleReadFn simpleRead = nullptr;
    SimpleFreeFn simpleFree = nullptr;
    StrErrorFn strError = nullptr;
    MainloopNewFn mainloopNew = nullptr;
    MainloopFreeFn mainloopFree = nullptr;
    MainloopApiFn mainloopApi = nullptr;
    MainloopIterateFn mainloopIterate = nullptr;
    ContextNewFn contextNew = nullptr;
    ContextConnectFn contextConnect = nullptr;
    ContextStateFn contextState = nullptr;
    ContextDisconnectFn contextDisconnect = nullptr;
    ContextUnrefFn contextUnref = nullptr;
    SourceListFn sourceList = nullptr;
    OperationStateFn operationState = nullptr;
    OperationUnrefFn operationUnref = nullptr;

    PulseApi() = default;
    PulseApi(const PulseApi&) = delete;
    PulseApi& operator=(const PulseApi&) = delete;

    ~PulseApi() {
        if (core) {
            dlclose(core);
        }
        if (simple) {
            dlclose(simple);
        }
    }

    bool canList() const {
        return mainloopNew && mainloopFree && mainloopApi && mainloopIterate && contextNew && contextConnect && contextState && contextDisconnect && contextUnref && sourceList && operationState && operationUnref;
    }
};

template <class T>
T symbol(void* handle, const char* name) {
    return reinterpret_cast<T>(dlsym(handle, name));
}

bool loadApi(PulseApi& api, std::string& error) {
    const char* env = std::getenv("NYREXVJ_PULSE_LIB");
    const std::string override = env ? env : "";
    const std::string simpleName = override.empty() ? "libpulse-simple.so.0" : override;
    const std::string coreName = override.empty() ? "libpulse.so.0" : override;
    api.simple = dlopen(simpleName.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!api.simple) {
        error = "PulseAudio is not available (" + simpleName + " not found). Install PulseAudio or PipeWire with its PulseAudio server.";
        return false;
    }
    api.simpleNew = symbol<SimpleNewFn>(api.simple, "pa_simple_new");
    api.simpleRead = symbol<SimpleReadFn>(api.simple, "pa_simple_read");
    api.simpleFree = symbol<SimpleFreeFn>(api.simple, "pa_simple_free");
    api.strError = symbol<StrErrorFn>(api.simple, "pa_strerror");
    if (!api.simpleNew || !api.simpleRead || !api.simpleFree) {
        error = simpleName + ": the capture functions are missing";
        return false;
    }
    api.core = dlopen(coreName.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (api.core) {
        api.mainloopNew = symbol<MainloopNewFn>(api.core, "pa_mainloop_new");
        api.mainloopFree = symbol<MainloopFreeFn>(api.core, "pa_mainloop_free");
        api.mainloopApi = symbol<MainloopApiFn>(api.core, "pa_mainloop_get_api");
        api.mainloopIterate = symbol<MainloopIterateFn>(api.core, "pa_mainloop_iterate");
        api.contextNew = symbol<ContextNewFn>(api.core, "pa_context_new");
        api.contextConnect = symbol<ContextConnectFn>(api.core, "pa_context_connect");
        api.contextState = symbol<ContextStateFn>(api.core, "pa_context_get_state");
        api.contextDisconnect = symbol<ContextDisconnectFn>(api.core, "pa_context_disconnect");
        api.contextUnref = symbol<ContextUnrefFn>(api.core, "pa_context_unref");
        api.sourceList = symbol<SourceListFn>(api.core, "pa_context_get_source_info_list");
        api.operationState = symbol<OperationStateFn>(api.core, "pa_operation_get_state");
        api.operationUnref = symbol<OperationUnrefFn>(api.core, "pa_operation_unref");
    }
    return true;
}

bool endsWith(const std::string& text, const std::string& suffix) {
    return text.size() >= suffix.size() && text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
}

void collectSource(void*, const SourceInfo* info, int eol, void* user) {
    if (eol != 0 || !info || !info->name) {
        return;
    }
    std::vector<AudioDevice>* list = static_cast<std::vector<AudioDevice>*>(user);
    AudioDevice device;
    device.id = info->name;
    device.name = info->description && *info->description ? info->description : info->name;
    device.loopback = endsWith(device.id, ".monitor");
    list->push_back(device);
}

std::vector<AudioDevice> enumerateSources(const PulseApi& api) {
    std::vector<AudioDevice> list;
    if (!api.canList()) {
        return list;
    }
    void* loop = api.mainloopNew();
    if (!loop) {
        return list;
    }
    void* context = api.contextNew(api.mainloopApi(loop), "NyrexVJ");
    if (!context) {
        api.mainloopFree(loop);
        return list;
    }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(kListTimeoutMs);
    auto expired = [&]() { return std::chrono::steady_clock::now() > deadline; };
    auto pump = [&]() {
        int ret = 0;
        if (api.mainloopIterate(loop, 0, &ret) < 0) {
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        return true;
    };
    bool ready = false;
    if (api.contextConnect(context, nullptr, 0, nullptr) >= 0) {
        while (!expired()) {
            const int state = api.contextState(context);
            if (state == kContextReady) {
                ready = true;
                break;
            }
            if (state == kContextFailed || state == kContextTerminated) {
                break;
            }
            if (!pump()) {
                break;
            }
        }
    }
    if (ready) {
        void* operation = api.sourceList(context, collectSource, &list);
        if (operation) {
            while (!expired() && api.operationState(operation) == kOperationRunning) {
                if (!pump()) {
                    break;
                }
            }
            api.operationUnref(operation);
        }
    }
    api.contextDisconnect(context);
    api.contextUnref(context);
    api.mainloopFree(loop);
    return list;
}

class PulseInput final : public AudioInput {
public:
    ~PulseInput() override { stop(); }

    std::vector<AudioDevice> listDevices() override {
        std::string error;
        if (!ensureLoaded(error)) {
            return {};
        }
        std::vector<AudioDevice> list;
        list.push_back({kDefaultSource, "Default input", false});
        list.push_back({kDefaultMonitor, "System audio (default output)", true});
        for (const AudioDevice& device : enumerateSources(api_)) {
            if (device.id == kDefaultSource || device.id == kDefaultMonitor) {
                continue;
            }
            list.push_back(device);
        }
        return list;
    }

    bool start(const std::string& id, std::string& error) override {
        stop();
        if (!ensureLoaded(error)) {
            return false;
        }
        activeChannel_ = channel_.load();
        captureChannels_ = activeChannel_ == AudioChannel::Mix ? 1 : 2;
        const SampleSpec spec{kSampleFloat32, static_cast<uint32_t>(kSampleRate), static_cast<uint8_t>(captureChannels_)};
        const BufferAttr attr{UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX, static_cast<uint32_t>(kChunkFrames * sizeof(float) * static_cast<size_t>(captureChannels_))};
        const char* device = (id.empty() || id == kDefaultSource) ? nullptr : id.c_str();
        int code = 0;
        stream_ = api_.simpleNew(nullptr, "NyrexVJ", kStreamRecord, device, "Audio analysis", &spec, nullptr, &attr, &code);
        if (!stream_) {
            const char* text = api_.strError ? api_.strError(code) : nullptr;
            error = std::string("Could not open the audio input: ") + (text ? text : "unknown error");
            return false;
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            samples_.clear();
        }
        stopFlag_.store(false);
        running_.store(true);
        thread_ = std::thread([this]() { run(); });
        return true;
    }

    void stop() override {
        stopFlag_.store(true);
        if (thread_.joinable()) {
            thread_.join();
        }
        if (stream_) {
            api_.simpleFree(stream_);
            stream_ = nullptr;
        }
        running_.store(false);
        std::lock_guard<std::mutex> lock(mutex_);
        samples_.clear();
    }

    bool running() const override { return running_.load(); }

    int sampleRate() const override { return kSampleRate; }

    bool supportsChannel() const override { return true; }

    void setChannel(AudioChannel channel) override { channel_.store(channel); }

    void drain(std::vector<float>& mono) override {
        mono.clear();
        std::lock_guard<std::mutex> lock(mutex_);
        mono.swap(samples_);
    }

private:
    bool ensureLoaded(std::string& error) {
        if (loaded_) {
            return true;
        }
        if (!loadApi(api_, error)) {
            return false;
        }
        loaded_ = true;
        return true;
    }

    void run() {
        const int channels = captureChannels_;
        std::vector<float> chunk(kChunkFrames * static_cast<size_t>(channels));
        std::vector<float> mono;
        while (!stopFlag_.load()) {
            int code = 0;
            if (api_.simpleRead(stream_, chunk.data(), chunk.size() * sizeof(float), &code) < 0) {
                const char* text = api_.strError ? api_.strError(code) : nullptr;
                logLine(std::string("Audio input stopped: ") + (text ? text : "read failed"));
                running_.store(false);
                return;
            }
            const std::vector<float>* block = &chunk;
            if (channels > 1) {
                mono.clear();
                appendChannel(chunk.data(), kChunkFrames, channels, activeChannel_, mono);
                block = &mono;
            }
            std::lock_guard<std::mutex> lock(mutex_);
            if (samples_.size() + block->size() > kMaxBuffered) {
                samples_.clear();
            }
            samples_.insert(samples_.end(), block->begin(), block->end());
        }
    }

    PulseApi api_;
    bool loaded_ = false;
    void* stream_ = nullptr;
    std::thread thread_;
    std::atomic<bool> running_{false};
    std::atomic<bool> stopFlag_{false};
    std::atomic<AudioChannel> channel_{AudioChannel::Mix};
    AudioChannel activeChannel_ = AudioChannel::Mix;
    int captureChannels_ = 1;
    std::mutex mutex_;
    std::vector<float> samples_;
};

}

std::unique_ptr<AudioInput> createAudioInput() {
    return std::make_unique<PulseInput>();
}

}
