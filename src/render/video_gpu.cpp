#include "render/video_gpu.hpp"

#include "core/log.hpp"
#include "render/color_gpu.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace nvj {

namespace {

const char* kConvertVertex = R"glsl(#version 330 core
out vec2 vUv;
void main() {
    vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
    vUv = p;
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
)glsl";

const char* kConvertFragmentBody = R"glsl(in vec2 vUv;
out vec4 fragColor;
uniform sampler2D uY;
uniform sampler2D uU;
uniform sampler2D uV;
uniform sampler2D uA;
uniform int uMode;
uniform float uTexScale;
uniform float uAlphaScale;
uniform vec2 uYRange;
uniform vec2 uCRange;
uniform vec4 uCoeff;
uniform vec2 uCross;
uniform vec2 uStep;
uniform int uTaps;
uniform ivec2 uSrcSize;

vec4 convertPacked(vec2 uv) {
    ivec2 p = clamp(ivec2(uv * vec2(uSrcSize)), ivec2(0), uSrcSize - 1);
    vec4 t = texelFetch(uY, ivec2(p.x >> 1, p.y), 0);
    float y = ((((p.x & 1) == 0) ? t.g : t.a) * uTexScale - uYRange.x) * uYRange.y;
    vec2 c = (vec2(t.r, t.b) * uTexScale - uCRange.x) * uCRange.y;
    return vec4(y + uCoeff.x * c.y + uCross.x * c.x, y + uCoeff.y * c.x + uCoeff.z * c.y, y + uCoeff.w * c.x + uCross.y * c.y, 1.0);
}

vec4 convertRaw(vec2 uv) {
    if (uMode == 3) {
        return convertPacked(uv);
    }
    float y = (texture(uY, uv).r * uTexScale - uYRange.x) * uYRange.y;
    vec2 c = uMode == 1 ? texture(uU, uv).rg : vec2(texture(uU, uv).r, texture(uV, uv).r);
    c = (c * uTexScale - uCRange.x) * uCRange.y;
    vec3 rgb = vec3(y + uCoeff.x * c.y + uCross.x * c.x, y + uCoeff.y * c.x + uCoeff.z * c.y, y + uCoeff.w * c.x + uCross.y * c.y);
    float a = uMode == 2 ? texture(uA, uv).r * uAlphaScale : 1.0;
    return vec4(rgb, a);
}

vec4 convert(vec2 uv) {
    vec4 raw = convertRaw(uv);
    if (uHdrMode != 0) {
        raw.rgb = cmHdrToSdr(clamp(raw.rgb, 0.0, 1.0));
    }
    return raw;
}

void main() {
    if (uTaps > 1) {
        vec4 sum = convert(vUv + vec2(-uStep.x, -uStep.y)) + convert(vUv + vec2(uStep.x, -uStep.y)) + convert(vUv + vec2(-uStep.x, uStep.y)) + convert(vUv + vec2(uStep.x, uStep.y));
        fragColor = sum * 0.25;
    } else {
        fragColor = convert(vUv);
    }
}
)glsl";

struct MatrixWeights {
    double kr;
    double kb;
};

MatrixWeights weightsFor(int matrix) {
    if (matrix == 2) {
        return {0.2627, 0.0593};
    }
    if (matrix == 0) {
        return {0.299, 0.114};
    }
    return {0.2126, 0.0722};
}

void clearErrors() {
    while (glGetError() != GL_NO_ERROR) {
    }
}

bool probeCompressed() {
    clearErrors();
    static const unsigned char block[16] = {0};
    GLuint texture = 0;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glCompressedTexImage2D(GL_TEXTURE_2D, 0, GL_COMPRESSED_RGBA_S3TC_DXT5_EXT, 4, 4, 0, 16, block);
    bool ok = glGetError() == GL_NO_ERROR;
    glCompressedTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 4, 4, GL_COMPRESSED_RGBA_S3TC_DXT5_EXT, 16, block);
    ok = ok && glGetError() == GL_NO_ERROR;
    glCompressedTexImage2D(GL_TEXTURE_2D, 0, GL_COMPRESSED_RGB_S3TC_DXT1_EXT, 4, 4, 0, 8, block);
    ok = ok && glGetError() == GL_NO_ERROR;
    glBindTexture(GL_TEXTURE_2D, 0);
    glDeleteTextures(1, &texture);
    clearErrors();
    return ok;
}

