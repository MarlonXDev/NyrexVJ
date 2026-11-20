#include <fcntl.h>
#include <poll.h>
#include <unistd.h>

#include <cstdint>
#include <cstring>
#include <deque>
#include <mutex>
#include <string>

#define FAKE_EXPORT extern "C" __attribute__((visibility("default")))

namespace {

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

struct FakePort {
    int client;
    int port;
    const char* name;
    unsigned caps;
};

struct FakeClient {
    int id;
    const char* name;
};

const FakeClient kClients[] = {{0, "System"}, {14, "Midi Through"}, {20, "Test Controller"}, {21, "Test Controller"}};
const FakePort kPorts[] = {
    {0, 0, "Timer", 0x21},
    {14, 0, "Midi Through Port-0", 0x63},
    {20, 0, "Test Controller MIDI 1", 0x21},
    {20, 1, "Test Controller DIN", 0x42},
    {21, 0, "Test Controller MIDI 1", 0x21},
};

struct ClientInfo {
    int client = -1;
};

struct PortInfo {
    int client = 0;
    int port = -1;
};

struct Seq {
    int pipeFds[2] = {-1, -1};
    SeqEvent current{};
    int port = -1;
};

std::mutex lockState;
std::deque<SeqEvent> pending;
int opened = 0;
int closed = 0;
int lastClient = -1;
int lastPort = -1;
std::string lastName;
bool failOpen = false;
int portsCreated = 0;
int portsDeleted = 0;
Seq* active = nullptr;

void wake(Seq* seq) {
    if (seq && seq->pipeFds[1] >= 0) {
        const char byte = 1;
        const ssize_t written = write(seq->pipeFds[1], &byte, 1);
        (void)written;
    }
}

void drainPipe(Seq* seq) {
    char buffer[64];
    while (read(seq->pipeFds[0], buffer, sizeof(buffer)) > 0) {
    }
}

}

FAKE_EXPORT void fake_alsa_reset() {
    std::lock_guard<std::mutex> lock(lockState);
    pending.clear();
    opened = 0;
    closed = 0;
    lastClient = -1;
    lastPort = -1;
    lastName.clear();
    failOpen = false;
    portsCreated = 0;
    portsDeleted = 0;
}

FAKE_EXPORT void fake_alsa_fail_open(int value) {
    std::lock_guard<std::mutex> lock(lockState);
    failOpen = value != 0;
}

FAKE_EXPORT void fake_alsa_push(int type, int channel, int a, int b, int value) {
    SeqEvent event{};
    event.type = static_cast<uint8_t>(type);
    event.data[0] = static_cast<uint8_t>(channel);
    event.data[1] = static_cast<uint8_t>(a);
    event.data[2] = static_cast<uint8_t>(b);
    const uint32_t param = static_cast<uint32_t>(a);
    std::memcpy(event.data + 4, &param, sizeof(param));
    const int32_t v = value;
    std::memcpy(event.data + 8, &v, sizeof(v));
    std::lock_guard<std::mutex> lock(lockState);
    pending.push_back(event);
    wake(active);
}

FAKE_EXPORT int fake_alsa_opened() {
    std::lock_guard<std::mutex> lock(lockState);
    return opened;
}

FAKE_EXPORT int fake_alsa_closed() {
    std::lock_guard<std::mutex> lock(lockState);
    return closed;
}

FAKE_EXPORT int fake_alsa_connected_client() {
    std::lock_guard<std::mutex> lock(lockState);
    return lastClient;
}

FAKE_EXPORT int fake_alsa_connected_port() {
    std::lock_guard<std::mutex> lock(lockState);
    return lastPort;
}

FAKE_EXPORT int fake_alsa_ports_created() {
    std::lock_guard<std::mutex> lock(lockState);
    return portsCreated;
}

FAKE_EXPORT int fake_alsa_ports_deleted() {
    std::lock_guard<std::mutex> lock(lockState);
    return portsDeleted;
}

FAKE_EXPORT const char* fake_alsa_client_name() {
    std::lock_guard<std::mutex> lock(lockState);
    return lastName.c_str();
}

FAKE_EXPORT int snd_seq_open(void** handle, const char*, int, int) {
    std::lock_guard<std::mutex> lock(lockState);
    if (failOpen) {
        return -2;
    }
    Seq* seq = new Seq();
    if (pipe(seq->pipeFds) == 0) {
        for (int fd : seq->pipeFds) {
            fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
        }
    }
    *handle = seq;
    ++opened;
    return 0;
}

FAKE_EXPORT int snd_seq_close(void* handle) {
    Seq* seq = static_cast<Seq*>(handle);
    std::lock_guard<std::mutex> lock(lockState);
    if (active == seq) {
        active = nullptr;
    }
    for (int fd : seq->pipeFds) {
        if (fd >= 0) {
            close(fd);
        }
    }
    delete seq;
    ++closed;
    return 0;
}

