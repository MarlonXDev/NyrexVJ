#pragma once

#include "core/mask_images.hpp"
#include "core/ndi_sender.hpp"
#include "core/output_map.hpp"

#include <glad/gl.h>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

struct GLFWwindow;

namespace nvj {

struct OutputCapture {
    std::vector<uint8_t> rgb;
    int width = 0;
    int height = 0;
};

struct OutputNdiStatus {
    bool wanted = false;
    bool active = false;
    bool tooLarge = false;
    bool overBudget = false;
    int connections = 0;
    std::string error;
};

class OutputWindow {
public:
    OutputWindow() = default;
    OutputWindow(const OutputWindow&) = delete;
    OutputWindow& operator=(const OutputWindow&) = delete;
    ~OutputWindow();

    bool open(GLFWwindow* share, int index, int monitorIndex, bool fullscreen);
    void close();
    bool isOpen() const { return window_ != nullptr; }
    bool isFullscreen() const { return fullscreen_; }
    int monitorIndex() const { return monitorIndex_; }
    void setFullscreen(bool fullscreen, int monitorIndex);
    void render(GLuint texture, int textureWidth, int textureHeight, const OutputSpec& spec, bool testPattern, OutputCapture* capture = nullptr);
    bool draw(GLuint texture, int textureWidth, int textureHeight, const OutputSpec& spec, bool testPattern, int syncValue, OutputCapture* capture = nullptr);
    void swap(bool restoreContext);
    size_t gpuBytes() const;
    size_t ndiBytes() const;
    void setNdiBudget(size_t bytes) { ndiBudget_ = bytes; }
    const OutputNdiStatus& ndiStatus() const { return ndiStatus_; }

    static std::vector<std::string> monitorNames();
    static int monitorCount();

private:
    struct SliceGpu {
        GLuint vao = 0;
        GLuint vbo = 0;
        GLuint ebo = 0;
        GLsizei indexCount = 0;
        int divisions = 0;
        size_t vboBytes = 0;
        size_t eboBytes = 0;
        OutputMapping mapping;
        bool meshValid = false;
        GLuint maskTexture = 0;
        int maskWidth = 0;
        int maskHeight = 0;
        SliceMask mask;
        std::shared_ptr<const MaskBitmap> maskBitmap;
        bool maskValid = false;
    };

    static void onKey(GLFWwindow* window, int key, int scancode, int action, int mods);
    bool createResources();
    void destroyResources();
    bool ensureSlice(SliceGpu& slice);
    void releaseSlice(SliceGpu& slice);
    void releaseMask(SliceGpu& slice);
    void uploadMesh(SliceGpu& slice, const OutputMapping& map);
    void uploadMask(SliceGpu& slice, const SliceMask& mask);
    void updateNdi(const OutputSpec& spec, int width, int height);
    void releaseNdiBuffers();

    GLFWwindow* window_ = nullptr;
    GLuint program_ = 0;
    std::array<SliceGpu, kMaxSlices> slices_;
    GLint textureLocation_ = -1;
    GLint maskLocation_ = -1;
    GLint maskOnLocation_ = -1;
    GLint gainLocation_ = -1;
    GLint toneLocation_ = -1;
    GLint blackLocation_ = -1;
    GLint rectLocation_ = -1;
    GLint blendLocation_ = -1;
    GLint shapeLocation_ = -1;
    GLint brightnessLocation_ = -1;
    GLint patternLocation_ = -1;
    GLint syncLocation_ = -1;
    GLint sliceGainLocation_ = -1;
    GLint sliceToneLocation_ = -1;
    int index_ = 0;
    NdiSender ndi_;
    std::string ndiName_;
    GLuint ndiBuffers_[2] = {0, 0};
    int ndiWidth_ = 0;
    int ndiHeight_ = 0;
    int ndiIndex_ = 0;
    bool ndiPrimed_ = false;
    size_t ndiBudget_ = kOutputGpuBudgetBytes;
    OutputNdiStatus ndiStatus_;
    std::chrono::steady_clock::time_point ndiRetryAt_{};
    bool pendingSwap_ = false;
    bool fullscreen_ = false;
    int monitorIndex_ = 0;
    int windowedX_ = 100;
    int windowedY_ = 100;
    int windowedWidth_ = 1280;
    int windowedHeight_ = 720;
};

}
