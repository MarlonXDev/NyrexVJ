#include "core/run_conditions.hpp"

#include "platform/platform.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <string_view>

namespace nvj {

namespace {

std::string lowered(const std::string& text) {
    std::string out = text;
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

bool containsAny(const std::string& text, const std::array<std::string_view, 8>& markers) {
    for (const std::string_view marker : markers) {
        if (!marker.empty() && text.find(marker) != std::string::npos) {
            return true;
        }
    }
    return false;
}

constexpr std::array<std::string_view, 8> kSoftwareMarkers{"llvmpipe", "softpipe", "swrast", "lavapipe", "software rasterizer", "basic render", "swiftshader", "gdi generic"};
constexpr std::array<std::string_view, 8> kVirtualMarkers{"virgl", "vmware", "svga3d", "virtualbox", "vboxvideo", "parallels", "qxl", "microsoft hyper-v"};

}

ValidationClass classifyRenderer(const std::string& vendor, const std::string& renderer) {
    const std::string text = lowered(vendor + " " + renderer);
    if (containsAny(text, kSoftwareMarkers)) {
        return ValidationClass::Software;
    }
    if (containsAny(text, kVirtualMarkers)) {
        return ValidationClass::Virtual;
    }
    return ValidationClass::Reported;
}

const char* validationClassName(ValidationClass value) {
    switch (value) {
    case ValidationClass::Software:
        return "software-gl";
    case ValidationClass::Virtual:
        return "virtual-gpu";
    case ValidationClass::Reported:
        break;
    }
    return "gpu-reported";
}

std::string gpuVendorOf(const std::string& vendor, const std::string& renderer) {
    if (classifyRenderer(vendor, renderer) == ValidationClass::Software) {
        return "software";
    }
    const std::string text = lowered(vendor + " " + renderer);
    if (text.find("nvidia") != std::string::npos || text.find("geforce") != std::string::npos || text.find("quadro") != std::string::npos) {
        return "nvidia";
    }
    if (text.find("amd") != std::string::npos || text.find("radeon") != std::string::npos || text.find("advanced micro devices") != std::string::npos || text.find("ati technologies") != std::string::npos) {
        return "amd";
    }
    if (text.find("intel") != std::string::npos) {
        return "intel";
    }
    if (text.find("apple") != std::string::npos) {
        return "apple";
    }
    return "other";
}

nlohmann::json runConditionsJson(const RunContext& context) {
    using json = nlohmann::json;
    const platform::SystemInfo system = platform::systemInfo();
    const ValidationClass kind = classifyRenderer(context.graphics.vendor, context.graphics.renderer);
    json item;
    item["system"] = {{"os", system.os}, {"osVersion", system.osVersion}, {"architecture", system.architecture}, {"cpu", system.cpuName}, {"logicalCores", system.logicalCores}, {"memoryBytes", system.memoryBytes}};
    json build;
    build["version"] = NYREXVJ_VERSION;
    build["buildNumber"] = NYREXVJ_BUILD_NUMBER;
#ifdef NDEBUG
    build["configuration"] = "release";
#else
    build["configuration"] = "debug";
#endif
    item["build"] = std::move(build);
    item["graphics"] = {{"vendor", context.graphics.vendor}, {"renderer", context.graphics.renderer}, {"driver", context.graphics.glVersion}, {"glsl", context.graphics.glslVersion}, {"gpuVendor", gpuVendorOf(context.graphics.vendor, context.graphics.renderer)}, {"validationClass", validationClassName(kind)}};
    item["resolution"] = {{"width", context.width}, {"height", context.height}, {"text", std::to_string(context.width) + "x" + std::to_string(context.height)}};
    item["quality"] = context.quality;
    item["targetFps"] = context.targetFps;
    item["durationSeconds"] = context.durationSeconds;
    item["scenario"] = context.scenario;
    item["clips"] = context.clips;
    item["effects"] = context.effects;
    return item;
}

}
