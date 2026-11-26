#pragma once

#include "core/optical_flow.hpp"
#include "render/gl_util.hpp"
#include "render/video_gpu.hpp"

#include <string>

namespace nvj {

class FlowRenderer {
public:
    FlowRenderer() = default;
    ~FlowRenderer();
    FlowRenderer(const FlowRenderer&) = delete;
    FlowRenderer& operator=(const FlowRenderer&) = delete;

    bool create(const VideoGpu& gpu, std::string* error);
    void destroy();
    bool ready() const { return program_[0] != 0; }
    bool resize(int width, int height, int slots);
    void releaseTargets();
    void store(int slot, GLuint sourceTexture);
    void swapSlots();
    GLuint slotTexture(int slot) const;
    GLuint outTexture() const { return out_.texture; }
    bool setField(const flow::Field& field);
    void setNeutralField();
    GLuint drawInterpolated(float t);
    GLuint drawBlur(float shutter);
    GLuint drawVectors();
    int width() const { return width_; }
    int height() const { return height_; }
    int slots() const { return slots_; }
    unsigned long long bytes() const;
    double lastDrawMs() const { return lastDrawMs_; }

private:
    enum Program { Copy = 0, Interpolate = 1, Blur = 2, Vectors = 3, ProgramCount = 4 };

    void prepareState() const;
    void drawInto(Program program, const RenderTarget& target);

    const VideoGpu* gpu_ = nullptr;
    GLuint program_[ProgramCount] = {0, 0, 0, 0};
    GLint locTex_[ProgramCount][4] = {};
    GLint locT_ = -1;
    GLint locShutter_ = -1;
    FullscreenTriangle triangle_;
    RenderTarget slot_[2];
    RenderTarget out_;
    GLuint flowTexture_ = 0;
    int flowCols_ = 0;
    int flowRows_ = 0;
    int width_ = 0;
    int height_ = 0;
    int slots_ = 0;
    double lastDrawMs_ = 0.0;
};

}
