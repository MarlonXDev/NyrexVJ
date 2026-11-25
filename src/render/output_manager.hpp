#pragma once

#include "core/frame_lock.hpp"
#include "core/output_map.hpp"
#include "render/output_window.hpp"
#include "render/texture_history.hpp"

#include <array>

struct GLFWwindow;

namespace nvj {

class OutputManager {
public:
    OutputManager() { markerSelected_.fill(-1); }
    void attach(std::array<OutputSpec, kMaxOutputs>* specs) { specs_ = specs; }
    std::array<OutputSpec, kMaxOutputs>& specs() { return *specs_; }
    const std::array<OutputSpec, kMaxOutputs>& specs() const { return *specs_; }

    bool open(int index, GLFWwindow* share);
    int openEnabled(GLFWwindow* share, bool fullscreen);
    void close(int index);
    void closeAll();
    bool isOpen(int index) const;
    bool anyOpen() const;
    int openCount() const;
    bool isFullscreen(int index) const;
    bool anyFullscreen() const;
    void setFullscreen(int index, bool fullscreen);
    void toggleFullscreen(GLFWwindow* share);
    void setTestPattern(int index, bool on);
    bool testPattern(int index) const;
    void setMarkers(int index, bool on, int selected);
    bool markers(int index) const;
    bool depthAvailable(int index) const;
    void render(GLuint texture, int width, int height);
    bool capture(int index, GLuint texture, int width, int height, OutputCapture& out);
    size_t gpuBytes() const;
    size_t budgetBytes() const { return budget_; }
    void setBudgetBytes(size_t bytes) { budget_ = bytes; }
    const OutputNdiStatus& ndiStatus(int index) const;
    void setFrameLock(bool on);
    bool frameLock() const { return frameLock_; }
    void setSyncBars(bool on) { syncBars_ = on; }
    bool syncBars() const { return syncBars_; }
    const SkewTracker& skew() const { return skew_; }
    void resetSkew() { skew_.reset(); }
    long long frameCount() const { return frame_; }
    bool historyActive() const { return history_.active(); }
    size_t historyBytes() const { return history_.gpuBytes(); }

private:
    static bool valid(int index) { return index >= 0 && index < kMaxOutputs; }
    GLuint sourceFor(int index, GLuint texture) const;
    int syncValueFor(int index) const;

    std::array<OutputSpec, kMaxOutputs>* specs_ = nullptr;
    std::array<OutputWindow, kMaxOutputs> windows_;
    size_t budget_ = kOutputGpuBudgetBytes;
    std::array<bool, kMaxOutputs> patterns_{};
    std::array<bool, kMaxOutputs> markers_{};
    std::array<int, kMaxOutputs> markerSelected_{};
    TextureHistory history_;
    SkewTracker skew_;
    long long frame_ = 0;
    bool frameLock_ = true;
    bool syncBars_ = false;
};

}
