#include "control/djlink_input.hpp"

namespace nvj {

namespace {

constexpr int kMaxPerPoll = 256;

}

bool DjLinkInput::start(int port, std::string& error) {
    stop();
    if (!socket_.openShared(port, error)) {
        return false;
    }
    port_ = port;
    return true;
}

void DjLinkInput::stop() {
    socket_.close();
}

void DjLinkInput::poll(DjLinkTracker& tracker) {
    if (!socket_.isOpen()) {
        return;
    }
    unsigned char buffer[1024];
    for (int i = 0; i < kMaxPerPoll; ++i) {
        const int count = socket_.receive(buffer, static_cast<int>(sizeof(buffer)));
        if (count <= 0) {
            break;
        }
        ++datagrams_;
        DjBeat beat;
        if (parseDjBeat(buffer, static_cast<size_t>(count), beat)) {
            tracker.onBeat(beat);
        } else if (isDjLinkPacket(buffer, static_cast<size_t>(count)) && count >= 11 && buffer[10] != 0x28) {
            tracker.onOther();
        } else {
            tracker.onMalformed();
        }
    }
}

}
