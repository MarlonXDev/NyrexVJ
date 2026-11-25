#include "app/api_system.hpp"

#include "control/api_security.hpp"
#include "control/control_targets.hpp"
#include "core/log.hpp"
#include "core/preset.hpp"
#include "render/clip_library.hpp"
#include "render/effect_library.hpp"

#include <algorithm>

namespace nvj {

namespace {

constexpr double kPublishInterval = 0.25;
constexpr int kCommandsPerFrame = 16;

static_assert(api::kLayers == kLayerCount);
static_assert(api::kColumns == kColumnCount);
static_assert(api::kSlots == kEffectSlots);
static_assert(api::kParams == kParamCount);
static_assert(api::kDecks == kDeckCount);

const char* clipKindName(ClipKind kind) {
    switch (kind) {
    case ClipKind::Shader:
        return "shader";
    case ClipKind::BuiltinScene:
        return "scene";
    case ClipKind::ModelFile:
        return "model";
    case ClipKind::ScenePack:
        return "scenePack";
    case ClipKind::SpoutSource:
        return "spout";
    case ClipKind::ImageFile:
        return "image";
    case ClipKind::VideoFile:
        return "video";
    case ClipKind::ShaderFile:
        return "shaderFile";
    case ClipKind::NdiSource:
        return "ndi";
    case ClipKind::CameraSource:
        return "camera";
    case ClipKind::PluginSource:
        return "plugin";
    }
    return "shader";
}

void mix(std::uint64_t& hash, const std::string& text) {
    for (const char c : text) {
        hash ^= static_cast<unsigned char>(c);
        hash *= 1099511628211ull;
    }
    hash ^= 0xff;
    hash *= 1099511628211ull;
}

api::SlotInfo slotInfo(const EffectSlot& slot) {
    api::SlotInfo info;
    info.effect = slot.effect;
    info.enabled = slot.enabled;
    info.params = slot.params;
    return info;
}

std::string limitName(const std::string& text) {
    return text.size() > 120 ? text.substr(0, 120) : text;
}

}

ApiSystem::ApiSystem(Engine& engine, ControlSystem& control, OutputManager& outputs, LoadFn load) : engine_(engine), control_(control), outputs_(outputs), load_(std::move(load)) {}

ApiSystem::~ApiSystem() {
    shutdown();
}

void ApiSystem::shutdown() {
    stopServer();
    wanted_ = ApiConfig{};
    attempted_ = false;
}

void ApiSystem::stopServer() {
    server_.stop();
    hub_.reset();
    router_.reset();
    active_ = ApiConfig{};
    publishTimer_ = 0.0;
}

void ApiSystem::restart() {
    restartRequested_ = true;
}

bool ApiSystem::ensureToken() {
    if (api::validTokenFormat(token_)) {
        return true;
    }
    warning_.clear();
    if (api::loadToken(token_)) {
        return true;
    }
    if (!api::generateToken(token_)) {
        token_.clear();
        error_ = "Could not create a secure token";
        return false;
    }
    if (!api::saveToken(token_)) {
        warning_ = "The token could not be saved and will change at the next start";
        logLine("API: the token file could not be written to " + api::tokenPath());
    }
    return true;
}

bool ApiSystem::regenerateToken() {
    std::string fresh;
    if (!api::generateToken(fresh)) {
        error_ = "Could not create a secure token";
        return false;
    }
    warning_.clear();
    if (!api::saveToken(fresh)) {
        warning_ = "The token could not be saved and will change at the next start";
        logLine("API: the token file could not be written to " + api::tokenPath());
    }
    token_ = fresh;
    if (server_.running()) {
        restartRequested_ = true;
    }
    return true;
}

std::string ApiSystem::browserUrl() const {
    if (!server_.running() || token_.empty()) {
        return std::string();
    }
    return "http://127.0.0.1:" + std::to_string(server_.port()) + "/#token=" + token_;
}

void ApiSystem::configure(const ApiConfig& config) {
    if (!(config == wanted_)) {
        wanted_ = config;
        attempted_ = false;
        error_.clear();
    }
    if (restartRequested_) {
        restartRequested_ = false;
        attempted_ = false;
        error_.clear();
        if (server_.running()) {
            stopServer();
        }
    }
    if (!wanted_.enabled) {
        if (server_.running() || router_) {
            stopServer();
            logLine("API: stopped");
        }
        return;
    }
    if (server_.running() && active_ == wanted_) {
        return;
    }
    if (attempted_) {
        return;
    }
    attempted_ = true;
    if (server_.running()) {
        stopServer();
    }
    if (!ensureToken()) {
        return;
    }
    api::RouterConfig routerConfig;
    routerConfig.port = wanted_.port;
    routerConfig.allowRemote = wanted_.allowRemote;
    routerConfig.token = token_;
    router_ = std::make_unique<api::Router>(hub_, routerConfig);
    api::ServerConfig serverConfig;
    serverConfig.port = wanted_.port;
    serverConfig.allowRemote = wanted_.allowRemote;
    std::string failure;
    if (!server_.start(serverConfig, *router_, failure)) {
        router_.reset();
        error_ = failure.empty() ? "Could not start the web interface" : failure;
        return;
    }
    active_ = wanted_;
    error_.clear();
    publishTimer_ = kPublishInterval;
    logLine(std::string("API: listening on ") + (wanted_.allowRemote ? "all interfaces" : "127.0.0.1") + " port " + std::to_string(server_.port()));
}

void ApiSystem::update(double dt, float fps) {
    if (!server_.running()) {
        return;
    }
    const int applied = hub_.drain(kCommandsPerFrame, [this](const api::Command& command) { return execute(command); });
    applied_ += static_cast<std::uint64_t>(applied);
    publishTimer_ += dt;
    if (applied > 0 || publishTimer_ >= kPublishInterval || !hub_.snapshot()) {
        hub_.publish(buildSnapshot(fps));
        publishTimer_ = 0.0;
    }
}

std::shared_ptr<const api::Snapshot> ApiSystem::buildSnapshot(float fps) {
    auto snapshot = std::make_shared<api::Snapshot>();
    const Composition& comp = engine_.comp;
    snapshot->revision = ++revision_;
    snapshot->appVersion = NYREXVJ_VERSION;
    snapshot->composition = limitName(comp.name);
    snapshot->bpm = comp.bpm;
    snapshot->beatsPerBar = comp.beatsPerBar;
    snapshot->activeDeck = comp.activeDeck;
    snapshot->blackout = engine_.blackout;
    snapshot->masterLevel = comp.master.level;
    snapshot->fps = fps;
    snapshot->width = engine_.width();
    snapshot->height = engine_.height();
    snapshot->audioActive = engine_.audio().active;
    for (int deck = 0; deck < api::kDecks; ++deck) {
        snapshot->deckNames[static_cast<size_t>(deck)] = limitName(deckDisplayName(comp, deck));
        snapshot->deckUsed[static_cast<size_t>(deck)] = comp.deckUsed[static_cast<size_t>(deck)];
    }
    for (int layer = 0; layer < api::kLayers; ++layer) {
        const Layer& source = comp.layers[static_cast<size_t>(layer)];
        api::LayerInfo& info = snapshot->layers[static_cast<size_t>(layer)];
        info.cells = source.cells;
        info.activeColumn = source.activeColumn;
        info.opacity = source.opacity;
        info.bypass = source.bypass;
        info.solo = source.solo;
        info.speed = source.speed;
        info.scale = source.scale;
        for (int slot = 0; slot < api::kSlots; ++slot) {
            info.fx[static_cast<size_t>(slot)] = slotInfo(source.fx[static_cast<size_t>(slot)]);
        }
    }
    for (int slot = 0; slot < api::kSlots; ++slot) {
        snapshot->masterFx[static_cast<size_t>(slot)] = slotInfo(comp.master.fx[static_cast<size_t>(slot)]);
    }
    const size_t targets = controlTargets().size();
    snapshot->controlValues.resize(targets);
    for (size_t i = 0; i < targets; ++i) {
        snapshot->controlValues[i] = control_.router().value(static_cast<int>(i));
    }
    const auto& specs = outputs_.specs();
    for (int i = 0; i < kMaxOutputs; ++i) {
        const OutputSpec& spec = specs[static_cast<size_t>(i)];
        api::OutputInfo info;
        info.name = limitName(spec.name);
        info.enabled = spec.enabled;
        info.open = outputs_.isOpen(i);
        info.fullscreen = outputs_.isFullscreen(i);
        info.monitor = spec.monitor;
        info.ndi = spec.ndi;
        info.ndiName = limitName(spec.ndiName);
        snapshot->outputs.push_back(info);
    }

    const std::vector<ClipDef>& clips = clipLibrary();
    const std::vector<EffectDef>& effects = effectLibrary();
    const std::vector<Composition>& presets = factoryPresets();
    std::uint64_t signature = 1469598103934665603ull;
    for (const ClipDef& clip : clips) {
        mix(signature, clip.name);
        mix(signature, clipKindName(clip.kind));
    }
    for (const EffectDef& effect : effects) {
        mix(signature, effect.name);
        for (const char* param : effect.paramNames) {
            mix(signature, param != nullptr ? param : "");
        }
    }
    for (const Composition& preset : presets) {
        mix(signature, preset.name);
    }
    if (!library_ || library_->signature != signature) {
        auto library = std::make_shared<api::Library>();
        library->signature = signature;
        library->clips.reserve(clips.size());
        for (const ClipDef& clip : clips) {
            library->clips.push_back({limitName(clip.name), clipKindName(clip.kind)});
        }
        library->effects.reserve(effects.size());
        for (const EffectDef& effect : effects) {
            api::EffectInfo info;
            info.name = limitName(effect.name);
            const int count = effect.paramCount();
            for (int param = 0; param < kParamCount; ++param) {
                const char* name = effect.paramNames[static_cast<size_t>(param)];
                info.params.push_back(param < count && name != nullptr ? name : "");
            }
            info.graph = effect.isGraph;
            library->effects.push_back(std::move(info));
        }
        for (const Composition& preset : presets) {
            library->factoryPresets.push_back(preset.name);
        }
        library_ = library;
    }
    snapshot->library = library_;
    return snapshot;
}

api::Result ApiSystem::execute(const api::Command& command) {
    api::Result result;
    switch (command.type) {
    case api::CommandType::Control: {
        const auto& targets = controlTargets();
        if (command.control < 0 || static_cast<size_t>(command.control) >= targets.size()) {
            result.status = 404;
            result.code = "not_found";
            result.message = "This control does not exist";
            return result;
        }
        const std::string& id = targets[static_cast<size_t>(command.control)].id;
        if (!control_.fireTarget(id, command.value)) {
            result.status = 400;
            result.code = "rejected";
            result.message = "The control did not accept the value";
            return result;
        }
        result.ok = true;
        result.status = 200;
        result.value = control_.router().value(command.control);
        return result;
    }
    case api::CommandType::LoadFactoryPreset: {
        const int index = factoryPresetIndex(command.name);
        if (index < 0) {
            result.status = 404;
            result.code = "not_found";
            result.message = "This preset does not exist";
            return result;
        }
        load_(factoryPresets()[static_cast<size_t>(index)], command.name);
        result.ok = true;
        result.status = 200;
        return result;
    }
    case api::CommandType::LoadUserPreset: {
        Composition comp;
        std::string error;
        if (!parseUserPreset(command.name, command.text, comp, error)) {
            result.status = 422;
            result.code = "invalid_preset";
            result.message = error.empty() ? "The preset could not be read" : error;
            return result;
        }
        load_(comp, command.name);
        result.ok = true;
        result.status = 200;
        return result;
    }
    }
    result.status = 500;
    result.code = "internal";
    result.message = "Unknown command";
    return result;
}

}
