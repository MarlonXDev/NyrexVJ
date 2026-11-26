#include "render/output_window.hpp"

#include "render/color_gpu.hpp"
#include "render/gl_util.hpp"
#include "core/log.hpp"
#include "core/mask_images.hpp"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <cmath>
#include <string>

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
uniform vec4 uSliceGain;
uniform vec4 uSliceTone;
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

vec3 correct(vec3 c, vec4 gain, vec4 tone) {
    c *= gain.rgb;
    float l = dot(c, vec3(0.2126, 0.7152, 0.0722));
    c = vec3(l) + (c - vec3(l)) * gain.a;
    c = clamp((c - 0.5) * tone.x + 0.5, 0.0, 1.0);
    c = pow(c, vec3(tone.y));
    return vec3(tone.z) + (1.0 - tone.z) * c;
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
    if (uPattern == 0 && uSliceTone.w > 0.5) c = correct(c, uSliceGain, uSliceTone);
    if (uPattern == 0 && uTone.w > 0.5) c = correct(c, uGain, uTone);
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

const char* kSpatialVertex = R"glsl(#version 330 core
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUv;
layout(location = 2) in vec3 aDwq;
out vec3 vUvq;
noperspective out vec2 vOut;
uniform vec4 uRect;
void main() {
    vUvq = vec3(aUv, aDwq.z);
    vOut = aPos;
    vec2 p = uRect.xy + aPos * uRect.zw;
    float w = aDwq.y;
    gl_Position = vec4((p.x * 2.0 - 1.0) * w, (1.0 - p.y * 2.0) * w, aDwq.x * w, w);
}
)glsl";

const char* kMarkerVertex = R"glsl(#version 330 core
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec3 aColor;
out vec3 vColor;
void main() {
    vColor = aColor;
    gl_Position = vec4(aPos, 0.0, 1.0);
}
)glsl";

const char* kMarkerFragment = R"glsl(#version 330 core
in vec3 vColor;
out vec4 fragColor;
void main() {
    fragColor = vec4(vColor, 1.0);
}
)glsl";

bool replaceOnce(std::string& text, const std::string& from, const std::string& to) {
    const size_t at = text.find(from);
    if (at == std::string::npos) {
        return false;
    }
    text.replace(at, from.size(), to);
    return true;
}

std::string flatFragmentSource() {
    std::string source = kMeshFragment;
    bool ok = replaceOnce(source, "#version 330 core\n", "#version 330 core\n" + colorGlslCommon());
    ok = replaceOnce(source, "uniform vec2 uBlack;\n", "uniform vec2 uBlack;\nuniform int uDisplayOn;\nuniform mat3 uDisplayMatrix;\nuniform int uDisplayTransfer;\n") && ok;
    ok = replaceOnce(source, "    float f = 1.0;\n", "    if (uPattern == 0 && uDisplayOn == 1) c = cmLinearToDisplay(uDisplayMatrix * cmSrgbToLinear(c), uDisplayTransfer);\n    float f = 1.0;\n") && ok;
    return ok ? source : std::string();
}

std::string spatialFragmentSource() {
    std::string source = flatFragmentSource();
    if (source.empty()) {
        return source;
    }
    bool ok = replaceOnce(source, "in vec2 vUv;\nin vec2 vOut;", "in vec3 vUvq;\nnoperspective in vec2 vOut;");
    ok = replaceOnce(source, "uniform vec2 uBlack;\n", "uniform vec2 uBlack;\nuniform vec4 uCrop;\nuniform int uCropOn;\n") && ok;
    ok = replaceOnce(source, "void main() {\n    vec3 c =", "void main() {\n    if (vUvq.z <= 1e-6) discard;\n    vec2 vUv = vUvq.xy / vUvq.z;\n    if (uCropOn == 1 && (vUv.x < uCrop.x - 1e-4 || vUv.x > uCrop.z + 1e-4 || vUv.y < uCrop.y - 1e-4 || vUv.y > uCrop.w + 1e-4)) discard;\n    vec3 c =") && ok;
    return ok ? source : std::string();
}

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
    index_ = index;
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

