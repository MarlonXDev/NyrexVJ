#include "render/hardware_sync_probe.hpp"

#include <glad/gl.h>

#include <GLFW/glfw3.h>

namespace nvj {

namespace {

std::string readString(GLenum name) {
    const GLubyte* text = glGetString(name);
    if (text == nullptr) {
        return std::string();
    }
    return std::string(reinterpret_cast<const char*>(text)).substr(0, kHardwareSyncMaxText);
}

bool listed(const char* name) {
    return glfwExtensionSupported(name) == GLFW_TRUE;
}

}

HardwareSyncProbe readHardwareSyncProbe() {
    HardwareSyncProbe probe;
    if (glfwGetCurrentContext() == nullptr) {
        return probe;
    }
    probe.contextKnown = true;
    probe.vendor = readString(GL_VENDOR);
    probe.renderer = readString(GL_RENDERER);
    probe.wglNvSwapGroup = listed("WGL_NV_swap_group");
    probe.glxNvSwapGroup = listed("GLX_NV_swap_group");
    probe.wglI3dGenlock = listed("WGL_I3D_genlock");
    probe.wglI3dSwapFrameLock = listed("WGL_I3D_swap_frame_lock");
    probe.glxSgixSwapGroup = listed("GLX_SGIX_swap_group");
    probe.glxSgixSwapBarrier = listed("GLX_SGIX_swap_barrier");
    return probe;
}

}
