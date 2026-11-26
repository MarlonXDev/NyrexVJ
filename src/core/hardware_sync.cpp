#include "core/hardware_sync.hpp"

#include <algorithm>
#include <array>
#include <cctype>

namespace nvj {

namespace {

std::string lowered(const std::string& text) {
    std::string out = text.substr(0, kHardwareSyncMaxText);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

bool contains(const std::string& text, const char* part) {
    return text.find(part) != std::string::npos;
}

}

bool isSoftwareRenderer(const std::string& vendor, const std::string& renderer) {
    const std::string r = lowered(renderer);
    const std::string v = lowered(vendor);
    static const std::array<const char*, 6> kSoftware = {"llvmpipe", "softpipe", "swrast", "software rasterizer", "gdi generic", "microsoft basic render"};
    for (const char* name : kSoftware) {
        if (contains(r, name)) {
            return true;
        }
    }
    return contains(v, "microsoft") && !contains(r, "d3d12") && !contains(r, "directx") && !contains(r, "nvidia") && !contains(r, "amd") && !contains(r, "intel");
}

GraphicsVendor classifyVendor(const std::string& vendor, const std::string& renderer) {
    if (vendor.empty() && renderer.empty()) {
        return GraphicsVendor::Unknown;
    }
    if (isSoftwareRenderer(vendor, renderer)) {
        return GraphicsVendor::Software;
    }
    const std::string v = lowered(vendor);
    const std::string r = lowered(renderer);
    if (contains(v, "nvidia") || contains(r, "nvidia") || contains(r, "geforce") || contains(r, "quadro")) {
        return GraphicsVendor::Nvidia;
    }
    if (contains(v, "amd") || contains(v, "ati technologies") || contains(v, "advanced micro devices") || contains(r, "radeon") || contains(r, "firepro")) {
        return GraphicsVendor::Amd;
    }
    if (contains(v, "intel") || contains(r, "intel")) {
        return GraphicsVendor::Intel;
    }
    return GraphicsVendor::Other;
}

HardwareSyncStatus assessHardwareSync(const HardwareSyncProbe& probe) {
    HardwareSyncStatus status;
    if (!probe.contextKnown) {
        return status;
    }
    status.vendor = classifyVendor(probe.vendor, probe.renderer);
    status.softwareRenderer = status.vendor == GraphicsVendor::Software;
    if (probe.wglNvSwapGroup) {
        status.names.push_back("WGL_NV_swap_group");
    }
    if (probe.glxNvSwapGroup) {
        status.names.push_back("GLX_NV_swap_group");
    }
    if (probe.wglI3dGenlock) {
        status.names.push_back("WGL_I3D_genlock");
    }
    if (probe.wglI3dSwapFrameLock) {
        status.names.push_back("WGL_I3D_swap_frame_lock");
    }
    if (probe.glxSgixSwapGroup) {
        status.names.push_back("GLX_SGIX_swap_group");
    }
    if (probe.glxSgixSwapBarrier) {
        status.names.push_back("GLX_SGIX_swap_barrier");
    }
    status.state = (status.softwareRenderer || status.names.empty()) ? HardwareSyncState::NotOffered : HardwareSyncState::OfferedByDriver;
    return status;
}

const char* hardwareSyncStateName(HardwareSyncState state) {
    switch (state) {
    case HardwareSyncState::NotOffered:
        return "not offered";
    case HardwareSyncState::OfferedByDriver:
        return "offered by the driver";
    case HardwareSyncState::Unknown:
        break;
    }
    return "not checked";
}

const char* graphicsVendorName(GraphicsVendor vendor) {
    switch (vendor) {
    case GraphicsVendor::Nvidia:
        return "NVIDIA";
    case GraphicsVendor::Amd:
        return "AMD";
    case GraphicsVendor::Intel:
        return "Intel";
    case GraphicsVendor::Software:
        return "software renderer";
    case GraphicsVendor::Other:
        return "other";
    case GraphicsVendor::Unknown:
        break;
    }
    return "unknown";
}

std::string joinExtensionNames(const std::vector<std::string>& names) {
    std::string out;
    for (const std::string& name : names) {
        if (!out.empty()) {
            out += ", ";
        }
        out += name;
    }
    return out;
}

}
