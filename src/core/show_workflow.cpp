#include "core/show_workflow.hpp"

#include "core/log.hpp"
#include "core/node_graph.hpp"
#include "core/preset.hpp"
#include "core/sha256.hpp"
#include "render/clip_library.hpp"
#include "render/effect_library.hpp"
#include "render/shader_source.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <regex>
#include <sstream>

namespace nvj::show {

namespace fs = std::filesystem;

namespace {

constexpr size_t kMaxJsonText = 8u * 1024u * 1024u;
constexpr size_t kMaxShaderText = 256u * 1024u;
constexpr int kJsonDepth = 32;
constexpr int kMaxShaderDepth = 4;

fs::path u8path(const std::string& text) {
    return fs::path(std::u8string(text.begin(), text.end()));
}

std::string u8text(const fs::path& path) {
    const std::u8string text = path.u8string();
    return std::string(text.begin(), text.end());
}

std::string lower(std::string text) {
    for (char& c : text) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    return text;
}

std::string extensionOf(const std::string& name) {
    const size_t dot = name.find_last_of('.');
    return dot == std::string::npos ? std::string() : lower(name.substr(dot));
}

std::string stemOf(const std::string& name) {
    const size_t dot = name.find_last_of('.');
    return dot == std::string::npos || dot == 0 ? name : name.substr(0, dot);
}

bool readText(const fs::path& file, size_t maxBytes, std::string& out) {
    std::error_code ec;
    if (!fs::is_regular_file(file, ec)) {
        return false;
    }
    const auto size = fs::file_size(file, ec);
    if (ec || size > maxBytes) {
        return false;
    }
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        return false;
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    out = buffer.str();
    return true;
}

bool writeBinary(const fs::path& file, const std::string& data) {
    std::error_code ec;
    if (file.has_parent_path()) {
        fs::create_directories(file.parent_path(), ec);
    }
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    if (!out) {
        return false;
    }
    out.write(data.data(), static_cast<std::streamsize>(data.size()));
    out.flush();
    return static_cast<bool>(out);
}

std::string pairKey(Kind kind, const std::string& name) {
    return std::string(kindName(kind)) + ":" + lower(name);
}

std::string percentDecode(const std::string& text) {
    std::string out;
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '%' && i + 2 < text.size() && std::isxdigit(static_cast<unsigned char>(text[i + 1])) && std::isxdigit(static_cast<unsigned char>(text[i + 2]))) {
            out.push_back(static_cast<char>(std::stoi(text.substr(i + 1, 2), nullptr, 16)));
            i += 2;
        } else {
            out.push_back(text[i]);
        }
    }
    return out;
}

bool isVideoName(const std::string& name) {
    const std::string extension = extensionOf(name);
    return extension == ".mp4" || extension == ".mov" || extension == ".m4v" || extension == ".mkv" || extension == ".avi" || extension == ".webm";
}

void collectWalk(const nlohmann::json& node, References& out) {
    if (node.is_object()) {
        for (auto it = node.begin(); it != node.end(); ++it) {
            if (it.key() == "cells" && it.value().is_array()) {
                for (const nlohmann::json& cell : it.value()) {
                    if (cell.is_string() && !cell.get<std::string>().empty()) {
                        out.clips.insert(cell.get<std::string>());
                    }
                }
            } else if (it.key() == "effect" && it.value().is_string()) {
                if (!it.value().get<std::string>().empty()) {
                    out.effects.insert(it.value().get<std::string>());
                }
            } else {
                collectWalk(it.value(), out);
            }
        }
    } else if (node.is_array()) {
        for (const nlohmann::json& item : node) {
            collectWalk(item, out);
        }
    }
}

void renameWalk(nlohmann::json& node, const NameMap& clips, const NameMap& effects) {
    if (node.is_object()) {
        for (auto it = node.begin(); it != node.end(); ++it) {
            if (it.key() == "cells" && it.value().is_array()) {
                for (nlohmann::json& cell : it.value()) {
                    if (cell.is_string()) {
                        auto found = clips.find(cell.get<std::string>());
                        if (found != clips.end()) {
                            cell = found->second;
                        }
                    }
                }
            } else if (it.key() == "effect" && it.value().is_string()) {
                auto found = effects.find(it.value().get<std::string>());
                if (found != effects.end()) {
                    it.value() = found->second;
                }
            } else {
                renameWalk(it.value(), clips, effects);
            }
        }
    } else if (node.is_array()) {
        for (nlohmann::json& item : node) {
            renameWalk(item, clips, effects);
        }
    }
}

void collectMaskImages(const nlohmann::json& node, std::vector<std::string>& out) {
    if (node.is_object()) {
        for (auto it = node.begin(); it != node.end(); ++it) {
            if (it.key() == "image" && it.value().is_string()) {
                const std::string name = it.value().get<std::string>();
                if (!name.empty() && std::find(out.begin(), out.end(), name) == out.end()) {
                    out.push_back(name);
                }
            } else {
                collectMaskImages(it.value(), out);
            }
        }
    } else if (node.is_array()) {
        for (const nlohmann::json& item : node) {
            collectMaskImages(item, out);
        }
    }
}

class PlanBuilder {
public:
    PlanBuilder(const Catalogue& catalogue, const Layout& layout, const Limits& limits)
        : catalogue_(catalogue), layout_(layout), limits_(limits) {}

