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
uniform sampler2D uMask;
uniform int uMaskOn;
uniform vec4 uBlend;
uniform vec2 uShape;
uniform float uBrightness;
uniform int uPattern;
uniform int uSync;
uniform vec4 uGain;
uniform vec4 uTone;
uniform vec2 uBlack;

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

vec3 syncBars(vec2 uv) {
    int column = clamp(int(floor(uv.x * 12.0)), 0, 11);
    int band = clamp(int(floor((1.0 - uv.y) * 4.0)), 0, 3);
    float gap = step(0.46, abs(fract(uv.x * 12.0) - 0.5));
    vec3 c = vec3(0.0);
    if (band == 0) {
        c = mix(vec3(float((uSync >> (11 - column)) & 1)), vec3(0.35), gap);
    } else if (band == 1) {
        c = vec3(float(uSync & 1));
    } else if (band == 2) {
        c = vec3(floor(uv.x * 60.0) == float(uSync % 60) ? 1.0 : 0.08);
    } else {
        c = vec3(0.10, 0.55, 0.30) * (1.0 - gap);
    }
    return c;
}

vec3 correct(vec3 c) {
    c *= uGain.rgb;
    float l = dot(c, vec3(0.2126, 0.7152, 0.0722));
    c = vec3(l) + (c - vec3(l)) * uGain.a;
    c = clamp((c - 0.5) * uTone.x + 0.5, 0.0, 1.0);
    c = pow(c, vec3(uTone.y));
    return vec3(uTone.z) + (1.0 - uTone.z) * c;
}

float zoneEdge(float distanceToEdge, float size, float pixel) {
    return 1.0 - clamp((distanceToEdge - size) / max(pixel, 1e-5) + 0.5, 0.0, 1.0);
}

float blendZone(vec2 p) {
    float z = 0.0;
    float px = fwidth(p.x);
    float py = fwidth(p.y);
    if (uBlend.x > 0.0) z = max(z, zoneEdge(p.x, uBlend.x, px));
    if (uBlend.y > 0.0) z = max(z, zoneEdge(1.0 - p.x, uBlend.y, px));
    if (uBlend.z > 0.0) z = max(z, zoneEdge(p.y, uBlend.z, py));
    if (uBlend.w > 0.0) z = max(z, zoneEdge(1.0 - p.y, uBlend.w, py));
    return z;
}

void main() {
    vec3 c = uPattern == 2 ? syncBars(vUv) : (uPattern == 1 ? pattern(vUv) : texture(uTex, vUv).rgb);
    if (uPattern == 0 && uTone.w > 0.5) c = correct(c);
    float f = 1.0;
    if (uBlend.x > 0.0) f *= ramp(vOut.x / uBlend.x);
    if (uBlend.y > 0.0) f *= ramp((1.0 - vOut.x) / uBlend.y);
    if (uBlend.z > 0.0) f *= ramp(vOut.y / uBlend.z);
    if (uBlend.w > 0.0) f *= ramp((1.0 - vOut.y) / uBlend.w);
    c = c * f * uBrightness;
    if (uBlack.y > 0.5) {
        float lift = uBlack.x * (1.0 - blendZone(vOut));
        c = vec3(lift) + (1.0 - lift) * c;
    }
    float a = uMaskOn == 1 ? texture(uMask, vOut).r : 1.0;
    fragColor = vec4(c, a);
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
    pendingSwap_ = false;
    logLine("Output window closed");
}

bool OutputWindow::createResources() {
    std::string error;
    program_ = buildProgram(kMeshVertex, kMeshFragment, &error);
    if (!program_) {
        logLine("Output shader failed: " + error);
        return false;
    }
    textureLocation_ = glGetUniformLocation(program_, "uTex");
    maskLocation_ = glGetUniformLocation(program_, "uMask");
    maskOnLocation_ = glGetUniformLocation(program_, "uMaskOn");
    rectLocation_ = glGetUniformLocation(program_, "uRect");
    blendLocation_ = glGetUniformLocation(program_, "uBlend");
    shapeLocation_ = glGetUniformLocation(program_, "uShape");
    brightnessLocation_ = glGetUniformLocation(program_, "uBrightness");
    patternLocation_ = glGetUniformLocation(program_, "uPattern");
    syncLocation_ = glGetUniformLocation(program_, "uSync");
    gainLocation_ = glGetUniformLocation(program_, "uGain");
    toneLocation_ = glGetUniformLocation(program_, "uTone");
    blackLocation_ = glGetUniformLocation(program_, "uBlack");
    return true;
}

