#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace nvj {

struct TcpPeer {
    std::uint32_t address = 0;
    int port = 0;

    bool loopback() const { return (address >> 24) == 127; }
    std::string text() const;
};

class TcpConnection {
public:
    TcpConnection();
    ~TcpConnection();
    TcpConnection(const TcpConnection&) = delete;
    TcpConnection& operator=(const TcpConnection&) = delete;

    bool isOpen() const;
    int receive(void* buffer, int capacity);
    int send(const void* data, int size);
    void shutdownWrite();
    void close();
    const TcpPeer& peer() const { return peer_; }

    struct Impl;
    std::unique_ptr<Impl> impl;

private:
    TcpPeer peer_;
    friend class TcpListener;
    friend std::unique_ptr<TcpConnection> tcpConnectLoopback(int port, std::string& error);
};

class TcpListener {
public:
    TcpListener();
    ~TcpListener();
    TcpListener(const TcpListener&) = delete;
    TcpListener& operator=(const TcpListener&) = delete;

    bool open(int port, bool allowRemote, std::string& error);
    void close();
    bool isOpen() const;
    int port() const { return port_; }
    std::unique_ptr<TcpConnection> accept();

    struct Impl;
    std::unique_ptr<Impl> impl;

private:
    int port_ = 0;
};

struct TcpPollEntry {
    const TcpListener* listener = nullptr;
    const TcpConnection* connection = nullptr;
    bool wantWrite = false;
    bool readable = false;
    bool writable = false;
    bool broken = false;
};

int tcpPoll(std::vector<TcpPollEntry>& entries, int timeoutMs);
std::unique_ptr<TcpConnection> tcpConnectLoopback(int port, std::string& error);

}