    ExportPlan plan;

    void addFile(Kind kind, const std::string& name, const fs::path& file, const std::string& neededBy) {
        const std::string key = pairKey(kind, name);
        auto found = index_.find(key);
        if (found != index_.end()) {
            plan.items[found->second].neededBy.insert(neededBy);
            return;
        }
        PlanItem item;
        item.source.kind = kind;
        item.source.name = name;
        item.source.file = file;
        item.neededBy.insert(neededBy);
        std::string reason;
        if (!isSafeItemName(kind, name, &reason)) {
            item.problem = Problem::UnsafeName;
            item.detail = reason;
            item.source.include = false;
        } else {
            std::error_code ec;
            if (!fs::is_regular_file(file, ec) || ec) {
                item.problem = Problem::Missing;
                item.source.include = false;
            } else {
                const auto size = fs::file_size(file, ec);
                if (ec) {
                    item.problem = Problem::Missing;
                    item.source.include = false;
                } else {
                    item.size = static_cast<unsigned long long>(size);
                    if (item.size > limits_.maxBytesFor(kind)) {
                        item.problem = Problem::TooLarge;
                        item.source.include = false;
                    }
                }
            }
        }
        index_[key] = plan.items.size();
        plan.items.push_back(std::move(item));
    }

    void addData(Kind kind, const std::string& name, const std::string& data, const std::string& neededBy) {
        const std::string key = pairKey(kind, name);
        if (index_.count(key) != 0) {
            return;
        }
        PlanItem item;
        item.source.kind = kind;
        item.source.name = name;
        item.source.data = data;
        item.source.fromData = true;
        item.size = data.size();
        item.neededBy.insert(neededBy);
        if (item.size > limits_.maxBytesFor(kind)) {
            item.problem = Problem::TooLarge;
            item.source.include = false;
        }
        index_[key] = plan.items.size();
        plan.items.push_back(std::move(item));
    }

    void addReferences(const References& references, const std::string& neededBy) {
        for (const std::string& clip : references.clips) {
            addClip(clip, neededBy, 0);
        }
        for (const std::string& effect : references.effects) {
            addEffect(effect, neededBy);
        }
    }

    void finish() {
        std::sort(plan.plugins.begin(), plan.plugins.end());
        plan.plugins.erase(std::unique(plan.plugins.begin(), plan.plugins.end()), plan.plugins.end());
        std::sort(plan.live.begin(), plan.live.end());
        plan.live.erase(std::unique(plan.live.begin(), plan.live.end()), plan.live.end());
    }

private:
    void addClip(const std::string& clip, const std::string& neededBy, int depth) {
        auto entry = catalogue_.clips.find(clip);
        if (entry != catalogue_.clips.end()) {
            const CatalogueEntry& found = entry->second;
            addFile(found.kind, found.fileName, found.path, neededBy);
            if (found.kind == Kind::Model && extensionOf(found.fileName) == ".gltf") {
                std::string text;
                if (readText(found.path, kMaxJsonText, text)) {
                    for (const std::string& external : gltfExternalFiles(text)) {
                        addFile(Kind::Model, external, found.path.parent_path() / u8path(external), neededBy);
                    }
                }
            }
            if (found.kind == Kind::Shader && depth < kMaxShaderDepth && visited_.insert(clip).second) {
                std::string text;
                if (readText(found.path, kMaxShaderText, text)) {
                    std::set<std::string> nested;
                    collectShaderMediaReferences(text, nested);
                    for (const std::string& name : nested) {
                        addClip(name, "shader " + found.fileName, depth + 1);
                    }
                }
            }
            return;
        }
        if (catalogue_.builtInClips.count(clip) != 0) {
            return;
        }
        auto live = catalogue_.liveClips.find(clip);
        if (live != catalogue_.liveClips.end()) {
            plan.live.push_back(live->second + ": " + clip);
            return;
        }
        auto plugin = catalogue_.pluginClips.find(clip);
        if (plugin != catalogue_.pluginClips.end()) {
            plan.plugins.push_back(plugin->second);
            return;
        }
        plan.warnings.push_back({Warn::UnknownClip, clip});
    }

    void addEffect(const std::string& effect, const std::string& neededBy) {
        auto entry = catalogue_.effects.find(effect);
        if (entry != catalogue_.effects.end()) {
            addFile(entry->second.kind, entry->second.fileName, entry->second.path, neededBy);
            return;
        }
        if (catalogue_.builtInEffects.count(effect) != 0) {
            return;
        }
        plan.warnings.push_back({Warn::UnknownEffect, effect});
    }

    const Catalogue& catalogue_;
    const Layout& layout_;
    const Limits& limits_;
    std::map<std::string, size_t> index_;
    std::set<std::string> visited_;
};

bool movePath(const fs::path& from, const fs::path& to) {
    std::error_code ec;
    fs::rename(from, to, ec);
    if (!ec) {
        return true;
    }
    ec.clear();
    fs::copy_file(from, to, fs::copy_options::overwrite_existing, ec);
    if (ec) {
        return false;
    }
    fs::remove(from, ec);
    return true;
}

bool sameFile(const fs::path& file, unsigned long long size, const std::string& sha) {
    std::error_code ec;
    if (!fs::is_regular_file(file, ec)) {
        return false;
    }
    const auto onDisk = fs::file_size(file, ec);
    if (ec || static_cast<unsigned long long>(onDisk) != size || sha.empty()) {
        return false;
    }
    std::string hex;
    return sha256FileHex(file, hex) && hex == sha;
}