bool probeBuffers() {
    clearErrors();
    GLuint buffer = 0;
    glGenBuffers(1, &buffer);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, buffer);
    glBufferData(GL_PIXEL_UNPACK_BUFFER, 256, nullptr, GL_STREAM_DRAW);
    void* mapped = glMapBufferRange(GL_PIXEL_UNPACK_BUFFER, 0, 256, GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_BUFFER_BIT);
    bool ok = mapped != nullptr;
    if (mapped != nullptr) {
        ok = glUnmapBuffer(GL_PIXEL_UNPACK_BUFFER) == GL_TRUE;
    }
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
    glDeleteBuffers(1, &buffer);
    ok = ok && glGetError() == GL_NO_ERROR;
    clearErrors();
    return ok;
}

}

const char* uploadRouteName(UploadRoute route) {
    switch (route) {
    case UploadRoute::CpuRgba:
        return "cpu rgba";
    case UploadRoute::GpuConvert:
        return "gpu convert";
    case UploadRoute::CompressedDirect:
        return "compressed direct";
    default:
        return "none";
    }
}

bool VideoGpu::create() {
    destroy();
    caps_ = VideoGpuCaps{};
    caps_.detected = true;
    const GLubyte* renderer = glGetString(GL_RENDERER);
    caps_.renderer = renderer != nullptr ? reinterpret_cast<const char*>(renderer) : "";
    std::string error;
    const std::string fragment = std::string("#version 330 core\n") + colorGlslCommon() + colorGlslHdr() + kConvertFragmentBody;
    program_ = buildProgram(kConvertVertex, fragment, &error);
    if (program_ != 0) {
        caps_.conversion = true;
        glGenVertexArrays(1, &vao_);
        locY_ = glGetUniformLocation(program_, "uY");
        locU_ = glGetUniformLocation(program_, "uU");
        locV_ = glGetUniformLocation(program_, "uV");
        locA_ = glGetUniformLocation(program_, "uA");
        locMode_ = glGetUniformLocation(program_, "uMode");
        locTexScale_ = glGetUniformLocation(program_, "uTexScale");
        locAlphaScale_ = glGetUniformLocation(program_, "uAlphaScale");
        locYRange_ = glGetUniformLocation(program_, "uYRange");
        locCRange_ = glGetUniformLocation(program_, "uCRange");
        locCoeff_ = glGetUniformLocation(program_, "uCoeff");
        locCross_ = glGetUniformLocation(program_, "uCross");
        locStep_ = glGetUniformLocation(program_, "uStep");
        locTaps_ = glGetUniformLocation(program_, "uTaps");
        locSrcSize_ = glGetUniformLocation(program_, "uSrcSize");
        hdrUniforms_.bind(program_);
    } else {
        caps_.note = "the picture conversion shader failed: " + error;
        logLine("Video conversion shader failed: " + error);
    }
    caps_.compressedTextures = GLAD_GL_EXT_texture_compression_s3tc != 0 && probeCompressed();
    caps_.pixelBuffers = probeBuffers();
    return caps_.conversion;
}

void VideoGpu::destroy() {
    if (program_ != 0) {
        glDeleteProgram(program_);
        program_ = 0;
    }
    if (vao_ != 0) {
        glDeleteVertexArrays(1, &vao_);
        vao_ = 0;
    }
    caps_.conversion = false;
}

VideoGpu::Saved VideoGpu::save() const {
    Saved saved;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &saved.framebuffer);
    glGetIntegerv(GL_VIEWPORT, saved.viewport);
    glGetIntegerv(GL_CURRENT_PROGRAM, &saved.program);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &saved.vertexArray);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &saved.activeTexture);
    saved.blend = glIsEnabled(GL_BLEND);
    saved.scissor = glIsEnabled(GL_SCISSOR_TEST);
    saved.depth = glIsEnabled(GL_DEPTH_TEST);
    saved.cull = glIsEnabled(GL_CULL_FACE);
    return saved;
}

void VideoGpu::restore(const Saved& saved) const {
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(saved.framebuffer));
    glViewport(saved.viewport[0], saved.viewport[1], saved.viewport[2], saved.viewport[3]);
    glUseProgram(static_cast<GLuint>(saved.program));
    glBindVertexArray(static_cast<GLuint>(saved.vertexArray));
    glActiveTexture(static_cast<GLenum>(saved.activeTexture));
    const auto set = [](GLenum capability, GLboolean on) {
        if (on) {
            glEnable(capability);
        } else {
            glDisable(capability);
        }
    };
    set(GL_BLEND, saved.blend);
    set(GL_SCISSOR_TEST, saved.scissor);
    set(GL_DEPTH_TEST, saved.depth);
    set(GL_CULL_FACE, saved.cull);
}

