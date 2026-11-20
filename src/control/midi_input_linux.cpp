#include "control/midi_input.hpp"

#include "core/log.hpp"

#include <dlfcn.h>
#include <poll.h>

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

constexpr int kOpenInput = 2;
constexpr int kNonBlock = 1;
constexpr unsigned kCapRead = 1u << 0;
constexpr unsigned kCapWrite = 1u << 1;
constexpr unsigned kCapSubsRead = 1u << 5;
constexpr unsigned kCapSubsWrite = 1u << 6;
constexpr unsigned kTypeMidiGeneric = 1u << 1;
constexpr unsigned kTypeApplication = 1u << 20;
constexpr int kEventNoteOn = 6;
constexpr int kEventNoteOff = 7;
constexpr int kEventKeyPress = 8;
constexpr int kEventController = 10;
constexpr int kEventProgram = 11;
constexpr int kEventChannelPress = 12;
constexpr int kEventPitchBend = 13;
constexpr size_t kMaxQueue = 4096;
constexpr int kPollMilliseconds = 20;

struct SeqEvent {
    uint8_t type;
    uint8_t flags;
    uint8_t tag;
    uint8_t queue;
    uint8_t time[8];
    uint8_t sourceClient;
    uint8_t sourcePort;
    uint8_t destClient;
    uint8_t destPort;
    uint8_t data[12];
};

static_assert(sizeof(SeqEvent) == 28, "ALSA sequencer event layout");

using OpenFn = int (*)(void**, const char*, int, int);
using CloseFn = int (*)(void*);
using SetNameFn = int (*)(void*, const char*);
using CreatePortFn = int (*)(void*, const char*, unsigned, unsigned);
using DeletePortFn = int (*)(void*, int);
using ConnectFromFn = int (*)(void*, int, int, int);
using EventInputFn = int (*)(void*, SeqEvent**);
using PollCountFn = int (*)(void*, short);
using PollFillFn = int (*)(void*, pollfd*, unsigned, short);
using InfoMallocFn = int (*)(void**);
using InfoFreeFn = void (*)(void*);
using InfoSetFn = void (*)(void*, int);
using QueryFn = int (*)(void*, void*);
using InfoGetIntFn = int (*)(const void*);
using InfoGetNameFn = const char* (*)(const void*);
using InfoGetCapsFn = unsigned (*)(const void*);

struct AlsaApi {
    void* handle = nullptr;
    OpenFn open = nullptr;
    CloseFn close = nullptr;
    SetNameFn setName = nullptr;
    CreatePortFn createPort = nullptr;
    DeletePortFn deletePort = nullptr;
    ConnectFromFn connectFrom = nullptr;
    EventInputFn eventInput = nullptr;
    PollCountFn pollCount = nullptr;
    PollFillFn pollFill = nullptr;
    InfoMallocFn clientMalloc = nullptr;
    InfoFreeFn clientFree = nullptr;
    InfoSetFn clientSetClient = nullptr;
    QueryFn queryClient = nullptr;
    InfoGetIntFn clientGetClient = nullptr;
    InfoGetNameFn clientGetName = nullptr;
    InfoMallocFn portMalloc = nullptr;
    InfoFreeFn portFree = nullptr;
    InfoSetFn portSetClient = nullptr;
    InfoSetFn portSetPort = nullptr;
    QueryFn queryPort = nullptr;
    InfoGetIntFn portGetPort = nullptr;
    InfoGetNameFn portGetName = nullptr;
    InfoGetCapsFn portGetCaps = nullptr;

    AlsaApi() = default;
    AlsaApi(const AlsaApi&) = delete;
    AlsaApi& operator=(const AlsaApi&) = delete;

    ~AlsaApi() {
        if (handle) {
            dlclose(handle);
        }
    }
};

template <class T>
T symbol(void* handle, const char* name) {
    return reinterpret_cast<T>(dlsym(handle, name));
}

