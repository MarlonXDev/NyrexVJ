#include "check.hpp"

#include "core/log.hpp"
#include "core/preset.hpp"
#include "core/sha256.hpp"
#include "core/show_package.hpp"
#include "core/show_workflow.hpp"
#include "render/clip_library.hpp"
#include "render/effect_library.hpp"
#include "render/shader_source.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace {

namespace fs = std::filesystem;
using namespace nvj::show;

fs::path asPath(const std::string& text) {
    return fs::path(std::u8string(text.begin(), text.end()));
}

fs::path scratchDirectory(const std::string& name) {
    const fs::path path = fs::temp_directory_path() / name;
    std::error_code ec;
    fs::remove_all(path, ec);
    fs::create_directories(path, ec);
    return path;
}

void writeBytes(const fs::path& file, const std::string& data) {
    std::error_code ec;
    fs::create_directories(file.parent_path(), ec);
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    out.write(data.data(), static_cast<std::streamsize>(data.size()));
}

std::string readBytes(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

std::string pattern(size_t size, unsigned seed) {
    std::string data(size, '\0');
    unsigned state = seed * 2654435761u + 99u;
    for (char& c : data) {
        state = state * 1664525u + 1013904223u;
        c = static_cast<char>(state >> 24);
    }
    return data;
}

std::vector<std::string> filesUnder(const fs::path& root) {
    std::vector<std::string> names;
    std::error_code ec;
    if (!fs::exists(root, ec)) {
        return names;
    }
    for (fs::recursive_directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec)) {
        if (it->is_regular_file(ec)) {
            names.push_back(fs::relative(it->path(), root, ec).generic_string());
        }
    }
    std::sort(names.begin(), names.end());
    return names;
}

bool anyLeftover(const fs::path& root) {
    for (const std::string& name : filesUnder(root)) {
        if (name.find(".nyxshow-new") != std::string::npos || name.find(".nyxshow-part") != std::string::npos) {
            return true;
        }
    }
    return false;
}

Catalogue catalogueFor(const Layout& layout) {
    Catalogue catalogue;
    catalogue.builtInClips.insert("Plasma");
    catalogue.builtInEffects.insert("Blur");
    catalogue.liveClips["NDI: Cam"] = "NDI";
    catalogue.pluginClips["Plug: Sparks"] = "Plug: Sparks";
    auto scan = [&](Kind kind, const fs::path& folder) {
        std::error_code ec;
        for (fs::directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec)) {
            if (!it->is_regular_file(ec)) {
                continue;
            }
            CatalogueEntry entry;
            entry.kind = kind;
            entry.fileName = it->path().filename().string();
            entry.path = it->path();
            const std::string clip = clipNameForFile(kind, entry.fileName);
            if (kind == Kind::Shader) {
                if (entry.fileName.size() > 3 && entry.fileName.substr(entry.fileName.size() - 3) == ".fs") {
                    catalogue.effects[effectNameForFile(kind, entry.fileName)] = entry;
                } else {
                    catalogue.clips[clip] = entry;
                }
            } else if (kind == Kind::Node) {
                catalogue.effects[effectNameForFile(kind, entry.fileName)] = entry;
            } else if (!clip.empty()) {
                catalogue.clips[clip] = entry;
            }
        }
    };
    scan(Kind::Media, layout.media);
    scan(Kind::Model, layout.models);
    scan(Kind::ScenePack, layout.scenePacks);
    scan(Kind::Shader, layout.shaders);
    scan(Kind::Node, layout.nodes);
    return catalogue;
}

std::string loopClip() { return clipNameForFile(Kind::Media, "loop.mp4"); }
std::string coverClip() { return clipNameForFile(Kind::Media, "cover.png"); }
std::string tunnelClip() { return clipNameForFile(Kind::Shader, "tunnel.frag"); }

std::string compositionText() {
    nlohmann::json doc;
    doc["format"] = 2;
    doc["name"] = "Friday";
    doc["bpm"] = 126;
    nlohmann::json layer0;
    layer0["cells"] = nlohmann::json::array({loopClip(), coverClip(), nullptr, "Plasma", "NDI: Cam", "Model: robot", "Scene: city", tunnelClip()});
    layer0["effects"] = nlohmann::json::array({nlohmann::json{{"effect", "Node: glow"}, {"enabled", true}}, nlohmann::json{{"effect", "Blur"}}, nlohmann::json{{"effect", nullptr}}});
    nlohmann::json layer1;
    layer1["cells"] = nlohmann::json::array({"Model: arm", "Plug: Sparks"});
    doc["layers"] = nlohmann::json::array({layer0, layer1});
    nlohmann::json master;
    master["effects"] = nlohmann::json::array({nlohmann::json{{"effect", "ISF: blur"}}});
    doc["master"] = master;
    nlohmann::json deck;
    deck["slot"] = 2;
    deck["layers"] = nlohmann::json::array({nlohmann::json{{"cells", nlohmann::json::array({loopClip()})}}});
    doc["decks"] = nlohmann::json{{"slots", nlohmann::json::array({deck})}};
    return doc.dump(2);
}

struct Source {
    fs::path root;
    Layout layout;
    std::map<std::string, std::string> content;
};

