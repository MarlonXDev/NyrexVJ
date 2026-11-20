#pragma once

#include "audio/audio_features.hpp"
#include "core/composition.hpp"
#include "core/settings.hpp"
#include "render/clip_library.hpp"
#include "render/dmx_strip.hpp"
#include "render/gl_util.hpp"
#include "render/gpu_profiler.hpp"
#include "render/media_player.hpp"
#include "render/scene_player.hpp"
#include "render/spout_input.hpp"

#include <array>
#include <cstdint>
#include <deque>
#include <memory>
#include <string>
#include <vector>

namespace nvj {

struct GpuMemory {
    size_t targets = 0;
    size_t thumbnails = 0;
    size_t sceneDepth = 0;
    size_t sceneGeometry = 0;
    size_t media = 0;
    VideoMemory video;

    size_t total() const { return targets + thumbnails + sceneDepth + sceneGeometry + media; }
};

class Engine {
public:
    bool init();
    void shutdown();
    void syncLibrary();
    bool setResolutionIndex(int index);
    void applyComposition(const Composition& source);
    void update(double dt);
    void render();
    bool switchDeck(int index);
    void duplicateActiveDeckTo(int index);
    void clearDeck(int index);
    void removeDeck(int index);
    void loadLook(const Composition& look);
    bool transitioning() const { return transition_.active; }
    float transitionProgress() const;
    int transitionTarget() const { return transition_.target; }
    int transitionSource() const { return transition_.from; }
    void trigger(int layer, int column);
    void triggerColumn(int column);
    void clearLayer(int layer);
    void tap();
    void resync();
    void nudgeBeat(float amount);
    void shiftBeat(double beats);
    float sourceValue(int source) const;
    void setAudio(const AudioFeatures& features);
    bool readOutput(std::vector<unsigned char>& rgba, int& width, int& height);
    void setDmxStrip(bool enabled, bool atTop, const std::uint8_t* grid);
    bool dmxStripActive() const { return stripEnabled_; }
    int prepareAll(std::string& report);
    bool thumbnailsReady() const { return nextThumb_ >= clips_.size() && deferredThumbs_.empty() && reloadThumbs_.empty(); }
    void reloadShaderClips(const std::vector<size_t>& indices);
    std::string clipError(int clip) const;

    GLuint outputTexture() const { return out_.texture; }
    int width() const { return out_.width; }
    int height() const { return out_.height; }
    double beat() const { return beat_; }
    double time() const { return time_; }
    GLuint clipThumbnail(int clip) const;
    bool isSceneClip(int clip) const;
    ScenePlayer* scenePlayer(int clip);
    SceneRenderer& sceneRenderer() { return sceneRenderer_; }
    void reloadScene(int clip);
    int loadedSceneCount() const;
    bool isMediaClip(int clip) const;
    MediaPlayer* mediaPlayer(int clip);
    int loadedMediaCount() const;
    GpuMemory gpuMemory() const;
    void setVideoSettings(VideoDecodeMode mode, bool hapDirect, bool gpuConversion = true);
    VideoDecodeMode videoDecodeMode() const { return videoMode_; }
    bool hapDirect() const { return hapDirect_; }
    bool gpuConversion() const { return gpuConversion_; }
    const VideoGpuCaps& videoCaps() const { return videoGpu_.caps(); }
    VideoOptions videoOptions() const;
    std::vector<const MediaPlayer*> activeVideos() const;
    void setGpuTiming(bool enabled) { gpuTiming_ = enabled; }
    void setQuality(int mode, double budgetMs);
    int qualityMode() const { return qualityMode_; }
    float layerScale() const { return layerScale_; }
    int layerWidth() const { return layerScale_ < 0.999f && layerLoA_.fbo ? layerLoA_.width : out_.width; }
    int layerHeight() const { return layerScale_ < 0.999f && layerLoA_.fbo ? layerLoA_.height : out_.height; }
    bool gpuTimingAvailable() const { return gpuQueries_[0] != 0; }
    double gpuFrameMs() const { return gpuMs_; }
    void setProfiling(bool enabled) { profiler_.setEnabled(enabled); }
    bool profiling() const { return profiler_.enabled(); }
    const GpuProfiler& profiler() const { return profiler_; }
    void profileFlush() { profiler_.flushNow(); }
    GLuint layerPreview(int layer) const;
    void requestPreview(int layer, int column);
    GLuint previewTexture() const { return previewResult_ ? previewResult_->texture : 0; }
    bool layerLive(int layer) const;
    const AudioFeatures& audio() const { return audio_; }
    void configureSpout(const std::array<std::string, kSpoutInputSlots>& names, const std::string& excluded, bool flip);
    const SpoutReceiveStatus& spoutStatus(int slot) const;
    const std::vector<std::string>& spoutSenders();