std::string numberedName(const std::string& name, int number) {
    const size_t dot = name.find_last_of('.');
    const std::string stem = dot == std::string::npos || dot == 0 ? name : name.substr(0, dot);
    const std::string extension = dot == std::string::npos || dot == 0 ? std::string() : name.substr(dot);
    return stem + " (" + std::to_string(number) + ")" + extension;
}

}

Layout Layout::underRoot(const fs::path& root) {
    Layout layout;
    layout.root = root;
    layout.media = root / "media";
    layout.models = root / "models";
    layout.scenePacks = root / "clips";
    layout.shaders = root / "shaders";
    layout.nodes = root / "nodes";
    layout.venues = root / "venues";
    layout.masks = root / "masks";
    layout.presets = root / "presets";
    layout.cues = root / "cues.json";
    layout.timeline = root / "timeline.json";
    return layout;
}

Layout Layout::standard() {
    Layout layout = underRoot(u8path(userDataDir()));
    layout.media = u8path(mediaDirectory());
    layout.models = u8path(modelDirectory());
    layout.scenePacks = u8path(clipsDirectory());
    layout.shaders = u8path(shaderDirectory());
    layout.nodes = u8path(graphDirectory());
    layout.presets = u8path(presetsDirectory());
    return layout;
}

bool Layout::placesFile(Kind kind) const {
    return kind != Kind::Composition;
}

fs::path Layout::folderFor(Kind kind) const {
    switch (kind) {
        case Kind::Media: return media;
        case Kind::Model: return models;
        case Kind::ScenePack: return scenePacks;
        case Kind::Shader: return shaders;
        case Kind::Node: return nodes;
        case Kind::Venue: return venues;
        case Kind::Mask: return masks;
        case Kind::Preset: return presets;
        case Kind::Cues: return cues.parent_path();
        case Kind::Timeline: return timeline.parent_path();
        case Kind::Composition: break;
    }
    return fs::path();
}

fs::path Layout::pathFor(Kind kind, const std::string& name) const {
    if (kind == Kind::Cues) {
        return cues;
    }
    if (kind == Kind::Timeline) {
        return timeline;
    }
    if (kind == Kind::Composition) {
        return fs::path();
    }
    return folderFor(kind) / u8path(name);
}

Catalogue Catalogue::fromLibraries() {
    Catalogue catalogue;
    for (const ClipDef& clip : clipLibrary()) {
        CatalogueEntry entry;
        entry.path = u8path(clip.path);
        entry.fileName = u8text(entry.path.filename());
        switch (clip.kind) {
            case ClipKind::ImageFile:
            case ClipKind::VideoFile:
                entry.kind = Kind::Media;
                catalogue.clips[clip.name] = entry;
                break;
            case ClipKind::ModelFile:
                entry.kind = Kind::Model;
                catalogue.clips[clip.name] = entry;
                break;
            case ClipKind::ScenePack:
                entry.kind = Kind::ScenePack;
                catalogue.clips[clip.name] = entry;
                break;
            case ClipKind::ShaderFile:
                entry.kind = Kind::Shader;
                catalogue.clips[clip.name] = entry;
                break;
            case ClipKind::SpoutSource:
                catalogue.liveClips[clip.name] = "Spout";
                break;
            case ClipKind::NdiSource:
                catalogue.liveClips[clip.name] = "NDI";
                break;
            case ClipKind::CameraSource:
                catalogue.liveClips[clip.name] = "Camera";
                break;
            case ClipKind::PluginSource:
                catalogue.pluginClips[clip.name] = clip.name;
                break;
            case ClipKind::Shader:
            case ClipKind::BuiltinScene:
                catalogue.builtInClips.insert(clip.name);
                break;
        }
    }
    for (const EffectDef& effect : effectLibrary()) {
        if (effect.isGraph || effect.isIsf) {
            CatalogueEntry entry;
            entry.kind = effect.isGraph ? Kind::Node : Kind::Shader;
            entry.path = u8path(effect.path);
            entry.fileName = u8text(entry.path.filename());
            catalogue.effects[effect.name] = entry;
        } else {
            catalogue.builtInEffects.insert(effect.name);
        }
    }
    return catalogue;
}

bool collectReferences(const std::string& jsonText, References& out, std::string& error) {
    if (jsonText.size() > kMaxJsonText || !jsonNestingOk(jsonText, kJsonDepth)) {
        error = "The file is too large or nested too deeply";
        return false;
    }
    const nlohmann::json doc = nlohmann::json::parse(jsonText, nullptr, false);
    if (doc.is_discarded()) {
        error = "The file cannot be read";
        return false;
    }
    collectWalk(doc, out);
    return true;
}

bool renameReferences(const std::string& jsonText, const NameMap& clips, const NameMap& effects, std::string& out, std::string& error) {
    if (clips.empty() && effects.empty()) {
        out = jsonText;
        return true;
    }
    if (jsonText.size() > kMaxJsonText || !jsonNestingOk(jsonText, kJsonDepth)) {
        error = "The file is too large or nested too deeply";
        return false;
    }
    nlohmann::json doc = nlohmann::json::parse(jsonText, nullptr, false);
    if (doc.is_discarded()) {
        error = "The file cannot be read";
        return false;
    }
    renameWalk(doc, clips, effects);
    out = doc.dump(2, ' ', false, nlohmann::json::error_handler_t::replace);
    return true;
}

