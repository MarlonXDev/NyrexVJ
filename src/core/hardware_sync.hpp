#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace nvj {

enum class HardwareSyncState { Unknown, NotOffered, OfferedByDriver };

enum class GraphicsVendor { Unknown, Nvidia, Amd, Intel, Software, Other };

constexpr size_t kHardwareSyncMaxText = 256;

struct HardwareSyncProbe {
    bool contextKnown = false;
    bool wglNvSwapGroup = false;
    bool glxNvSwapGroup = false;
    bool wglI3dGenlock = false;
    bool wglI3dSwapFrameLock = false;
    bool glxSgixSwapGroup = false;
    bool glxSgixSwapBarrier = false;
    std::string vendor;
    std::string renderer;
};

struct HardwareSyncStatus {
    HardwareSyncState state = HardwareSyncState::Unknown;
    GraphicsVendor vendor = GraphicsVendor::Unknown;
    bool softwareRenderer = false;
    std::vector<std::string> names;
};

bool isSoftwareRenderer(const std::string& vendor, const std::string& renderer);
GraphicsVendor classifyVendor(const std::string& vendor, const std::string& renderer);
HardwareSyncStatus assessHardwareSync(const HardwareSyncProbe& probe);
const char* hardwareSyncStateName(HardwareSyncState state);
const char* graphicsVendorName(GraphicsVendor vendor);
std::string joinExtensionNames(const std::vector<std::string>& names);

}