void VideoGpu::beginConversion(const VideoFrame& frame, int outWidth, int outHeight) const {
    glUseProgram(program_);
    glUniform1i(locY_, 0);
    glUniform1i(locU_, 1);
    glUniform1i(locV_, 2);
    glUniform1i(locA_, 3);
    int mode = frame.layout == VideoLayout::SemiPlanar ? 1 : (frame.planeCount == 4 ? 2 : 0);
    if (frame.layout == VideoLayout::PackedUyvy) {
        mode = 3;
    }
    glUniform1i(locMode_, mode);
    glUniform2i(locSrcSize_, frame.width, frame.height);
    if (hdr_ && isHdrTransfer(frame.transfer)) {
        HdrToSdr params = *hdr_;
        params.source = frame.transfer == 18 ? Transfer::Hlg : Transfer::Pq;
        params.primaries = frame.primaries == 12 ? Primaries::DisplayP3 : Primaries::Rec2020;
        hdrUniforms_.upload(&params);
    } else {
        hdrUniforms_.upload(nullptr);
    }
    const int depth = std::clamp(frame.bitDepth, 8, 16);
    const double maxCode = std::pow(2.0, depth) - 1.0;
    const double k = std::pow(2.0, depth - 8);
    double texScale = 255.0;
    if (depth > 8) {
        texScale = frame.msbAligned ? 65535.0 / std::pow(2.0, 16 - depth) : 65535.0;
    }
    glUniform1f(locTexScale_, static_cast<float>(texScale));
    glUniform1f(locAlphaScale_, static_cast<float>(texScale / maxCode));
    if (frame.fullRange) {
        glUniform2f(locYRange_, 0.0f, static_cast<float>(1.0 / maxCode));
        glUniform2f(locCRange_, static_cast<float>(128.0 * k), static_cast<float>(1.0 / maxCode));
    } else {
        glUniform2f(locYRange_, static_cast<float>(16.0 * k), static_cast<float>(1.0 / (219.0 * k)));
        glUniform2f(locCRange_, static_cast<float>(128.0 * k), static_cast<float>(1.0 / (224.0 * k)));
    }
    if (frame.matrix == 3) {
        glUniform4f(locCoeff_, 1.0f, 1.0f, 0.0f, -1.0f);
        glUniform2f(locCross_, -1.0f, -1.0f);
    } else {
        const MatrixWeights w = weightsFor(frame.matrix);
        const double kg = 1.0 - w.kr - w.kb;
        glUniform4f(locCoeff_, static_cast<float>(2.0 * (1.0 - w.kr)), static_cast<float>(-2.0 * w.kb * (1.0 - w.kb) / kg), static_cast<float>(-2.0 * w.kr * (1.0 - w.kr) / kg), static_cast<float>(2.0 * (1.0 - w.kb)));
        glUniform2f(locCross_, 0.0f, 0.0f);
    }
    const bool shrink = frame.width > outWidth * 3 / 2 || frame.height > outHeight * 3 / 2;
    glUniform1i(locTaps_, shrink ? 4 : 1);
    glUniform2f(locStep_, 0.25f / static_cast<float>(std::max(outWidth, 1)), 0.25f / static_cast<float>(std::max(outHeight, 1)));
}

VideoUploader::VideoUploader(VideoGpu& gpu) : gpu_(gpu) {}

VideoUploader::~VideoUploader() {
    release();
}

void VideoUploader::destroyTexture(Texture& texture) {
    if (texture.id != 0) {
        glDeleteTextures(1, &texture.id);
    }
    texture = Texture{};
}

void VideoUploader::release() {
    destroyTexture(output_);
    for (Texture& plane : planes_) {
        destroyTexture(plane);
    }
    if (framebuffer_ != 0) {
        glDeleteFramebuffers(1, &framebuffer_);
        framebuffer_ = 0;
    }
    for (size_t i = 0; i < buffers_.size(); ++i) {
        if (buffers_[i] != 0) {
            glDeleteBuffers(1, &buffers_[i]);
            buffers_[i] = 0;
        }
        bufferSizes_[i] = 0;
    }
    compressedAllocated_ = false;
    width_ = 0;
    height_ = 0;
    scratch_.clear();
    scratch_.shrink_to_fit();
    route_ = UploadRoute::None;
}