void collectShaderMediaReferences(const std::string& shaderText, std::set<std::string>& clips) {
    if (shaderText.size() > kMaxShaderText) {
        return;
    }
    static const std::regex pattern("channel[ \\t]+[0-9]+[ \\t]+media[ \\t]+\"([^\"\\r\\n]{1,200})\"");
    for (std::sregex_iterator it(shaderText.begin(), shaderText.end(), pattern), end; it != end; ++it) {
        clips.insert((*it)[1].str());
    }
}

std::string clipNameForFile(Kind kind, const std::string& fileName) {
    const std::string stem = stemOf(fileName);
    switch (kind) {
        case Kind::Media:
            return isVideoName(fileName) ? videoClipName(stem) : imageClipName(stem);
        case Kind::Model: {
            const std::string extension = extensionOf(fileName);
            return extension == ".glb" || extension == ".gltf" ? "Model: " + stem : std::string();
        }
        case Kind::ScenePack:
            return packClipName(stem);
        case Kind::Shader:
            return shaderClipName(stem);
        default:
            break;
    }
    return std::string();
}

std::string effectNameForFile(Kind kind, const std::string& fileName) {
    if (kind == Kind::Node) {
        return graphEffectName(stemOf(fileName));
    }
    if (kind == Kind::Shader) {
        return isfEffectName(stemOf(fileName));
    }
    return std::string();
}

std::vector<std::string> gltfExternalFiles(const std::string& gltfText) {
    std::vector<std::string> files;
    if (gltfText.size() > kMaxJsonText || !jsonNestingOk(gltfText, kJsonDepth)) {
        return files;
    }
    const nlohmann::json doc = nlohmann::json::parse(gltfText, nullptr, false);
    if (doc.is_discarded() || !doc.is_object()) {
        return files;
    }
    for (const char* key : {"buffers", "images"}) {
        auto list = doc.find(key);
        if (list == doc.end() || !list->is_array()) {
            continue;
        }
        for (const nlohmann::json& entry : *list) {
            if (!entry.is_object()) {
                continue;
            }
            auto uri = entry.find("uri");
            if (uri == entry.end() || !uri->is_string()) {
                continue;
            }
            const std::string text = uri->get<std::string>();
            if (text.compare(0, 5, "data:") == 0 || text.empty()) {
                continue;
            }
            const std::string decoded = percentDecode(text);
            if (std::find(files.begin(), files.end(), decoded) == files.end()) {
                files.push_back(decoded);
            }
        }
    }
    return files;
}

std::vector<std::string> venueMaskImages(const std::string& venueText) {
    std::vector<std::string> images;
    if (venueText.size() > kMaxJsonText || !jsonNestingOk(venueText, kJsonDepth)) {
        return images;
    }
    const nlohmann::json doc = nlohmann::json::parse(venueText, nullptr, false);
    if (!doc.is_discarded()) {
        collectMaskImages(doc, images);
    }
    return images;
}

int compareVersions(const std::string& left, const std::string& right) {
    auto parts = [](const std::string& text) {
        std::vector<long> numbers;
        size_t i = 0;
        while (i < text.size() && numbers.size() < 4) {
            if (std::isdigit(static_cast<unsigned char>(text[i]))) {
                long value = 0;
                while (i < text.size() && std::isdigit(static_cast<unsigned char>(text[i]))) {
                    value = std::min(value * 10 + (text[i] - '0'), 1000000L);
                    ++i;
                }
                numbers.push_back(value);
                if (i < text.size() && text[i] == '.') {
                    ++i;
                    continue;
                }
                break;
            }
            break;
        }
        numbers.resize(4, 0);
        return numbers;
    };
    const std::vector<long> a = parts(left);
    const std::vector<long> b = parts(right);
    for (size_t i = 0; i < 4; ++i) {
        if (a[i] != b[i]) {
            return a[i] < b[i] ? -1 : 1;
        }
    }
    return 0;
}

unsigned long long ExportPlan::includedBytes() const {
    unsigned long long total = 0;
    for (const PlanItem& item : items) {
        if (item.problem == Problem::None && item.source.include) {
            total += item.size;
        }
    }
    return total;
}

size_t ExportPlan::includedCount() const {
    return static_cast<size_t>(std::count_if(items.begin(), items.end(), [](const PlanItem& item) { return item.problem == Problem::None && item.source.include; }));
}

size_t ExportPlan::missingCount() const {
    return static_cast<size_t>(std::count_if(items.begin(), items.end(), [](const PlanItem& item) { return item.problem == Problem::Missing; }));
}

size_t ExportPlan::blockedCount() const {
    return static_cast<size_t>(std::count_if(items.begin(), items.end(), [](const PlanItem& item) { return item.problem == Problem::TooLarge || item.problem == Problem::UnsafeName; }));
}

size_t ExportPlan::withoutLicenseCount() const {
    return static_cast<size_t>(std::count_if(items.begin(), items.end(), [](const PlanItem& item) {
        return item.problem == Problem::None && item.source.include && item.source.kind == Kind::Media && item.source.license.empty();
    }));
}