Source buildSource(const fs::path& root) {
    Source source;
    source.root = root;
    source.layout = Layout::underRoot(root);
    const Layout& l = source.layout;
    auto put = [&](const fs::path& file, const std::string& data) {
        writeBytes(file, data);
        source.content[fs::relative(file, root).generic_string()] = data;
    };
    put(l.media / "loop.mp4", pattern(70000, 1));
    put(l.media / "cover.png", pattern(2000, 2));
    put(l.media / "extra.webm", pattern(900, 3));
    put(l.models / "robot.glb", pattern(3000, 4));
    put(l.models / "arm.gltf", "{\"buffers\":[{\"uri\":\"arm.bin\"}],\"images\":[{\"uri\":\"arm%20tex.png\"},{\"uri\":\"data:image/png;base64,AAAA\"},{\"uri\":\"..%2Fevil.png\"}]}");
    put(l.models / "arm.bin", pattern(400, 5));
    put(l.models / "arm tex.png", pattern(300, 6));
    put(l.scenePacks / "city.nyxclip", pattern(500, 7));
    put(l.shaders / "tunnel.frag", "// channel 1 media \"Video: extra\"\nchannel 1 media \"Video: extra\"\nvoid main(){}\n");
    put(l.shaders / "blur.fs", pattern(200, 8));
    put(l.nodes / "glow.nyxfx", "{\"version\":1}");
    put(l.masks / "left.png", pattern(250, 9));
    put(l.venues / "Main.json", "{\"format\":\"nyrexvj-venue\",\"outputs\":[{\"slices\":[{\"mask\":{\"image\":\"left.png\"}}]}]}");
    nlohmann::json warm;
    warm["layers"] = nlohmann::json::array({nlohmann::json{{"cells", nlohmann::json::array({"Scene: city", "Video: extra"})}, {"effects", nlohmann::json::array({nlohmann::json{{"effect", "Node: glow"}}})}}});
    put(l.presets / "Warm.json", warm.dump(2));
    put(l.cues, "{\"cues\":[]}");
    put(l.timeline, "{\"tracks\":[]}");
    return source;
}

ExportRequest fullRequest() {
    ExportRequest request;
    request.name = "Friday show";
    request.notes = "Bring the big screen";
    request.compositionJson = compositionText();
    request.presets = {"Warm"};
    request.venues = {"Main"};
    request.includeCues = true;
    request.includeTimeline = true;
    return request;
}

const PlanItem* findPlan(const ExportPlan& plan, Kind kind, const std::string& name) {
    for (const PlanItem& item : plan.items) {
        if (item.source.kind == kind && item.source.name == name) {
            return &item;
        }
    }
    return nullptr;
}

const ImportEntry* findEntry(const ImportPlan& plan, Kind kind, const std::string& name) {
    for (const ImportEntry& entry : plan.entries) {
        if (entry.item.kind == kind && entry.item.name == name) {
            return &entry;
        }
    }
    return nullptr;
}

bool hasWarning(const std::vector<Warning>& warnings, Warn code, const std::string& subject) {
    return std::any_of(warnings.begin(), warnings.end(), [&](const Warning& w) { return w.code == code && w.subject == subject; });
}

bool exportTo(const Source& source, const ExportRequest& request, const fs::path& destination, ExportPlan* planOut = nullptr, const Catalogue* known = nullptr) {
    const Catalogue catalogue = known ? *known : catalogueFor(source.layout);
    ExportPlan plan = planExport(request, catalogue, source.layout);
    const WriteResult written = writePackage(plan.toRequest(request.name, request.notes, "0.41.0", "2026-11-25T12:00:00Z"), destination, true);
    if (planOut) {
        *planOut = plan;
    }
    return written.ok;
}

void testReferences() {
    References references;
    std::string error;
    CHECK(collectReferences(compositionText(), references, error));
    CHECK(references.clips.count(loopClip()) == 1);
    CHECK(references.clips.count("Plasma") == 1);
    CHECK(references.clips.count("Model: arm") == 1);
    CHECK(references.clips.size() == 9);
    CHECK(references.effects.count("Node: glow") == 1);
    CHECK(references.effects.count("ISF: blur") == 1);
    CHECK(references.effects.count("Blur") == 1);
    CHECK(references.effects.size() == 3);
    CHECK(!collectReferences("not json", references, error));
    CHECK(!collectReferences(std::string(3000, '['), references, error));

    NameMap clips{{loopClip(), "Video: loop (2)"}};
    NameMap effects{{"Node: glow", "Node: glow (2)"}};
    std::string renamed;
    CHECK(renameReferences(compositionText(), clips, effects, renamed, error));
    References after;
    CHECK(collectReferences(renamed, after, error));
    CHECK(after.clips.count(loopClip()) == 0);
    CHECK(after.clips.count("Video: loop (2)") == 1);
    CHECK(after.effects.count("Node: glow") == 0);
    CHECK(after.effects.count("Node: glow (2)") == 1);
    CHECK(after.clips.size() == references.clips.size());
    CHECK(renamed.find("Friday") != std::string::npos);

    nlohmann::json other = nlohmann::json::parse(compositionText());
    other["name"] = loopClip();
    other["notes"] = "Node: glow";
    std::string untouched;
    CHECK(renameReferences(other.dump(), clips, effects, untouched, error));
    CHECK(nlohmann::json::parse(untouched)["name"] == loopClip());
    CHECK(nlohmann::json::parse(untouched)["notes"] == "Node: glow");
    CHECK(!renameReferences("{", clips, effects, renamed, error));

    CHECK(clipNameForFile(Kind::Media, "a b.MP4") == "Video: a b");
    CHECK(clipNameForFile(Kind::Media, "a.png") == "Image: a");
    CHECK(clipNameForFile(Kind::Model, "m.glb") == "Model: m");
    CHECK(clipNameForFile(Kind::Model, "m.bin").empty());
    CHECK(clipNameForFile(Kind::ScenePack, "c.nyxclip") == "Scene: c");
    CHECK(clipNameForFile(Kind::Preset, "x.json").empty());
    CHECK(effectNameForFile(Kind::Node, "g.nyxfx") == "Node: g");
    CHECK(effectNameForFile(Kind::Shader, "b.fs") == "ISF: b");
    CHECK(effectNameForFile(Kind::Media, "b.mp4").empty());

    std::set<std::string> media;
    collectShaderMediaReferences("channel 1 media \"Video: a\"\nchannel 2 media \"Image: b\"\nchannel x media \"no\"", media);
    CHECK(media.size() == 2 && media.count("Video: a") == 1);

    const auto files = gltfExternalFiles("{\"buffers\":[{\"uri\":\"a.bin\"},{\"uri\":\"data:x\"}],\"images\":[{\"uri\":\"t%20x.png\"},{\"uri\":\"a.bin\"}]}");
    CHECK(files.size() == 2 && files[1] == "t x.png");
    CHECK(gltfExternalFiles("not json").empty());
    CHECK(venueMaskImages("{\"a\":[{\"mask\":{\"image\":\"l.png\"}},{\"mask\":{\"image\":\"l.png\"}},{\"mask\":{}}]}").size() == 1);
}