VideoMemory VideoUploader::memory() const {
    VideoMemory memory;
    if (output_.id != 0) {
        if (compressedAllocated_) {
            memory.compressed = output_.bytes;
        } else {
            memory.output = output_.bytes;
        }
    }
    for (const Texture& plane : planes_) {
        memory.planes += plane.bytes;
    }
    for (const size_t size : bufferSizes_) {
        memory.staging += size;
    }
    return memory;
}

void VideoUploader::ensureTexture(Texture& texture, GLenum internalFormat, int width, int height, GLenum format, GLenum type, unsigned long long bytesPerTexel) {
    if (texture.id != 0 && texture.width == width && texture.height == height && texture.internalFormat == internalFormat) {
        return;
    }
    destroyTexture(texture);
    glGenTextures(1, &texture.id);
    glBindTexture(GL_TEXTURE_2D, texture.id);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(internalFormat), width, height, 0, format, type, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    texture.width = width;
    texture.height = height;
    texture.internalFormat = internalFormat;
    texture.bytes = static_cast<unsigned long long>(width) * static_cast<unsigned long long>(height) * bytesPerTexel;
}

void VideoUploader::ensureFramebuffer() {
    if (framebuffer_ == 0) {
        glGenFramebuffers(1, &framebuffer_);
    }
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, framebuffer_);
    glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, output_.id, 0);
}

unsigned char* VideoUploader::stage(size_t size) {
    staged_ = false;
    if (gpu_.caps().pixelBuffers) {
        slot_ = (slot_ + 1) % kRing;
        const size_t index = static_cast<size_t>(slot_);
        if (buffers_[index] == 0) {
            glGenBuffers(1, &buffers_[index]);
        }
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, buffers_[index]);
        glBufferData(GL_PIXEL_UNPACK_BUFFER, static_cast<GLsizeiptr>(size), nullptr, GL_STREAM_DRAW);
        bufferSizes_[index] = size;
        void* mapped = glMapBufferRange(GL_PIXEL_UNPACK_BUFFER, 0, static_cast<GLsizeiptr>(size), GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_BUFFER_BIT | GL_MAP_UNSYNCHRONIZED_BIT);
        if (mapped != nullptr) {
            staged_ = true;
            return static_cast<unsigned char*>(mapped);
        }
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
    }
    if (scratch_.size() < size) {
        scratch_.resize(size);
    }
    return scratch_.data();
}

const void* VideoUploader::source(size_t offset) const {
    if (staged_) {
        return reinterpret_cast<const void*>(static_cast<uintptr_t>(offset));
    }
    return scratch_.data() + offset;
}

namespace {

size_t tightBytes(const VideoPlane& plane) {
    return static_cast<size_t>(plane.width) * static_cast<size_t>(plane.channels) * static_cast<size_t>(plane.bytes) * static_cast<size_t>(plane.height);
}

size_t compressedBytes(const VideoPlane& plane) {
    return static_cast<size_t>(plane.stride) * static_cast<size_t>((plane.height + 3) / 4);
}

size_t alignUp(size_t value) {
    return (value + 63) & ~static_cast<size_t>(63);
}

void copyTight(const VideoPlane& plane, unsigned char* destination) {
    const size_t row = static_cast<size_t>(plane.width) * static_cast<size_t>(plane.channels) * static_cast<size_t>(plane.bytes);
    if (static_cast<size_t>(plane.stride) == row) {
        std::memcpy(destination, plane.data, row * static_cast<size_t>(plane.height));
        return;
    }
    for (int y = 0; y < plane.height; ++y) {
        std::memcpy(destination + static_cast<size_t>(y) * row, plane.data + static_cast<size_t>(y) * static_cast<size_t>(plane.stride), row);
    }
}

}

namespace {

std::atomic<unsigned long long> g_streamedBytes{0};

}

unsigned long long streamedUploadBytes() {
    return g_streamedBytes.load(std::memory_order_relaxed);
}