bool OutputWindow::ensureSlice(SliceGpu& slice) {
    if (slice.vao != 0) {
        return true;
    }
    glGenVertexArrays(1, &slice.vao);
    glGenBuffers(1, &slice.vbo);
    glGenBuffers(1, &slice.ebo);
    glBindVertexArray(slice.vao);
    glBindBuffer(GL_ARRAY_BUFFER, slice.vbo);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(MeshVertex), reinterpret_cast<void*>(0));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(MeshVertex), reinterpret_cast<void*>(2 * sizeof(float)));
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, slice.ebo);
    glBindVertexArray(0);
    slice.divisions = 0;
    slice.indexCount = 0;
    slice.vboBytes = 0;
    slice.eboBytes = 0;
    slice.meshValid = false;
    return true;
}

void OutputWindow::releaseMask(SliceGpu& slice) {
    if (slice.maskTexture) {
        glDeleteTextures(1, &slice.maskTexture);
        slice.maskTexture = 0;
    }
    slice.maskWidth = 0;
    slice.maskHeight = 0;
    slice.maskValid = false;
    slice.mask = SliceMask{};
}

void OutputWindow::releaseSlice(SliceGpu& slice) {
    releaseMask(slice);
    if (slice.vao) {
        glDeleteVertexArrays(1, &slice.vao);
        slice.vao = 0;
    }
    if (slice.vbo) {
        glDeleteBuffers(1, &slice.vbo);
        slice.vbo = 0;
    }
    if (slice.ebo) {
        glDeleteBuffers(1, &slice.ebo);
        slice.ebo = 0;
    }
    slice = SliceGpu{};
}

void OutputWindow::destroyResources() {
    for (SliceGpu& slice : slices_) {
        releaseSlice(slice);
    }
    if (program_) {
        glDeleteProgram(program_);
        program_ = 0;
    }
}

void OutputWindow::uploadMesh(SliceGpu& slice, const OutputMapping& map) {
    const int divisions = meshDivisionsFor(map);
    if (slice.divisions != divisions) {
        const std::vector<uint32_t> indices = buildMeshIndices(divisions);
        glBindVertexArray(slice.vao);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, slice.ebo);
        slice.eboBytes = indices.size() * sizeof(uint32_t);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(slice.eboBytes), indices.data(), GL_STATIC_DRAW);
        slice.indexCount = static_cast<GLsizei>(indices.size());
        slice.divisions = divisions;
        slice.meshValid = false;
    } else {
        glBindVertexArray(slice.vao);
    }
    if (slice.meshValid && slice.mapping == map) {
        return;
    }
    const std::vector<MeshVertex> mesh = buildMesh(map, divisions);
    glBindBuffer(GL_ARRAY_BUFFER, slice.vbo);
    slice.vboBytes = mesh.size() * sizeof(MeshVertex);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(slice.vboBytes), mesh.data(), GL_DYNAMIC_DRAW);
    slice.mapping = map;
    slice.meshValid = true;
}

void OutputWindow::uploadMask(SliceGpu& slice, const SliceMask& mask) {
    if (!maskUsable(mask)) {
        releaseMask(slice);
        return;
    }
    if (slice.maskTexture && slice.maskValid && slice.mask == mask) {
        return;
    }
    const bool soft = mask.feather > 0.0f;
    const int width = soft ? kMaskWidth / 2 : kMaskWidth;
    const int height = soft ? kMaskHeight / 2 : kMaskHeight;
    const std::vector<uint8_t> image = buildMaskImage(mask, width, height);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    if (!slice.maskTexture || slice.maskWidth != width || slice.maskHeight != height) {
        if (!slice.maskTexture) {
            glGenTextures(1, &slice.maskTexture);
        }
        glBindTexture(GL_TEXTURE_2D, slice.maskTexture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, width, height, 0, GL_RED, GL_UNSIGNED_BYTE, image.data());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        slice.maskWidth = width;
        slice.maskHeight = height;
    } else {
        glBindTexture(GL_TEXTURE_2D, slice.maskTexture);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RED, GL_UNSIGNED_BYTE, image.data());
    }
    glBindTexture(GL_TEXTURE_2D, 0);
    slice.mask = mask;
    slice.maskValid = true;
}

