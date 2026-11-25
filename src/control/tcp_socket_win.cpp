#include "control/tcp_socket.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>

#include <mutex>

namespace nvj {

struct TcpConnection::Impl {
    SOCKET handle = INVALID_SOCKET;
};

struct TcpListener::Impl {
    SOCKET handle = INVALID_SOCKET;
};

namespace {

std::mutex winsockMutex;
int winsockUsers = 0;

bool acquireWinsock() {
    std::lock_guard<std::mutex> lock(winsockMutex);
    if (winsockUsers == 0) {
        WSADATA data{};
        if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
            return false;
        }
    }
    ++winsockUsers;
    return true;
}

void releaseWinsock() {
    std::lock_guard<std::mutex> lock(winsockMutex);
    if (winsockUsers > 0 && --winsockUsers == 0) {
        WSACleanup();
    }
}

void makeNonBlocking(SOCKET handle) {
    u_long enable = 1;
    ioctlsocket(handle, FIONBIO, &enable);
}

void tuneConnection(SOCKET handle) {
    const BOOL enable = TRUE;
    setsockopt(handle, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&enable), sizeof(enable));
}

bool wouldBlock() {
    const int code = WSAGetLastError();
    return code == WSAEWOULDBLOCK || code == WSAEINTR;
}

}

std::string TcpPeer::text() const {
    return std::to_string((address >> 24) & 0xff) + "." + std::to_string((address >> 16) & 0xff) + "." + std::to_string((address >> 8) & 0xff) + "." + std::to_string(address & 0xff);
}

TcpConnection::TcpConnection() : impl(std::make_unique<Impl>()) {
    acquireWinsock();
}

TcpConnection::~TcpConnection() {
    close();
    releaseWinsock();
}

bool TcpConnection::isOpen() const {
    return impl->handle != INVALID_SOCKET;
}

int TcpConnection::receive(void* buffer, int capacity) {
    if (impl->handle == INVALID_SOCKET || capacity <= 0) {
        return -1;
    }
    const int got = ::recv(impl->handle, static_cast<char*>(buffer), capacity, 0);
    if (got > 0) {
        return got;
    }
    if (got == 0) {
        return -1;
    }
    return wouldBlock() ? 0 : -1;
}

int TcpConnection::send(const void* data, int size) {
    if (impl->handle == INVALID_SOCKET || size <= 0) {
        return -1;
    }
    const int sent = ::send(impl->handle, static_cast<const char*>(data), size, 0);
    if (sent >= 0) {
        return sent;
    }
    return wouldBlock() ? 0 : -1;
}

void TcpConnection::shutdownWrite() {
    if (impl->handle != INVALID_SOCKET) {
        ::shutdown(impl->handle, SD_SEND);
    }
}

void TcpConnection::close() {
    if (impl->handle != INVALID_SOCKET) {
        ::closesocket(impl->handle);
        impl->handle = INVALID_SOCKET;
    }
}

TcpListener::TcpListener() : impl(std::make_unique<Impl>()) {
    acquireWinsock();
}

TcpListener::~TcpListener() {
    close();
    releaseWinsock();
}

bool TcpListener::isOpen() const {
    return impl->handle != INVALID_SOCKET;
}

bool TcpListener::open(int port, bool allowRemote, std::string& error) {
    close();
    if (port < 0 || port > 65535) {
        error = "The port must be between 1 and 65535";
        return false;
    }
    const SOCKET handle = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (handle == INVALID_SOCKET) {
        error = "Could not create the network socket";
        return false;
    }
    const BOOL exclusive = TRUE;
    setsockopt(handle, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char*>(&exclusive), sizeof(exclusive));
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(static_cast<unsigned short>(port));
    address.sin_addr.s_addr = htonl(allowRemote ? INADDR_ANY : INADDR_LOOPBACK);
    if (::bind(handle, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
        ::closesocket(handle);
        error = "Could not open the port, it may be in use";
        return false;
    }
    if (::listen(handle, 32) != 0) {
        ::closesocket(handle);
        error = "Could not listen on the port";
        return false;
    }
    makeNonBlocking(handle);
    sockaddr_in bound{};
    int length = sizeof(bound);
    port_ = port;
    if (::getsockname(handle, reinterpret_cast<sockaddr*>(&bound), &length) == 0) {
        port_ = ntohs(bound.sin_port);
    }
    impl->handle = handle;
    return true;
}

void TcpListener::close() {
    if (impl->handle != INVALID_SOCKET) {
        ::closesocket(impl->handle);
        impl->handle = INVALID_SOCKET;
    }
    port_ = 0;
}

std::unique_ptr<TcpConnection> TcpListener::accept() {
    if (impl->handle == INVALID_SOCKET) {
        return nullptr;
    }
    sockaddr_in remote{};
    int length = sizeof(remote);
    const SOCKET handle = ::accept(impl->handle, reinterpret_cast<sockaddr*>(&remote), &length);
    if (handle == INVALID_SOCKET) {
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
    std::vector<WSAPOLLFD> fds;
    fds.reserve(entries.size());
    for (TcpPollEntry& entry : entries) {
        entry.readable = false;
        entry.writable = false;
        entry.broken = false;
        WSAPOLLFD item{};
        item.fd = INVALID_SOCKET;
        if (entry.listener != nullptr) {
            item.fd = entry.listener->impl->handle;
            item.events = POLLRDNORM;
        } else if (entry.connection != nullptr) {
            item.fd = entry.connection->impl->handle;
            item.events = static_cast<SHORT>(POLLRDNORM | (entry.wantWrite ? POLLWRNORM : 0));
        }
        fds.push_back(item);
    }
    if (fds.empty()) {
        Sleep(static_cast<DWORD>(timeoutMs > 0 ? timeoutMs : 0));
        return 0;
    }
    const int ready = WSAPoll(fds.data(), static_cast<ULONG>(fds.size()), timeoutMs);
    if (ready <= 0) {
        return ready < 0 ? -1 : 0;
    }
    for (size_t i = 0; i < entries.size(); ++i) {
        entries[i].readable = (fds[i].revents & (POLLRDNORM | POLLRDBAND)) != 0;
        entries[i].writable = (fds[i].revents & POLLWRNORM) != 0;
        entries[i].broken = (fds[i].revents & (POLLERR | POLLNVAL)) != 0;
        if ((fds[i].revents & POLLHUP) != 0 && !entries[i].readable) {
            entries[i].broken = true;
        }
    }
    return ready;
}

std::unique_ptr<TcpConnection> tcpConnectLoopback(int port, std::string& error) {
    auto connection = std::make_unique<TcpConnection>();
    const SOCKET handle = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (handle == INVALID_SOCKET) {
        error = "Could not create the client socket";
        return nullptr;
    }
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(static_cast<unsigned short>(port));
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (::connect(handle, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
        ::closesocket(handle);
        error = "Could not connect to the port";
        return nullptr;
    }
    makeNonBlocking(handle);
    tuneConnection(handle);
    connection->impl->handle = handle;
    connection->peer_.address = 0x7f000001u;
    connection->peer_.port = port;
    return connection;
}

}