void testExportPlan(const fs::path& base) {
    const Source source = buildSource(base / "src-plan");
    const Catalogue catalogue = catalogueFor(source.layout);
    ExportPlan plan = planExport(fullRequest(), catalogue, source.layout);
    CHECK(findPlan(plan, Kind::Composition, "composition.json"));
    CHECK(findPlan(plan, Kind::Media, "loop.mp4"));
    CHECK(findPlan(plan, Kind::Media, "cover.png"));
    CHECK(findPlan(plan, Kind::Media, "extra.webm"));
    CHECK(findPlan(plan, Kind::Model, "robot.glb"));
    CHECK(findPlan(plan, Kind::Model, "arm.gltf"));
    CHECK(findPlan(plan, Kind::Model, "arm.bin"));
    CHECK(findPlan(plan, Kind::Model, "arm tex.png"));
    CHECK(findPlan(plan, Kind::ScenePack, "city.nyxclip"));
    CHECK(findPlan(plan, Kind::Shader, "tunnel.frag"));
    CHECK(findPlan(plan, Kind::Shader, "blur.fs"));
    CHECK(findPlan(plan, Kind::Node, "glow.nyxfx"));
    CHECK(findPlan(plan, Kind::Preset, "Warm.json"));
    CHECK(findPlan(plan, Kind::Venue, "Main.json"));
    CHECK(findPlan(plan, Kind::Mask, "left.png"));
    CHECK(findPlan(plan, Kind::Cues, "cues.json"));
    CHECK(findPlan(plan, Kind::Timeline, "timeline.json"));
    const PlanItem* evil = findPlan(plan, Kind::Model, "../evil.png");
    CHECK(evil == nullptr || evil->problem == Problem::UnsafeName);
    CHECK(plan.plugins.size() == 1 && plan.plugins[0] == "Plug: Sparks");
    CHECK(plan.live.size() == 1 && plan.live[0] == "NDI: NDI: Cam");
    CHECK(plan.missingCount() == 0);
    CHECK(plan.blockedCount() <= 1);
    CHECK(plan.includedBytes() > 70000);
    CHECK(plan.withoutLicenseCount() == 3);
    const PlanItem* loop = findPlan(plan, Kind::Media, "loop.mp4");
    CHECK(loop && loop->neededBy.count("composition") == 1 && loop->size == 70000);
    for (const PlanItem& item : plan.items) {
        CHECK(item.source.file.empty() || item.source.file.string().find(source.root.string()) == 0);
    }

    ExportRequest unknown = fullRequest();
    nlohmann::json doc = nlohmann::json::parse(unknown.compositionJson);
    doc["layers"][0]["cells"].push_back("Video: not in library");
    doc["layers"][0]["effects"].push_back(nlohmann::json{{"effect", "Node: ghost"}});
    unknown.compositionJson = doc.dump();
    plan = planExport(unknown, catalogue, source.layout);
    CHECK(hasWarning(plan.warnings, Warn::UnknownClip, "Video: not in library"));
    CHECK(hasWarning(plan.warnings, Warn::UnknownEffect, "Node: ghost"));

    std::error_code ec;
    fs::remove(source.layout.media / "loop.mp4", ec);
    plan = planExport(fullRequest(), catalogue, source.layout);
    const PlanItem* gone = findPlan(plan, Kind::Media, "loop.mp4");
    CHECK(gone && gone->problem == Problem::Missing && !gone->source.include);
    CHECK(plan.missingCount() == 1);

    Limits tiny;
    tiny.maxTotalBytes = 1000;
    plan = planExport(fullRequest(), catalogue, source.layout, tiny);
    CHECK(plan.blockedCount() > 0);
    const PlanItem* cover = findPlan(plan, Kind::Media, "cover.png");
    CHECK(cover && cover->problem == Problem::TooLarge);

    ExportRequest unsafe = fullRequest();
    unsafe.presets = {"..\\x", "a/b"};
    plan = planExport(unsafe, catalogue, source.layout);
    CHECK(findPlan(plan, Kind::Preset, "..\\x.json")->problem == Problem::UnsafeName);
    CHECK(findPlan(plan, Kind::Preset, "a/b.json")->problem == Problem::UnsafeName);
    const WriteRequest request = plan.toRequest("x", "", "0.41.0", "now");
    for (const SourceItem& item : request.items) {
        CHECK(item.name.find('/') == std::string::npos && item.name.find('\\') == std::string::npos);
    }
}

