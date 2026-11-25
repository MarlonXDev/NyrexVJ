#pragma once

#include <cstdint>
#include <memory>
#include <string>

namespace nvj {

class UdpSocket {
public:
    UdpSocket();
    ~UdpSocket();
    UdpSocket(const UdpSocket&) = delete;
    UdpSocket& operator=(const UdpSocket&) = delete;

    bool open(int port, bool allowRemote, std::string& error);
    bool openShared(int port, std::string& error);
    bool openExclusive(int port, bool allowRemote, std::string& error);
    int localPort() const;
    int receiveFrom(unsigned char* buffer, int capacity, std::uint32_t& address, std::uint16_t& port);
    bool sendTo(std::uint32_t address, std::uint16_t port, const unsigned char* data, int size);
    static bool parseIpv4(const std::string& host, std::uint32_t& address);
    void close();
    bool isOpen() const;
    int receive(unsigned char* buffer, int capacity);
    bool connectTo(const std::string& host, int port, std::string& error);
    bool send(const unsigned char* data, int size);
    static bool sendLoopback(int port, const unsigned char* data, int size);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}