bool OutputWindow::buildMeshProgram(MeshProgram& out, const std::string& vertex, const std::string& fragment, const char* label) {
    std::string error;
    out = MeshProgram{};
    out.program = buildProgram(vertex, fragment, &error);
    if (!out.program) {
        logLine(std::string(label) + " shader failed: " + error);
        return false;
    }
    out.texture = glGetUniformLocation(out.program, "uTex");
    out.mask = glGetUniformLocation(out.program, "uMask");
    out.maskOn = glGetUniformLocation(out.program, "uMaskOn");
    out.rect = glGetUniformLocation(out.program, "uRect");
    out.blend = glGetUniformLocation(out.program, "uBlend");
    out.shape = glGetUniformLocation(out.program, "uShape");
    out.brightness = glGetUniformLocation(out.program, "uBrightness");
    out.pattern = glGetUniformLocation(out.program, "uPattern");
    out.sync = glGetUniformLocation(out.program, "uSync");
    out.gain = glGetUniformLocation(out.program, "uGain");
    out.tone = glGetUniformLocation(out.program, "uTone");
    out.black = glGetUniformLocation(out.program, "uBlack");
    out.sliceGain = glGetUniformLocation(out.program, "uSliceGain");
    out.sliceTone = glGetUniformLocation(out.program, "uSliceTone");
    out.crop = glGetUniformLocation(out.program, "uCrop");
    out.cropOn = glGetUniformLocation(out.program, "uCropOn");
    out.display.bind(out.program);
    return true;
}

bool OutputWindow::createResources() {
    const std::string flatFragment = flatFragmentSource();
    if (flatFragment.empty() || !buildMeshProgram(flat_, kMeshVertex, flatFragment, "Output")) {
        return false;
    }
    const std::string spatialFragment = spatialFragmentSource();
    if (spatialFragment.empty() || !buildMeshProgram(spatial_, kSpatialVertex, spatialFragment, "Spatial output")) {
        spatial_ = MeshProgram{};
        logLine("Spatial mapping is not available on this output");
    }
    std::string error;
    markerProgram_ = buildProgram(kMarkerVertex, kMarkerFragment, &error);
    if (!markerProgram_) {
        logLine("Marker shader failed: " + error);
    }
    depthBits_ = 0;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    while (glGetError() != GL_NO_ERROR) {
    }
    glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER, GL_DEPTH, GL_FRAMEBUFFER_ATTACHMENT_DEPTH_SIZE, &depthBits_);
    if (glGetError() != GL_NO_ERROR) {
        depthBits_ = 0;
    }
    if (depthBits_ <= 0) {
        logLine("This output window has no depth buffer; spatial surfaces are drawn in list order without occlusion");
    }
    return true;
}

void OutputWindow::configureAttributes(SliceGpu& slice, bool spatial) {
    glBindVertexArray(slice.vao);
    glBindBuffer(GL_ARRAY_BUFFER, slice.vbo);
    const GLsizei stride = static_cast<GLsizei>(spatial ? sizeof(SpatialVertex) : sizeof(MeshVertex));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(0));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(2 * sizeof(float)));
    if (spatial) {
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(4 * sizeof(float)));
    } else {
        glDisableVertexAttribArray(2);
    }
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, slice.ebo);
    slice.spatial = spatial;
    slice.divisions = 0;
    slice.indexCount = 0;
    slice.meshValid = false;
}

bool OutputWindow::ensureSlice(SliceGpu& slice) {
    if (slice.vao != 0) {
        return true;
    }
    glGenVertexArrays(1, &slice.vao);
    glGenBuffers(1, &slice.vbo);
    glGenBuffers(1, &slice.ebo);
    configureAttributes(slice, false);
    glBindVertexArray(0);
    slice.vboBytes = 0;
    slice.eboBytes = 0;
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
    if (ndi_.active()) {
        ndi_.stop();
    }
    releaseNdiBuffers();
    ndiStatus_ = OutputNdiStatus{};
    ndiRetryAt_ = {};
    for (SliceGpu& slice : slices_) {
        releaseSlice(slice);
    }
    if (flat_.program) {
        glDeleteProgram(flat_.program);
    }
    flat_ = MeshProgram{};
    if (spatial_.program) {
        glDeleteProgram(spatial_.program);
    }
    spatial_ = MeshProgram{};
    if (markerProgram_) {
        glDeleteProgram(markerProgram_);
        markerProgram_ = 0;
    }
    if (markerVao_) {
        glDeleteVertexArrays(1, &markerVao_);
        markerVao_ = 0;
    }
    if (markerVbo_) {
        glDeleteBuffers(1, &markerVbo_);
        markerVbo_ = 0;
    }
}

