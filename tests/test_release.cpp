#include "check.hpp"
#include "core/run_conditions.hpp"
#include "core/soak_plan.hpp"
#include "platform/platform.hpp"

#include <string>

namespace {

void testSoftwareRenderers() {
    using nvj::ValidationClass;
    CHECK(nvj::classifyRenderer("Mesa", "llvmpipe (LLVM 15.0.7, 256 bits)") == ValidationClass::Software);
    CHECK(nvj::classifyRenderer("Mesa", "softpipe") == ValidationClass::Software);
    CHECK(nvj::classifyRenderer("Mesa", "llvmpipe") == ValidationClass::Software);
    CHECK(nvj::classifyRenderer("Mesa/X.org", "Software Rasterizer") == ValidationClass::Software);
    CHECK(nvj::classifyRenderer("Mesa", "Mesa Software Renderer (swrast)") == ValidationClass::Software);
    CHECK(nvj::classifyRenderer("Mesa", "llvmpipe") == ValidationClass::Software);
    CHECK(nvj::classifyRenderer("Mesa", "lavapipe") == ValidationClass::Software);
    CHECK(nvj::classifyRenderer("Microsoft Corporation", "GDI Generic") == ValidationClass::Software);
    CHECK(nvj::classifyRenderer("Microsoft Corporation", "D3D12 (Microsoft Basic Render Driver)") == ValidationClass::Software);
    CHECK(nvj::classifyRenderer("Google Inc.", "Google SwiftShader") == ValidationClass::Software);
}

void testVirtualRenderers() {
    using nvj::ValidationClass;
    CHECK(nvj::classifyRenderer("Red Hat", "virgl (NVIDIA GeForce GTX 1060)") == ValidationClass::Virtual);
    CHECK(nvj::classifyRenderer("VMware, Inc.", "SVGA3D; build: RELEASE") == ValidationClass::Virtual);
    CHECK(nvj::classifyRenderer("Parallels", "Parallels Display Adapter") == ValidationClass::Virtual);
    CHECK(nvj::classifyRenderer("InnoTek", "VirtualBox Graphics Adapter") == ValidationClass::Virtual);
}

void testReportedGraphicsCards() {
    using nvj::ValidationClass;
    CHECK(nvj::classifyRenderer("NVIDIA Corporation", "NVIDIA GeForce RTX 3060/PCIe/SSE2") == ValidationClass::Reported);
    CHECK(nvj::classifyRenderer("ATI Technologies Inc.", "AMD Radeon RX 6600") == ValidationClass::Reported);
    CHECK(nvj::classifyRenderer("Intel", "Intel(R) UHD Graphics 770") == ValidationClass::Reported);
    CHECK(nvj::classifyRenderer("AMD", "AMD Radeon RX 6600 (radeonsi, navi23, LLVM 15.0.7, DRM 3.49, 6.1.0)") == ValidationClass::Reported);
    CHECK(nvj::classifyRenderer("Apple", "Apple M2") == ValidationClass::Reported);
    CHECK(nvj::classifyRenderer("", "") == ValidationClass::Reported);
}

void testNames() {
    CHECK(std::string(nvj::validationClassName(nvj::ValidationClass::Software)) == "software-gl");
    CHECK(std::string(nvj::validationClassName(nvj::ValidationClass::Virtual)) == "virtual-gpu");
    CHECK(std::string(nvj::validationClassName(nvj::ValidationClass::Reported)) == "gpu-reported");
}

void testVendors() {
    CHECK(nvj::gpuVendorOf("NVIDIA Corporation", "NVIDIA GeForce RTX 3060/PCIe/SSE2") == "nvidia");
    CHECK(nvj::gpuVendorOf("NVIDIA Corporation", "Quadro P2000/PCIe/SSE2") == "nvidia");
    CHECK(nvj::gpuVendorOf("ATI Technologies Inc.", "AMD Radeon RX 6600") == "amd");
    CHECK(nvj::gpuVendorOf("AMD", "AMD Radeon RX 6600 (radeonsi, navi23)") == "amd");
    CHECK(nvj::gpuVendorOf("Intel", "Intel(R) UHD Graphics 770") == "intel");
    CHECK(nvj::gpuVendorOf("Intel", "Mesa Intel(R) Xe Graphics (TGL GT2)") == "intel");
    CHECK(nvj::gpuVendorOf("Apple", "Apple M2") == "apple");
    CHECK(nvj::gpuVendorOf("Mesa", "llvmpipe (LLVM 15.0.7, 256 bits)") == "software");
    CHECK(nvj::gpuVendorOf("Qualcomm", "Adreno (TM) 730") == "other");
    CHECK(nvj::gpuVendorOf("", "") == "other");
}

void testConditions() {
    nvj::RunContext context;
    context.graphics = nvj::GraphicsInfo{"Mesa", "llvmpipe (LLVM 15.0.7, 256 bits)", "4.5 (Core Profile) Mesa 23.2.1", "4.50"};
    context.width = 1920;
    context.height = 1080;
    context.quality = "cycled";
    context.targetFps = 60;
    context.durationSeconds = 12.5;
    context.scenario = "test";
    context.clips = {"Plasma", "Tunnel"};
    context.effects = {"Glow"};
    const nlohmann::json item = nvj::runConditionsJson(context);
    CHECK(item["graphics"]["validationClass"] == "software-gl");
    CHECK(item["graphics"]["gpuVendor"] == "software");
    CHECK(item["graphics"]["driver"] == "4.5 (Core Profile) Mesa 23.2.1");
    CHECK(item["graphics"]["renderer"] == "llvmpipe (LLVM 15.0.7, 256 bits)");
    CHECK(item["resolution"]["width"] == 1920 && item["resolution"]["height"] == 1080 && item["resolution"]["text"] == "1920x1080");
    CHECK(item["durationSeconds"] == 12.5);
    CHECK(item["clips"].size() == 2 && item["effects"].size() == 1);
    CHECK(item["build"]["version"] == std::string(NYREXVJ_VERSION));
    CHECK(item["build"].contains("buildNumber") && item["build"].contains("configuration"));
    CHECK(item["system"].contains("os") && item["system"].contains("logicalCores") && item["system"].contains("memoryBytes"));
    CHECK(item["system"]["logicalCores"].get<int>() >= 1);
    CHECK(item["system"]["memoryBytes"].get<unsigned long long>() > 0);
    CHECK(item["system"]["architecture"].get<std::string>().size() > 0);
}

void testSystemInfo() {
    const nvj::platform::SystemInfo info = nvj::platform::systemInfo();
    CHECK(!info.os.empty());
    CHECK(!info.architecture.empty());
    CHECK(info.logicalCores >= 1);
    CHECK(info.memoryBytes > 0);
}

void testMediaPlan() {
    nvj::SoakPlan plan;
    plan.media = true;
    int launches = 0;
    int clears = 0;
    int looks = 0;
    for (int second = 0; second < 100; ++second) {
        const nvj::SoakEvents events = nvj::soakEventsAt(plan, second);
        launches += events.media ? 1 : 0;
        clears += events.clear ? 1 : 0;
        looks += events.look ? 1 : 0;
    }
    CHECK(launches == 49);
    CHECK(clears == 11);
    CHECK(looks == 0);
    nvj::SoakPlan normal;
    int normalMedia = 0;
    int normalLooks = 0;
    for (int second = 0; second < 100; ++second) {
        const nvj::SoakEvents events = nvj::soakEventsAt(normal, second);
        normalMedia += (events.media || events.clear) ? 1 : 0;
        normalLooks += events.look ? 1 : 0;
    }
    CHECK(normalMedia == 0);
    CHECK(normalLooks == 4);
    CHECK(!nvj::soakEventsAt(plan, 0).media);
}

}

int main() {
    testSoftwareRenderers();
    testVirtualRenderers();
    testReportedGraphicsCards();
    testNames();
    testVendors();
    testConditions();
    testSystemInfo();
    testMediaPlan();
    return testing::finish();
}
