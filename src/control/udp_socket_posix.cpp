#include "control/udp_socket.hpp"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace nvj {

struct UdpSocket::Impl {
    int handle = -1;
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
    const int handle = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (handle < 0) {
        error = "Could not create the OSC socket";
        return false;
    }
    const int enable = 1;
    ::setsockopt(handle, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable));
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(static_cast<unsigned short>(port));
    address.sin_addr.s_addr = htonl(allowRemote ? INADDR_ANY : INADDR_LOOPBACK);
    if (::bind(handle, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
        ::close(handle);
        error = "Could not open the OSC port, it may be in use";
        return false;
    }
    const int flags = ::fcntl(handle, F_GETFL, 0);
    ::fcntl(handle, F_SETFL, flags | O_NONBLOCK);
    impl_->handle = handle;
    return true;
}

void UdpSocket::close() {
    if (impl_->handle >= 0) {
        ::close(impl_->handle);
        impl_->handle = -1;
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
    address.sin_port = htons(static_cast<unsigned short>(port));
    const std::string literal = host == "localhost" ? std::string("127.0.0.1") : host;
    if (::inet_pton(AF_INET, literal.c_str(), &address.sin_addr) != 1) {
        error = "The OSC output address must be an IPv4 address or localhost";
        return false;
    }
    const int handle = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (handle < 0) {
        error = "Could not create the OSC output socket";
        return false;
    }
    const int flags = ::fcntl(handle, F_GETFL, 0);
    ::fcntl(handle, F_SETFL, flags | O_NONBLOCK);
    const int broadcast = 1;
    ::setsockopt(handle, SOL_SOCKET, SO_BROADCAST, &broadcast, sizeof(broadcast));
    impl_->handle = handle;
    impl_->target = address;
    impl_->hasTarget = true;
    return true;
}

bool UdpSocket::send(const unsigned char* data, int size) {
    if (impl_->handle < 0 || !impl_->hasTarget || size <= 0) {
        return false;
    }
    const ssize_t sent = ::sendto(impl_->handle, data, static_cast<size_t>(size), 0, reinterpret_cast<const sockaddr*>(&impl_->target), sizeof(impl_->target));
    return sent == size;
}

bool UdpSocket::isOpen() const {
    return impl_->handle >= 0;
}

bool UdpSocket::sendLoopback(int port, const unsigned char* data, int size) {
    const int handle = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (handle < 0) {
        return false;
    }
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(static_cast<unsigned short>(port));
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    const ssize_t sent = ::sendto(handle, data, static_cast<size_t>(size), 0, reinterpret_cast<sockaddr*>(&address), sizeof(address));
    ::close(handle);
    return sent == size;
}

int UdpSocket::receive(unsigned char* buffer, int capacity) {
    if (impl_->handle < 0) {
        return -1;
    }
    const ssize_t count = ::recvfrom(impl_->handle, buffer, static_cast<size_t>(capacity), 0, nullptr, nullptr);
    return count < 0 ? 0 : static_cast<int>(count);
}

}