bool loadApi(AlsaApi& api, std::string& error) {
    const char* env = std::getenv("NYREXVJ_ALSA_LIB");
    const std::string name = env && *env ? env : "libasound.so.2";
    api.handle = dlopen(name.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!api.handle) {
        error = "ALSA is not available (" + name + " not found). Install the ALSA library to use MIDI devices.";
        return false;
    }
    api.open = symbol<OpenFn>(api.handle, "snd_seq_open");
    api.close = symbol<CloseFn>(api.handle, "snd_seq_close");
    api.setName = symbol<SetNameFn>(api.handle, "snd_seq_set_client_name");
    api.createPort = symbol<CreatePortFn>(api.handle, "snd_seq_create_simple_port");
    api.deletePort = symbol<DeletePortFn>(api.handle, "snd_seq_delete_simple_port");
    api.connectFrom = symbol<ConnectFromFn>(api.handle, "snd_seq_connect_from");
    api.eventInput = symbol<EventInputFn>(api.handle, "snd_seq_event_input");
    api.pollCount = symbol<PollCountFn>(api.handle, "snd_seq_poll_descriptors_count");
    api.pollFill = symbol<PollFillFn>(api.handle, "snd_seq_poll_descriptors");
    api.clientMalloc = symbol<InfoMallocFn>(api.handle, "snd_seq_client_info_malloc");
    api.clientFree = symbol<InfoFreeFn>(api.handle, "snd_seq_client_info_free");
    api.clientSetClient = symbol<InfoSetFn>(api.handle, "snd_seq_client_info_set_client");
    api.queryClient = symbol<QueryFn>(api.handle, "snd_seq_query_next_client");
    api.clientGetClient = symbol<InfoGetIntFn>(api.handle, "snd_seq_client_info_get_client");
    api.clientGetName = symbol<InfoGetNameFn>(api.handle, "snd_seq_client_info_get_name");
    api.portMalloc = symbol<InfoMallocFn>(api.handle, "snd_seq_port_info_malloc");
    api.portFree = symbol<InfoFreeFn>(api.handle, "snd_seq_port_info_free");
    api.portSetClient = symbol<InfoSetFn>(api.handle, "snd_seq_port_info_set_client");
    api.portSetPort = symbol<InfoSetFn>(api.handle, "snd_seq_port_info_set_port");
    api.queryPort = symbol<QueryFn>(api.handle, "snd_seq_query_next_port");
    api.portGetPort = symbol<InfoGetIntFn>(api.handle, "snd_seq_port_info_get_port");
    api.portGetName = symbol<InfoGetNameFn>(api.handle, "snd_seq_port_info_get_name");
    api.portGetCaps = symbol<InfoGetCapsFn>(api.handle, "snd_seq_port_info_get_capability");
    const bool complete = api.open && api.close && api.setName && api.createPort && api.deletePort && api.connectFrom && api.eventInput && api.pollCount && api.pollFill && api.clientMalloc && api.clientFree && api.clientSetClient && api.queryClient && api.clientGetClient && api.clientGetName && api.portMalloc && api.portFree && api.portSetClient && api.portSetPort && api.queryPort && api.portGetPort && api.portGetName && api.portGetCaps;
    if (!complete) {
        error = name + ": the sequencer functions are missing";
        return false;
    }
    return true;
}

struct Source {
    std::string label;
    int client = 0;
    int port = 0;
};

std::vector<Source> enumerateSources(const AlsaApi& api, void* seq) {
    std::vector<Source> list;
    void* client = nullptr;
    void* port = nullptr;
    if (api.clientMalloc(&client) < 0 || !client) {
        return list;
    }
    if (api.portMalloc(&port) < 0 || !port) {
        api.clientFree(client);
        return list;
    }
    api.clientSetClient(client, -1);
    while (api.queryClient(seq, client) >= 0) {
        const int clientId = api.clientGetClient(client);
        if (clientId == 0) {
            continue;
        }
        const char* clientName = api.clientGetName(client);
        api.portSetClient(port, clientId);
        api.portSetPort(port, -1);
        while (api.queryPort(seq, port) >= 0) {
            const unsigned caps = api.portGetCaps(port);
            if ((caps & (kCapRead | kCapSubsRead)) != (kCapRead | kCapSubsRead)) {
                continue;
            }
            const char* portName = api.portGetName(port);
            Source source;
            source.client = clientId;
            source.port = api.portGetPort(port);
            source.label = std::string(clientName ? clientName : "MIDI") + ":" + (portName ? portName : "port");
            list.push_back(source);
        }
    }
    api.portFree(port);
    api.clientFree(client);
    for (size_t i = 0; i < list.size(); ++i) {
        int duplicates = 0;
        for (size_t j = 0; j < i; ++j) {
            const std::string& other = list[j].label;
            const std::string& own = list[i].label;
            if (other == own || other.compare(0, own.size() + 2, own + " #") == 0) {
                ++duplicates;
            }
        }
        if (duplicates > 0) {
            list[i].label += " #" + std::to_string(duplicates + 1);
        }
    }
    return list;
}

