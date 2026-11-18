#pragma once

#include "audio/audio_features.hpp"
#include "core/composition.hpp"
#include "render/gl_util.hpp"

#include <array>
#include <deque>
#include <vector>

namespace nvj {

class Engine {
public:
    bool init();
    void shutdown();
    bool setResolutionIndex(int index);
    void applyComposition(const Composition& source);
    void update(double dt);
    void render();
    void trigger(int layer, int column);
    void triggerColumn(int column);
    void clearLayer(int layer);
    void tap();
    void resync();
    void nudgeBeat(float amount);
    void setAudio(const AudioFeatures& features);
    bool readOutput(std::vector<unsigned char>& rgba, int& width, int& height);
    int prepareAll(std::string& report);
    bool thumbnailsReady() const { return nextThumb_ >= clips_.size(); }

    GLuint outputTexture() const { return out_.texture; }
    int width() const { return out_.width; }
    int height() const { return out_.height; }
    double beat() const { return beat_; }
    double time() const { return time_; }
    GLuint clipThumbnail(int clip) const;
    GLuint layerPreview(int layer) const;
    bool layerLive(int layer) const;
    const AudioFeatures& audio() const { return audio_; }

    Composition comp;
    bool blackout = false;

private:
    struct ClipProgram {
        GLuint id = 0;
        bool tried = false;
        GLint time = -1;
        GLint beat = -1;
        GLint bpm = -1;
        GLint res = -1;
        GLint colorA = -1;
        GLint colorB = -1;
        GLint rainbow = -1;
        GLint scale = -1;
        GLint audio = -1;
        GLint kick = -1;
        GLint audioTex = -1;
    };

    struct EffectProgram {
        GLuint id = 0;
        bool tried = false;
        GLint tex = -1;
        GLint prev = -1;
        GLint params = -1;
        GLint time = -1;
        GLint beat = -1;
        GLint aspect = -1;
        GLint res = -1;
        GLint audio = -1;
        GLint kick = -1;
        GLint audioTex = -1;
        bool usesPrevious = false;
    };

    struct Effective {
        float opacity = 1.0f;
        float scale = 1.0f;
        float speed = 1.0f;
        ColorGrade grade;
    };

    struct CompositeProgram {
        GLuint id = 0;
        GLint tex = -1;
        GLint opacity = -1;
        GLint mode = -1;
        GLint grade = -1;
    };

    struct FinalProgram {
        GLuint id = 0;
        GLint tex = -1;
        GLint level = -1;
        GLint grade = -1;
    };

    struct ThumbProgram {
        GLuint id = 0;
        GLint tex = -1;
        GLint texel = -1;
    };

    bool buildPrograms();
    bool createTargets(int width, int height);
    void destroyTargets();
    void destroyFeedback();
    void renderClipThumbnails(int maxCount);
    bool ensureClip(size_t index);
    bool ensureEffect(size_t index);
    void uploadAudio();
    void computeEffective();
    void setAudioUniforms(GLint audio, GLint kick, GLint audioTex);
    void drawClip(const ClipProgram& clip, float time, float beat, float bpm, float width, float height, const std::array<float, 3>& colorA, const std::array<float, 3>& colorB, bool rainbow, float scale);
    RenderTarget* runChain(const std::array<EffectSlot, kEffectSlots>& slots, int chain, RenderTarget* source, RenderTarget* scratch);
    void downsample(const RenderTarget& source, const RenderTarget& target);
    void blit(const RenderTarget& source, const RenderTarget& target);

    std::vector<ClipProgram> clips_;
    std::vector<EffectProgram> effects_;
    CompositeProgram composite_;
    FinalProgram final_;
    ThumbProgram thumb_;
    RenderTarget layerA_;
    RenderTarget layerB_;
    RenderTarget compA_;
    RenderTarget compB_;
    RenderTarget out_;
    std::vector<RenderTarget> clipThumbs_;
    size_t nextThumb_ = 0;
    std::array<RenderTarget, kLayerCount> previews_;
    std::array<bool, kLayerCount> live_{};
    std::array<RenderTarget, (kLayerCount + 1) * kEffectSlots> feedback_;
    std::array<bool, (kLayerCount + 1) * kEffectSlots> feedbackUsed_{};
    std::array<double, kLayerCount> layerClock_{};
    FullscreenTriangle triangle_;
    int currentResolution_ = -1;
    double time_ = 0.0;
    double beat_ = 0.0;
    std::deque<double> taps_;
    AudioFeatures audioInput_;
    AudioFeatures audio_;
    GLuint audioTexture_ = 0;
    std::array<Effective, kLayerCount> effective_{};
    float effectiveLevel_ = 1.0f;
    ColorGrade effectiveMasterGrade_;
};

}