bool VideoUploader::present(const VideoFrame& frame) {
    const auto begin = std::chrono::steady_clock::now();
    bool ok = false;
    unsigned long long bytes = 0;
    for (int i = 0; i < frame.planeCount; ++i) {
        const VideoPlane& plane = frame.planes[static_cast<size_t>(i)];
        bytes += frame.layout == VideoLayout::Compressed ? compressedBytes(plane) : tightBytes(plane);
    }
    switch (frame.layout) {
    case VideoLayout::Rgba:
        ok = presentRgba(frame);
        break;
    case VideoLayout::Compressed:
        ok = presentCompressed(frame);
        break;
    default:
        ok = presentYuv(frame);
        break;
    }
    if (staged_) {
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
        staged_ = false;
    }
    if (ok) {
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count();
        stats_.lastMs = ms;
        stats_.averageMs = stats_.frames == 0 ? ms : stats_.averageMs * 0.9 + ms * 0.1;
        samples_.add(ms);
        const FrameSummary spread = samples_.summary();
        stats_.p95Ms = spread.p95;
        stats_.maxMs = spread.max;
        stats_.lastBytes = bytes;
        ++stats_.frames;
        g_streamedBytes.fetch_add(bytes, std::memory_order_relaxed);
    }
    return ok;
}

bool VideoUploader::presentRgba(const VideoFrame& frame) {
    const VideoPlane& plane = frame.planes[0];
    if (plane.data == nullptr || plane.width <= 0 || plane.height <= 0) {
        return false;
    }
    if (compressedAllocated_) {
        destroyTexture(output_);
        compressedAllocated_ = false;
    }
    ensureTexture(output_, GL_RGBA8, plane.width, plane.height, GL_RGBA, GL_UNSIGNED_BYTE, 4);
    unsigned char* memory = stage(tightBytes(plane));
    copyTight(plane, memory);
    if (staged_) {
        glUnmapBuffer(GL_PIXEL_UNPACK_BUFFER);
    }
    glBindTexture(GL_TEXTURE_2D, output_.id);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, plane.width, plane.height, GL_RGBA, GL_UNSIGNED_BYTE, source(0));
    glBindTexture(GL_TEXTURE_2D, 0);
    width_ = plane.width;
    height_ = plane.height;
    scaleX_ = 1.0f;
    scaleY_ = 1.0f;
    ycocg_ = false;
    route_ = UploadRoute::CpuRgba;
    return true;
}

bool VideoUploader::presentCompressed(const VideoFrame& frame) {
    if (!gpu_.caps().compressedTextures) {
        return false;
    }
    const VideoPlane& plane = frame.planes[0];
    if (plane.data == nullptr || plane.width <= 0 || plane.height <= 0) {
        return false;
    }
    const GLenum format = frame.compressed == CompressedFormat::Dxt1 ? GL_COMPRESSED_RGB_S3TC_DXT1_EXT : GL_COMPRESSED_RGBA_S3TC_DXT5_EXT;
    const size_t size = compressedBytes(plane);
    unsigned char* memory = stage(size);
    std::memcpy(memory, plane.data, size);
    if (staged_) {
        glUnmapBuffer(GL_PIXEL_UNPACK_BUFFER);
    }
    const bool recreate = output_.id == 0 || !compressedAllocated_ || output_.width != plane.width || output_.height != plane.height || compressedFormat_ != format;
    if (recreate) {
        destroyTexture(output_);
        glGenTextures(1, &output_.id);
        glBindTexture(GL_TEXTURE_2D, output_.id);
        glCompressedTexImage2D(GL_TEXTURE_2D, 0, format, plane.width, plane.height, 0, static_cast<GLsizei>(size), source(0));
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        output_.width = plane.width;
        output_.height = plane.height;
        output_.internalFormat = format;
        output_.bytes = size;
        compressedAllocated_ = true;
        compressedFormat_ = format;
    } else {
        glBindTexture(GL_TEXTURE_2D, output_.id);
        glCompressedTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, plane.width, plane.height, format, static_cast<GLsizei>(size), source(0));
    }
    glBindTexture(GL_TEXTURE_2D, 0);
    for (Texture& unused : planes_) {
        destroyTexture(unused);
    }
    width_ = frame.width;
    height_ = frame.height;
    scaleX_ = static_cast<float>(frame.width) / static_cast<float>(plane.width);
    scaleY_ = static_cast<float>(frame.height) / static_cast<float>(plane.height);
    ycocg_ = frame.compressed == CompressedFormat::YcoCgDxt5;
    route_ = UploadRoute::CompressedDirect;
    return true;
}