bool translate(const SeqEvent& event, MidiMessage& out) {
    const uint8_t* d = event.data;
    const int channel = d[0] & 0x0F;
    int32_t value = 0;
    uint32_t param = 0;
    std::memcpy(&param, d + 4, sizeof(param));
    std::memcpy(&value, d + 8, sizeof(value));
    switch (event.type) {
    case kEventNoteOn:
        out.status = 0x90 | channel;
        out.data1 = d[1] & 0x7F;
        out.data2 = d[2] & 0x7F;
        return true;
    case kEventNoteOff:
        out.status = 0x80 | channel;
        out.data1 = d[1] & 0x7F;
        out.data2 = d[2] & 0x7F;
        return true;
    case kEventKeyPress:
        out.status = 0xA0 | channel;
        out.data1 = d[1] & 0x7F;
        out.data2 = d[2] & 0x7F;
        return true;
    case kEventController:
        out.status = 0xB0 | channel;
        out.data1 = static_cast<int>(param & 0x7F);
        out.data2 = std::clamp(static_cast<int>(value), 0, 127);
        return true;
    case kEventProgram:
        out.status = 0xC0 | channel;
        out.data1 = std::clamp(static_cast<int>(value), 0, 127);
        out.data2 = 0;
        return true;
    case kEventChannelPress:
        out.status = 0xD0 | channel;
        out.data1 = std::clamp(static_cast<int>(value), 0, 127);
        out.data2 = 0;
        return true;
    case kEventPitchBend: {
        const int bend = std::clamp(static_cast<int>(value) + 8192, 0, 16383);
        out.status = 0xE0 | channel;
        out.data1 = bend & 0x7F;
        out.data2 = (bend >> 7) & 0x7F;
        return true;
    }
    default:
        return false;
    }
}

class AlsaMidiInput final : public MidiInput {
public:
    ~AlsaMidiInput() override { stop(); }

    std::vector<std::string> listDevices() override {
        std::vector<std::string> names;
        std::string error;
        if (!ensureLoaded(error)) {
            return names;
        }
        void* seq = nullptr;
        if (api_.open(&seq, "default", kOpenInput, kNonBlock) < 0 || !seq) {
            return names;
        }
        api_.setName(seq, "NyrexVJ scan");
        for (const Source& source : enumerateSources(api_, seq)) {
            names.push_back(source.label);
        }
        api_.close(seq);
        return names;
    }

    bool start(const std::string& name, std::string& error) override {
        stop();
        if (!ensureLoaded(error)) {
            return false;
        }
        if (api_.open(&seq_, "default", kOpenInput, kNonBlock) < 0 || !seq_) {
            seq_ = nullptr;
            error = "Could not open the ALSA sequencer";
            return false;
        }
        api_.setName(seq_, "NyrexVJ");
        const std::vector<Source> sources = enumerateSources(api_, seq_);
        const Source* chosen = nullptr;
        for (const Source& source : sources) {
            if (name.empty() || source.label == name) {
                chosen = &source;
                break;
            }
        }
        if (!chosen) {
            error = "MIDI device not found: " + name;
            closeSequencer();
            return false;
        }
        port_ = api_.createPort(seq_, "Input", kCapWrite | kCapSubsWrite, kTypeMidiGeneric | kTypeApplication);
        if (port_ < 0) {
            error = "Could not create a MIDI input port";
            closeSequencer();
            return false;
        }
        if (api_.connectFrom(seq_, port_, chosen->client, chosen->port) < 0) {
            error = "Could not connect to " + chosen->label;
            closeSequencer();
            return false;
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            queue_.clear();
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
        running_.store(false);
        closeSequencer();
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.clear();
    }

    bool running() const override { return running_.load(); }

    void drain(std::vector<MidiMessage>& messages) override {
        std::lock_guard<std::mutex> lock(mutex_);
        messages.insert(messages.end(), queue_.begin(), queue_.end());
        queue_.clear();
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

    void closeSequencer() {
        if (seq_) {
            if (port_ >= 0) {
                api_.deletePort(seq_, port_);
            }
            api_.close(seq_);
            seq_ = nullptr;
        }
        port_ = -1;
    }

    void run() {
        const int count = api_.pollCount(seq_, POLLIN);
        std::vector<pollfd> descriptors(static_cast<size_t>(std::max(count, 1)));
        const int filled = count > 0 ? api_.pollFill(seq_, descriptors.data(), static_cast<unsigned>(descriptors.size()), POLLIN) : 0;
        while (!stopFlag_.load()) {
            if (filled > 0) {
                poll(descriptors.data(), static_cast<nfds_t>(filled), kPollMilliseconds);
            }
            for (;;) {
                SeqEvent* event = nullptr;
                const int result = api_.eventInput(seq_, &event);
                if (result < 0 || !event) {
                    break;
                }
                MidiMessage message;
                if (!translate(*event, message)) {
                    continue;
                }
                std::lock_guard<std::mutex> lock(mutex_);
                if (queue_.size() < kMaxQueue) {
                    queue_.push_back(message);
                }
            }
            if (filled <= 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(kPollMilliseconds));
            }
        }
    }

    AlsaApi api_;
    bool loaded_ = false;
    void* seq_ = nullptr;
    int port_ = -1;
    std::thread thread_;
    std::atomic<bool> running_{false};
    std::atomic<bool> stopFlag_{false};
    std::mutex mutex_;
    std::vector<MidiMessage> queue_;
};

}

std::unique_ptr<MidiInput> createMidiInput() {
    return std::make_unique<AlsaMidiInput>();
}

}
