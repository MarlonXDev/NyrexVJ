#pragma once

#include "app/creator.hpp"
#include "app/engine_host.hpp"
#include "app/generator_dialog.hpp"
#include "app/shader_editor_dialog.hpp"
#include "app/manual_view.hpp"
#include "app/node_editor.hpp"
#include "app/pack_dialog.hpp"
#include "app/show_dialog.hpp"
#include "app/timeline_view.hpp"
#include "audio/audio_system.hpp"
#include "app/api_system.hpp"
#include "control/control_system.hpp"
#include "control/osc_out.hpp"
#include "control/stage_out.hpp"
#include "control/sync_system.hpp"
#include "control/timeline_system.hpp"
#include "core/edit_history.hpp"
#include "core/frame_stats.hpp"
#include "core/i18n.hpp"
#include "core/library_meta.hpp"
#include "core/plugin_host.hpp"
#include "core/recovery.hpp"
#include "core/settings.hpp"
#include "media/camera_capture.hpp"
#include "media/convert_manager.hpp"
#include "media/ffmpeg_setup.hpp"
#include "media/video_hardware.hpp"
#include "render/asset_import.hpp"
#include "render/engine.hpp"
#include "render/media_player.hpp"
#include "render/output_manager.hpp"
#include "render/output_window.hpp"
#include "render/ndi_output.hpp"
#include "render/recorder.hpp"
#include "render/spout_output.hpp"
#include "update/updater.hpp"

#include <imgui.h>

#include <array>
#include <functional>
#include <future>
#include <memory>
#include <string>
#include <vector>

struct GLFWwindow;