    Composition comp;
    bool blackout = false;
    bool paused = false;

private:
    struct ClipProgram {
        GLuint id = 0;
        bool tried = false;
        std::string error;
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
        GLint mix = -1;
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

    struct BlendProgram {
        GLuint id = 0;
        GLint tex = -1;
        GLint back = -1;
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

    struct TransitionProgram {
        GLuint id = 0;
        GLint a = -1;
        GLint b = -1;
        GLint progress = -1;
        GLint type = -1;
        GLint aspect = -1;
        GLint time = -1;
    };

    struct SpoutProgram {
        GLuint id = 0;
        GLint tex = -1;
        GLint fit = -1;
        GLint flip = -1;
    };

    struct MediaProgram {
        GLuint id = 0;
        GLint tex = -1;
        GLint fit = -1;
        GLint premult = -1;
        GLint backdrop = -1;
        GLint texScale = -1;
        GLint ycocg = -1;
    };

    struct Transition {
        bool active = false;
        bool frozen = false;
        float elapsed = 0.0f;
        float duration = 1.0f;
        int type = 0;
        int target = 0;
        int from = 0;
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
    bool ensureScene(size_t index);
    std::unique_ptr<ScenePlayer> makeScene(const ClipDef& def) const;
    void renderSceneThumbnail(size_t index);
    void serviceScenes();
    bool ensureMedia(size_t index);
    std::unique_ptr<MediaPlayer> makeMedia(const ClipDef& def);
    void renderMediaThumbnail(size_t index);
    void serviceMedia();
    bool drawMedia(MediaPlayer& player, float scale, bool backdrop, double clock);
    bool ensureEffect(size_t index);
    void uploadAudio();
    void computeEffective();
    void computeEffectiveFor(const std::array<Layer, kLayerCount>& layers, const Master& master, std::array<Effective, kLayerCount>& effective, float& level, ColorGrade& grade) const;
    bool ensureTransitionTargets();
    void updateQuality();
    void destroyTransitionTargets();
    void endTransition();
    RenderTarget* renderDeck(const std::array<Layer, kLayerCount>& layers, const Master& master, const std::array<Effective, kLayerCount>& effective, const std::array<double, kLayerCount>& clocks, int feedbackSet, bool updatePreviews);
    void finalPass(const RenderTarget& source, const ColorGrade& grade, float level, const RenderTarget& destination);
    void transitionPass(const RenderTarget& a, const RenderTarget& b, float progress, int type, const RenderTarget& destination);
    void setAudioUniforms(GLint audio, GLint kick, GLint audioTex);
    void drawClip(const ClipProgram& clip, float time, float beat, float bpm, float width, float height, const std::array<float, 3>& colorA, const std::array<float, 3>& colorB, bool rainbow, float scale);
    RenderTarget* runChain(const std::array<EffectSlot, kEffectSlots>& slots, RenderTarget* feedback, bool* feedbackUsed, RenderTarget* source, RenderTarget* scratch, int deck, int owner);
    RenderTarget* chainFeedback(int chain) { return &feedback_[static_cast<size_t>(chain * kEffectSlots)]; }
    bool* chainUsed(int chain) { return &feedbackUsed_[static_cast<size_t>(chain * kEffectSlots)]; }
    void drawLayerSource(int clipIndex, const Layer& layer, float scale, double clock, const RenderTarget& target);
    void renderPreview();
    void pollGpuQueries();
    void downsample(const RenderTarget& source, const RenderTarget& target);
    void prepareSpout(int slot);
    bool drawSpout(int slot, float scale);
    void serviceSpout();
    void blit(const RenderTarget& source, const RenderTarget& target);