WriteRequest ExportPlan::toRequest(const std::string& name, const std::string& notes, const std::string& appVersion, const std::string& created) const {
    WriteRequest request;
    request.name = name;
    request.notes = notes;
    request.appVersion = appVersion;
    request.created = created;
    request.plugins = plugins;
    request.live = live;
    for (const PlanItem& item : items) {
        if (item.problem == Problem::UnsafeName || item.problem == Problem::TooLarge) {
            continue;
        }
        request.items.push_back(item.source);
    }
    return request;
}

ExportPlan planExport(const ExportRequest& request, const Catalogue& catalogue, const Layout& layout, const Limits& limits) {
    PlanBuilder builder(catalogue, layout, limits);
    if (!request.compositionJson.empty()) {
        builder.addData(Kind::Composition, "composition.json", request.compositionJson, "show");
        References references;
        std::string error;
        if (collectReferences(request.compositionJson, references, error)) {
            builder.addReferences(references, "composition");
        } else {
            builder.plan.warnings.push_back({Warn::UnreadableFile, "composition.json"});
        }
    }
    for (const std::string& preset : request.presets) {
        const std::string fileName = preset + ".json";
        const fs::path file = layout.presets / u8path(fileName);
        builder.addFile(Kind::Preset, fileName, file, "show");
        std::string text;
        References references;
        std::string error;
        if (readText(file, kMaxJsonText, text) && collectReferences(text, references, error)) {
            builder.addReferences(references, "preset " + preset);
        } else if (fs::exists(file)) {
            builder.plan.warnings.push_back({Warn::UnreadableFile, fileName});
        }
    }
    for (const std::string& venue : request.venues) {
        const std::string fileName = venue + ".json";
        const fs::path file = layout.venues / u8path(fileName);
        builder.addFile(Kind::Venue, fileName, file, "show");
        std::string text;
        if (readText(file, kMaxJsonText, text)) {
            for (const std::string& image : venueMaskImages(text)) {
                builder.addFile(Kind::Mask, image, layout.masks / u8path(image), "venue " + venue);
            }
        }
    }
    if (request.includeCues) {
        builder.addFile(Kind::Cues, "cues.json", layout.cues, "show");
    }
    if (request.includeTimeline) {
        builder.addFile(Kind::Timeline, "timeline.json", layout.timeline, "show");
    }
    builder.finish();
    return std::move(builder.plan);
}

size_t ImportPlan::count(State state) const {
    return static_cast<size_t>(std::count_if(entries.begin(), entries.end(), [&](const ImportEntry& entry) { return entry.state == state; }));
}

size_t ImportPlan::count(Action action) const {
    return static_cast<size_t>(std::count_if(entries.begin(), entries.end(), [&](const ImportEntry& entry) { return entry.action == action; }));
}