namespace nvj {

enum SettingsPage {
    SettingsGeneral = 0,
    SettingsAppearance,
    SettingsAudio,
    SettingsOutput,
    SettingsRecording,
    SettingsControl,
    SettingsStage,
    SettingsSync,
    SettingsUpdates,
    SettingsShortcuts,
    SettingsPerformance,
    SettingsPlugins,
    SettingsAbout,
    SettingsPageCount
};

struct StartOptions {
    std::string preset;
    std::string clip;
    std::string effect;
    std::string language;
    std::string decks;
    std::string transition;
    std::string demoUpdate;
    std::string creator;
    std::string nodeEffect;
    bool openNodes = false;
    bool openTimeline = false;
    bool openManual = false;
    int showPackageTab = -1;
    int packageTab = -1;
    int generatorTab = -1;
    std::string generatorSelect;
    bool shaderEditor = false;
    std::string shaderEditorFile;
    int gpuParticles = -1;
    int chainTarget = -1;
    std::string chainSelect;
    std::string packageFile;
    std::string manualChapter;
    float transitionSeconds = -1.0f;
    int column = -1;
    int resolution = -1;
    int quality = -1;
    int selectLayer = -1;
    int tab = -1;
    int settingsPage = -1;
    int libraryMode = -1;
    int libraryKind = -1;
    int oscPort = 0;
    bool openOutput = false;
    bool outputPattern = false;
    std::string ndiName;
    std::string venue;
    int selectOutput = -1;
    bool openCreator = false;
    bool fresh = false;
    bool skipUpdateCheck = false;
    bool audioDemo = false;
    bool forceWelcome = false;
    bool skipWelcome = false;
    bool crashTest = false;
    bool noPlugins = false;
};

class App {
public:
    bool init(GLFWwindow* window, const StartOptions& options);
    void frame(double dt);
    void startShaderEditorTest();
    void startRoutingDemo();
    int tickShaderEditorTest();
    std::string shaderEditorTestReport() const;
    void applyPendingFonts();
    void shutdown();
    Engine& engine() { return engine_; }
    ControlSystem& control() { return control_; }
    ApiSystem& api() { return api_; }
    bool wantsRestart() const { return restart_; }
    int dumpOutputs(const std::string& prefix);
    bool wantsVsync() const { return settings_.vsync; }
    int fpsLimit() const { return settings_.fpsLimit; }

private:
    void buildLayout(ImGuiID dock);
    void handleShortcuts();
    void drawMenuBar();
    void drawDeck();
    void drawOutput();
    void drawPreview();
    void drawClipPanel();
    void drawTransport(bool wide);
    void drawSceneControls(ScenePlayer& player);
    void drawLayer();
    void drawMaster();
    void drawBrowser();
    void drawPresetsTab();
    void drawLibraryTab();
    void drawSceneDetails(const ScenePlayer& player);
    void drawMediaDetails(const MediaPlayer& player, bool tooltip);
    void drawMediaControls(MediaPlayer& player);
    void drawPlaybackControls(MediaPlayer& player, ClipPlayback& playback);
    void queueDroppedFiles(const std::vector<std::string>& paths);
    void serviceImports();
    void drawClipTile(int index, const ImVec2& size);
    void loadLibraryMeta();
    void saveLibraryMeta();
    void drawLibraryPopups();
    void drawLibraryClips();
    void drawLibraryEffects();
    void drawLibraryAssets();
    void openCreator();
    void openCreatorWith(const std::string& clipName);
    bool creatorSave(bool copy, bool useAsClip);
    void undoEdit();
    void drawPerformancePage();
    void drawGpuDiagnostics();
    void drawRoutingWindow();
    void applySenderSources();
    bool senderRunning(int kind) const;
    void drawDiagnosticsBadge();
    void drawPluginsPage();
    void drawPluginFrameSlot(int slot);
    void updatePlugins();
    void drawFrameRateLimit();
    void drawVideoSettings();
    void drawConversionSettings();
    void drawFfmpegSection();
    void drawConversionCards();
    void initConversion();
    void shutdownConversion();
    void serviceConversion();
    void applyConvertSettings();
    void offerLibraryVideos(bool manual);
    void startFfmpegCheck();
    void startFfmpegInstall();
    void drawVideoMonitor();
    void drawPassProfiler();
    void redoEdit();
    void drawCreator();
    void drawNodeEditor();
    void openNodeEditor(const std::string& stem);
    void syncNodeEffects(const std::string& forceName = std::string());
    bool importNodeGraph(const std::string& path);
    void shutdownCreator();
    void restoreCreatorDoc(const SceneDoc& doc);
    bool creatorUnsaved() const;
    std::string creatorDocJson() const;
    void assignClipToSelectedCell(int clip);
    void assignClipToFreeCell(int clip, int layer);
    void addEffectToLayer(int effect, int layer);
    void addEffectToMaster(int effect);
    void drawThemePicker();
    void drawAudioTab();
    void drawDeckBar();
    void drawSettingsWindow();
    void drawSettingsPage(int page);
    void drawUpdateSection();
    void drawControlPage();
    void drawStagePage();
    void drawSyncPage();
    void drawSyncClockSection();
    void drawSyncMidiSection();
    void drawSyncTimecodeSection();
    void drawSyncDjLinkSection();
    void drawSyncCueSection();
    void updateSync(double dt);
    void updateTimeline(double dt);
    void drawTimeline();
    void drawManual();
    void drawCommunityPackages();
    void openGeneratorStudio(int tab, const std::string& name);
    void drawGeneratorStudio();
    void openShaderEditor(const std::string& fileName);
    void drawShaderEditor();
    void buildShaderEditorTest();
    void drawGeneratorTextTab();
    void drawGeneratorParticleTab();
    void drawGeneratorClipInfo(const ClipDef& def, int clip);
    void openChainLibrary(int target);
    void drawChainLibrary();
    void drawChainButtons(std::array<EffectSlot, kEffectSlots>& slots);
    void drawPackInstall();
    void drawPackInstalled();
    void drawPackCreate();
    void drawPackProgress();
    void startPackCheck();
    void startPackJob(PackJobKind kind, const std::function<void(PackJobResult&, const show::ProgressFn&)>& body);
    void servicePackJob();
    void openCommunityPackages(int tab);
    void importCommunityPackage(const std::string& path);
    void afterPackChange();
    void drawShowPackages();
    void drawShowExport();
    void drawShowImport();
    void drawShowProgress();
    void buildShowPlan();
    void startShowCheck();
    void startShowJob(ShowJobKind kind, const std::function<void(ShowJobResult&, const show::ProgressFn&)>& body);
    void serviceShowJob();
    void openShowPackages(int tab);
    void importShowPackage(const std::string& path);
    void openManual(const std::string& target);
    void manualGo(size_t chapter, const std::string& section, bool remember);
    void drawTimelineToolbar();
    void drawTimelineCanvas();
    void drawTimelineTargetPicker();
    void timelineUndo();
    void timelineRedo();
    void timelineBeginEdit();
    void timelineEndEdit();
    void drawOscSection();
    void drawApiSection();
    void loadFromApi(const Composition& comp, const std::string& name);
    void drawOscOutSection();
    void drawMidiSection();
    void drawProfilesSection();
    void drawBindingsSection();
    void drawAudioSource();
    void drawUpdateCard();
    void drawStatusBar();
    void openSettings(int page);
    std::string audioSourceLabel() const;
    void loadFactoryBank();
    void switchDeckTo(int index);
    void fillDeckFromLook(int index, const Composition& look);
    void drawWelcome();
    void drawRecovery();
    void restoreAutosave(const AutosaveEntry& entry);
    void restoreCreatorScene();
    void keepLatestAutosave();
    void autosaveTick(double dt);
    bool recoveryPending() const { return recoveryRequested_ || recoveryOpen_; }
    void drawGrade(ColorGrade& grade);
    void drawMods(std::array<AudioMod, kModSlots>& mods, bool master);
    void drawModulators(Modulators& modulators);
    void drawEffectRack(std::array<EffectSlot, kEffectSlots>& slots);
    void drawOutputControls();
    void drawOutputMapping(int index);
    void drawMappingCanvas(OutputSpec& spec, int index);
    void drawOutputSlices(OutputSpec& spec);
    void drawSliceMask(OutputMapping& map);
    void drawOutputColor(OutputSpec& spec);
    void drawSliceColor(OutputMapping& map);
    void drawOutputNdi(OutputSpec& spec, int index);
    void drawMaskImage(SliceMask& mask);
    void drawOutputBudget();
    void drawVenueControls();
    void applyVenue(const std::array<OutputSpec, kMaxOutputs>& set);
    bool importDroppedVenue(const std::string& path);
    bool captureOutput(int index, OutputCapture& out);
    void drawSpoutControls();
    void drawNdiControls();
    void drawRecordingPage();
    void drawRecordingStatusBar();
    bool startRecording();
    void stopRecording();
    void cancelRecording();
    void serviceRecording();
    void startRecordProbe();
    RecordSettings recordSettingsFromConfig() const;
    void drawSpoutInputControls(int slot);
    void drawLiveInputControls(const ClipDef& def);
    void serviceLiveBrowse();
    void loadComposition(const Composition& comp, const std::string& message);
    void saveCurrentPreset();
    void refreshUserPresets();
    void toast(const std::string& message);
    void openOutputWindow(bool fullscreen);
    void updateAudio(double dt);
    void setLanguageAndSave(Language language);
    void openWelcome(bool firstRun);