size_t OutputWindow::gpuBytes() const {
    size_t total = 0;
    for (const SliceGpu& slice : slices_) {
        total += slice.vboBytes + slice.eboBytes;
        if (slice.maskTexture) {
            total += static_cast<size_t>(slice.maskWidth) * static_cast<size_t>(slice.maskHeight);
        }
    }
    return total;
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

void OutputWindow::render(GLuint texture, int textureWidth, int textureHeight, const OutputSpec& spec, bool testPattern, OutputCapture* capture) {
    if (draw(texture, textureWidth, textureHeight, spec, testPattern, -1, capture)) {
        swap(true);
    }
}

void OutputWindow::swap(bool restoreContext) {
    if (!window_ || !pendingSwap_) {
        return;
    }
    pendingSwap_ = false;
    GLFWwindow* previous = glfwGetCurrentContext();
    if (previous != window_) {
        glfwMakeContextCurrent(window_);
    }
    glfwSwapBuffers(window_);
    if (restoreContext && previous != window_) {
        glfwMakeContextCurrent(previous);
    }
}

bool OutputWindow::draw(GLuint texture, int textureWidth, int textureHeight, const OutputSpec& spec, bool testPattern, int syncValue, OutputCapture* capture) {
    if (!window_) {
        return false;
    }
    if (glfwWindowShouldClose(window_)) {
        close();
        return false;
    }
    const bool patternOn = testPattern || syncValue >= 0;
    if ((!texture || textureWidth <= 0 || textureHeight <= 0) && !patternOn) {
        return false;
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
        const OutputMapping& primary = spec.map;
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, frameWidth, frameHeight);
        glDisable(GL_BLEND);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        float rect[4] = {0.0f, 0.0f, 1.0f, 1.0f};
        if (!primary.fill) {
            const float aspect = (primary.slice[2] * static_cast<float>(textureWidth)) / (primary.slice[3] * static_cast<float>(textureHeight));
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

        OutputColor color = spec.color;
        normalizeColor(color);
        const bool colorOn = !isDefaultColor(color);

        glUseProgram(program_);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, texture);
        glUniform1i(textureLocation_, 0);
        glUniform1i(maskLocation_, 1);
        glUniform4f(rectLocation_, rect[0], rect[1], rect[2], rect[3]);
        glUniform1i(patternLocation_, syncValue >= 0 ? 2 : (testPattern ? 1 : 0));
        glUniform1i(syncLocation_, std::max(syncValue, 0));
        glUniform4f(gainLocation_, color.red, color.green, color.blue, color.saturation);
        glUniform4f(toneLocation_, color.contrast, 1.0f / color.gamma, color.black, colorOn ? 1.0f : 0.0f);

        const int count = std::min(sliceCount(spec), kMaxSlices);
        for (int i = 0; i < kMaxSlices; ++i) {
            SliceGpu& gpu = slices_[static_cast<size_t>(i)];
            const bool active = i < count && (i == 0 || spec.slices[static_cast<size_t>(i - 1)].enabled);
            if (!active) {
                if (gpu.vao) {
                    releaseSlice(gpu);
                }
                continue;
            }
            const OutputMapping& map = sliceMapping(spec, i);
            ensureSlice(gpu);
            uploadMesh(gpu, map);
            uploadMask(gpu, map.mask);
            const bool masked = gpu.maskTexture != 0;
            if (i > 0 || masked) {
                glEnable(GL_BLEND);
                glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE);
            } else {
                glDisable(GL_BLEND);
            }
            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D, masked ? gpu.maskTexture : 0);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, texture);
            glUniform1i(maskOnLocation_, masked ? 1 : 0);
            glUniform4f(blendLocation_, map.blend.left, map.blend.right, map.blend.top, map.blend.bottom);
            glUniform2f(shapeLocation_, map.blend.power, map.blend.gamma);
            glUniform1f(brightnessLocation_, map.brightness);
            const bool lift = map.blend.blackLevel > 0.0f && hasEdgeBlend(map.blend);
            glUniform2f(blackLocation_, map.blend.blackLevel, lift ? 1.0f : 0.0f);
            glBindVertexArray(gpu.vao);
            glDrawElements(GL_TRIANGLES, gpu.indexCount, GL_UNSIGNED_INT, nullptr);
        }
        glDisable(GL_BLEND);
        glBindVertexArray(0);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, 0);
        glActiveTexture(GL_TEXTURE0);
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
    glFlush();
    glfwMakeContextCurrent(previous);
    pendingSwap_ = true;
    return true;
}

}
