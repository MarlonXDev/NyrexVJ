#include "check.hpp"
#include "core/hardware_sync.hpp"

#include <string>

namespace {

nvj::HardwareSyncProbe probeOf(const std::string& vendor, const std::string& renderer) {
    nvj::HardwareSyncProbe probe;
    probe.contextKnown = true;
    probe.vendor = vendor;
    probe.renderer = renderer;
    return probe;
}

void testSoftwareRenderers() {
    CHECK(nvj::isSoftwareRenderer("Mesa", "llvmpipe (LLVM 15.0.7, 256 bits)"));
    CHECK(nvj::isSoftwareRenderer("Mesa/X.org", "softpipe"));
    CHECK(nvj::isSoftwareRenderer("Mesa", "Software Rasterizer"));
    CHECK(nvj::isSoftwareRenderer("Mesa", "SWRAST"));
    CHECK(nvj::isSoftwareRenderer("Microsoft Corporation", "GDI Generic"));
    CHECK(nvj::isSoftwareRenderer("Microsoft Corporation", "Microsoft Basic Render Driver"));
    CHECK(nvj::isSoftwareRenderer("Microsoft Corporation", ""));
    CHECK(!nvj::isSoftwareRenderer("NVIDIA Corporation", "NVIDIA RTX A4000/PCIe/SSE2"));
    CHECK(!nvj::isSoftwareRenderer("ATI Technologies Inc.", "AMD Radeon Pro W6600"));
    CHECK(!nvj::isSoftwareRenderer("Intel", "Intel(R) UHD Graphics 770"));
    CHECK(!nvj::isSoftwareRenderer("Microsoft Corporation", "D3D12 (NVIDIA GeForce RTX 3060)"));
    CHECK(!nvj::isSoftwareRenderer("", ""));
}

void testVendors() {
    using nvj::GraphicsVendor;
    CHECK(nvj::classifyVendor("NVIDIA Corporation", "Quadro P2200/PCIe/SSE2") == GraphicsVendor::Nvidia);
    CHECK(nvj::classifyVendor("NVIDIA Corporation", "NVIDIA GeForce RTX 4070/PCIe/SSE2") == GraphicsVendor::Nvidia);
    CHECK(nvj::classifyVendor("ATI Technologies Inc.", "AMD Radeon RX 6800") == GraphicsVendor::Amd);
    CHECK(nvj::classifyVendor("AMD", "AMD Radeon Graphics (radeonsi, navi21)") == GraphicsVendor::Amd);
    CHECK(nvj::classifyVendor("Advanced Micro Devices, Inc.", "AMD FirePro W9100") == GraphicsVendor::Amd);
    CHECK(nvj::classifyVendor("Intel", "Intel(R) Iris(R) Xe Graphics") == GraphicsVendor::Intel);
    CHECK(nvj::classifyVendor("Intel Open Source Technology Center", "Mesa Intel(R) UHD Graphics 620 (KBL GT2)") == GraphicsVendor::Intel);
    CHECK(nvj::classifyVendor("Mesa", "llvmpipe (LLVM 17.0.6, 256 bits)") == GraphicsVendor::Software);
    CHECK(nvj::classifyVendor("Apple", "Apple M2") == GraphicsVendor::Other);
    CHECK(nvj::classifyVendor("", "") == GraphicsVendor::Unknown);
    CHECK(nvj::classifyVendor("NVIDIA CORPORATION", "") == GraphicsVendor::Nvidia);
    CHECK(std::string(nvj::graphicsVendorName(GraphicsVendor::Nvidia)) == "NVIDIA");
    CHECK(std::string(nvj::graphicsVendorName(GraphicsVendor::Unknown)) == "unknown");
}

void testNoContext() {
    nvj::HardwareSyncProbe probe;
    probe.wglNvSwapGroup = true;
    const nvj::HardwareSyncStatus status = nvj::assessHardwareSync(probe);
    CHECK(status.state == nvj::HardwareSyncState::Unknown);
    CHECK(status.names.empty());
    CHECK(!status.softwareRenderer);
    CHECK(std::string(nvj::hardwareSyncStateName(status.state)) == "not checked");
}

void testNotOffered() {
    const nvj::HardwareSyncStatus consumer = nvj::assessHardwareSync(probeOf("NVIDIA Corporation", "NVIDIA GeForce RTX 3060/PCIe/SSE2"));
    CHECK(consumer.state == nvj::HardwareSyncState::NotOffered);
    CHECK(consumer.vendor == nvj::GraphicsVendor::Nvidia);
    CHECK(consumer.names.empty());
    CHECK(!consumer.softwareRenderer);
    CHECK(std::string(nvj::hardwareSyncStateName(consumer.state)) == "not offered");
    const nvj::HardwareSyncStatus intel = nvj::assessHardwareSync(probeOf("Intel", "Intel(R) UHD Graphics 630"));
    CHECK(intel.state == nvj::HardwareSyncState::NotOffered);
    CHECK(intel.vendor == nvj::GraphicsVendor::Intel);
}

void testOffered() {
    nvj::HardwareSyncProbe windows = probeOf("NVIDIA Corporation", "NVIDIA RTX A5000/PCIe/SSE2");
    windows.wglNvSwapGroup = true;
    windows.wglI3dGenlock = true;
    const nvj::HardwareSyncStatus status = nvj::assessHardwareSync(windows);
    CHECK(status.state == nvj::HardwareSyncState::OfferedByDriver);
    CHECK(status.vendor == nvj::GraphicsVendor::Nvidia);
    CHECK(status.names.size() == 2);
    CHECK(status.names[0] == "WGL_NV_swap_group");
    CHECK(status.names[1] == "WGL_I3D_genlock");
    CHECK(nvj::joinExtensionNames(status.names) == "WGL_NV_swap_group, WGL_I3D_genlock");
    CHECK(std::string(nvj::hardwareSyncStateName(status.state)) == "offered by the driver");

    nvj::HardwareSyncProbe amd = probeOf("ATI Technologies Inc.", "AMD FirePro W7100");
    amd.wglNvSwapGroup = true;
    CHECK(nvj::assessHardwareSync(amd).state == nvj::HardwareSyncState::OfferedByDriver);
    CHECK(nvj::assessHardwareSync(amd).vendor == nvj::GraphicsVendor::Amd);

    nvj::HardwareSyncProbe linux = probeOf("NVIDIA Corporation", "Quadro RTX 4000/PCIe/SSE2");
    linux.glxNvSwapGroup = true;
    linux.glxSgixSwapGroup = true;
    linux.glxSgixSwapBarrier = true;
    const nvj::HardwareSyncStatus linuxStatus = nvj::assessHardwareSync(linux);
    CHECK(linuxStatus.state == nvj::HardwareSyncState::OfferedByDriver);
    CHECK(linuxStatus.names.size() == 3);
    CHECK(linuxStatus.names[0] == "GLX_NV_swap_group");
    CHECK(linuxStatus.names[2] == "GLX_SGIX_swap_barrier");

    nvj::HardwareSyncProbe all = probeOf("NVIDIA Corporation", "Quadro P4000");
    all.wglNvSwapGroup = all.glxNvSwapGroup = all.wglI3dGenlock = all.wglI3dSwapFrameLock = all.glxSgixSwapGroup = all.glxSgixSwapBarrier = true;
    CHECK(nvj::assessHardwareSync(all).names.size() == 6);
}

void testSoftwareNeverOffered() {
    nvj::HardwareSyncProbe probe = probeOf("Mesa", "llvmpipe (LLVM 15.0.7, 256 bits)");
    probe.glxSgixSwapGroup = true;
    probe.glxSgixSwapBarrier = true;
    const nvj::HardwareSyncStatus status = nvj::assessHardwareSync(probe);
    CHECK(status.state == nvj::HardwareSyncState::NotOffered);
    CHECK(status.softwareRenderer);
    CHECK(status.vendor == nvj::GraphicsVendor::Software);
    CHECK(status.names.size() == 2);
}

void testBoundedText() {
    const std::string huge(100000, 'x');
    CHECK(!nvj::isSoftwareRenderer(huge, huge));
    CHECK(nvj::classifyVendor(huge, huge) == nvj::GraphicsVendor::Other);
    std::string late = std::string(nvj::kHardwareSyncMaxText, 'x') + "llvmpipe";
    CHECK(!nvj::isSoftwareRenderer("", late));
    nvj::HardwareSyncProbe probe = probeOf(huge, huge);
    probe.wglNvSwapGroup = true;
    CHECK(nvj::assessHardwareSync(probe).state == nvj::HardwareSyncState::OfferedByDriver);
    const std::string withNulls("NVIDIA\0 Corporation", 19);
    CHECK(nvj::classifyVendor(withNulls, "") == nvj::GraphicsVendor::Nvidia);
}

void testDeterministic() {
    nvj::HardwareSyncProbe probe = probeOf("NVIDIA Corporation", "Quadro RTX 5000");
    probe.wglNvSwapGroup = true;
    const nvj::HardwareSyncStatus a = nvj::assessHardwareSync(probe);
    const nvj::HardwareSyncStatus b = nvj::assessHardwareSync(probe);
    CHECK(a.state == b.state && a.names == b.names && a.vendor == b.vendor);
}

}

int main() {
    testSoftwareRenderers();
    testVendors();
    testNoContext();
    testNotOffered();
    testOffered();
    testSoftwareNeverOffered();
    testBoundedText();
    testDeterministic();
    return testing::finish();
}