void testRoundtrip(const fs::path& base) {
    const Source source = buildSource(base / "pc-a");
    const fs::path package = base / "show.nyxshow";
    ExportPlan plan;
    CHECK(exportTo(source, fullRequest(), package, &plan));

    const std::string packageBytes = readBytes(package);
    CHECK(packageBytes.find(source.root.string()) == std::string::npos);
    CHECK(packageBytes.find("pc-a") == std::string::npos);
    CHECK(packageBytes.find("\\\\") == std::string::npos);

    const Inspection inspection = inspectPackage(package);
    CHECK(inspection.ok);
    CHECK(inspection.manifest.plugins.size() == 1);
    CHECK(inspection.manifest.live.size() == 1);

    const Layout target = Layout::underRoot(base / "pc-b-other-user" / "Data");
    const Catalogue emptyCatalogue = catalogueFor(target);
    ImportOptions options;
    options.applyCues = true;
    options.applyTimeline = true;
    const ImportPlan dry = planImport(package, inspection, target, emptyCatalogue, options);
    CHECK(dry.ok);
    CHECK(dry.count(State::New) == inspection.manifest.items.size());
    CHECK(dry.copyBytes > 70000);
    CHECK(dry.enoughSpace);
    CHECK(dry.unresolved.empty());
    CHECK(hasWarning(dry.warnings, Warn::PluginNeeded, "Plug: Sparks") == false);
    CHECK(!fs::exists(target.root));

    const ImportResult applied = applyImport(package, dry, target, "20261125-120000");
    CHECK(applied.ok);
    CHECK(!anyLeftover(target.root));
    for (const auto& [relative, data] : source.content) {
        const fs::path moved = target.root / relative;
        CHECK(fs::exists(moved));
        CHECK(readBytes(moved) == data);
    }
    CHECK(applied.compositionJson == compositionText() || nlohmann::json::parse(applied.compositionJson) == nlohmann::json::parse(compositionText()));
    CHECK(applied.applied == 2);
    CHECK(!fs::exists(target.root / "show_backups"));

    const Catalogue afterwards = catalogueFor(target);
    const Catalogue original = catalogueFor(source.layout);
    CHECK(afterwards.clips.size() == original.clips.size());
    CHECK(afterwards.effects.size() == original.effects.size());
    for (const auto& [name, entry] : original.clips) {
        CHECK(afterwards.clips.count(name) == 1);
    }

    const Inspection again = inspectPackage(package);
    const ImportPlan second = planImport(package, again, target, afterwards, options);
    CHECK(second.count(State::Identical) == again.manifest.items.size() - 1);
    CHECK(second.count(Action::Copy) == 0);
    const ImportResult repeat = applyImport(package, second, target, "20261125-120001");
    CHECK(repeat.ok && repeat.copied == 0 && repeat.replaced == 0);

    const ImportPlan guarded = planImport(package, again, target, afterwards, ImportOptions{});
    CHECK(guarded.count(State::NotApplied) == 2);
}

void testMissingFiles(const fs::path& base) {
    const Source source = buildSource(base / "pc-a");
    const Catalogue knownBefore = catalogueFor(source.layout);
    std::error_code ec;
    fs::remove(source.layout.media / "loop.mp4", ec);
    const fs::path package = base / "missing.nyxshow";
    CHECK(exportTo(source, fullRequest(), package, nullptr, &knownBefore));
    const Inspection inspection = inspectPackage(package);
    CHECK(inspection.ok);
    bool flagged = false;
    for (const Item& item : inspection.manifest.items) {
        if (item.name == "loop.mp4") {
            flagged = item.missing && !item.included;
        }
    }
    CHECK(flagged);

    const Layout target = Layout::underRoot(base / "pc-b");
    const Catalogue catalogue = catalogueFor(target);
    const ImportPlan plan = planImport(package, inspection, target, catalogue, ImportOptions{});
    CHECK(plan.ok);
    const ImportEntry* loop = findEntry(plan, Kind::Media, "loop.mp4");
    CHECK(loop && loop->state == State::NotIncluded);
    CHECK(std::find(plan.unresolved.begin(), plan.unresolved.end(), loopClip()) != plan.unresolved.end());
    const ImportResult applied = applyImport(package, plan, target, "stamp");
    CHECK(applied.ok);
    CHECK(!fs::exists(target.media / "loop.mp4"));
    CHECK(applied.compositionJson.find(loopClip()) != std::string::npos);
    CHECK(applied.unresolved.size() == plan.unresolved.size());

    const fs::path search = base / "search";
    writeBytes(search / "deep" / "er" / "loop.MP4", pattern(10, 77));
    writeBytes(search / "other.mp4", "x");
    const RepairScan scan = findMissingFiles(plan, search);
    CHECK(scan.candidates.size() == 1);
    CHECK(scan.candidates[0].entry < plan.entries.size());
    CHECK(!scan.candidates[0].verified);
    std::vector<std::string> copied;
    std::string error;
    CHECK(applyRepair(plan, scan.candidates, copied, error));
    CHECK(copied.size() == 1);
    CHECK(fs::exists(target.media / "loop.mp4"));
    CHECK(readBytes(target.media / "loop.mp4") == pattern(10, 77));
    CHECK(!anyLeftover(target.root));
    CHECK(applyRepair(plan, scan.candidates, copied, error));
    CHECK(copied.size() == 1);

    RepairCandidate foreign;
    foreign.entry = 9999;
    CHECK(!applyRepair(plan, {foreign}, copied, error));
    RepairCandidate wrongEntry;
    for (size_t i = 0; i < plan.entries.size(); ++i) {
        if (plan.entries[i].state == State::New) {
            wrongEntry.entry = i;
        }
    }
    wrongEntry.found = search / "other.mp4";
    CHECK(!applyRepair(plan, {wrongEntry}, copied, error));
}