ImportPlan planImport(const fs::path& package, const Inspection& inspection, const Layout& layout, const Catalogue& catalogue, const ImportOptions& options, const ProgressFn& progress) {
    ImportPlan plan;
    if (!inspection.ok) {
        plan.error = inspection.error;
        return plan;
    }
    plan.manifest = inspection.manifest;
    const Manifest& manifest = plan.manifest;

    std::set<std::string> taken;
    for (const Item& item : manifest.items) {
        taken.insert(pairKey(item.kind, item.name));
    }

    Progress state;
    state.total = manifest.items.size();
    size_t index = 0;
    for (const Item& item : manifest.items) {
        state.done = index++;
        state.current = item.name;
        if (progress && !progress(state)) {
            plan.cancelled = true;
            plan.error = "Cancelled";
            return plan;
        }
        ImportEntry entry;
        entry.item = item;
        entry.finalName = item.name;
        if (item.kind == Kind::Composition) {
            entry.state = State::New;
            entry.action = item.included ? Action::Apply : Action::Skip;
            if (!item.included) {
                entry.state = State::NotIncluded;
            }
            plan.entries.push_back(std::move(entry));
            continue;
        }
        entry.target = layout.pathFor(item.kind, item.name);
        const bool stateKind = item.kind == Kind::Cues || item.kind == Kind::Timeline;
        const bool wanted = !stateKind || (item.kind == Kind::Cues ? options.applyCues : options.applyTimeline);
        std::error_code ec;
        const bool exists = fs::exists(entry.target, ec);
        if (exists && fs::is_directory(entry.target, ec)) {
            entry.state = State::Blocked;
            entry.action = Action::Skip;
            plan.entries.push_back(std::move(entry));
            continue;
        }
        if (!wanted) {
            entry.state = State::NotApplied;
            entry.action = Action::Skip;
            plan.entries.push_back(std::move(entry));
            continue;
        }
        if (!item.included) {
            if (exists && sameFile(entry.target, item.size, item.sha256)) {
                entry.state = State::Identical;
            } else {
                entry.state = State::NotIncluded;
            }
            entry.action = Action::Skip;
            plan.entries.push_back(std::move(entry));
            continue;
        }
        if (!exists) {
            entry.state = State::New;
            entry.action = Action::Copy;
        } else if (sameFile(entry.target, item.size, item.sha256)) {
            entry.state = State::Identical;
            entry.action = Action::Skip;
        } else {
            entry.state = State::Different;
            if (stateKind) {
                entry.action = Action::ReplaceFile;
            } else if (options.policy == Policy::Replace) {
                entry.action = Action::ReplaceFile;
            } else if (options.policy == Policy::KeepBoth) {
                entry.action = Action::CopyRenamed;
                int number = 2;
                std::string candidate;
                for (;; ++number) {
                    candidate = numberedName(item.name, number);
                    std::string reason;
                    if (!isSafeItemName(item.kind, candidate, &reason)) {
                        candidate.clear();
                        break;
                    }
                    if (taken.count(pairKey(item.kind, candidate)) != 0 || fs::exists(layout.pathFor(item.kind, candidate), ec)) {
                        continue;
                    }
                    break;
                }
                if (candidate.empty()) {
                    entry.action = Action::Skip;
                } else {
                    taken.insert(pairKey(item.kind, candidate));
                    entry.finalName = candidate;
                    entry.target = layout.pathFor(item.kind, candidate);
                }
            } else {
                entry.action = Action::Skip;
            }
        }
        plan.entries.push_back(std::move(entry));
    }

    std::set<std::string> presentClips;
    std::set<std::string> presentEffects;
    for (const ImportEntry& entry : plan.entries) {
        const Item& item = entry.item;
        if (item.kind == Kind::Composition) {
            continue;
        }
        if (entry.action == Action::CopyRenamed) {
            const std::string from = clipNameForFile(item.kind, item.name);
            const std::string to = clipNameForFile(item.kind, entry.finalName);
            if (!from.empty() && !to.empty()) {
                plan.clipRenames[from] = to;
            }
            const std::string fromEffect = effectNameForFile(item.kind, item.name);
            const std::string toEffect = effectNameForFile(item.kind, entry.finalName);
            if (!fromEffect.empty() && !toEffect.empty()) {
                plan.effectRenames[fromEffect] = toEffect;
            }
        }
        if (entry.action == Action::Skip && entry.state != State::Identical && entry.state != State::Different) {
            std::error_code ec;
            if (!(entry.state == State::NotIncluded && fs::exists(entry.target, ec))) {
                continue;
            }
        }
        const std::string clip = clipNameForFile(item.kind, entry.finalName);
        if (!clip.empty()) {
            presentClips.insert(clip);
        }
        const std::string effect = effectNameForFile(item.kind, entry.finalName);
        if (!effect.empty()) {
            presentEffects.insert(effect);
        }
    }

    std::set<std::string> unresolved;
    std::set<std::string> renamedShaders;
    for (const ImportEntry& entry : plan.entries) {
        const Item& item = entry.item;
        const bool scansReferences = item.kind == Kind::Composition || (item.kind == Kind::Preset && (entry.action == Action::Copy || entry.action == Action::CopyRenamed || entry.action == Action::ReplaceFile));
        if (scansReferences && item.included) {
            std::string text;
            std::string error;
            References references;
            if (!readItemData(package, item, kMaxJsonText, text, error) || !collectReferences(text, references, error)) {
                plan.warnings.push_back({Warn::UnreadableFile, item.name});
                continue;
            }
            for (const std::string& clip : references.clips) {
                auto renamed = plan.clipRenames.find(clip);
                const std::string name = renamed != plan.clipRenames.end() ? renamed->second : clip;
                const bool known = catalogue.clips.count(name) != 0 || catalogue.builtInClips.count(name) != 0 || catalogue.liveClips.count(name) != 0 || catalogue.pluginClips.count(name) != 0 || presentClips.count(name) != 0;
                if (!known) {
                    unresolved.insert(name);
                }
            }
            for (const std::string& effect : references.effects) {
                auto renamed = plan.effectRenames.find(effect);
                const std::string name = renamed != plan.effectRenames.end() ? renamed->second : effect;
                const bool known = catalogue.effects.count(name) != 0 || catalogue.builtInEffects.count(name) != 0 || presentEffects.count(name) != 0;
                if (!known) {
                    unresolved.insert(name);
                }
            }
        }
        if (item.kind == Kind::Shader && item.included && !plan.clipRenames.empty() && (entry.action == Action::Copy || entry.action == Action::CopyRenamed || entry.action == Action::ReplaceFile)) {
            std::string text;
            std::string error;
            if (readItemData(package, item, kMaxShaderText, text, error)) {
                std::set<std::string> media;
                collectShaderMediaReferences(text, media);
                for (const std::string& name : media) {
                    if (plan.clipRenames.count(name) != 0) {
                        renamedShaders.insert(entry.finalName);
                    }
                }
            }
        }
    }
    plan.unresolved.assign(unresolved.begin(), unresolved.end());
    for (const std::string& shader : renamedShaders) {
        plan.warnings.push_back({Warn::UnsafeExternalFile, shader});
    }

    if (!options.appVersion.empty() && !manifest.appVersion.empty() && compareVersions(manifest.appVersion, options.appVersion) > 0) {
        plan.warnings.push_back({Warn::NewerApp, manifest.appVersion});
    }
    for (const std::string& plugin : manifest.plugins) {
        if (catalogue.pluginClips.count(plugin) == 0) {
            plan.warnings.push_back({Warn::PluginNeeded, plugin});
        }
    }

    for (const ImportEntry& entry : plan.entries) {
        if (entry.action == Action::Copy || entry.action == Action::CopyRenamed || entry.action == Action::ReplaceFile) {
            plan.copyBytes += entry.item.size;
        }
    }
    std::error_code spaceEc;
    fs::path probe = layout.root;
    while (!probe.empty() && !fs::exists(probe, spaceEc) && probe.has_parent_path() && probe.parent_path() != probe) {
        probe = probe.parent_path();
    }
    const fs::space_info space = fs::space(probe, spaceEc);
    if (!spaceEc) {
        plan.freeBytes = static_cast<unsigned long long>(space.available);
        plan.enoughSpace = plan.copyBytes + plan.copyBytes / 20 + 1024 * 1024 <= plan.freeBytes;
    }
    plan.ok = true;
    return plan;
}

