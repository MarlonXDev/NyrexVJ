#pragma once

#include "media/video_types.hpp"
#include "render/gl_util.hpp"

#include <array>
#include <string>
#include <vector>

namespace nvj {

struct VideoGpuCaps {
    bool detected = false;
    bool conversion = false;
    bool compressedTextures = false;
    bool pixelBuffers = false;
    std::string renderer;
    std::string note;
};

struct VideoMemory {
    unsigned long long output = 0;
    unsigned long long planes = 0;
    unsigned long long compressed = 0;
    unsigned long long staging = 0;

    unsigned long long total() const { return output + planes + compressed + staging; }
};

enum class UploadRoute { None, CpuRgba, GpuConvert, CompressedDirect };

const char* uploadRouteName(UploadRoute route);

struct UploadStats {
    double lastMs = 0.0;
    double averageMs = 0.0;
    unsigned long long lastBytes = 0;
    unsigned long long frames = 0;
};

class VideoGpu {
public:
    bool create();
    void destroy();
    const VideoGpuCaps& caps() const { return caps_; }

    struct Saved {
        GLint framebuffer = 0;
        GLint viewport[4] = {0, 0, 0, 0};
        GLint program = 0;
        GLint vertexArray = 0;
        GLint activeTexture = 0;
        GLboolean blend = GL_FALSE;
        GLboolean scissor = GL_FALSE;
        GLboolean depth = GL_FALSE;
        GLboolean cull = GL_FALSE;
    };

    Saved save() const;
    void restore(const Saved& saved) const;
    void beginConversion(const VideoFrame& frame, int outWidth, int outHeight) const;

private:
    VideoGpuCaps caps_;
    GLuint program_ = 0;
    GLuint vao_ = 0;
    GLint locY_ = -1;
    GLint locU_ = -1;
    GLint locV_ = -1;
    GLint locA_ = -1;
    GLint locMode_ = -1;
    GLint locTexScale_ = -1;
    GLint locAlphaScale_ = -1;
    GLint locYRange_ = -1;
    GLint locCRange_ = -1;
    GLint locCoeff_ = -1;
    GLint locStep_ = -1;
    GLint locTaps_ = -1;

    friend class VideoUploader;
};

class VideoUploader {
public:
    explicit VideoUploader(VideoGpu& gpu);
    ~VideoUploader();
    VideoUploader(const VideoUploader&) = delete;
    VideoUploader& operator=(const VideoUploader&) = delete;

    bool present(const VideoFrame& frame);
    void release();

    GLuint texture() const { return output_.id; }
    int width() const { return width_; }
    int height() const { return height_; }
    float scaleX() const { return scaleX_; }
    float scaleY() const { return scaleY_; }
    bool ycocg() const { return ycocg_; }
    bool hasPicture() const { return output_.id != 0 && width_ > 0; }
    VideoMemory memory() const;
    const UploadStats& stats() const { return stats_; }
    UploadRoute route() const { return route_; }

private:
    struct Texture {
        GLuint id = 0;
        int width = 0;
        int height = 0;
        GLenum internalFormat = 0;
        unsigned long long bytes = 0;
    };

    static constexpr int kRing = 1;

    bool presentRgba(const VideoFrame& frame);
    bool presentCompressed(const VideoFrame& frame);
    bool presentYuv(const VideoFrame& frame);
    void ensureTexture(Texture& texture, GLenum internalFormat, int width, int height, GLenum format, GLenum type, unsigned long long bytesPerTexel);
    void destroyTexture(Texture& texture);
    void ensureFramebuffer();
    unsigned char* stage(size_t size);
    const void* source(size_t offset) const;
    void convert(const VideoFrame& frame);

    VideoGpu& gpu_;
    Texture output_;
    std::array<Texture, 4> planes_{};
    GLuint framebuffer_ = 0;
    std::array<GLuint, kRing> buffers_{};
    std::array<size_t, kRing> bufferSizes_{};
    int slot_ = 0;
    bool staged_ = false;
    bool compressedAllocated_ = false;
    GLenum compressedFormat_ = 0;
    int width_ = 0;
    int height_ = 0;
    float scaleX_ = 1.0f;
    float scaleY_ = 1.0f;
    bool ycocg_ = false;
    UploadStats stats_;
    UploadRoute route_ = UploadRoute::None;
    std::vector<unsigned char> scratch_;
};

}
