#pragma once

#include "core/mask_images.hpp"
#include "core/ndi_sender.hpp"
#include "core/output_map.hpp"
#include "core/spatial_mesh.hpp"

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
    void setMarkers(bool on, int selected) {
        markersOn_ = on;
        markerSelected_ = selected;
    }
    bool depthAvailable() const { return depthBits_ > 0; }
    const OutputNdiStatus& ndiStatus() const { return ndiStatus_; }

    static std::vector<std::string> monitorNames();
    static int monitorCount();

private:
    struct MeshProgram {
        GLuint program = 0;
        GLint texture = -1;
        GLint mask = -1;
        GLint maskOn = -1;
        GLint gain = -1;
        GLint tone = -1;
        GLint black = -1;
        GLint rect = -1;
        GLint blend = -1;
        GLint shape = -1;
        GLint brightness = -1;
        GLint pattern = -1;
        GLint sync = -1;
        GLint sliceGain = -1;
        GLint sliceTone = -1;
        GLint crop = -1;
        GLint cropOn = -1;
    };

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
        bool spatial = false;
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
    bool buildMeshProgram(MeshProgram& out, const std::string& vertex, const std::string& fragment, const char* label);
    void configureAttributes(SliceGpu& slice, bool spatial);
    void uploadMesh(SliceGpu& slice, const OutputMapping& map);
    void uploadSpatialMesh(SliceGpu& slice, const OutputMapping& map);
    void drawMarkers(const OutputSpec& spec, int frameWidth, int frameHeight);
    void uploadMask(SliceGpu& slice, const SliceMask& mask);
    void updateNdi(const OutputSpec& spec, int width, int height);
    void releaseNdiBuffers();

    GLFWwindow* window_ = nullptr;
    MeshProgram flat_;
    MeshProgram spatial_;
    GLuint markerProgram_ = 0;
    GLuint markerVao_ = 0;
    GLuint markerVbo_ = 0;
    std::array<SliceGpu, kMaxSlices> slices_;
    GLint depthBits_ = 0;
    bool markersOn_ = false;
    int markerSelected_ = -1;
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