void OutputWindow::uploadSpatialMesh(SliceGpu& slice, const OutputMapping& map) {
    if (!slice.spatial) {
        configureAttributes(slice, true);
    }
    glBindVertexArray(slice.vao);
    if (slice.meshValid && slice.mapping == map) {
        return;
    }
    const SpatialMesh mesh = buildSpatialMesh(map);
    glBindBuffer(GL_ARRAY_BUFFER, slice.vbo);
    slice.vboBytes = mesh.vertices.size() * sizeof(SpatialVertex);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(slice.vboBytes), mesh.vertices.data(), GL_DYNAMIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, slice.ebo);
    slice.eboBytes = mesh.indices.size() * sizeof(uint32_t);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(slice.eboBytes), mesh.indices.data(), GL_DYNAMIC_DRAW);
    slice.indexCount = static_cast<GLsizei>(mesh.indices.size());
    slice.mapping = map;
    slice.meshValid = true;
}

void OutputWindow::uploadMesh(SliceGpu& slice, const OutputMapping& map) {
    if (slice.spatial) {
        configureAttributes(slice, false);
    }
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
    std::shared_ptr<const MaskBitmap> bitmap;
    if (maskIsImage(mask)) {
        bitmap = MaskImages::instance().get(mask.image);
    }
    if (slice.maskTexture && slice.maskValid && slice.mask == mask && slice.maskBitmap == bitmap) {
        return;
    }
    const bool soft = mask.feather > 0.0f;
    const int width = soft ? kMaskWidth / 2 : kMaskWidth;
    const int height = soft ? kMaskHeight / 2 : kMaskHeight;
    const std::vector<uint8_t> image = buildMaskImage(mask, width, height, bitmap.get());
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
    slice.maskBitmap = bitmap;
    slice.maskValid = true;
}

size_t OutputWindow::ndiBytes() const {
    return ndiBuffers_[0] != 0 ? 2u * static_cast<size_t>(ndiWidth_) * static_cast<size_t>(ndiHeight_) * 4u : 0u;
}

size_t OutputWindow::gpuBytes() const {
    size_t total = ndiBytes();
    for (const SliceGpu& slice : slices_) {
        total += slice.vboBytes + slice.eboBytes;
        if (slice.maskTexture) {
            total += static_cast<size_t>(slice.maskWidth) * static_cast<size_t>(slice.maskHeight);
        }
    }
    return total;
}

void OutputWindow::releaseNdiBuffers() {
    if (ndiBuffers_[0] != 0) {
        glDeleteBuffers(2, ndiBuffers_);
        ndiBuffers_[0] = 0;
        ndiBuffers_[1] = 0;
    }
    ndiWidth_ = 0;
    ndiHeight_ = 0;
    ndiIndex_ = 0;
    ndiPrimed_ = false;
}

void OutputWindow::updateNdi(const OutputSpec& spec, int width, int height) {
    ndiStatus_.wanted = spec.ndi;
    if (!spec.ndi) {
        if (ndi_.active()) {
            ndi_.stop();
        }
        releaseNdiBuffers();
        ndiStatus_ = OutputNdiStatus{};
        ndiRetryAt_ = {};
        return;
    }
    const std::string name = spec.ndiName.empty() ? defaultNdiName(index_) : spec.ndiName;
    if (ndi_.active() && ndiName_ != name) {
        ndi_.stop();
        releaseNdiBuffers();
    }
    if (!ndi_.active()) {
        ndiStatus_.active = false;
        ndiStatus_.connections = 0;
        const auto now = std::chrono::steady_clock::now();
        if (now < ndiRetryAt_) {
            return;
        }
        if (!NdiSender::available()) {
            ndiStatus_.error = "The NDI runtime is not installed";
            ndiRetryAt_ = now + std::chrono::seconds(3);
            return;
        }
        if (!ndi_.start(name)) {
            ndiStatus_.error = ndi_.error();
            ndiRetryAt_ = now + std::chrono::seconds(3);
            return;
        }
        ndiName_ = name;
        logLine("NDI output " + std::to_string(index_ + 1) + " started as " + name);
    }
    ndiStatus_.active = true;
    ndiStatus_.error.clear();
    ndiStatus_.connections = ndi_.connections();
    ndiStatus_.tooLarge = false;
    ndiStatus_.overBudget = false;
    if (ndiStatus_.connections <= 0) {
        releaseNdiBuffers();
        return;
    }
    const size_t needed = ndiReadbackBytes(width, height);
    if (needed == 0) {
        ndiStatus_.tooLarge = true;
        releaseNdiBuffers();
        return;
    }
    const bool allocated = ndiBuffers_[0] != 0 && ndiWidth_ == width && ndiHeight_ == height;
    if (needed > ndiBudget_) {
        ndiStatus_.overBudget = true;
        releaseNdiBuffers();
        return;
    }
    GLint previousPack = 0;
    GLint previousAlignment = 4;
    glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &previousPack);
    glGetIntegerv(GL_PACK_ALIGNMENT, &previousAlignment);
    const size_t bytes = needed / 2u;
    if (!allocated) {
        releaseNdiBuffers();
        glGenBuffers(2, ndiBuffers_);
        for (GLuint buffer : ndiBuffers_) {
            glBindBuffer(GL_PIXEL_PACK_BUFFER, buffer);
            glBufferData(GL_PIXEL_PACK_BUFFER, static_cast<GLsizeiptr>(bytes), nullptr, GL_STREAM_READ);
        }
        ndiWidth_ = width;
        ndiHeight_ = height;
        ndiIndex_ = 0;
        ndiPrimed_ = false;
    }
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadBuffer(GL_BACK);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, ndiBuffers_[ndiIndex_]);
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    if (ndiPrimed_) {
        glBindBuffer(GL_PIXEL_PACK_BUFFER, ndiBuffers_[1 - ndiIndex_]);
        const void* mapped = glMapBufferRange(GL_PIXEL_PACK_BUFFER, 0, static_cast<GLsizeiptr>(bytes), GL_MAP_READ_BIT);
        if (mapped) {
            ndi_.sendRgbaBottomUp(static_cast<const uint8_t*>(mapped), width, height);
            glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
        }
    }
    ndiIndex_ = 1 - ndiIndex_;
    ndiPrimed_ = true;
    glPixelStorei(GL_PACK_ALIGNMENT, previousAlignment);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, static_cast<GLuint>(previousPack));
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

