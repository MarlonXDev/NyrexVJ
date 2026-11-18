#include "app.hpp"
#include "log.hpp"
#include "platform.hpp"
#include "theme.hpp"

#include <glad/gl.h>

#include <GLFW/glfw3.h>

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include <stb_image_write.h>

#include <cstdlib>
#include <string>
#include <vector>

namespace {

struct Options {
    int frames = 0;
    std::string screenshot;
    std::string dumpOutput;
    nvj::StartOptions start;
};

Options parseOptions(int argc, char** argv) {
    Options options;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        bool hasValue = i + 1 < argc;
        if (arg == "--frames" && hasValue) {
            options.frames = std::atoi(argv[++i]);
        } else if (arg == "--column" && hasValue) {
            options.start.column = std::atoi(argv[++i]) - 1;
        } else if (arg == "--screenshot" && hasValue) {
            options.screenshot = argv[++i];
        } else if (arg == "--dump-output" && hasValue) {
            options.dumpOutput = argv[++i];
        } else if (arg == "--preset" && hasValue) {
            options.start.preset = argv[++i];
        } else if (arg == "--clip" && hasValue) {
            options.start.clip = argv[++i];
        } else if (arg == "--effect" && hasValue) {
            options.start.effect = argv[++i];
        } else if (arg == "--resolution" && hasValue) {
            options.start.resolution = std::atoi(argv[++i]);
        } else if (arg == "--select-layer" && hasValue) {
            options.start.selectLayer = std::atoi(argv[++i]) - 1;
        } else if (arg == "--tab" && hasValue) {
            options.start.tab = std::atoi(argv[++i]);
        } else if (arg == "--output") {
            options.start.openOutput = true;
        } else if (arg == "--fresh") {
            options.start.fresh = true;
        } else if (arg == "--no-update-check") {
            options.start.skipUpdateCheck = true;
        }
    }
    if (options.frames > 0) {
        options.start.skipUpdateCheck = true;
    }
    return options;
}

void onGlfwError(int code, const char* message) {
    nvj::logLine("GLFW error " + std::to_string(code) + ": " + (message ? message : ""));
}

bool saveScreenshot(const std::string& path, int width, int height) {
    std::vector<unsigned char> pixels(static_cast<size_t>(width) * static_cast<size_t>(height) * 3u);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadBuffer(GL_BACK);
    glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
    stbi_flip_vertically_on_write(1);
    return stbi_write_png(path.c_str(), width, height, 3, pixels.data(), width * 3) != 0;
}

bool saveOutput(const std::string& path, nvj::Engine& engine) {
    std::vector<unsigned char> pixels;
    int width = 0;
    int height = 0;
    if (!engine.readOutput(pixels, width, height)) {
        return false;
    }
    stbi_flip_vertically_on_write(1);
    return stbi_write_png(path.c_str(), width, height, 4, pixels.data(), width * 4) != 0;
}

}

int main(int argc, char** argv) {
    Options options = parseOptions(argc, argv);

    nvj::platform::cleanupOldBinary();
    nvj::logInit();
    nvj::logLine(std::string("NyrexVJ ") + NYREXVJ_VERSION);

    glfwSetErrorCallback(onGlfwError);
    if (!glfwInit()) {
        nvj::logLine("GLFW initialisation failed");
        return 1;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);

    GLFWwindow* window = glfwCreateWindow(1600, 900, (std::string("NyrexVJ ") + NYREXVJ_VERSION).c_str(), nullptr, nullptr);
    if (!window) {
        nvj::logLine("Could not create a window with an OpenGL 3.3 core context");
        glfwTerminate();
        return 1;
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(options.frames > 0 ? 0 : 1);

    if (!gladLoadGL(reinterpret_cast<GLADloadfunc>(glfwGetProcAddress))) {
        nvj::logLine("OpenGL loader failed");
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }

    nvj::logLine(std::string("OpenGL ") + reinterpret_cast<const char*>(glGetString(GL_VERSION)));
    nvj::logLine(std::string("Renderer ") + reinterpret_cast<const char*>(glGetString(GL_RENDERER)));

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable | ImGuiConfigFlags_ViewportsEnable;

    nvj::applyTheme();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 0.0f;
    style.Colors[ImGuiCol_WindowBg].w = 1.0f;

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330 core");

    nvj::App app;
    if (!app.init(window, options.start)) {
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }

    double last = glfwGetTime();
    int frameCount = 0;
    int exitCode = 0;

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        if (glfwGetWindowAttrib(window, GLFW_ICONIFIED)) {
            glfwWaitEventsTimeout(0.1);
            continue;
        }

        double now = glfwGetTime();
        double dt = options.frames > 0 ? 1.0 / 60.0 : now - last;
        last = now;

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        app.frame(dt);

        ImGui::Render();

        int framebufferWidth = 0;
        int framebufferHeight = 0;
        glfwGetFramebufferSize(window, &framebufferWidth, &framebufferHeight);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, framebufferWidth, framebufferHeight);
        glClearColor(0.05f, 0.05f, 0.06f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        ++frameCount;
        if (options.frames > 0 && frameCount >= options.frames) {
            if (!options.screenshot.empty()) {
                if (saveScreenshot(options.screenshot, framebufferWidth, framebufferHeight)) {
                    nvj::logLine("Screenshot saved to " + options.screenshot);
                } else {
                    nvj::logLine("Screenshot failed");
                    exitCode = 1;
                }
            }
            if (!options.dumpOutput.empty()) {
                if (saveOutput(options.dumpOutput, app.engine())) {
                    nvj::logLine("Output saved to " + options.dumpOutput);
                } else {
                    nvj::logLine("Output dump failed");
                    exitCode = 1;
                }
            }
            break;
        }

        if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
            GLFWwindow* backup = glfwGetCurrentContext();
            ImGui::UpdatePlatformWindows();
            ImGui::RenderPlatformWindowsDefault();
            glfwMakeContextCurrent(backup);
        }

        glfwSwapBuffers(window);
    }

    app.shutdown();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    if (app.wantsRestart()) {
        nvj::platform::relaunchSelf();
    }
    return exitCode;
}
