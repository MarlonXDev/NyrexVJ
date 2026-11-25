#include "control/tcp_socket.hpp"

#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

namespace nvj {

struct TcpConnection::Impl {
    int handle = -1;
};

struct TcpListener::Impl {
    int handle = -1;
};

namespace {

void makeNonBlocking(int handle) {
    const int flags = ::fcntl(handle, F_GETFL, 0);
    ::fcntl(handle, F_SETFL, flags | O_NONBLOCK);
    ::fcntl(handle, F_SETFD, FD_CLOEXEC);
}

void tuneConnection(int handle) {
    const int enable = 1;
    ::setsockopt(handle, IPPROTO_TCP, TCP_NODELAY, &enable, sizeof(enable));
#ifdef SO_NOSIGPIPE
    ::setsockopt(handle, SOL_SOCKET, SO_NOSIGPIPE, &enable, sizeof(enable));
#endif
}

}

std::string TcpPeer::text() const {
    return std::to_string((address >> 24) & 0xff) + "." + std::to_string((address >> 16) & 0xff) + "." + std::to_string((address >> 8) & 0xff) + "." + std::to_string(address & 0xff);
}

TcpConnection::TcpConnection() : impl(std::make_unique<Impl>()) {}

TcpConnection::~TcpConnection() {
    close();
}

bool TcpConnection::isOpen() const {
    return impl->handle >= 0;
}

int TcpConnection::receive(void* buffer, int capacity) {
    if (impl->handle < 0 || capacity <= 0) {
        return -1;
    }
    const ssize_t got = ::recv(impl->handle, buffer, static_cast<size_t>(capacity), 0);
    if (got > 0) {
        return static_cast<int>(got);
    }
    if (got == 0) {
        return -1;
    }
    if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
        return 0;
    }
    return -1;
}

int TcpConnection::send(const void* data, int size) {
    if (impl->handle < 0 || size <= 0) {
        return -1;
    }
    int flags = 0;
#ifdef MSG_NOSIGNAL
    flags |= MSG_NOSIGNAL;
#endif
    const ssize_t sent = ::send(impl->handle, data, static_cast<size_t>(size), flags);
    if (sent >= 0) {
        return static_cast<int>(sent);
    }
    if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
        return 0;
    }
    return -1;
}

void TcpConnection::shutdownWrite() {
    if (impl->handle >= 0) {
        ::shutdown(impl->handle, SHUT_WR);
    }
}

void TcpConnection::close() {
    if (impl->handle >= 0) {
        ::close(impl->handle);
        impl->handle = -1;
    }
}

TcpListener::TcpListener() : impl(std::make_unique<Impl>()) {}

TcpListener::~TcpListener() {
    close();
}

bool TcpListener::isOpen() const {
    return impl->handle >= 0;
}

bool TcpListener::open(int port, bool allowRemote, std::string& error) {
    close();
    if (port < 0 || port > 65535) {
        error = "The port must be between 1 and 65535";
        return false;
    }
    const int handle = ::socket(AF_INET, SOCK_STREAM, 0);
    if (handle < 0) {
        error = "Could not create the network socket";
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
        error = "Could not open the port, it may be in use";
        return false;
    }
    if (::listen(handle, 32) != 0) {
        ::close(handle);
        error = "Could not listen on the port";
        return false;
    }
    makeNonBlocking(handle);
    sockaddr_in bound{};
    socklen_t length = sizeof(bound);
    port_ = port;
    if (::getsockname(handle, reinterpret_cast<sockaddr*>(&bound), &length) == 0) {
        port_ = ntohs(bound.sin_port);
    }
    impl->handle = handle;
    return true;
}

void TcpListener::close() {
    if (impl->handle >= 0) {
        ::close(impl->handle);
        impl->handle = -1;
    }
    port_ = 0;
}

std::unique_ptr<TcpConnection> TcpListener::accept() {
    if (impl->handle < 0) {
        return nullptr;
    }
    sockaddr_in remote{};
    socklen_t length = sizeof(remote);
    const int handle = ::accept(impl->handle, reinterpret_cast<sockaddr*>(&remote), &length);
    if (handle < 0) {
        return nullptr;
    }
    makeNonBlocking(handle);
    tuneConnection(handle);
    auto connection = std::make_unique<TcpConnection>();
    connection->impl->handle = handle;
    connection->peer_.address = ntohl(remote.sin_addr.s_addr);
    connection->peer_.port = ntohs(remote.sin_port);
    return connection;
}

int tcpPoll(std::vector<TcpPollEntry>& entries, int timeoutMs) {
    std::vector<pollfd> fds;
    fds.reserve(entries.size());
    for (TcpPollEntry& entry : entries) {
        entry.readable = false;
        entry.writable = false;
        entry.broken = false;
        pollfd item{};
        item.fd = -1;
        if (entry.listener != nullptr) {
            item.fd = entry.listener->impl->handle;
            item.events = POLLIN;
        } else if (entry.connection != nullptr) {
            item.fd = entry.connection->impl->handle;
            item.events = static_cast<short>(POLLIN | (entry.wantWrite ? POLLOUT : 0));
        }
        fds.push_back(item);
    }
    const int ready = ::poll(fds.data(), static_cast<nfds_t>(fds.size()), timeoutMs);
    if (ready <= 0) {
        return ready < 0 && errno != EINTR ? -1 : 0;
    }
    for (size_t i = 0; i < entries.size(); ++i) {
        entries[i].readable = (fds[i].revents & POLLIN) != 0;
        entries[i].writable = (fds[i].revents & POLLOUT) != 0;
        entries[i].broken = (fds[i].revents & (POLLERR | POLLNVAL)) != 0;
        if ((fds[i].revents & POLLHUP) != 0 && !entries[i].readable) {
            entries[i].broken = true;
        }
    }
    return ready;
}

std::unique_ptr<TcpConnection> tcpConnectLoopback(int port, std::string& error) {
    const int handle = ::socket(AF_INET, SOCK_STREAM, 0);
    if (handle < 0) {
        error = "Could not create the client socket";
        return nullptr;
    }
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(static_cast<unsigned short>(port));
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (::connect(handle, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
        ::close(handle);
        error = "Could not connect to the port";
        return nullptr;
    }
    makeNonBlocking(handle);
    tuneConnection(handle);
    auto connection = std::make_unique<TcpConnection>();
    connection->impl->handle = handle;
    connection->peer_.address = 0x7f000001u;
    connection->peer_.port = port;
    return connection;
}

}