namespace {

struct Staged {
    const ImportEntry* entry = nullptr;
    fs::path temporary;
};

struct Committed {
    fs::path target;
    fs::path backup;
    bool hadBackup = false;
};

void discard(std::vector<Staged>& staged) {
    std::error_code ec;
    for (const Staged& item : staged) {
        if (!item.temporary.empty()) {
            fs::remove(item.temporary, ec);
        }
    }
    staged.clear();
}

void rollBack(std::vector<Committed>& done) {
    std::error_code ec;
    for (auto it = done.rbegin(); it != done.rend(); ++it) {
        fs::remove(it->target, ec);
        if (it->hadBackup) {
            movePath(it->backup, it->target);
        }
    }
    done.clear();
}

}

ImportResult applyImport(const fs::path& package, const ImportPlan& plan, const Layout& layout, const std::string& backupStamp, const ProgressFn& progress) {
    ImportResult result;
    if (!plan.ok) {
        result.error = plan.error.empty() ? "The import was not planned" : plan.error;
        return result;
    }
    if (!plan.enoughSpace) {
        result.error = "There is not enough free space for this show";
        return result;
    }
    std::vector<Staged> staged;
    Progress state;
    state.total = plan.copyBytes;
    unsigned long long base = 0;
    bool cancelled = false;
    auto relay = [&](const Progress& inner) {
        state.done = base + inner.done;
        state.current = inner.current;
        if (progress && !progress(state)) {
            cancelled = true;
            return false;
        }
        return true;
    };

    for (const ImportEntry& entry : plan.entries) {
        const Item& item = entry.item;
        if (item.kind == Kind::Composition && entry.action == Action::Apply) {
            std::string text;
            std::string error;
            if (!readItemData(package, item, kMaxJsonText, text, error) || !renameReferences(text, plan.clipRenames, plan.effectRenames, result.compositionJson, error)) {
                discard(staged);
                result.error = error;
                return result;
            }
            continue;
        }
        const bool writes = entry.action == Action::Copy || entry.action == Action::CopyRenamed || entry.action == Action::ReplaceFile;
        if (!writes) {
            continue;
        }
        fs::path temporary = entry.target;
        temporary += ".nyxshow-new";
        std::string error;
        if (item.kind == Kind::Preset && (!plan.clipRenames.empty() || !plan.effectRenames.empty())) {
            std::string text;
            std::string renamed;
            if (!readItemData(package, item, kMaxJsonText, text, error) || !renameReferences(text, plan.clipRenames, plan.effectRenames, renamed, error) || !writeBinary(temporary, renamed)) {
                discard(staged);
                std::error_code ec;
                fs::remove(temporary, ec);
                result.error = error.empty() ? "The file could not be written" : error;
                return result;
            }
            staged.push_back({&entry, temporary});
            base += item.size;
            continue;
        }
        if (!extractItem(package, item, temporary, true, relay, error)) {
            discard(staged);
            std::error_code ec;
            fs::remove(temporary, ec);
            result.cancelled = cancelled;
            result.error = cancelled ? "Cancelled" : error;
            return result;
        }
        staged.push_back({&entry, temporary});
        base += item.size;
    }

    std::vector<Committed> committed;
    fs::path backupRoot;
    for (Staged& item : staged) {
        const ImportEntry& entry = *item.entry;
        std::error_code ec;
        Committed op;
        op.target = entry.target;
        if (fs::is_directory(entry.target, ec)) {
            discard(staged);
            rollBack(committed);
            result.rolledBack = true;
            result.error = "A folder is in the way of a file of the show";
            return result;
        }
        if (fs::exists(entry.target, ec)) {
            if (backupRoot.empty()) {
                backupRoot = layout.root / "show_backups" / u8path(backupStamp);
                int suffix = 2;
                while (fs::exists(backupRoot, ec)) {
                    backupRoot = layout.root / "show_backups" / u8path(backupStamp + "-" + std::to_string(suffix++));
                }
            }
            const fs::path folder = entry.item.kind == Kind::Cues || entry.item.kind == Kind::Timeline ? backupRoot : backupRoot / u8path(kindName(entry.item.kind));
            fs::create_directories(folder, ec);
            op.backup = folder / entry.target.filename();
            if (!movePath(entry.target, op.backup)) {
                discard(staged);
                rollBack(committed);
                result.rolledBack = true;
                result.error = "An existing file could not be saved before it was replaced";
                return result;
            }
            op.hadBackup = true;
        }
        if (entry.target.has_parent_path()) {
            fs::create_directories(entry.target.parent_path(), ec);
        }
        fs::rename(item.temporary, entry.target, ec);
        if (ec) {
            if (op.hadBackup) {
                movePath(op.backup, op.target);
            }
            discard(staged);
            rollBack(committed);
            result.rolledBack = true;
            result.error = "A file could not be moved to its place";
            return result;
        }
        item.temporary.clear();
        committed.push_back(op);
        result.written.push_back(u8text(entry.target.filename()));
        if (entry.action == Action::ReplaceFile) {
            ++result.replaced;
        } else if (entry.action == Action::CopyRenamed) {
            ++result.renamed;
        } else {
            ++result.copied;
        }
        if (entry.item.kind == Kind::Cues || entry.item.kind == Kind::Timeline) {
            ++result.applied;
        }
    }
    for (const ImportEntry& entry : plan.entries) {
        if (entry.state == State::Different && entry.action == Action::Skip) {
            ++result.kept;
        }
    }
    result.backupFolder = backupRoot;
    result.unresolved = plan.unresolved;
    result.ok = true;
    return result;
}