void testVerifiedRepair(const fs::path& base) {
    const Source source = buildSource(base / "pc-a");
    ExportRequest request = fullRequest();
    const Catalogue catalogue = catalogueFor(source.layout);
    ExportPlan plan = planExport(request, catalogue, source.layout);
    for (PlanItem& item : plan.items) {
        if (item.source.kind == Kind::Media) {
            item.source.include = false;
        }
    }
    const fs::path package = base / "slim.nyxshow";
    CHECK(writePackage(plan.toRequest("slim", "", "0.41.0", "now"), package, true).ok);
    const Inspection inspection = inspectPackage(package);
    CHECK(inspection.ok);
    CHECK(fs::file_size(package) < 20000);

    const Layout target = Layout::underRoot(base / "pc-b");
    const ImportPlan importPlan = planImport(package, inspection, target, catalogueFor(target), ImportOptions{});
    CHECK(importPlan.count(State::NotIncluded) == 3);

    const fs::path search = base / "search";
    writeBytes(search / "a" / "loop.mp4", pattern(70000, 99));
    writeBytes(search / "z" / "loop.mp4", source.content.at("media/loop.mp4"));
    writeBytes(search / "cover.png", pattern(2000, 98));
    const RepairScan scan = findMissingFiles(importPlan, search);
    CHECK(scan.candidates.size() == 1);
    for (const RepairCandidate& candidate : scan.candidates) {
        CHECK(importPlan.entries[candidate.entry].item.name == "loop.mp4");
        CHECK(candidate.verified);
        CHECK(candidate.found.parent_path().filename() == "z");
    }
    std::vector<RepairCandidate> onlyVerified;
    for (const RepairCandidate& candidate : scan.candidates) {
        if (candidate.verified) {
            onlyVerified.push_back(candidate);
        }
    }
    std::vector<std::string> copied;
    std::string error;
    CHECK(applyRepair(importPlan, onlyVerified, copied, error));
    CHECK(readBytes(target.media / "loop.mp4") == source.content.at("media/loop.mp4"));
    CHECK(!fs::exists(target.media / "cover.png"));

    writeBytes(search / "z" / "loop.mp4", "changed");
    const ImportPlan fresh = planImport(package, inspection, Layout::underRoot(base / "pc-c"), catalogueFor(Layout::underRoot(base / "pc-c")), ImportOptions{});
    RepairCandidate stale = onlyVerified[0];
    for (size_t i = 0; i < fresh.entries.size(); ++i) {
        if (fresh.entries[i].item.name == "loop.mp4") {
            stale.entry = i;
        }
    }
    CHECK(!applyRepair(fresh, {stale}, copied, error));
    CHECK(!fs::exists(Layout::underRoot(base / "pc-c").media / "loop.mp4"));

    const fs::path deepRoot = base / "deep";
    fs::path deep = deepRoot;
    for (int i = 0; i < 9; ++i) {
        deep /= "d" + std::to_string(i);
    }
    writeBytes(deep / "loop.mp4", source.content.at("media/loop.mp4"));
    CHECK(findMissingFiles(importPlan, deepRoot).candidates.empty());
    fs::path shallow = deepRoot;
    for (int i = 0; i < 5; ++i) {
        shallow /= "s" + std::to_string(i);
    }
    writeBytes(shallow / "loop.mp4", source.content.at("media/loop.mp4"));
    CHECK(findMissingFiles(importPlan, deepRoot).candidates.size() == 1);

    const fs::path linkRoot = base / "links";
    const fs::path realFolder = base / "real-folder";
    writeBytes(realFolder / "loop.mp4", source.content.at("media/loop.mp4"));
    std::error_code ec;
    fs::create_directories(linkRoot, ec);
    fs::create_directory_symlink(realFolder, linkRoot / "link", ec);
    if (!ec) {
        CHECK(findMissingFiles(importPlan, linkRoot).candidates.empty());
    }
    CHECK(findMissingFiles(importPlan, base / "does-not-exist").candidates.empty());

    const fs::path crowded = base / "crowded";
    fs::create_directories(crowded, ec);
    for (size_t i = 0; i < kRepairMaxEntries + 200; ++i) {
        std::ofstream(crowded / ("f" + std::to_string(i) + ".txt")).put('x');
    }
    const RepairScan limited = findMissingFiles(importPlan, crowded);
    CHECK(limited.truncated);
    CHECK(limited.scanned <= kRepairMaxEntries + 1);

    int calls = 0;
    const RepairScan cancelled = findMissingFiles(importPlan, crowded, [&](const Progress&) { return ++calls < 2; });
    CHECK(cancelled.cancelled);
}

