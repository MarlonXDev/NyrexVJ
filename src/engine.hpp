#pragma once

#include "gl_util.hpp"

#include <deque>
#include <vector>

namespace nvj {

enum BlendMode {
    BlendNormal = 0,
    BlendAdd,
    BlendScreen,
    BlendMultiply,
    BlendModeCount
};

struct Layer {
    std::vector<int> cells;
    int activeColumn = -1;
    float opacity = 1.0f;
    int blend = BlendNormal;
    bool bypass = false;
    bool solo = false;
    double triggerTime = 0.0;
};

struct MasterEffects {
    float hue = 0.0f;
    bool invert = false;
    bool mirrorX = false;
    bool mirrorY = false;
    float pixelate = 0.0f;
    float rgbSplit = 0.0f;
};

class Engine {
public:
    bool init(int width, int height, int layerCount, int columnCount);
    void shutdown();
    bool setResolution(int width, int height);
    void update(double dt);
    void render();
    void trigger(int layer, int column);
    void triggerColumn(int column);
    void clearLayer(int layer);
    void tap();
    void resync();

    GLuint outputTexture() const { return out_.texture; }
    int width() const { return out_.width; }
    int height() const { return out_.height; }
    double beat() const { return beat_; }

    std::vector<Layer> layers;
    int columns = 0;
    MasterEffects fx;
    float master = 1.0f;
    float bpm = 128.0f;

private:
    struct ClipProgram {
        GLuint id = 0;
        GLint time = -1;
        GLint beat = -1;
        GLint bpm = -1;
        GLint res = -1;
    };

    bool createTargets(int width, int height);
    void destroyTargets();

    std::vector<ClipProgram> clips_;
    GLuint composite_ = 0;
    GLuint final_ = 0;
    RenderTarget layerTarget_;
    RenderTarget comp_;
    RenderTarget out_;
    FullscreenTriangle triangle_;
    double time_ = 0.0;
    double beat_ = 0.0;
    std::deque<double> taps_;
};

}