void OutputWindow::drawMarkers(const OutputSpec& spec, int frameWidth, int frameHeight) {
    if (!markerProgram_ || frameWidth <= 0 || frameHeight <= 0) {
        return;
    }
    std::vector<float> vertices;
    const float fw = static_cast<float>(frameWidth);
    const float fh = static_cast<float>(frameHeight);
    const auto push = [&](float px, float py, const float* rgb) {
        vertices.push_back(px / fw * 2.0f - 1.0f);
        vertices.push_back(1.0f - py / fh * 2.0f);
        vertices.push_back(rgb[0]);
        vertices.push_back(rgb[1]);
        vertices.push_back(rgb[2]);
    };
    const auto line = [&](float x0, float y0, float x1, float y1, float thickness, const float* rgb) {
        const float dx = x1 - x0;
        const float dy = y1 - y0;
        const float length = std::sqrt(dx * dx + dy * dy);
        if (length < 1e-3f) {
            return;
        }
        const float nx = -dy / length * thickness * 0.5f;
        const float ny = dx / length * thickness * 0.5f;
        push(x0 + nx, y0 + ny, rgb);
        push(x0 - nx, y0 - ny, rgb);
        push(x1 + nx, y1 + ny, rgb);
        push(x1 + nx, y1 + ny, rgb);
        push(x0 - nx, y0 - ny, rgb);
        push(x1 - nx, y1 - ny, rgb);
    };
    const float target[3] = {0.1f, 1.0f, 0.4f};
    const float chosen[3] = {1.0f, 1.0f, 1.0f};
    const float off[3] = {0.45f, 0.45f, 0.45f};
    const float predicted[3] = {1.0f, 0.55f, 0.1f};
    const float gap[3] = {1.0f, 0.9f, 0.1f};
    const int count = std::min(sliceCount(spec), kMaxSlices);
    for (int i = 0; i < count; ++i) {
        if (i > 0 && !spec.slices[static_cast<size_t>(i - 1)].enabled) {
            continue;
        }
        const OutputMapping& map = sliceMapping(spec, i);
        if (!map.spatial.enabled) {
            continue;
        }
        for (size_t k = 0; k < map.spatial.points.size(); ++k) {
            const CalibrationPoint& point = map.spatial.points[k];
            const float x = point.outputX * fw;
            const float y = point.outputY * fh;
            const float* colour = !point.enabled ? off : (static_cast<int>(k) == markerSelected_ ? chosen : target);
            const ProjectedPoint p = projectPoint(map.spatial.projector, point.world);
            const bool showPrediction = point.enabled && p.inFront;
            const float px = p.x * fw;
            const float py = p.y * fh;
            if (showPrediction && std::fabs(px - x) + std::fabs(py - y) > 1.5f) {
                line(x, y, px, py, 1.5f, gap);
            }
            line(x - 14.0f, y, x + 14.0f, y, 2.0f, colour);
            line(x, y - 14.0f, x, y + 14.0f, 2.0f, colour);
            if (showPrediction) {
                line(px - 8.0f, py - 8.0f, px + 8.0f, py + 8.0f, 2.0f, predicted);
                line(px - 8.0f, py + 8.0f, px + 8.0f, py - 8.0f, 2.0f, predicted);
            }
        }
    }
    if (vertices.empty()) {
        return;
    }
    if (!markerVao_) {
        glGenVertexArrays(1, &markerVao_);
        glGenBuffers(1, &markerVbo_);
        glBindVertexArray(markerVao_);
        glBindBuffer(GL_ARRAY_BUFFER, markerVbo_);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), reinterpret_cast<void*>(0));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), reinterpret_cast<void*>(2 * sizeof(float)));
    }
    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glUseProgram(markerProgram_);
    glBindVertexArray(markerVao_);
    glBindBuffer(GL_ARRAY_BUFFER, markerVbo_);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertices.size() * sizeof(float)), vertices.data(), GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(vertices.size() / 5));
    glBindVertexArray(0);
    glUseProgram(0);
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

        const float fullRect[4] = {0.0f, 0.0f, 1.0f, 1.0f};
        const MeshProgram* activeProgram = nullptr;
        const auto useProgram = [&](const MeshProgram& program, const float* viewRect) {
            glUseProgram(program.program);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, texture);
            glUniform1i(program.texture, 0);
            glUniform1i(program.mask, 1);
            glUniform4f(program.rect, viewRect[0], viewRect[1], viewRect[2], viewRect[3]);
            glUniform1i(program.pattern, syncValue >= 0 ? 2 : (testPattern ? 1 : 0));
            glUniform1i(program.sync, std::max(syncValue, 0));
            glUniform4f(program.gain, color.red, color.green, color.blue, color.saturation);
            glUniform4f(program.tone, color.contrast, 1.0f / color.gamma, color.black, colorOn ? 1.0f : 0.0f);
            program.display.upload(spec.display);
            activeProgram = &program;
        };

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
            const bool spatialSlice = map.spatial.enabled;
            if (spatialSlice && spatial_.program == 0) {
                continue;
            }
            const MeshProgram& program = spatialSlice ? spatial_ : flat_;
            if (activeProgram != &program) {
                useProgram(program, spatialSlice ? fullRect : rect);
            }
            ensureSlice(gpu);
            if (spatialSlice) {
                uploadSpatialMesh(gpu, map);
            } else {
                uploadMesh(gpu, map);
            }
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
            glUniform1i(program.maskOn, masked ? 1 : 0);
            glUniform4f(program.blend, map.blend.left, map.blend.right, map.blend.top, map.blend.bottom);
            glUniform2f(program.shape, map.blend.power, map.blend.gamma);
            glUniform1f(program.brightness, map.brightness);
            OutputColor sliceColor = map.sliceColor;
            normalizeColor(sliceColor);
            const bool sliceColorOn = !isDefaultColor(sliceColor);
            glUniform4f(program.sliceGain, sliceColor.red, sliceColor.green, sliceColor.blue, sliceColor.saturation);
            glUniform4f(program.sliceTone, sliceColor.contrast, 1.0f / sliceColor.gamma, sliceColor.black, sliceColorOn ? 1.0f : 0.0f);
            const bool lift = map.blend.blackLevel > 0.0f && hasEdgeBlend(map.blend);
            glUniform2f(program.black, map.blend.blackLevel, lift ? 1.0f : 0.0f);
            if (spatialSlice) {
                const bool crop = map.spatial.source == SpatialSource::ViewerCamera;
                glUniform1i(program.cropOn, crop ? 1 : 0);
                glUniform4f(program.crop, map.slice[0], 1.0f - (map.slice[1] + map.slice[3]), map.slice[0] + map.slice[2], 1.0f - map.slice[1]);
                if (depthBits_ > 0) {
                    glEnable(GL_DEPTH_TEST);
                    glDepthFunc(GL_LESS);
                    glDepthMask(GL_TRUE);
                    glClear(GL_DEPTH_BUFFER_BIT);
                } else {
                    glDisable(GL_DEPTH_TEST);
                }
            } else {
                glDisable(GL_DEPTH_TEST);
            }
            glBindVertexArray(gpu.vao);
            if (gpu.indexCount > 0) {
                glDrawElements(GL_TRIANGLES, gpu.indexCount, GL_UNSIGNED_INT, nullptr);
            }
        }
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_BLEND);
        glBindVertexArray(0);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, 0);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, 0);
        glUseProgram(0);

        updateNdi(spec, frameWidth, frameHeight);

        if (markersOn_) {
            drawMarkers(spec, frameWidth, frameHeight);
        }

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