bool VideoUploader::presentYuv(const VideoFrame& frame) {
    const bool packed = frame.layout == VideoLayout::PackedUyvy;
    if (!gpu_.caps().conversion || frame.planeCount < (packed ? 1 : 2) || frame.outWidth <= 0 || frame.outHeight <= 0) {
        return false;
    }
    if (packed && (frame.width <= 0 || frame.height <= 0 || (frame.width & 1) != 0 || frame.planes[0].width != frame.width / 2 || frame.planes[0].height != frame.height || frame.planes[0].channels != 4)) {
        return false;
    }
    if (compressedAllocated_) {
        destroyTexture(output_);
        compressedAllocated_ = false;
    }
    struct PlaneFormat {
        GLenum internal;
        GLenum format;
        GLenum type;
    };
    PlaneFormat formats[4];
    size_t offsets[4] = {0, 0, 0, 0};
    size_t total = 0;
    for (int i = 0; i < frame.planeCount; ++i) {
        const VideoPlane& plane = frame.planes[static_cast<size_t>(i)];
        if (plane.data == nullptr || plane.width <= 0 || plane.height <= 0) {
            return false;
        }
        if (plane.bytes == 1 && plane.channels == 4) {
            formats[i] = PlaneFormat{GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE};
        } else if (plane.bytes == 1) {
            formats[i] = plane.channels == 2 ? PlaneFormat{GL_RG8, GL_RG, GL_UNSIGNED_BYTE} : PlaneFormat{GL_R8, GL_RED, GL_UNSIGNED_BYTE};
        } else {
            formats[i] = plane.channels == 2 ? PlaneFormat{GL_RG16, GL_RG, GL_UNSIGNED_SHORT} : PlaneFormat{GL_R16, GL_RED, GL_UNSIGNED_SHORT};
        }
        offsets[i] = total;
        total += alignUp(tightBytes(plane));
    }
    for (int i = 0; i < frame.planeCount; ++i) {
        const VideoPlane& plane = frame.planes[static_cast<size_t>(i)];
        ensureTexture(planes_[static_cast<size_t>(i)], formats[i].internal, plane.width, plane.height, formats[i].format, formats[i].type, static_cast<unsigned long long>(plane.channels * plane.bytes));
    }
    for (size_t i = static_cast<size_t>(frame.planeCount); i < planes_.size(); ++i) {
        destroyTexture(planes_[i]);
    }
    ensureTexture(output_, GL_RGBA8, frame.outWidth, frame.outHeight, GL_RGBA, GL_UNSIGNED_BYTE, 4);
    unsigned char* memory = stage(total);
    for (int i = 0; i < frame.planeCount; ++i) {
        copyTight(frame.planes[static_cast<size_t>(i)], memory + offsets[i]);
    }
    if (staged_) {
        glUnmapBuffer(GL_PIXEL_UNPACK_BUFFER);
    }
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    for (int i = 0; i < frame.planeCount; ++i) {
        const VideoPlane& plane = frame.planes[static_cast<size_t>(i)];
        glBindTexture(GL_TEXTURE_2D, planes_[static_cast<size_t>(i)].id);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, plane.width, plane.height, formats[i].format, formats[i].type, source(offsets[i]));
    }
    glBindTexture(GL_TEXTURE_2D, 0);
    if (staged_) {
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
        staged_ = false;
    }
    convert(frame);
    width_ = frame.outWidth;
    height_ = frame.outHeight;
    scaleX_ = 1.0f;
    scaleY_ = 1.0f;
    ycocg_ = false;
    route_ = UploadRoute::GpuConvert;
    return true;
}

void VideoUploader::convert(const VideoFrame& frame) {
    const VideoGpu::Saved saved = gpu_.save();
    ensureFramebuffer();
    glViewport(0, 0, frame.outWidth, frame.outHeight);
    glDisable(GL_BLEND);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    gpu_.beginConversion(frame, frame.outWidth, frame.outHeight);
    for (int unit = 0; unit < 4; ++unit) {
        glActiveTexture(GL_TEXTURE0 + static_cast<GLenum>(unit));
        const Texture& texture = unit < frame.planeCount ? planes_[static_cast<size_t>(unit)] : planes_[0];
        glBindTexture(GL_TEXTURE_2D, texture.id);
    }
    glBindVertexArray(gpu_.vao_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    for (int unit = 3; unit >= 0; --unit) {
        glActiveTexture(GL_TEXTURE0 + static_cast<GLenum>(unit));
        glBindTexture(GL_TEXTURE_2D, 0);
    }
    gpu_.restore(saved);
}

}