void testConflicts(const fs::path& base) {
    const Source source = buildSource(base / "pc-a");
    const fs::path package = base / "show.nyxshow";
    CHECK(exportTo(source, fullRequest(), package));
    const Inspection inspection = inspectPackage(package);

    auto prepare = [&](const char* name) {
        const Layout layout = Layout::underRoot(base / name);
        writeBytes(layout.media / "loop.mp4", "my own loop");
        writeBytes(layout.media / "cover.png", source.content.at("media/cover.png"));
        writeBytes(layout.presets / "Warm.json", "{\"layers\":[],\"mine\":true}");
        writeBytes(layout.presets / "Unrelated.json", "keep me");
        writeBytes(layout.cues, "{\"cues\":[\"mine\"]}");
        return layout;
    };

    {
        const Layout layout = prepare("keep-mine");
        const ImportPlan plan = planImport(package, inspection, layout, catalogueFor(layout), ImportOptions{});
        const ImportEntry* loop = findEntry(plan, Kind::Media, "loop.mp4");
        CHECK(loop && loop->state == State::Different && loop->action == Action::Skip);
        const ImportEntry* cover = findEntry(plan, Kind::Media, "cover.png");
        CHECK(cover && cover->state == State::Identical);
        const ImportResult result = applyImport(package, plan, layout, "s");
        CHECK(result.ok);
        CHECK(result.kept == 2);
        CHECK(readBytes(layout.media / "loop.mp4") == "my own loop");
        CHECK(readBytes(layout.presets / "Warm.json") == "{\"layers\":[],\"mine\":true}");
        CHECK(readBytes(layout.presets / "Unrelated.json") == "keep me");
        CHECK(readBytes(layout.cues) == "{\"cues\":[\"mine\"]}");
        CHECK(!fs::exists(layout.root / "show_backups"));
        CHECK(result.compositionJson.find(loopClip()) != std::string::npos);
        CHECK(result.compositionJson.find("(2)") == std::string::npos);
    }
    {
        const Layout layout = prepare("keep-both");
        ImportOptions options;
        options.policy = Policy::KeepBoth;
        writeBytes(layout.media / "loop (2).mp4", "taken");
        const ImportPlan plan = planImport(package, inspection, layout, catalogueFor(layout), options);
        const ImportEntry* loop = findEntry(plan, Kind::Media, "loop.mp4");
        CHECK(loop && loop->action == Action::CopyRenamed && loop->finalName == "loop (3).mp4");
        CHECK(plan.clipRenames.at(loopClip()) == "Video: loop (3)");
        const ImportEntry* preset = findEntry(plan, Kind::Preset, "Warm.json");
        CHECK(preset && preset->finalName == "Warm (2).json");
        const ImportResult result = applyImport(package, plan, layout, "s");
        CHECK(result.ok);
        CHECK(result.renamed == 2);
        CHECK(readBytes(layout.media / "loop.mp4") == "my own loop");
        CHECK(readBytes(layout.media / "loop (3).mp4") == source.content.at("media/loop.mp4"));
        CHECK(readBytes(layout.media / "loop (2).mp4") == "taken");
        CHECK(readBytes(layout.presets / "Warm.json") == "{\"layers\":[],\"mine\":true}");
        References references;
        std::string error;
        CHECK(collectReferences(result.compositionJson, references, error));
        CHECK(references.clips.count("Video: loop (3)") == 1);
        CHECK(references.clips.count(loopClip()) == 0);
        std::string presetText;
        CHECK(!readBytes(layout.presets / "Warm (2).json").empty());
        CHECK(collectReferences(readBytes(layout.presets / "Warm (2).json"), references, error));
        CHECK(plan.unresolved.empty());
        const Catalogue after = catalogueFor(layout);
        CHECK(after.clips.count("Video: loop (3)") == 1);
        CHECK(after.clips.count(loopClip()) == 1);
        CHECK(!anyLeftover(layout.root));
    }
    {
        const Layout layout = prepare("replace");
        ImportOptions options;
        options.policy = Policy::Replace;
        options.applyCues = true;
        const ImportPlan plan = planImport(package, inspection, layout, catalogueFor(layout), options);
        const ImportResult result = applyImport(package, plan, layout, "20261125-130000");
        CHECK(result.ok);
        CHECK(result.replaced == 3);
        CHECK(readBytes(layout.media / "loop.mp4") == source.content.at("media/loop.mp4"));
        CHECK(readBytes(layout.cues) == "{\"cues\":[]}");
        CHECK(readBytes(layout.presets / "Unrelated.json") == "keep me");
        const fs::path backup = layout.root / "show_backups" / "20261125-130000";
        CHECK(result.backupFolder == backup);
        CHECK(readBytes(backup / "media" / "loop.mp4") == "my own loop");
        CHECK(readBytes(backup / "preset" / "Warm.json") == "{\"layers\":[],\"mine\":true}");
        CHECK(readBytes(backup / "cues.json") == "{\"cues\":[\"mine\"]}");
        CHECK(!anyLeftover(layout.root));
    }
    {
        Source longSource = buildSource(base / "pc-long");
        const std::string longName = std::string(115, 'x') + ".mp4";
        writeBytes(longSource.layout.media / longName, "package version");
        nlohmann::json doc = nlohmann::json::parse(compositionText());
        doc["layers"][0]["cells"].push_back("Video: " + std::string(115, 'x'));
        ExportRequest request = fullRequest();
        request.compositionJson = doc.dump();
        const fs::path longPackage = base / "long.nyxshow";
        CHECK(exportTo(longSource, request, longPackage));
        const Layout layout = Layout::underRoot(base / "long-target");
        writeBytes(layout.media / longName, "mine");
        ImportOptions options;
        options.policy = Policy::KeepBoth;
        const ImportPlan plan = planImport(longPackage, inspectPackage(longPackage), layout, catalogueFor(layout), options);
        const ImportEntry* entry = findEntry(plan, Kind::Media, longName);
        CHECK(entry && entry->action == Action::Skip && entry->state == State::Different);
    }
}

