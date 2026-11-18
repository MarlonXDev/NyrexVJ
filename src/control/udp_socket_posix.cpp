#include "control/udp_socket.hpp"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace nvj {

struct UdpSocket::Impl {
    int handle = -1;
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
