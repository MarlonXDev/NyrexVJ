#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace nvj {

class Sha256 {
public:
    Sha256();
    void update(const unsigned char* data, size_t size);
    std::string finalHex();

private:
    void processBlock(const unsigned char* block);

    std::array<std::uint32_t, 8> state_;
    std::array<unsigned char, 64> buffer_;
    size_t bufferSize_ = 0;
    std::uint64_t totalBytes_ = 0;
};

std::string sha256Hex(const unsigned char* data, size_t size);
std::string sha256Hex(const std::string& text);
std::string sha256Hex(const std::vector<unsigned char>& data);

}