void testFailureRollsBack(const fs::path& base) {
    const Source source = buildSource(base / "pc-a");
    const fs::path package = base / "show.nyxshow";
    CHECK(exportTo(source, fullRequest(), package));
    const Inspection inspection = inspectPackage(package);

    {
        const Layout layout = Layout::underRoot(base / "rollback");
        writeBytes(layout.media / "loop.mp4", "mine");
        ImportOptions options;
        options.policy = Policy::Replace;
        ImportPlan plan = planImport(package, inspection, layout, catalogueFor(layout), options);
        CHECK(plan.ok);
        size_t last = 0;
        for (size_t i = 0; i < plan.entries.size(); ++i) {
            if (plan.entries[i].action == Action::Copy || plan.entries[i].action == Action::ReplaceFile) {
                last = i;
            }
        }
        std::error_code ec;
        fs::create_directories(plan.entries[last].target, ec);
        const ImportResult result = applyImport(package, plan, layout, "rb");
        CHECK(!result.ok);
        CHECK(result.rolledBack);
        CHECK(readBytes(layout.media / "loop.mp4") == "mine");
        std::vector<std::string> files = filesUnder(layout.root);
        CHECK(files.size() == 1 && files[0] == "media/loop.mp4");
        CHECK(!anyLeftover(layout.root));
    }
    {
        const fs::path tampered = base / "tampered.nyxshow";
        std::string bytes = readBytes(package);
        const Inspection good = inspectPackage(package);
        unsigned long long offset = 0;
        for (const Item& item : good.manifest.items) {
            if (item.name == "robot.glb") {
                offset = item.offset;
            }
        }
        bytes[static_cast<size_t>(offset) + 5] = static_cast<char>(bytes[static_cast<size_t>(offset) + 5] ^ 0x7F);
        writeBytes(tampered, bytes);
        const Inspection inspected = inspectPackage(tampered);
        CHECK(inspected.ok);
        const Layout layout = Layout::underRoot(base / "tampered-target");
        const ImportPlan plan = planImport(tampered, inspected, layout, catalogueFor(layout), ImportOptions{});
        const ImportResult result = applyImport(tampered, plan, layout, "t");
        CHECK(!result.ok);
        CHECK(filesUnder(layout.root).empty());
    }
    {
        const Layout layout = Layout::underRoot(base / "cancel-target");
        ImportPlan plan = planImport(package, inspection, layout, catalogueFor(layout), ImportOptions{});
        int calls = 0;
        const ImportResult result = applyImport(package, plan, layout, "c", [&](const Progress&) { return ++calls < 2; });
        CHECK(!result.ok && result.cancelled);
        CHECK(filesUnder(layout.root).empty());

        int planCalls = 0;
        const ImportPlan cancelledPlan = planImport(package, inspection, layout, catalogueFor(layout), ImportOptions{}, [&](const Progress&) { return ++planCalls < 3; });
        CHECK(!cancelledPlan.ok && cancelledPlan.cancelled);
    }
    {
        const Layout layout = Layout::underRoot(base / "space-target");
        ImportPlan plan = planImport(package, inspection, layout, catalogueFor(layout), ImportOptions{});
        plan.enoughSpace = false;
        const ImportResult result = applyImport(package, plan, layout, "s");
        CHECK(!result.ok);
        CHECK(filesUnder(layout.root).empty());
        ImportPlan notPlanned;
        CHECK(!applyImport(package, notPlanned, layout, "s").ok);
    }
    {
        Inspection broken;
        broken.ok = false;
        broken.error = "bad";
        const Layout layout = Layout::underRoot(base / "bad-target");
        const ImportPlan plan = planImport(package, broken, layout, catalogueFor(layout), ImportOptions{});
        CHECK(!plan.ok && plan.error == "bad");
        CHECK(!fs::exists(layout.root));
    }
    {
        const Layout layout = Layout::underRoot(base / "folder-target");
        std::error_code ec;
        fs::create_directories(layout.media / "loop.mp4", ec);
        const ImportPlan plan = planImport(package, inspection, layout, catalogueFor(layout), ImportOptions{});
        const ImportEntry* loop = findEntry(plan, Kind::Media, "loop.mp4");
        CHECK(loop && loop->state == State::Blocked && loop->action == Action::Skip);
        const ImportResult result = applyImport(package, plan, layout, "f");
        CHECK(result.ok);
        CHECK(fs::is_directory(layout.media / "loop.mp4"));
    }
}

void testOldPresetInsidePackage(const fs::path& base) {
    const fs::path source = base / "old";
    const Layout layout = Layout::underRoot(source);
    writeBytes(layout.presets / "Old.json", "{\"bpm\":120,\"layers\":[{\"cells\":[\"Video: lost clip\",\"Plasma\"]}]}");
    const Catalogue catalogue = catalogueFor(layout);
    ExportRequest request;
    request.name = "old";
    request.presets = {"Old"};
    const fs::path package = base / "old.nyxshow";
    const ExportPlan plan = planExport(request, catalogue, layout);
    CHECK(hasWarning(plan.warnings, Warn::UnknownClip, "Video: lost clip"));
    CHECK(writePackage(plan.toRequest("old", "", "0.30.0", "2020"), package, true).ok);
    const Inspection inspection = inspectPackage(package);
    CHECK(inspection.ok);
    const Layout target = Layout::underRoot(base / "old-target");
    const ImportPlan importPlan = planImport(package, inspection, target, catalogueFor(target), ImportOptions{});
    CHECK(std::find(importPlan.unresolved.begin(), importPlan.unresolved.end(), "Video: lost clip") != importPlan.unresolved.end());
    CHECK(applyImport(package, importPlan, target, "o").ok);
    nvj::Composition composition;
    std::string error;
    std::vector<std::string> warnings;
    CHECK(nvj::compositionFromJson(readBytes(target.presets / "Old.json"), composition, error, &warnings));
    CHECK(std::any_of(warnings.begin(), warnings.end(), [](const std::string& w) { return w.find("Video: lost clip") != std::string::npos; }));
    CHECK(composition.bpm > 119.0f && composition.bpm < 121.0f);
}

void testShaderRename(const fs::path& base) {
    const Source source = buildSource(base / "pc-a");
    const fs::path package = base / "show.nyxshow";
    CHECK(exportTo(source, fullRequest(), package));
    const Layout layout = Layout::underRoot(base / "target");
    writeBytes(layout.media / "extra.webm", "different extra");
    ImportOptions options;
    options.policy = Policy::KeepBoth;
    const ImportPlan plan = planImport(package, inspectPackage(package), layout, catalogueFor(layout), options);
    CHECK(plan.clipRenames.count("Video: extra") == 1);
    CHECK(hasWarning(plan.warnings, Warn::UnsafeExternalFile, "tunnel.frag"));
    CHECK(plan.unresolved.empty());
}