RepairScan findMissingFiles(const ImportPlan& plan, const fs::path& folder, const ProgressFn& progress) {
    RepairScan scan;
    std::map<std::string, std::vector<size_t>> wanted;
    for (size_t i = 0; i < plan.entries.size(); ++i) {
        const ImportEntry& entry = plan.entries[i];
        if (entry.state == State::NotIncluded && entry.item.kind != Kind::Composition) {
            wanted[lower(entry.item.name)].push_back(i);
        }
    }
    if (wanted.empty()) {
        return scan;
    }
    std::error_code ec;
    if (!fs::is_directory(folder, ec)) {
        return scan;
    }
    std::vector<std::pair<size_t, fs::path>> found;
    fs::recursive_directory_iterator it(folder, fs::directory_options::skip_permission_denied, ec);
    const fs::recursive_directory_iterator end;
    Progress state;
    for (; !ec && it != end; it.increment(ec)) {
        if (++scan.scanned > kRepairMaxEntries) {
            scan.truncated = true;
            break;
        }
        std::error_code entryEc;
        if (it->is_symlink(entryEc)) {
            it.disable_recursion_pending();
            continue;
        }
        if (it->is_directory(entryEc)) {
            if (it.depth() >= kRepairMaxDepth) {
                it.disable_recursion_pending();
            }
            continue;
        }
        if (!it->is_regular_file(entryEc)) {
            continue;
        }
        if ((scan.scanned % 200) == 0 && progress) {
            state.done = scan.scanned;
            state.total = kRepairMaxEntries;
            state.current = u8text(it->path().filename());
            if (!progress(state)) {
                scan.cancelled = true;
                return scan;
            }
        }
        auto match = wanted.find(lower(u8text(it->path().filename())));
        if (match == wanted.end()) {
            continue;
        }
        for (size_t entry : match->second) {
            found.emplace_back(entry, it->path());
        }
    }
    std::map<size_t, RepairCandidate> best;
    for (const auto& [entryIndex, path] : found) {
        const Item& item = plan.entries[entryIndex].item;
        RepairCandidate candidate;
        candidate.entry = entryIndex;
        candidate.found = path;
        const bool checksumKnown = !item.sha256.empty();
        candidate.verified = checksumKnown && sameFile(path, item.size, item.sha256);
        if (checksumKnown && !candidate.verified) {
            continue;
        }
        auto existing = best.find(entryIndex);
        if (existing == best.end() || (candidate.verified && !existing->second.verified)) {
            best[entryIndex] = candidate;
        }
    }
    for (auto& [entryIndex, candidate] : best) {
        scan.candidates.push_back(candidate);
    }
    return scan;
}

bool applyRepair(const ImportPlan& plan, const std::vector<RepairCandidate>& accepted, std::vector<std::string>& copied, std::string& error, const ProgressFn& progress) {
    Progress state;
    state.total = accepted.size();
    size_t index = 0;
    for (const RepairCandidate& candidate : accepted) {
        state.done = index++;
        if (candidate.entry >= plan.entries.size()) {
            error = "The file does not belong to this show";
            return false;
        }
        const ImportEntry& entry = plan.entries[candidate.entry];
        if (entry.state != State::NotIncluded || entry.item.kind == Kind::Composition) {
            error = "The file does not belong to this show";
            return false;
        }
        state.current = entry.item.name;
        if (progress && !progress(state)) {
            error = "Cancelled";
            return false;
        }
        std::error_code ec;
        if (fs::exists(entry.target, ec)) {
            continue;
        }
        if (candidate.verified && !sameFile(candidate.found, entry.item.size, entry.item.sha256)) {
            error = "The file changed while it was being checked: " + entry.item.name;
            return false;
        }
        if (entry.target.has_parent_path()) {
            fs::create_directories(entry.target.parent_path(), ec);
        }
        fs::path temporary = entry.target;
        temporary += ".nyxshow-new";
        fs::copy_file(candidate.found, temporary, fs::copy_options::overwrite_existing, ec);
        if (ec) {
            fs::remove(temporary, ec);
            error = "The file could not be copied: " + entry.item.name;
            return false;
        }
        if (candidate.verified && !sameFile(temporary, entry.item.size, entry.item.sha256)) {
            fs::remove(temporary, ec);
            error = "The copy does not match its checksum: " + entry.item.name;
            return false;
        }
        fs::rename(temporary, entry.target, ec);
        if (ec) {
            fs::remove(temporary, ec);
            error = "The file could not be moved to its place: " + entry.item.name;
            return false;
        }
        copied.push_back(entry.item.name);
    }
    return true;
}

}
