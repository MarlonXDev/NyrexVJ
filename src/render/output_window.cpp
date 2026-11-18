#include "render/output_window.hpp"

#include "render/clip_library.hpp"
#include "render/gl_util.hpp"
#include "core/log.hpp"

#include <GLFW/glfw3.h>

#include <algorithm>

namespace nvj {

namespace {

const char* kBlitFragment = R"glsl(#version 330 core
in vec2 vUv;
out vec4 fragColor;
uniform sampler2D uTex;
void main() {
    fragColor = vec4(texture(uTex, vUv).rgb, 1.0);
}
)glsl";

GLFWmonitor* pickMonitor(int index) {
    int count = 0;
    GLFWmonitor** monitors = glfwGetMonitors(&count);
    if (!monitors || count <= 0) {
        return glfwGetPrimaryMonitor();
    }
    return monitors[std::clamp(index, 0, count - 1)];
}

}

OutputWindow::~OutputWindow() {
    close();
}

std::vector<std::string> OutputWindow::monitorNames() {
    std::vector<std::string> names;
    int count = 0;
    GLFWmonitor** monitors = glfwGetMonitors(&count);
    for (int i = 0; i < count; ++i) {
        const char* name = glfwGetMonitorName(monitors[i]);
        const GLFWvidmode* mode = glfwGetVideoMode(monitors[i]);
        std::string label = std::to_string(i + 1) + ": " + (name ? name : "Display");
        if (mode) {
            label += " (" + std::to_string(mode->width) + "x" + std::to_string(mode->height) + ")";
        }
        names.push_back(label);
    }
    return names;
}

bool OutputWindow::open(GLFWwindow* share, int monitorIndex, bool fullscreen) {
    if (window_) {
        return true;
    }
    window_ = glfwCreateWindow(windowedWidth_, windowedHeight_, "NyrexVJ Output", nullptr, share);
    if (!window_) {
        logLine("Could not create the output window");
        return false;
    }
    glfwSetWindowUserPointer(window_, this);
    glfwSetKeyCallback(window_, &OutputWindow::onKey);
    glfwGetWindowPos(window_, &windowedX_, &windowedY_);

    GLFWwindow* previous = glfwGetCurrentContext();
    glfwMakeContextCurrent(window_);
    glfwSwapInterval(0);
    bool ready = createResources();
    glfwMakeContextCurrent(previous);
    if (!ready) {
        glfwDestroyWindow(window_);
        window_ = nullptr;
        return false;
    }

    monitorIndex_ = monitorIndex;
    fullscreen_ = false;
    if (fullscreen) {
        setFullscreen(true, monitorIndex);
    }
    logLine("Output window opened");
    return true;
}

void OutputWindow::close() {
    if (!window_) {
        return;
    }
    GLFWwindow* previous = glfwGetCurrentContext();
    glfwMakeContextCurrent(window_);
    destroyResources();
    glfwMakeContextCurrent(previous == window_ ? nullptr : previous);
    glfwDestroyWindow(window_);
    window_ = nullptr;
    fullscreen_ = false;
    logLine("Output window closed");
}

bool OutputWindow::createResources() {
    std::string error;
    program_ = buildProgram(clipVertexSource(), kBlitFragment, &error);
    if (!program_) {
        logLine("Output shader failed: " + error);
        return false;
    }
    textureLocation_ = glGetUniformLocation(program_, "uTex");
    glGenVertexArrays(1, &vao_);
    return true;
}

void OutputWindow::destroyResources() {
    if (program_) {
        glDeleteProgram(program_);
        program_ = 0;
    }
    if (vao_) {
        glDeleteVertexArrays(1, &vao_);
        vao_ = 0;
    }
}

void OutputWindow::setFullscreen(bool fullscreen, int monitorIndex) {
    if (!window_) {
        return;
    }
    if (fullscreen) {
        if (!fullscreen_) {
            glfwGetWindowPos(window_, &windowedX_, &windowedY_);
            glfwGetWindowSize(window_, &windowedWidth_, &windowedHeight_);
        }
        GLFWmonitor* monitor = pickMonitor(monitorIndex);
        const GLFWvidmode* mode = monitor ? glfwGetVideoMode(monitor) : nullptr;
        if (!monitor || !mode) {
            return;
        }
        glfwSetWindowMonitor(window_, monitor, 0, 0, mode->width, mode->height, mode->refreshRate);
        glfwSetInputMode(window_, GLFW_CURSOR, GLFW_CURSOR_HIDDEN);
        fullscreen_ = true;
        monitorIndex_ = monitorIndex;
    } else if (fullscreen_) {
        glfwSetWindowMonitor(window_, nullptr, windowedX_, windowedY_, windowedWidth_, windowedHeight_, 0);
        glfwSetInputMode(window_, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
        fullscreen_ = false;
    }
}

void OutputWindow::onKey(GLFWwindow* window, int key, int, int action, int) {
    if (action != GLFW_PRESS) {
        return;
    }
    OutputWindow* self = static_cast<OutputWindow*>(glfwGetWindowUserPointer(window));
    if (!self) {
        return;
    }
    if (key == GLFW_KEY_F11) {
        self->setFullscreen(!self->fullscreen_, self->monitorIndex_);
    } else if (key == GLFW_KEY_ESCAPE) {
        if (self->fullscreen_) {
            self->setFullscreen(false, self->monitorIndex_);
        } else {
            glfwSetWindowShouldClose(window, GLFW_TRUE);
        }
    }
}

void OutputWindow::render(GLuint texture, int textureWidth, int textureHeight) {
    if (!window_) {
        return;
    }
    if (glfwWindowShouldClose(window_)) {
        close();
        return;
    }
    if (!texture || textureWidth <= 0 || textureHeight <= 0) {
        return;
    }

    GLFWwindow* previous = glfwGetCurrentContext();
    glFlush();
    glfwMakeContextCurrent(window_);

    int frameWidth = 0;
    int frameHeight = 0;
    glfwGetFramebufferSize(window_, &frameWidth, &frameHeight);
    if (frameWidth > 0 && frameHeight > 0) {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, frameWidth, frameHeight);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        const float aspect = static_cast<float>(textureWidth) / static_cast<float>(textureHeight);
        int viewWidth = frameWidth;
        int viewHeight = static_cast<int>(static_cast<float>(frameWidth) / aspect);
        if (viewHeight > frameHeight) {
            viewHeight = frameHeight;
            viewWidth = static_cast<int>(static_cast<float>(frameHeight) * aspect);
        }
        glViewport((frameWidth - viewWidth) / 2, (frameHeight - viewHeight) / 2, viewWidth, viewHeight);

        glUseProgram(program_);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, texture);
        glUniform1i(textureLocation_, 0);
        glBindVertexArray(vao_);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glBindVertexArray(0);
        glBindTexture(GL_TEXTURE_2D, 0);
        glUseProgram(0);
    }
    glfwSwapBuffers(window_);
    glfwMakeContextCurrent(previous);
}

}