void testVersions(const fs::path& base) {
    CHECK(compareVersions("0.41.0", "0.41.0") == 0);
    CHECK(compareVersions("0.41.0-dev", "0.41.0") == 0);
    CHECK(compareVersions("0.42.0", "0.41.9") > 0);
    CHECK(compareVersions("0.9.0", "0.41.0") < 0);
    CHECK(compareVersions("1.0", "0.99.99") > 0);
    CHECK(compareVersions("", "0.1.0") < 0);
    CHECK(compareVersions("garbage", "0.0.0") == 0);
    CHECK(compareVersions("99999999999999999999.0.0", "1.0.0") > 0);

    const Source source = buildSource(base / "pc-a");
    ExportPlan exportPlan = planExport(fullRequest(), catalogueFor(source.layout), source.layout);
    const fs::path package = base / "newer.nyxshow";
    CHECK(writePackage(exportPlan.toRequest("newer", "", "9.9.9", "now"), package, true).ok);
    const Layout target = Layout::underRoot(base / "target");
    ImportOptions options;
    options.appVersion = "0.41.0";
    ImportPlan plan = planImport(package, inspectPackage(package), target, catalogueFor(target), options);
    CHECK(hasWarning(plan.warnings, Warn::NewerApp, "9.9.9"));
    CHECK(hasWarning(plan.warnings, Warn::PluginNeeded, "Plug: Sparks") == false);
    options.appVersion = "10.0.0";
    plan = planImport(package, inspectPackage(package), target, catalogueFor(target), options);
    CHECK(!hasWarning(plan.warnings, Warn::NewerApp, "9.9.9"));
    Catalogue noPlugin = catalogueFor(target);
    noPlugin.pluginClips.clear();
    plan = planImport(package, inspectPackage(package), target, noPlugin, options);
    CHECK(hasWarning(plan.warnings, Warn::PluginNeeded, "Plug: Sparks"));
    for (const ImportEntry& entry : plan.entries) {
        CHECK(entry.item.kind != Kind::Composition || entry.item.name == "composition.json");
    }
}

void testLayoutAndCatalogue(const fs::path& base) {
    const Layout standard = Layout::standard();
    CHECK(standard.media == asPath(nvj::mediaDirectory()));
    CHECK(standard.models == asPath(nvj::modelDirectory()));
    CHECK(standard.scenePacks == asPath(nvj::clipsDirectory()));
    CHECK(standard.shaders == asPath(nvj::shaderDirectory()));
    CHECK(standard.presets == asPath(nvj::presetsDirectory()));
    const Layout under = Layout::underRoot(asPath(nvj::userDataDir()));
    CHECK(standard.media == under.media);
    CHECK(standard.models == under.models);
    CHECK(standard.scenePacks == under.scenePacks);
    CHECK(standard.shaders == under.shaders);
    CHECK(standard.presets == under.presets);
    CHECK(standard.nodes == under.nodes);
    CHECK(standard.venues == under.venues);
    CHECK(standard.masks == under.masks);
    CHECK(standard.cues == under.cues);
    CHECK(standard.timeline == under.timeline);

    CHECK(!standard.placesFile(Kind::Composition));
    CHECK(standard.placesFile(Kind::Media));
    CHECK(standard.pathFor(Kind::Composition, "composition.json").empty());
    CHECK(standard.pathFor(Kind::Cues, "cues.json") == standard.cues);
    CHECK(standard.pathFor(Kind::Media, "a.mp4") == standard.media / "a.mp4");

    nvj::rescanLibrary();
    const Catalogue catalogue = Catalogue::fromLibraries();
    const size_t accounted = catalogue.clips.size() + catalogue.builtInClips.size() + catalogue.liveClips.size() + catalogue.pluginClips.size();
    CHECK(accounted == nvj::clipLibrary().size());
    CHECK(!catalogue.builtInClips.empty());
    CHECK(!catalogue.builtInEffects.empty());
    CHECK(catalogue.effects.size() + catalogue.builtInEffects.size() == nvj::effectLibrary().size());

    const fs::path media = standard.media;
    writeBytes(media / "showtest-clip.mp4", "not really a video");
    nvj::rescanLibrary();
    const Catalogue withFile = Catalogue::fromLibraries();
    auto found = withFile.clips.find("Video: showtest-clip");
    CHECK(found != withFile.clips.end());
    if (found != withFile.clips.end()) {
        CHECK(found->second.kind == Kind::Media);
        CHECK(found->second.fileName == "showtest-clip.mp4");
        CHECK(clipNameForFile(Kind::Media, found->second.fileName) == found->first);
    }
    std::error_code ec;
    fs::remove(media / "showtest-clip.mp4", ec);
    nvj::rescanLibrary();
    (void)base;
}

}

int main() {
    const fs::path base = scratchDirectory("nyrexvj-show-workflow-tests");
    const fs::path dataDir = base / "data";
    fs::create_directories(dataDir);
#ifdef _WIN32
    _putenv_s("NYREXVJ_DATA_DIR", dataDir.string().c_str());
#else
    setenv("NYREXVJ_DATA_DIR", dataDir.string().c_str(), 1);
#endif
    testReferences();
    testExportPlan(base / "plan");
    testRoundtrip(base / "roundtrip");
    testMissingFiles(base / "missing");
    testVerifiedRepair(base / "verified");
    testConflicts(base / "conflicts");
    testFailureRollsBack(base / "failure");
    testOldPresetInsidePackage(base / "old");
    testShaderRename(base / "shader");
    testVersions(base / "versions");
    testLayoutAndCatalogue(base / "layout");
    std::error_code ec;
    fs::remove_all(base, ec);
    return testing::finish();
}
