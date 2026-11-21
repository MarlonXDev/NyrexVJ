#include "control/udp_socket.hpp"

#include <winsock2.h>
#include <ws2tcpip.h>

namespace nvj {

namespace {

int socketUsers = 0;

bool acquireWinsock() {
    if (socketUsers == 0) {
        WSADATA data{};
        if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
            return false;
        }
    }
    ++socketUsers;
    return true;
}

void releaseWinsock() {
    if (socketUsers > 0 && --socketUsers == 0) {
        WSACleanup();
    }
}

}

struct UdpSocket::Impl {
    SOCKET handle = INVALID_SOCKET;
    sockaddr_in target{};
    bool hasTarget = false;
};

UdpSocket::UdpSocket() : impl_(std::make_unique<Impl>()) {}

UdpSocket::~UdpSocket() {
    close();
}

bool UdpSocket::open(int port, bool allowRemote, std::string& error) {
    close();
    if (port < 1 || port > 65535) {
        error = "The OSC port must be between 1 and 65535";
        return false;
    }
    if (!acquireWinsock()) {
        error = "Could not start the network layer";
        return false;
    }
    SOCKET handle = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (handle == INVALID_SOCKET) {
        releaseWinsock();
        error = "Could not create the OSC socket";
        return false;
    }
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(static_cast<u_short>(port));
    address.sin_addr.s_addr = htonl(allowRemote ? INADDR_ANY : INADDR_LOOPBACK);
    if (::bind(handle, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
        ::closesocket(handle);
        releaseWinsock();
        error = "Could not open the OSC port, it may be in use";
        return false;
    }
    u_long nonBlocking = 1;
    ::ioctlsocket(handle, FIONBIO, &nonBlocking);
    impl_->handle = handle;
    return true;
}

bool UdpSocket::openShared(int port, std::string& error) {
    close();
    if (port < 1 || port > 65535) {
        error = "The network port must be between 1 and 65535";
        return false;
    }
    if (!acquireWinsock()) {
        error = "Could not start the network layer";
        return false;
    }
    SOCKET handle = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (handle == INVALID_SOCKET) {
        releaseWinsock();
        error = "Could not create the network socket";
        return false;
    }
    const BOOL enable = TRUE;
    ::setsockopt(handle, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&enable), sizeof(enable));
    ::setsockopt(handle, SOL_SOCKET, SO_BROADCAST, reinterpret_cast<const char*>(&enable), sizeof(enable));
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(static_cast<u_short>(port));
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    if (::bind(handle, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
        ::closesocket(handle);
        releaseWinsock();
        error = "Could not open the network port, it may be in use";
        return false;
    }
    u_long nonBlocking = 1;
    ::ioctlsocket(handle, FIONBIO, &nonBlocking);
    impl_->handle = handle;
    return true;
}

void UdpSocket::close() {
    if (impl_->handle != INVALID_SOCKET) {
        ::closesocket(impl_->handle);
        impl_->handle = INVALID_SOCKET;
        releaseWinsock();
    }
    impl_->hasTarget = false;
}

bool UdpSocket::connectTo(const std::string& host, int port, std::string& error) {
    close();
    if (port < 1 || port > 65535) {
        error = "The OSC output port must be between 1 and 65535";
        return false;
    }
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(static_cast<u_short>(port));
    const std::string literal = host == "localhost" ? std::string("127.0.0.1") : host;
    if (::inet_pton(AF_INET, literal.c_str(), &address.sin_addr) != 1) {
        error = "The OSC output address must be an IPv4 address or localhost";
        return false;
    }
    if (!acquireWinsock()) {
        error = "Could not start the network layer";
        return false;
    }
    SOCKET handle = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (handle == INVALID_SOCKET) {
        releaseWinsock();
        error = "Could not create the OSC output socket";
        return false;
    }
    u_long nonBlocking = 1;
    ::ioctlsocket(handle, FIONBIO, &nonBlocking);
    const BOOL broadcast = TRUE;
    ::setsockopt(handle, SOL_SOCKET, SO_BROADCAST, reinterpret_cast<const char*>(&broadcast), sizeof(broadcast));
    impl_->handle = handle;
    impl_->target = address;
    impl_->hasTarget = true;
    return true;
}

bool UdpSocket::send(const unsigned char* data, int size) {
    if (impl_->handle == INVALID_SOCKET || !impl_->hasTarget || size <= 0) {
        return false;
    }
    return ::sendto(impl_->handle, reinterpret_cast<const char*>(data), size, 0, reinterpret_cast<const sockaddr*>(&impl_->target), sizeof(impl_->target)) == size;
}

bool UdpSocket::isOpen() const {
    return impl_->handle != INVALID_SOCKET;
}

bool UdpSocket::sendLoopback(int port, const unsigned char* data, int size) {
    if (!acquireWinsock()) {
        return false;
    }
    SOCKET handle = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    bool ok = false;
    if (handle != INVALID_SOCKET) {
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = htons(static_cast<u_short>(port));
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        ok = ::sendto(handle, reinterpret_cast<const char*>(data), size, 0, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == size;
        ::closesocket(handle);
    }
    releaseWinsock();
    return ok;
}

int UdpSocket::receive(unsigned char* buffer, int capacity) {
    if (impl_->handle == INVALID_SOCKET) {
        return -1;
    }
    const int count = ::recvfrom(impl_->handle, reinterpret_cast<char*>(buffer), capacity, 0, nullptr, nullptr);
    return count < 0 ? 0 : count;
}

}