    std::vector<ClipProgram> clips_;
    std::vector<EffectProgram> effects_;
    CompositeProgram composite_;
    BlendProgram blend_;
    FinalProgram final_;
    TransitionProgram transition_program_;
    ThumbProgram thumb_;
    SpoutProgram spoutProgram_;
    MediaProgram mediaProgram_;
    VideoGpu videoGpu_;
    VideoDecodeMode videoMode_ = VideoDecodeMode::Auto;
    bool hapDirect_ = true;
    bool gpuConversion_ = true;
    std::vector<std::unique_ptr<MediaPlayer>> media_;
    std::vector<size_t> deferredMediaThumbs_;
    std::array<SpoutInput, kSpoutInputSlots> spoutSlots_;
    std::array<std::string, kSpoutInputSlots> spoutNames_;
    std::string spoutExcluded_;
    bool spoutFlip_ = false;
    std::array<double, kSpoutInputSlots> spoutLastUse_{};
    std::array<unsigned long long, kSpoutInputSlots> spoutFrame_{};
    unsigned long long frameId_ = 0;
    double clock_ = 0.0;
    std::vector<std::string> spoutSenderList_;
    double spoutListedAt_ = -1.0e9;
    RenderTarget layerA_;
    RenderTarget layerB_;
    RenderTarget compA_;
    RenderTarget compB_;
    RenderTarget out_;
    std::array<RenderTarget, 2> deckTex_;
    bool transitionTargetsReady_ = false;
    IdleRelease transitionIdle_{300};
    int qualityMode_ = QualityFull;
    double qualityBudgetMs_ = 1000.0 / 60.0;
    QualityController qualityController_;
    float layerScale_ = 1.0f;
    RenderTarget layerLoA_;
    RenderTarget layerLoB_;
    std::array<unsigned long long, kLayerCount> hiddenPreviewFrame_{};
    Transition transition_;
    std::array<Layer, kLayerCount> outLayers_;
    Master outMaster_;
    std::array<double, kLayerCount> outClocks_{};
    std::array<Effective, kLayerCount> outEffective_{};
    float outLevel_ = 1.0f;
    ColorGrade outGrade_;
    long long cycleSlot_ = -1;
    int feedbackSet_ = 0;
    std::vector<RenderTarget> clipThumbs_;
    std::vector<std::unique_ptr<ScenePlayer>> scenes_;
    SceneRenderer sceneRenderer_;
    std::vector<size_t> deferredThumbs_;
    std::vector<size_t> reloadThumbs_;
    size_t nextThumb_ = 0;
    std::array<RenderTarget, kLayerCount> previews_;
    static constexpr int kGpuQueries = 3;
    std::array<GLuint, kGpuQueries> gpuQueries_{};
    std::array<bool, kGpuQueries> gpuPending_{};
    int gpuSlot_ = 0;
    bool gpuTiming_ = true;
    bool gpuActive_ = false;
    GpuProfiler profiler_;
    double gpuMs_ = 0.0;
    RenderTarget previewA_;
    RenderTarget previewB_;
    std::array<RenderTarget, kEffectSlots> previewFeedback_;
    std::array<bool, kEffectSlots> previewFeedbackUsed_{};
    RenderTarget* previewResult_ = nullptr;
    bool previewRequested_ = false;
    int previewLayer_ = 0;
    int previewColumn_ = 0;
    int previewClip_ = -1;
    double previewClock_ = 0.0;
    double previewLastTime_ = 0.0;
    std::array<bool, kLayerCount> live_{};
    static constexpr int kChainsPerSet = kLayerCount + 1;
    static constexpr int kFeedbackSets = 2;
    std::array<RenderTarget, kFeedbackSets * kChainsPerSet * kEffectSlots> feedback_;
    std::array<bool, kFeedbackSets * kChainsPerSet * kEffectSlots> feedbackUsed_{};
    std::array<double, kLayerCount> layerClock_{};
    FullscreenTriangle triangle_;
    DmxStrip strip_;
    bool stripEnabled_ = false;
    bool stripAtTop_ = true;
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
