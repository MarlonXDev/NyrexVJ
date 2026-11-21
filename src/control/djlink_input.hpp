#pragma once

#include "control/udp_socket.hpp"
#include "core/djlink.hpp"

#include <string>

namespace nvj {

class DjLinkInput {
public:
    bool start(int port, std::string& error);
    void stop();
    bool running() const { return socket_.isOpen(); }
    int port() const { return port_; }
    void poll(DjLinkTracker& tracker);
    unsigned long long datagrams() const { return datagrams_; }

private:
    UdpSocket socket_;
    int port_ = kDjLinkBeatPort;
    unsigned long long datagrams_ = 0;
};

}
