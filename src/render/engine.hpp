#pragma once

#include "audio/audio_features.hpp"
#include "core/beat_clock.hpp"
#include "core/color_lut.hpp"
#include "core/color_model.hpp"
#include "core/composition.hpp"
#include "core/gpu_diag.hpp"
#include "core/hardware_sync.hpp"
#include "core/launch_quantizer.hpp"
#include "core/optical_flow.hpp"
#include "core/person_mask.hpp"
#include "core/routing.hpp"
#include "core/settings.hpp"
#include "core/shader_editor.hpp"
#include "render/clip_library.hpp"
#include "render/color_gpu.hpp"
#include "render/color_stage.hpp"
#include "render/dmx_strip.hpp"
#include "render/gl_util.hpp"
#include "render/person_session.hpp"
#include "render/gpu_profiler.hpp"
#include "render/live_input.hpp"
#include "render/generators.hpp"
#include "render/media_player.hpp"
#include "render/scene_player.hpp"
#include "render/shader_passes.hpp"
#include "render/spout_input.hpp"

#include <array>
#include <chrono>
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace nvj {

struct ShaderPlaygroundStatus {
    bool active = false;
    bool hasWorking = false;
    bool compiling = false;
    bool parallelCompile = false;
    bool filter = false;
    bool alpha = false;
    bool paused = false;
    bool throttled = false;
    int interval = 1;
    double previewMs = 0.0;
    double lastCompileMs = 0.0;
    std::string error;
    std::string runtimeError;
    std::string note;
    bool shadertoy = false;
    bool advanced = false;
    int passCount = 1;
    std::uint64_t workingRevision = 0;
    std::uint64_t attemptedRevision = 0;
    unsigned long long compiles = 0;
    unsigned long long failures = 0;
    unsigned long long frames = 0;
};

struct GpuMemory {
    size_t targets = 0;
    size_t thumbnails = 0;
    size_t sceneDepth = 0;
    size_t sceneGeometry = 0;
    size_t media = 0;
    size_t live = 0;
    size_t shaderBuffers = 0;
    size_t generators = 0;
    size_t routing = 0;
    VideoMemory video;

    size_t total() const { return targets + thumbnails + sceneDepth + sceneGeometry + media + live + shaderBuffers + generators + routing; }
};

struct RouteSlotStatus {
    bool captured = false;
    bool fresh = false;
    int width = 0;
    int height = 0;
    int copies = 0;
    int references = 0;
    size_t bytes = 0;
    GLuint preview = 0;
};

struct MemoryPolicy {
    std::uint64_t budgetBytes = 0;
    bool trim = true;
    bool lowerQuality = false;
};

struct Diagnostics {
    gpudiag::DriverMemory driver;
    std::uint64_t estimateBytes = 0;
    std::uint64_t budgetBytes = 0;
    std::uint64_t residentBytes = 0;
    std::uint64_t evictableBytes = 0;
    std::uint64_t releasedBytes = 0;
    std::uint64_t releasedCount = 0;
    int pressureLevel = 0;
    bool trimming = false;
    double uploadBytesPerSecond = 0.0;
    double uploadPeakBytesPerSecond = 0.0;
    std::uint64_t uploadTotalBytes = 0;
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
    std::uint32_t triggerCount(int layer) const { return layer >= 0 && layer < kLayerCount ? triggerCounts_[static_cast<size_t>(layer)] : 0; }
    void triggerColumn(int column);
    void clearLayer(int layer);
    void requestTrigger(int layer, int column);
    void requestTriggerColumn(int column);
    bool requestSwitchDeck(int index);
    void setQuantizeGrid(QuantizeGrid grid);
    LaunchQuantizer& quantizer() { return quantizer_; }
    const LaunchQuantizer& quantizer() const { return quantizer_; }
    QuantizeClock quantizeClock() const;
    void tap();
    void resync();
    void nudgeBeat(float amount);
    void shiftBeat(double beats);
    float sourceValue(int source) const;
    void setPluginHost(PluginHost* host) { plugins_ = host; }
    PluginHost* pluginHost() const { return plugins_; }
    void setAudio(const AudioFeatures& features);
    bool readOutput(std::vector<unsigned char>& rgba, int& width, int& height);
    void setDmxStrip(bool enabled, bool atTop, const std::uint8_t* grid);
    bool dmxStripActive() const { return stripEnabled_; }
    int prepareAll(std::string& report);
    bool thumbnailsReady() const { return nextThumb_ >= clips_.size() && deferredThumbs_.empty() && reloadThumbs_.empty(); }
    void reloadShaderClips(const std::vector<size_t>& indices);
    void reloadGeneratorClips(const std::vector<size_t>& indices);
    void setParticleLimit(int limit);
    int particleLimit() const { return particleLimit_; }
    void setGpuParticles(bool enabled, int budgetMb);
    bool gpuParticlesEnabled() const { return gpuParticles_; }
    struct ColorStageStatus {
        bool requested = false;
        bool active = false;
        bool lutLoaded = false;
        int lutSize = 0;
        std::string error;
    };
    void setColorStage(bool enabled, const CubeLut* lut, float strength);
    void setColorStrength(float strength);
    bool colorStageActive() const { return colorActive_; }
    FramebufferDepth outputDepth() const;
    const ColorStageStatus& colorStageStatus() const { return colorStatus_; }
    std::uint64_t colorStageBytes() const;
    void setHdrToSdr(const std::optional<HdrToSdr>& params) { videoGpu_.setHdr(params); }
    const std::optional<HdrToSdr>& hdrToSdr() const { return videoGpu_.hdr(); }
    int gpuParticleBudgetMb() const { return gpuParticleBudgetMb_; }
    std::uint64_t gpuParticleBytes() const { return generatorGl_.gpuParticles.budget->used(); }
    bool isGeneratorClip(int clip) const;
    GeneratorPlayer* generatorPlayer(int clip);
    void requestTextPreview(const text::TextDoc& doc, double clock);
    void requestParticlePreview(const particles::Params& params, double clock);
    void requestChainPreview(const std::array<EffectSlot, kEffectSlots>& slots, double clock);
    GLuint generatorPreviewTexture() const { return generatorPreviewReady_ ? generatorPreview_.texture : 0; }
    GLuint chainPreviewTexture() const { return chainPreviewResult_ ? chainPreviewResult_->texture : 0; }
    GeneratorGl& generatorGl() { return generatorGl_; }
    TextPlayer* textEditorPlayer() { return textEditor_.get(); }
    ParticlePlayer* particleEditorPlayer() { return particleEditor_.get(); }
    void syncEffects();
    void reloadNodeEffects(const std::vector<size_t>& indices);
    int checkNodeCatalogue(std::string& report);
    std::string effectError(int effect) const;
    std::string clipError(int clip) const;
    int playingFaults(std::vector<std::string>* names = nullptr);

    GLuint outputTexture() const { return out_.texture; }
    int width() const { return out_.width; }
    int height() const { return out_.height; }
    double beat() const { return beat_; }
    BeatClock& beatClock() { return beatClock_; }
    const BeatClock& beatClock() const { return beatClock_; }
    void setTimecode(bool valid, double seconds);
    bool timecodeValid() const { return timecodeValid_; }
    double timecodeSeconds() const { return timecodeSeconds_; }
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
    void setMemoryPolicy(const MemoryPolicy& policy);
    const MemoryPolicy& memoryPolicy() const { return memoryPolicy_; }
    const Diagnostics& diagnostics() const { return diagnostics_; }
    const HardwareSyncStatus& hardwareSync() const { return hardwareSync_; }
    bool memoryTrimming() const { return memoryPolicy_.budgetBytes > 0 && memoryPolicy_.trim; }
    void refreshDiagnostics();
    std::string shaderNote(int clip) const;
    std::string shaderRuntimeError(int clip) const;
    size_t shaderBufferBytes() const;
    void setShaderBufferBudget(unsigned long long bytes);
    void requestShaderPlayground(double clock);
    void submitShaderPlayground(const std::string& text, std::uint64_t revision);
    void restartShaderPlayground();
    void resumeShaderPlayground();
    void releaseShaderPlayground();
    const ShaderPlaygroundStatus& shaderPlayground() const { return playground_.status; }
    GLuint shaderPlaygroundTexture() const { return playground_.hasFrame ? playground_.target.texture : 0; }
    int shaderPlaygroundWidth() const { return playground_.target.width; }
    int shaderPlaygroundHeight() const { return playground_.target.height; }
    void setVideoSettings(VideoDecodeMode mode, bool hapDirect, bool gpuConversion = true);
    VideoDecodeMode videoDecodeMode() const { return videoMode_; }
    bool hapDirect() const { return hapDirect_; }
    bool gpuConversion() const { return gpuConversion_; }
    const VideoGpuCaps& videoCaps() const { return videoGpu_.caps(); }
    VideoOptions videoOptions() const;
    void setFlowSettings(const flow::Settings& settings);
    void setPersonSettings(const person::Settings& settings);
    const person::Settings& personSettings() const { return personSettings_; }
    PersonSession* personSession() { return person_.get(); }
    const PersonSession* personSession() const { return person_.get(); }
    const std::string& personNote() const { return personNote_; }
    void setPersonFeedExternal(bool external) { personExternal_ = external; }
    const flow::Settings& flowSettings() const { return flowSettings_; }
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
    void configureLive(const std::array<std::string, kLiveInputSlots>& ndi, const std::array<std::string, kLiveInputSlots>& camera, const std::string& excludedNdi, const CameraMode& mode, const std::array<std::string, kLiveInputSlots>& streams = {}, stream::Profile streamProfile = stream::Profile::Balanced);
    const LiveInput* liveInput(LiveKind kind, int slot) const;
    LiveInputInfo liveInfo(LiveKind kind, int slot) const;
    void resetLiveLatency();
    void setSenderSource(routing::SenderKind kind, const routing::Source& source);
    routing::Source senderSource(routing::SenderKind kind) const;
    GLuint routedTexture(routing::SenderKind kind) const;
    bool senderSourceLive(routing::SenderKind kind) const;
    void setRoutePreviewsWanted(bool wanted) { routePreviewsWanted_ = wanted; }
    RouteSlotStatus routeSlot(int slot) const;
    const routing::Plan& routePlan() const { return routePlan_; }
    size_t routeBytes() const;
    int routeCapturesAlive() const;
    unsigned long long routeCaptureCount() const { return routeCaptureCount_; }
    unsigned long long frameCounter() const { return frameId_; }

    Composition comp;
    bool blackout = false;
    bool holdOutput = false;
    bool paused = false;
    bool prewarmNext = true;

private:
    struct PassProgram {
        GLuint id = 0;
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
        GLint frame = -1;
        GLint delta = -1;
        GLint mix = -1;
        GLint picture = -1;
        std::array<GLint, kMaxShaderParams> param{-1, -1, -1, -1, -1, -1, -1, -1};
        std::array<GLint, kShaderChannels> channel{};
        std::array<GLint, kShaderChannels> channelRes{};
        std::array<GLint, kShaderChannels> channelTime{};
    };

    struct ClipProgram {
        GLuint id = 0;
        bool tried = false;
        std::string error;
        std::string runtimeError;
        std::string note;
        std::vector<PassProgram> passes;
        bool ready() const { return id != 0 || !passes.empty(); }
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
        GLint macros = -1;
        GLint person = -1;
        GLint personOn = -1;
        GLint personFit = -1;
        bool usesPrevious = false;
        std::vector<PassProgram> passes;
        std::shared_ptr<const ShaderPlan> plan;
        std::string error;
        std::string runtimeError;
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
    bool ensureGenerator(size_t index);
    std::unique_ptr<GeneratorPlayer> makeGenerator(const ClipDef& def) const;
    void renderGeneratorThumbnail(size_t index);
    void serviceGenerators();
    bool drawGenerator(size_t index, int instance, const Layer& layer, float scale, double clock, const RenderTarget& target);
    GeneratorFrame generatorFrame(const Layer& layer, float scale, double clock, int width, int height) const;
    bool drawMedia(MediaPlayer& player, float scale, bool backdrop, double clock, const ClipPlayback* playback = nullptr);
    void prewarmMedia();
    bool ensureEffect(size_t index);
    void uploadAudio();
    void computeEffective();
    void pushCompTempo();
    void pullClockTempo();
    void computeEffectiveFor(const std::array<Layer, kLayerCount>& layers, const Master& master, std::array<Effective, kLayerCount>& effective, float& level, ColorGrade& grade) const;
    bool ensureTransitionTargets();
    void updateQuality();
    void destroyTransitionTargets();
    void endTransition();
    void runLaunch(const LaunchRequest& request);
    RenderTarget* renderDeck(const std::array<Layer, kLayerCount>& layers, const Master& master, const std::array<Effective, kLayerCount>& effective, const std::array<double, kLayerCount>& clocks, int feedbackSet, bool updatePreviews);
    void finalPass(const RenderTarget& source, const ColorGrade& grade, float level, const RenderTarget& destination);
    void transitionPass(const RenderTarget& a, const RenderTarget& b, float progress, int type, const RenderTarget& destination);
    void setAudioUniforms(GLint audio, GLint kick, GLint audioTex);
    void drawClip(const ClipProgram& clip, float time, float beat, float bpm, float width, float height, const std::array<float, 3>& colorA, const std::array<float, 3>& colorB, bool rainbow, float scale);
    RenderTarget* runChain(const std::array<EffectSlot, kEffectSlots>& slots, RenderTarget* feedback, bool* feedbackUsed, RenderTarget* source, RenderTarget* scratch, int deck, int owner);
    RenderTarget* chainFeedback(int chain) { return &feedback_[static_cast<size_t>(chain * kEffectSlots)]; }
    bool* chainUsed(int chain) { return &feedbackUsed_[static_cast<size_t>(chain * kEffectSlots)]; }
    void drawLayerSource(int clipIndex, int instance, const Layer& layer, const ClipPlayback& playback, float scale, double clock, const RenderTarget& target);
    void releaseClipProgram(ClipProgram& clip);
    bool buildAdvancedClip(size_t index, const ClipDef& def, ClipProgram& clip);
    bool buildPlanPasses(const ShaderPlan& plan, bool shadertoy, const std::string& name, std::vector<PassProgram>& passes, std::string& errors);
    PassProgram passProgramFor(GLuint program, const ShaderPlan& plan) const;
    void servicePlayground();
    void startPlaygroundBuild();
    bool pollPlaygroundBuild();
    void finishPlaygroundBuild(bool success, const std::string& errors);
    void drawPlayground();
    void destroyPlaygroundBuild();
    RenderTarget* runPlanEffect(size_t effectIndex, int stateKey, const EffectSlot& slot, const std::array<float, kParamCount>& params, float mixAmount, RenderTarget* in, RenderTarget* out);
    bool allocateShaderState(ShaderPassState& state, size_t clipIndex, const ShaderPlan& plan, int width, int height, std::string& error, std::string& note);
    void drawShaderInstance(size_t clipIndex, int instance, const std::array<float, 3>& colorA, const std::array<float, 3>& colorB, bool rainbow, float scale, double clock, const RenderTarget& target, const ShaderValues& stored);
    void runShaderPasses(const ShaderPlan& plan, const std::vector<PassProgram>& programs, ShaderPassState& state, const std::array<float, 3>& colorA, const std::array<float, 3>& colorB, bool rainbow, float scale, double clock, float beat, float bpm, const RenderTarget& target, bool thumbnail, const ShaderValues& values, const RenderTarget* picture, float mixAmount);
    void renderShaderThumbnail(size_t clipIndex, const RenderTarget& target);
    void releaseIdleShaderStates();
    void serviceMemory();
    void sampleDriverMemory();
    void trimMemory(std::uint64_t estimate, std::uint64_t target);
    std::vector<char> activeAndNextCells(size_t count, bool includeNext) const;
    void restartShaders(bool carryOver);
    void restartShaderLayer(int layer);
    void clearShaderStates(int clipIndex);
    int resolveMediaClip(const std::string& name) const;
    void renderPreview();
    void pollGpuQueries();
    void downsample(const RenderTarget& source, const RenderTarget& target);
    void prepareSpout(int slot);
    bool drawSpout(int slot, float scale);
    void serviceSpout();
    void prepareLive(const ClipDef& def);
    void prepareLiveSlot(LiveKind kind, int slot);
    void servicePerson();
    bool drawLive(const ClipDef& def, float scale);
    void serviceLive();
    LiveInput* liveSlot(LiveKind kind, int slot);
    static size_t liveIndex(LiveKind kind, int slot) { return static_cast<size_t>(kind == LiveKind::Ndi ? 0 : (kind == LiveKind::Camera ? kLiveInputSlots : (kind == LiveKind::Plugin ? kLiveInputSlots * 2 : kLiveInputSlots * 3))) + static_cast<size_t>(slot); }
    void blit(const RenderTarget& source, const RenderTarget& target);

    struct PlaygroundPending {
        GLuint program = 0;
        GLuint vs = 0;
        GLuint fs = 0;
        std::string label;
    };

    struct Playground {
        bool wanted = false;
        bool hasFrame = false;
        bool restart = false;
        bool building = false;
        bool buildSucceeded = false;
        bool queued = false;
        double clock = 0.0;
        std::string queuedText;
        std::uint64_t queuedRevision = 0;
        std::uint64_t buildRevision = 0;
        std::shared_ptr<const ShaderPlan> buildPlan;
        bool buildShadertoy = false;
        std::vector<PlaygroundPending> pending;
        std::chrono::steady_clock::time_point buildStarted;
        std::shared_ptr<const ShaderPlan> plan;
        ClipProgram clip;
        ShaderPassState state;
        RenderTarget target;
        shaderedit::PreviewGovernor governor;
        std::array<GLuint, 2> queries{};
        std::array<bool, 2> queryPending{};
        std::array<bool, 2> queryStale{};
        int querySlot = 0;
        ShaderPlaygroundStatus status;
    };

    std::vector<ClipProgram> clips_;
    std::map<int, ShaderPassState> shaderStates_;
    Playground playground_;
    std::array<GLuint, 4> channelSamplers_{};
    GLuint blackTexture_ = 0;
    unsigned long long shaderBudget_ = kShaderBufferBudgetBytes;
    MemoryPolicy memoryPolicy_;
    Diagnostics diagnostics_;
    HardwareSyncStatus hardwareSync_;
    gpudiag::PressureLadder pressure_;
    gpudiag::TrafficMeter traffic_;
    gpudiag::DriverMemory driverMemory_;
    bool driverMemoryDisabled_ = false;
    double driverSampledAt_ = -1.0e9;
    double memoryClock_ = 0.0;
    double memoryCheckAt_ = -1.0e9;
    unsigned long long lastStreamedBytes_ = 0;
    std::uint64_t releasedBytes_ = 0;
    std::uint64_t releasedCount_ = 0;
    int appliedPressure_ = 0;
    std::vector<EffectProgram> effects_;
    CompositeProgram composite_;
    BlendProgram blend_;
    FinalProgram final_;
    TransitionProgram transition_program_;
    ThumbProgram thumb_;
    SpoutProgram spoutProgram_;
    MediaProgram mediaProgram_;
    VideoGpu videoGpu_;
    ColorStage colorStage_;
    RenderTarget colorScratch_;
    ColorStageStatus colorStatus_;
    bool colorActive_ = false;
    float colorStrength_ = 1.0f;
    void applyColorStage();
    VideoDecodeMode videoMode_ = VideoDecodeMode::Auto;
    flow::Settings flowSettings_;
    bool hapDirect_ = true;
    bool gpuConversion_ = true;
    std::vector<std::unique_ptr<MediaPlayer>> media_;
    std::vector<std::unique_ptr<GeneratorPlayer>> generators_;
    std::vector<size_t> deferredGeneratorThumbs_;
    GeneratorGl generatorGl_;
    int particleLimit_ = particles::kDefaultLimit;
    bool gpuParticles_ = false;
    int gpuParticleBudgetMb_ = gpuparticles::kDefaultBudgetMb;
    std::unique_ptr<TextPlayer> textEditor_;
    std::unique_ptr<ParticlePlayer> particleEditor_;
    std::string textEditorKey_;
    std::string particleEditorKey_;
    enum class EditorPreviewKind { None, Text, Particles };
    EditorPreviewKind generatorPreviewKind_ = EditorPreviewKind::None;
    text::TextDoc generatorPreviewText_;
    particles::Params generatorPreviewParticles_;
    double generatorPreviewClock_ = 0.0;
    RenderTarget generatorPreview_;
    bool generatorPreviewReady_ = false;
    bool chainPreviewRequested_ = false;
    std::array<EffectSlot, kEffectSlots> chainPreviewSlots_{};
    double chainPreviewClock_ = 0.0;
    int chainPreviewClip_ = -2;
    RenderTarget chainPreviewA_;
    RenderTarget chainPreviewB_;
    std::array<RenderTarget, kEffectSlots> chainPreviewFeedback_;
    std::array<bool, kEffectSlots> chainPreviewFeedbackUsed_{};
    RenderTarget* chainPreviewResult_ = nullptr;
    void renderEditorPreviews();
    bool drawTextPreview(const text::TextDoc& doc, RenderTarget& target, double clock);
    bool drawParticlePreview(const particles::Params& params, RenderTarget& target, double clock);
    std::vector<size_t> deferredMediaThumbs_;
    std::array<SpoutInput, kSpoutInputSlots> spoutSlots_;
    std::array<std::string, kSpoutInputSlots> spoutNames_;
    std::string spoutExcluded_;
    bool spoutFlip_ = false;
    std::array<double, kSpoutInputSlots> spoutLastUse_{};
    std::array<unsigned long long, kSpoutInputSlots> spoutFrame_{};
    std::array<std::unique_ptr<LiveInput>, static_cast<size_t>(kLiveInputSlots) * 4> liveInputs_;
    std::array<double, static_cast<size_t>(kLiveInputSlots) * 4> liveLastUse_{};
    std::array<unsigned long long, static_cast<size_t>(kLiveInputSlots) * 4> liveFrame_{};
    std::array<std::string, kLiveInputSlots> liveNdi_;
    std::array<std::string, kLiveInputSlots> liveCamera_;
    std::array<std::string, kLiveInputSlots> liveStream_;
    std::unique_ptr<PersonSession> person_;
    person::Settings personSettings_;
    LiveInput* personInput_ = nullptr;
    std::string personNote_;
    bool personExternal_ = false;
    stream::Profile liveStreamProfile_ = stream::Profile::Balanced;
    std::string liveExcluded_;
    PluginHost* plugins_ = nullptr;
    CameraMode liveMode_;
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
    struct RouteCapture {
        std::array<RenderTarget, 2> target;
        std::array<unsigned long long, 2> frame{};
        int current = 0;
    };
    std::array<std::array<RouteCapture, routing::kSlotCount>, routing::kDeckPasses> routeCaptures_;
    routing::CacheTracker routeCache_;
    routing::Plan routePlan_;
    std::array<routing::Source, routing::kSenderCount> senderSources_{};
    std::array<RenderTarget, routing::kSenderCount> senderTargets_;
    std::array<bool, routing::kSenderCount> senderLive_{};
    bool routePreviewsWanted_ = false;
    std::array<RenderTarget, routing::kSlotCount> routePreviews_;
    std::array<unsigned long long, routing::kSlotCount> routePreviewFrame_{};
    unsigned long long routeCaptureCount_ = 0;
    bool routeBudgetLogged_ = false;
    static constexpr size_t kRouteBudgetBytes = 512ull * 1024ull * 1024ull;
    static constexpr int kRoutePreviewWidth = 160;
    static constexpr int kRoutePreviewHeight = 90;
    routing::Plan planRouting(const std::array<int, kLayerCount>& clipOf, int deck);
    void routeCapture(int deck, int slot, const RenderTarget& source, const routing::Plan& plan);
    void drawRouted(int sourceIndex, int instance, const RenderTarget& target);
    void serviceRouting();
    void releaseRouting();
    void destroyRouteSlot(int deck, int slot);
    void blitFiltered(const RenderTarget& source, const RenderTarget& target, bool linear);
    static constexpr int kEffectStateBase = 100000;
    static constexpr int kEffectKeyBase = 1000;
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
    std::array<std::uint32_t, kLayerCount> triggerCounts_{};
    BeatClock beatClock_;
    LaunchQuantizer quantizer_;
    double lastCompBpm_ = 0.0;
    double lastClockBpm_ = 0.0;
    bool clockStarted_ = false;
    bool timecodeValid_ = false;
    double timecodeSeconds_ = 0.0;
    AudioFeatures audioInput_;
    AudioFeatures audio_;
    GLuint audioTexture_ = 0;
    std::array<Effective, kLayerCount> effective_{};
    float effectiveLevel_ = 1.0f;
    ColorGrade effectiveMasterGrade_;
};

}
