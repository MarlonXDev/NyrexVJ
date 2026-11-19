#include "render/output_window.hpp"

#include "render/gl_util.hpp"
#include "core/log.hpp"

#include <GLFW/glfw3.h>

#include <algorithm>

namespace nvj {

namespace {

const char* kMeshVertex = R"glsl(#version 330 core
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUv;
out vec2 vUv;
out vec2 vOut;
uniform vec4 uRect;
void main() {
    vUv = aUv;
    vOut = aPos;
    vec2 p = uRect.xy + aPos * uRect.zw;
    gl_Position = vec4(p.x * 2.0 - 1.0, 1.0 - p.y * 2.0, 0.0, 1.0);
}
)glsl";

const char* kMeshFragment = R"glsl(#version 330 core
in vec2 vUv;
in vec2 vOut;
out vec4 fragColor;
uniform sampler2D uTex;
uniform vec4 uBlend;
uniform vec2 uShape;
uniform float uBrightness;
uniform int uPattern;

float ramp(float t) {
    t = clamp(t, 0.0, 1.0);
    float s = t < 0.5 ? 0.5 * pow(2.0 * t, uShape.x) : 1.0 - 0.5 * pow(2.0 * (1.0 - t), uShape.x);
    return pow(s, 1.0 / uShape.y);
}

float gridLine(float v, float n, float w) {
    float x = v * n;
    float d = abs(fract(x - 0.5) - 0.5) / max(fwidth(x), 1e-5);
    return 1.0 - clamp(d - w, 0.0, 1.0);
}

vec3 pattern(vec2 uv) {
    vec3 c = vec3(0.04, 0.05, 0.07) + vec3(0.10) * uv.x;
    float fine = max(gridLine(uv.x, 32.0, 0.4), gridLine(uv.y, 18.0, 0.4));
    float coarse = max(gridLine(uv.x, 8.0, 0.9), gridLine(uv.y, 4.5, 0.9));
    c = mix(c, vec3(0.25, 0.45, 0.55), fine * 0.6);
    c = mix(c, vec3(1.0), coarse);
    float diagonal = max(gridLine(uv.x + uv.y * 0.5625, 1.0, 1.2), gridLine(uv.x - uv.y * 0.5625, 1.0, 1.2));
    c = mix(c, vec3(1.0, 0.55, 0.1), diagonal * 0.7);
    float cross = max(gridLine(uv.x, 2.0, 1.6), gridLine(uv.y, 2.0, 1.6));
    c = mix(c, vec3(0.1, 1.0, 0.4), cross);
    vec2 ring = (uv - 0.5) * vec2(1.7778, 1.0);
    float r = length(ring);
    c = mix(c, vec3(1.0, 0.2, 0.6), 1.0 - clamp((abs(r - 0.4) / max(fwidth(r), 1e-5)) - 1.2, 0.0, 1.0));
    return c;
}

void main() {
    vec3 c = uPattern == 1 ? pattern(vUv) : texture(uTex, vUv).rgb;
    float f = 1.0;
    if (uBlend.x > 0.0) f *= ramp(vOut.x / uBlend.x);
    if (uBlend.y > 0.0) f *= ramp((1.0 - vOut.x) / uBlend.y);
    if (uBlend.z > 0.0) f *= ramp(vOut.y / uBlend.z);
    if (uBlend.w > 0.0) f *= ramp((1.0 - vOut.y) / uBlend.w);
    fragColor = vec4(c * f * uBrightness, 1.0);
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

int OutputWindow::monitorCount() {
    int count = 0;
    glfwGetMonitors(&count);
    return count;
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

bool OutputWindow::open(GLFWwindow* share, int index, int monitorIndex, bool fullscreen) {
    if (window_) {
        return true;
    }
    const std::string title = "NyrexVJ Output " + std::to_string(index + 1);
    window_ = glfwCreateWindow(windowedWidth_, windowedHeight_, title.c_str(), nullptr, share);
    if (!window_) {
        logLine("Could not create the output window");
        return false;
    }
    glfwSetWindowUserPointer(window_, this);
    glfwSetKeyCallback(window_, &OutputWindow::onKey);
    glfwSetWindowPos(window_, 100 + 60 * index, 100 + 40 * index);
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
    logLine("Output window " + std::to_string(index + 1) + " opened");
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
    meshValid_ = false;
}

bool OutputWindow::createResources() {
    std::string error;
    program_ = buildProgram(kMeshVertex, kMeshFragment, &error);
    if (!program_) {
        logLine("Output shader failed: " + error);
        return false;
    }
    textureLocation_ = glGetUniformLocation(program_, "uTex");
    rectLocation_ = glGetUniformLocation(program_, "uRect");
    blendLocation_ = glGetUniformLocation(program_, "uBlend");
    shapeLocation_ = glGetUniformLocation(program_, "uShape");
    brightnessLocation_ = glGetUniformLocation(program_, "uBrightness");
    patternLocation_ = glGetUniformLocation(program_, "uPattern");
    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    glGenBuffers(1, &ebo_);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(MeshVertex), reinterpret_cast<void*>(0));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(MeshVertex), reinterpret_cast<void*>(2 * sizeof(float)));
    const std::vector<uint32_t> indices = buildMeshIndices();
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(indices.size() * sizeof(uint32_t)), indices.data(), GL_STATIC_DRAW);
    indexCount_ = static_cast<GLsizei>(indices.size());
    glBindVertexArray(0);
    meshValid_ = false;
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
    if (vbo_) {
        glDeleteBuffers(1, &vbo_);
        vbo_ = 0;
    }
    if (ebo_) {
        glDeleteBuffers(1, &ebo_);
        ebo_ = 0;
    }
}