FAKE_EXPORT int snd_seq_set_client_name(void*, const char* name) {
    std::lock_guard<std::mutex> lock(lockState);
    lastName = name ? name : "";
    return 0;
}

FAKE_EXPORT int snd_seq_create_simple_port(void* handle, const char*, unsigned caps, unsigned) {
    Seq* seq = static_cast<Seq*>(handle);
    std::lock_guard<std::mutex> lock(lockState);
    if (caps != 0x42) {
        return -22;
    }
    seq->port = 3;
    ++portsCreated;
    return seq->port;
}

FAKE_EXPORT int snd_seq_delete_simple_port(void*, int) {
    std::lock_guard<std::mutex> lock(lockState);
    ++portsDeleted;
    return 0;
}

FAKE_EXPORT int snd_seq_connect_from(void* handle, int, int client, int port) {
    Seq* seq = static_cast<Seq*>(handle);
    std::lock_guard<std::mutex> lock(lockState);
    lastClient = client;
    lastPort = port;
    active = seq;
    return 0;
}

FAKE_EXPORT int snd_seq_event_input(void* handle, SeqEvent** event) {
    Seq* seq = static_cast<Seq*>(handle);
    std::lock_guard<std::mutex> lock(lockState);
    if (pending.empty()) {
        drainPipe(seq);
        *event = nullptr;
        return -11;
    }
    seq->current = pending.front();
    pending.pop_front();
    *event = &seq->current;
    return 1;
}

FAKE_EXPORT int snd_seq_poll_descriptors_count(void*, short) {
    return 1;
}

FAKE_EXPORT int snd_seq_poll_descriptors(void* handle, pollfd* descriptors, unsigned space, short events) {
    Seq* seq = static_cast<Seq*>(handle);
    if (space < 1) {
        return 0;
    }
    descriptors[0].fd = seq->pipeFds[0];
    descriptors[0].events = events;
    descriptors[0].revents = 0;
    return 1;
}

FAKE_EXPORT int snd_seq_client_info_malloc(void** info) {
    *info = new ClientInfo();
    return 0;
}

FAKE_EXPORT void snd_seq_client_info_free(void* info) {
    delete static_cast<ClientInfo*>(info);
}

FAKE_EXPORT void snd_seq_client_info_set_client(void* info, int client) {
    static_cast<ClientInfo*>(info)->client = client;
}

FAKE_EXPORT int snd_seq_query_next_client(void*, void* info) {
    ClientInfo* client = static_cast<ClientInfo*>(info);
    for (const FakeClient& candidate : kClients) {
        if (candidate.id > client->client) {
            client->client = candidate.id;
            return 0;
        }
    }
    return -2;
}

FAKE_EXPORT int snd_seq_client_info_get_client(const void* info) {
    return static_cast<const ClientInfo*>(info)->client;
}

FAKE_EXPORT const char* snd_seq_client_info_get_name(const void* info) {
    const int id = static_cast<const ClientInfo*>(info)->client;
    for (const FakeClient& candidate : kClients) {
        if (candidate.id == id) {
            return candidate.name;
        }
    }
    return nullptr;
}

FAKE_EXPORT int snd_seq_port_info_malloc(void** info) {
    *info = new PortInfo();
    return 0;
}

FAKE_EXPORT void snd_seq_port_info_free(void* info) {
    delete static_cast<PortInfo*>(info);
}

FAKE_EXPORT void snd_seq_port_info_set_client(void* info, int client) {
    static_cast<PortInfo*>(info)->client = client;
}

FAKE_EXPORT void snd_seq_port_info_set_port(void* info, int port) {
    static_cast<PortInfo*>(info)->port = port;
}

FAKE_EXPORT int snd_seq_query_next_port(void*, void* info) {
    PortInfo* port = static_cast<PortInfo*>(info);
    for (const FakePort& candidate : kPorts) {
        if (candidate.client == port->client && candidate.port > port->port) {
            port->port = candidate.port;
            return 0;
        }
    }
    return -2;
}

FAKE_EXPORT int snd_seq_port_info_get_port(const void* info) {
    return static_cast<const PortInfo*>(info)->port;
}

FAKE_EXPORT const char* snd_seq_port_info_get_name(const void* info) {
    const PortInfo* port = static_cast<const PortInfo*>(info);
    for (const FakePort& candidate : kPorts) {
        if (candidate.client == port->client && candidate.port == port->port) {
            return candidate.name;
        }
    }
    return nullptr;
}

FAKE_EXPORT unsigned snd_seq_port_info_get_capability(const void* info) {
    const PortInfo* port = static_cast<const PortInfo*>(info);
    for (const FakePort& candidate : kPorts) {
        if (candidate.client == port->client && candidate.port == port->port) {
            return candidate.caps;
        }
    }
    return 0;
}
