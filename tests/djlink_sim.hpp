#pragma once

#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace djtest {

inline void put32(std::vector<unsigned char>& data, size_t offset, unsigned long value) {
    data[offset] = static_cast<unsigned char>((value >> 24) & 0xff);
    data[offset + 1] = static_cast<unsigned char>((value >> 16) & 0xff);
    data[offset + 2] = static_cast<unsigned char>((value >> 8) & 0xff);
    data[offset + 3] = static_cast<unsigned char>(value & 0xff);
}

inline std::vector<unsigned char> header(unsigned char type, size_t size, int player, const std::string& name) {
    std::vector<unsigned char> data(size, 0);
    static const unsigned char magic[10] = {0x51, 0x73, 0x70, 0x74, 0x31, 0x57, 0x6d, 0x4a, 0x4f, 0x4c};
    for (size_t i = 0; i < 10; ++i) {
        data[i] = magic[i];
    }
    data[10] = type;
    for (size_t i = 0; i < name.size() && i < 20; ++i) {
        data[11 + i] = static_cast<unsigned char>(name[i]);
    }
    data[31] = 0x01;
    data[32] = 0x00;
    data[33] = static_cast<unsigned char>(player);
    return data;
}

inline std::vector<unsigned char> beatPacket(int player, const std::string& name, double trackBpm, double pitchPercent, int beatInBar,
                                             unsigned long nextBeatMs = 468, unsigned long nextBarMs = 1406) {
    std::vector<unsigned char> data = header(0x28, 96, player, name);
    data[34] = 0x00;
    data[35] = 0x3c;
    put32(data, 36, nextBeatMs);
    put32(data, 40, nextBeatMs * 2);
    put32(data, 44, nextBarMs);
    put32(data, 48, nextBeatMs * 4);
    put32(data, 52, nextBarMs + 1875);
    put32(data, 56, nextBeatMs * 8);
    for (size_t i = 60; i < 84; ++i) {
        data[i] = 0xff;
    }
    const unsigned long pitch = static_cast<unsigned long>(std::llround((1.0 + pitchPercent / 100.0) * 1048576.0));
    put32(data, 84, pitch);
    const unsigned bpm = static_cast<unsigned>(std::llround(trackBpm * 100.0));
    data[90] = static_cast<unsigned char>((bpm >> 8) & 0xff);
    data[91] = static_cast<unsigned char>(bpm & 0xff);
    data[92] = static_cast<unsigned char>(beatInBar);
    data[95] = static_cast<unsigned char>(player);
    return data;
}

inline std::vector<unsigned char> onAirPacket(int player) {
    std::vector<unsigned char> data = header(0x03, 45, player, "DJM-900NXS2");
    data[34] = 0x00;
    data[35] = 0x09;
    return data;
}

}