void OutputWindow::uploadMesh(const OutputMapping& map) {
    if (meshValid_ && meshMap_ == map) {
        return;
    }
    const std::vector<MeshVertex> mesh = buildMesh(map);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(mesh.size() * sizeof(MeshVertex)), mesh.data(), GL_DYNAMIC_DRAW);
    meshMap_ = map;
    meshValid_ = true;
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

void OutputWindow::render(GLuint texture, int textureWidth, int textureHeight, const OutputMapping& map, bool testPattern, OutputCapture* capture) {
    if (!window_) {
        return;
    }
    if (glfwWindowShouldClose(window_)) {
        close();
        return;
    }
    if ((!texture || textureWidth <= 0 || textureHeight <= 0) && !testPattern) {
        return;
    }
    if (textureWidth <= 0 || textureHeight <= 0) {
        textureWidth = 16;
        textureHeight = 9;
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
        glDisable(GL_BLEND);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        float rect[4] = {0.0f, 0.0f, 1.0f, 1.0f};
        if (!map.fill) {
            const float aspect = (map.slice[2] * static_cast<float>(textureWidth)) / (map.slice[3] * static_cast<float>(textureHeight));
            int viewWidth = frameWidth;
            int viewHeight = static_cast<int>(static_cast<float>(frameWidth) / aspect);
            if (viewHeight > frameHeight) {
                viewHeight = frameHeight;
                viewWidth = static_cast<int>(static_cast<float>(frameHeight) * aspect);
            }
            rect[0] = static_cast<float>(frameWidth - viewWidth) * 0.5f / static_cast<float>(frameWidth);
            rect[1] = static_cast<float>(frameHeight - viewHeight) * 0.5f / static_cast<float>(frameHeight);
            rect[2] = static_cast<float>(viewWidth) / static_cast<float>(frameWidth);
            rect[3] = static_cast<float>(viewHeight) / static_cast<float>(frameHeight);
        }

        glBindVertexArray(vao_);
        uploadMesh(map);
        glUseProgram(program_);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, texture);
        glUniform1i(textureLocation_, 0);
        glUniform4f(rectLocation_, rect[0], rect[1], rect[2], rect[3]);
        glUniform4f(blendLocation_, map.blend.left, map.blend.right, map.blend.top, map.blend.bottom);
        glUniform2f(shapeLocation_, map.blend.power, map.blend.gamma);
        glUniform1f(brightnessLocation_, map.brightness);
        glUniform1i(patternLocation_, testPattern ? 1 : 0);
        glDrawElements(GL_TRIANGLES, indexCount_, GL_UNSIGNED_INT, nullptr);
        glBindVertexArray(0);
        glBindTexture(GL_TEXTURE_2D, 0);
        glUseProgram(0);

        if (capture) {
            capture->width = frameWidth;
            capture->height = frameHeight;
            capture->rgb.assign(static_cast<size_t>(frameWidth) * static_cast<size_t>(frameHeight) * 3u, 0);
            glPixelStorei(GL_PACK_ALIGNMENT, 1);
            glReadBuffer(GL_BACK);
            glReadPixels(0, 0, frameWidth, frameHeight, GL_RGB, GL_UNSIGNED_BYTE, capture->rgb.data());
        }
    }
    glfwSwapBuffers(window_);
    glfwMakeContextCurrent(previous);
}

}