    std::unique_ptr<PluginHost> plugins_;
    std::unique_ptr<hapjobs::Manager> convert_;
    std::future<ffmpeg_setup::Capabilities> ffmpegCheck_;
    ffmpeg_setup::Capabilities ffmpegCaps_;
    bool ffmpegCapsKnown_ = false;
    ffmpeg_setup::Installer ffmpegInstaller_;
    ffmpeg_setup::Package ffmpegPackage_;
    bool ffmpegPackageKnown_ = false;
    std::string ffmpegPackageReason_;
    bool ffmpegInstallSeen_ = false;
    bool ffmpegManualOpen_ = false;
    bool convertScanPending_ = false;
    double convertListRefresh_ = 0.0;
    std::string convertCacheText_;
    Engine engine_;
    EngineControlHost controlHost_{engine_};
    ControlSystem control_{engine_.comp, controlHost_};
    OscSender oscOut_;
    StageOutput stage_;
    SyncSystem sync_;
    TimelineSystem timeline_;
    TimelineView timelineView_;
    bool timelineOpen_ = false;
    bool timelineFocus_ = false;
    bool timelineHasFocus_ = false;
    bool manualOpen_ = false;
    ShowDialog showDialog_;
    PackDialog packDialog_;
    GeneratorDialog generatorDialog_;
    ShaderEditorDialog shaderEditor_;
    ShaderEditorUiTest shaderEditorTest_;
    ChainDialog chainDialog_;
    bool manualFocus_ = false;
    ManualView manualView_;
    int ltcDeviceListedFrame_ = -1;
    char cueTimeBuffer_[24] = {};
    int cueTargetPick_ = -1;
    int cueEditRow_ = -1;
    char cueFilter_[64] = {};
    bool stageDirty_ = false;
    double shaderPoll_ = 0.0;
    Settings settings_;
    Updater updater_;
    OutputManager outputs_;
    ApiSystem api_{engine_, control_, outputs_, [this](const Composition& comp, const std::string& name) { loadFromApi(comp, name); }};
    AudioSystem audio_;
    SpoutOutput spout_;
    NdiOutput ndi_;
    Recorder recorder_;
    GLFWwindow* window_ = nullptr;
    std::string iniPath_;
    bool needsLayout_ = false;
    bool fontCheckPending_ = false;
    bool cjkTried_ = false;
    bool rescanModels_ = false;
    bool assetsDirty_ = true;
    std::vector<AssetFile> assetModels_;
    std::vector<AssetFile> assetTextures_;
    std::vector<AssetFile> assetPacks_;
    std::vector<AssetFile> assetMedia_;
    std::vector<std::string> pendingImports_;
    std::future<std::vector<ImportResult>> importJob_;
    std::unique_ptr<CreatorState> creator_;
    std::unique_ptr<NodeEditorState> nodes_;
    bool nodesOpen_ = false;
    bool routingOpen_ = false;
    bool nodesFocus_ = false;
    bool nodesHasFocus_ = false;
    bool creatorOpen_ = false;
    bool creatorFocus_ = false;
    bool creatorHasFocus_ = false;
    EditHistory history_;
    FrameStats perfFrame_{240};
    FrameStats perfCpu_{240};
    FrameStats perfGpu_{240};
    FrameSummary perfFrameSummary_;
    FrameSummary perfCpuSummary_;
    FrameSummary perfGpuSummary_;
    std::unique_ptr<gpudiag::Monitor> diagMonitor_ = std::make_unique<gpudiag::Monitor>();
    std::future<HardwareProbeReport> hardwareProbe_;
    HardwareProbeReport hardwareReport_;
    bool hasHardwareReport_ = false;
    double perfRefresh_ = 0.0;
    bool restart_ = false;
    int selectedLayer_ = 0;
    int selectedColumn_ = 0;
    int libraryMode_ = 0;
    int libraryKind_ = 0;
    int selectedLibraryClip_ = -1;
    int selectedLibraryEffect_ = -1;
    char libraryFilter_[64] = {};
    LibraryMeta libraryMeta_;
    int libraryPopup_ = 0;
    int libraryPopupClip_ = -1;
    bool replaceKeepPlayback_ = true;
    char replaceFilter_[64] = {};
    char tagInput_[40] = {};
    char profileName_[64] = {};
    std::string profileMessage_;
    bool profileMessageGood_ = true;
    char effectPickFilter_[64] = {};
    int focusTab_ = -1;
    int selectedOutput_ = 0;
    int outputTabForce_ = 0;
    std::array<OutputSpec, kMaxOutputs> savedOutputs_ = defaultOutputs();
    int gridColumns_ = 2;
    int gridRows_ = 1;
    float gridOverlap_ = 0.1f;
    int draggedPoint_ = -1;
    int draggedWarp_ = -1;
    int draggedMask_ = -1;
    int draggedHandle_ = -1;
    std::vector<Vec2f> maskStroke_;
    char maskImagePath_[512] = {};
    char ndiOutputName_[64] = {};
    int ndiOutputNameFor_ = -1;
    int selectedSlice_ = 0;
    int canvasMode_ = 0;
    std::vector<std::string> venueList_;
    bool venueListDirty_ = true;
    int selectedVenue_ = -1;
    char venueName_[64] = {};
    char venuePath_[512] = {};
    bool mappingFollowsTexture_ = true;
    char presetName_[64] = {};
    char spoutName_[64] = {};
    bool ndiBrowsing_ = false;
    double ndiBrowseTouch_ = 0.0;
    std::vector<CameraDevice> cameraList_;
    double cameraListedAt_ = -1.0e9;
    char ndiName_[64] = {};
    std::future<RecordCapabilities> recordProbe_;
    RecordCapabilities recordCaps_;
    bool recordCapsKnown_ = false;
    char recordFolder_[512] = {};
    std::string recordLastPath_;
    std::string recordLastNote_;
    bool recordLastFailed_ = false;
    int recordSizeChoice_ = -1;
    int recordCustomWidth_ = 1920;
    int recordCustomHeight_ = 1080;
    char oscOutFilter_[64] = {};
    int oscOutPick_ = -1;
    std::string selectedUserPreset_;
    std::vector<std::string> userPresets_;
    std::string toastText_;
    double toastUntil_ = 0.0;
    double autosaveTimer_ = 0.0;
    double rotatingTimer_ = 0.0;
    std::string lastSessionText_;
    std::string lastRotatingText_;
    bool ownsSession_ = false;
    double crashTestAt_ = -1.0;
    unsigned long processId_ = 0;
    PreviousSession previous_;
    std::vector<AutosaveEntry> recoveryEntries_;
    bool recoveryRequested_ = false;
    bool recoveryOpen_ = false;
    bool recoveryCreator_ = false;
    int recoverySelected_ = 0;
    std::string recoveryDetails_;
    bool shutDown_ = false;
    bool welcomeOpen_ = false;
    bool welcomeRequested_ = false;
    bool welcomeFirstRun_ = false;
    std::string welcomeSince_;
    bool settingsOpen_ = false;
    bool settingsFocus_ = false;
    int settingsPage_ = 0;
    float uiScaleEdit_ = 1.0f;
    bool updateCardCollapsed_ = false;
    std::string autoInstallKey_;
    int renameDeck_ = -1;
    bool renameRequested_ = false;
    char deckNameBuffer_[48] = {};
    int appliedVsync_ = -1;
    int oscOverridePort_ = 0;
    char targetFilter_[64] = {};
};

}
