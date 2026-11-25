#include "check.hpp"

#include "core/pack_workflow.hpp"
#include "core/plugin_config.hpp"
#include "core/plugin_manifest.hpp"
#include "core/sha256.hpp"
#include "core/show_package.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>

namespace {

namespace fs = std::filesystem;
using namespace nvj;
using namespace nvj::pack;

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

bool present(const fs::path& path) {
    std::error_code ec;
    return fs::exists(path, ec);
}

size_t countFiles(const fs::path& folder) {
    std::error_code ec;
    size_t count = 0;
    if (!fs::exists(folder, ec)) {
        return 0;
    }
    for (fs::recursive_directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec)) {
        if (it->is_regular_file(ec)) {
            ++count;
        }
    }
    return count;
}

std::string libraryName() {
    return std::string(pluginPlatformKey()).find("windows") != std::string::npos ? "demo.dll" : "demo.so";
}

std::string pluginJson(const std::string& id, unsigned apiMajor = 1, unsigned apiMinor = 0, bool otherPlatformOnly = false) {
    nlohmann::json doc;
    doc["format"] = "nyrexvj-plugin";
    doc["version"] = 1;
    doc["id"] = id;
    doc["name"] = "Demo plugin";
    doc["pluginVersion"] = "1.0";
    doc["api"] = {{"major", apiMajor}, {"minor", apiMinor}};
    doc["requires"] = nlohmann::json::array();
    nlohmann::json library;
    if (otherPlatformOnly) {
        library[std::string(pluginPlatformKey()).find("windows") != std::string::npos ? "linux-x64" : "windows-x64"] = "other.bin";
    } else {
        library[pluginPlatformKey()] = libraryName();
    }
    doc["library"] = library;
    return doc.dump(2);
}

struct Spec {
    std::string id = "demo-pack";
    std::string version = "1.0";
    std::string name = "Demo pack";
    std::string minApp;
    std::string maxApp;
    std::vector<std::pair<std::string, std::string>> dependencies;
    std::map<std::string, std::string> presets;
    std::map<std::string, std::string> shaders;
    std::map<std::string, std::string> nodes;
    std::string plugin;
    bool license = true;
    bool preview = false;
};

fs::path makeFolder(const fs::path& base, const Spec& spec) {
    const fs::path folder = base / (spec.id + "-" + spec.version + "-src");
    std::error_code ec;
    fs::remove_all(folder, ec);
    nlohmann::json doc;
    doc["id"] = spec.id;
    doc["version"] = spec.version;
    doc["name"] = spec.name;
    doc["author"] = "Tester";
    doc["description"] = "Made for the tests";
    doc["license"] = spec.license ? "MIT" : "";
    doc["nyrexvj"] = {{"min", spec.minApp}, {"max", spec.maxApp}};
    nlohmann::json dependencies = nlohmann::json::array();
    for (const auto& dependency : spec.dependencies) {
        dependencies.push_back({{"id", dependency.first}, {"min", dependency.second}});
    }
    doc["dependencies"] = dependencies;
    writeBytes(folder / "pack.json", doc.dump(2));
    for (const auto& item : spec.presets) {
        writeBytes(folder / "presets" / asPath(item.first), item.second);
    }
    for (const auto& item : spec.shaders) {
        writeBytes(folder / "shaders" / asPath(item.first), item.second);
    }
    for (const auto& item : spec.nodes) {
        writeBytes(folder / "nodes" / asPath(item.first), item.second);
    }
    if (!spec.plugin.empty()) {
        writeBytes(folder / "plugin" / "plugin.json", spec.plugin);
        writeBytes(folder / "plugin" / asPath(libraryName()), std::string("binary-") + spec.version);
    }
    if (spec.license) {
        writeBytes(folder / "LICENSE.txt", "MIT license text");
    }
    if (spec.preview) {
        writeBytes(folder / "preview.png", std::string("\x89PNG\r\n\x1a\n", 8) + "preview");
    }
    return folder;
}

fs::path buildPack(const fs::path& base, const Spec& spec) {
    const fs::path folder = makeFolder(base, spec);
    const fs::path file = base / (spec.id + "-" + spec.version + ".nyxpack");
    const BuildResult built = buildPackFromFolder(folder, file, true, "0.42.0", "2026-11-25T10:00:00");
    CHECK(built.ok);
    if (!built.ok) {
        std::fprintf(stderr, "build failed: %s\n", built.error.c_str());
    }
    return file;
}

InstallPlan planFor(const fs::path& file, const Layout& layout, InstallOptions options = {}) {
    if (options.appVersion.empty()) {
        options.appVersion = "0.42.0";
    }
    if (options.now.empty()) {
        options.now = "2026-11-25T11:00:00";
    }
    const show::Inspection inspection = show::inspectPackage(file, show::Limits::forPack(), show::Format::Pack);
    return planInstall(file, inspection, layout, options);
}

bool hasFinding(const InstallPlan& plan, Issue issue, Severity severity) {
    return std::any_of(plan.findings.begin(), plan.findings.end(), [&](const Finding& f) { return f.issue == issue && f.severity == severity; });
}

bool hasFinding(const RemovePlan& plan, Issue issue) {
    return std::any_of(plan.findings.begin(), plan.findings.end(), [&](const Finding& f) { return f.issue == issue; });
}

InstallResult install(const fs::path& file, const Layout& layout, InstallOptions options = {}, const std::string& stamp = "stamp") {
    options.acceptNativePlugin = true;
    const InstallPlan plan = planFor(file, layout, options);
    CHECK(plan.ready());
    if (!plan.ready()) {
        InstallResult failed;
        failed.error = plan.error.empty() ? "not ready" : plan.error;
        return failed;
    }
    return applyInstall(file, plan, layout, stamp);
}

void testVersions() {
    Version v;
    CHECK(parseVersion("1", v));
    CHECK(parseVersion("1.2", v));
    CHECK(parseVersion("1.2.3", v));
    CHECK(parseVersion("1.2.3.4", v));
    CHECK(parseVersion("0.42.0", v));
    CHECK(v.part[1] == 42);
    CHECK(!parseVersion("", v));
    CHECK(!parseVersion("1.", v));
    CHECK(!parseVersion(".1", v));
    CHECK(!parseVersion("1..2", v));
    CHECK(!parseVersion("1.2.3.4.5", v));
    CHECK(!parseVersion("01.2", v));
    CHECK(!parseVersion("1.2-beta", v));
    CHECK(!parseVersion("v1.2", v));
    CHECK(!parseVersion("-1", v));
    CHECK(!parseVersion("1.2 ", v));
    CHECK(!parseVersion("1234567", v));
    Version a;
    Version b;
    parseVersion("1.2", a);
    parseVersion("1.10", b);
    CHECK(compareVersion(a, b) < 0);
    CHECK(compareVersion(b, a) > 0);
    parseVersion("1.2.0.0", b);
    CHECK(compareVersion(a, b) == 0);
    CHECK(versionText(b) == "1.2");
}

void testMetaValidation() {
    show::Manifest manifest;
    manifest.name = "Pack";
    manifest.meta.id = "demo-pack";
    manifest.meta.version = "1.0";
    CHECK(validateMeta(manifest).empty());
    show::Manifest bad = manifest;
    bad.meta.id = "Bad Id";
    CHECK(!validateMeta(bad).empty());
    bad = manifest;
    bad.meta.id = std::string(40, 'a');
    CHECK(!validateMeta(bad).empty());
    bad = manifest;
    bad.meta.version = "one";
    CHECK(!validateMeta(bad).empty());
    bad = manifest;
    bad.name.clear();
    CHECK(!validateMeta(bad).empty());
    bad = manifest;
    bad.meta.minApp = "2.0";
    bad.meta.maxApp = "1.0";
    CHECK(!validateMeta(bad).empty());
    bad = manifest;
    bad.meta.minApp = "x";
    CHECK(!validateMeta(bad).empty());
    bad = manifest;
    bad.meta.dependencies = {{"demo-pack", ""}};
    CHECK(!validateMeta(bad).empty());
    bad = manifest;
    bad.meta.dependencies = {{"other", ""}, {"other", "1"}};
    CHECK(!validateMeta(bad).empty());
    bad = manifest;
    bad.meta.dependencies = {{"other", "1.x"}};
    CHECK(!validateMeta(bad).empty());
    bad = manifest;
    bad.meta.dependencies = {{"Other Pack", ""}};
    CHECK(!validateMeta(bad).empty());
}

void testBuilderAndFormats(const fs::path& base) {
    Spec spec;
    spec.presets["First.json"] = "{\"a\":1}";
    spec.shaders["glow.frag"] = "void main() {}";
    spec.preview = true;
    const fs::path file = buildPack(base, spec);
    const show::Inspection inspection = show::inspectPackage(file, show::Limits::forPack(), show::Format::Pack);
    CHECK(inspection.ok);
    CHECK(inspection.manifest.format == show::Format::Pack);
    CHECK(inspection.manifest.meta.id == "demo-pack");
    CHECK(inspection.manifest.meta.version == "1.0");
    CHECK(inspection.manifest.meta.license == "MIT");
    CHECK(inspection.manifest.meta.preview == "preview.png");
    CHECK(inspection.manifest.items.size() == 4);

    const show::Inspection asShow = show::inspectPackage(file);
    CHECK(!asShow.ok);
    CHECK(asShow.error.find("community package") != std::string::npos);

    show::WriteRequest request;
    request.name = "A show";
    show::SourceItem item;
    item.kind = show::Kind::Preset;
    item.name = "x.json";
    item.data = "{}";
    item.fromData = true;
    request.items.push_back(item);
    const fs::path showFile = base / "plain.nyxshow";
    CHECK(show::writePackage(request, showFile, true).ok);
    const show::Inspection showAsPack = show::inspectPackage(showFile, show::Limits::forPack(), show::Format::Pack);
    CHECK(!showAsPack.ok);
    CHECK(showAsPack.error.find("show package") != std::string::npos);

    show::WriteRequest wrong;
    wrong.format = show::Format::Pack;
    wrong.name = "Wrong";
    wrong.meta.id = "wrong";
    wrong.meta.version = "1";
    show::SourceItem media;
    media.kind = show::Kind::Media;
    media.name = "clip.mp4";
    media.data = "x";
    media.fromData = true;
    wrong.items.push_back(media);
    CHECK(!show::writePackage(wrong, base / "wrong.nyxpack", true, show::Limits::forPack()).ok);

    const fs::path garbage = base / "garbage.nyxpack";
    writeBytes(garbage, "this is not a package at all, only some text that is long enough");
    CHECK(!show::inspectPackage(garbage, show::Limits::forPack(), show::Format::Pack).ok);
    CHECK(!show::inspectPackage(base / "does-not-exist.nyxpack", show::Limits::forPack(), show::Format::Pack).ok);
}

void testBuilderRejects(const fs::path& base) {
    const fs::path destination = base / "rejected.nyxpack";
    CHECK(!buildPackFromFolder(base / "nowhere", destination, true, "0.42.0", "now").ok);

    fs::path folder = base / "no-json";
    fs::create_directories(folder);
    CHECK(!buildPackFromFolder(folder, destination, true, "0.42.0", "now").ok);

    Spec spec;
    spec.presets["ok.json"] = "{}";
    folder = makeFolder(base, spec);
    writeBytes(folder / "presets" / (std::string(120, 'a') + ".json"), "{}");
    BuildResult result = buildPackFromFolder(folder, destination, true, "0.42.0", "now");
    CHECK(!result.ok);
    CHECK(!result.problems.empty());

    spec.id = "second";
    folder = makeFolder(base, spec);
    writeBytes(folder / "presets" / "run.exe", "MZ");
    CHECK(!buildPackFromFolder(folder, destination, true, "0.42.0", "now").ok);

    spec.id = "third";
    folder = makeFolder(base, spec);
    fs::create_directories(folder / "presets" / "inner");
    CHECK(!buildPackFromFolder(folder, destination, true, "0.42.0", "now").ok);

    spec.id = "fourth";
    folder = makeFolder(base, spec);
    std::error_code ec;
    fs::create_symlink(folder / "pack.json", folder / "presets" / "link.json", ec);
    if (!ec) {
        CHECK(!buildPackFromFolder(folder, destination, true, "0.42.0", "now").ok);
    }

    spec.id = "fifth";
    spec.plugin = pluginJson("fifth-plugin");
    folder = makeFolder(base, spec);
    fs::remove(folder / "plugin" / asPath(libraryName()));
    CHECK(!buildPackFromFolder(folder, destination, true, "0.42.0", "now").ok);

    spec.id = "sixth";
    spec.plugin = pluginJson("sixth-plugin", 9, 0);
    folder = makeFolder(base, spec);
    CHECK(!buildPackFromFolder(folder, destination, true, "0.42.0", "now").ok);

    spec.id = "seventh";
    spec.plugin.clear();
    spec.version = "1.x";
    folder = makeFolder(base, spec);
    CHECK(!buildPackFromFolder(folder, destination, true, "0.42.0", "now").ok);

    spec.id = "eighth";
    spec.version = "1.0";
    folder = makeFolder(base, spec);
    writeBytes(folder / "preview.png", std::string(2 * 1024 * 1024, 'x'));
    CHECK(!buildPackFromFolder(folder, destination, true, "0.42.0", "now").ok);

    Spec empty;
    empty.id = "empty";
    empty.license = false;
    folder = makeFolder(base, empty);
    CHECK(!buildPackFromFolder(folder, destination, true, "0.42.0", "now").ok);
}

void testManifestSafety() {
    const std::string head = "{\"format\":\"nyrexvj-pack\",\"version\":1,\"name\":\"x\",\"id\":\"x\",\"packageVersion\":\"1\",";
    show::Manifest manifest;
    std::vector<std::string> warnings;
    std::string error;
    const std::string sha(64, 'a');
    auto text = [&](const std::string& kind, const std::string& name) {
        return head + "\"files\":[{\"kind\":\"" + kind + "\",\"name\":\"" + name + "\",\"size\":1,\"offset\":16,\"sha256\":\"" + sha + "\"}]}";
    };
    CHECK(show::manifestFromJson(text("preset", "fine.json"), show::Limits::forPack(), manifest, warnings, error, show::Format::Pack));
    CHECK(!show::manifestFromJson(text("preset", "../escape.json"), show::Limits::forPack(), manifest, warnings, error, show::Format::Pack));
    CHECK(!show::manifestFromJson(text("preset", "..\\\\escape.json"), show::Limits::forPack(), manifest, warnings, error, show::Format::Pack));
    CHECK(!show::manifestFromJson(text("plugin", "other.json"), show::Limits::forPack(), manifest, warnings, error, show::Format::Pack));
    CHECK(!show::manifestFromJson(text("plugin", "C:evil.dll"), show::Limits::forPack(), manifest, warnings, error, show::Format::Pack));
    CHECK(!show::manifestFromJson(text("plugin", "a/b.so"), show::Limits::forPack(), manifest, warnings, error, show::Format::Pack));
    warnings.clear();
    CHECK(show::manifestFromJson(text("media", "clip.mp4"), show::Limits::forPack(), manifest, warnings, error, show::Format::Pack));
    CHECK(manifest.items.empty());
    CHECK(!warnings.empty());
    CHECK(!show::manifestFromJson(text("preset", "fine.json"), show::Limits::forPack(), manifest, warnings, error, show::Format::Show));
}

void testFreshInstall(const fs::path& base) {
    const fs::path root = base / "fresh-data";
    const Layout layout = Layout::underRoot(root);
    writeBytes(root / "presets" / "mine.json", "{\"mine\":true}");
    Spec spec;
    spec.presets["Größe.json"] = "{\"p\":1}";
    spec.shaders["glow.frag"] = "void main() {}";
    spec.nodes["blur.nyxfx"] = "{\"n\":1}";
    spec.plugin = pluginJson("demo-plugin");
    spec.preview = true;
    const fs::path file = buildPack(base, spec);

    InstallOptions options;
    InstallPlan plan = planFor(file, layout, options);
    CHECK(plan.ok);
    CHECK(!plan.update);
    CHECK(!plan.blocked());
    CHECK(plan.needsConsent());
    CHECK(!plan.ready());
    CHECK(hasFinding(plan, Issue::NativePlugin, Severity::Consent));
    CHECK(hasFinding(plan, Issue::ShaderCode, Severity::Notice));
    CHECK(plan.pluginId == "demo-plugin");
    CHECK(plan.hasPreview);
    CHECK(plan.hasLicenseText);
    CHECK(plan.packageSha256.size() == 64);
    CHECK(plan.count(FileState::New) == 6);

    const InstallResult refused = applyInstall(file, plan, layout, "stamp");
    CHECK(!refused.ok);
    CHECK(!present(root / "presets" / asPath("Größe.json")));
    CHECK(!present(layout.plugins));
    CHECK(!present(layout.records));

    options.acceptNativePlugin = true;
    plan = planFor(file, layout, options);
    CHECK(plan.ready());
    const InstallResult result = applyInstall(file, plan, layout, "stamp");
    CHECK(result.ok);
    CHECK(result.written == 5);
    CHECK(result.pluginInstalled);
    CHECK(readBytes(root / "presets" / asPath("Größe.json")) == "{\"p\":1}");
    CHECK(readBytes(root / "shaders" / "glow.frag") == "void main() {}");
    CHECK(readBytes(root / "nodes" / "blur.nyxfx") == "{\"n\":1}");
    CHECK(present(layout.plugins / "demo-plugin" / "plugin.json"));
    CHECK(present(layout.plugins / "demo-plugin" / asPath(libraryName())));
    CHECK(readBytes(root / "presets" / "mine.json") == "{\"mine\":true}");
    CHECK(!present(root / "preview.png"));
    CHECK(readBytes(layout.licensePath("demo-pack")) == "MIT license text");
    CHECK(!present(pluginConfigPath(root)));
    CHECK(!present(pluginSafeListPath(root)));
    CHECK(PluginConfig(readPluginConfig(root)).enabled.empty());

    std::vector<std::string> problems;
    const std::vector<Record> records = readRecords(layout, &problems);
    CHECK(records.size() == 1);
    CHECK(problems.empty());
    CHECK(records[0].id == "demo-pack");
    CHECK(records[0].pluginId == "demo-plugin");
    CHECK(records[0].files.size() == 5);
    CHECK(records[0].packageSha256 == plan.packageSha256);
    CHECK(records[0].installedAt == "2026-11-25T11:00:00");

    const std::vector<std::string> leftovers = [&]() {
        std::vector<std::string> out;
        std::error_code ec;
        for (fs::recursive_directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec)) {
            const std::string name = it->path().filename().string();
            if (name.find(".nyxpack-new") != std::string::npos) {
                out.push_back(name);
            }
        }
        return out;
    }();
    CHECK(leftovers.empty());

    const Record& loaded = records[0];
    Record again;
    CHECK(recordFromJson(recordToJson(loaded), again));
    CHECK(again.files.size() == loaded.files.size());
    CHECK(again.pluginId == loaded.pluginId);

    InstallPlan second = planFor(file, layout, options);
    CHECK(second.update);
    CHECK(hasFinding(second, Issue::SameVersion, Severity::Notice));
    CHECK(second.count(FileState::Same) == 5);
    CHECK(second.writeBytes < 2000);
}

void testPluginChecks(const fs::path& base) {
    Spec spec;
    spec.id = "plug-check";
    spec.presets["a.json"] = "{}";

    {
        const fs::path root = base / "plug-major";
        const Layout layout = Layout::underRoot(root);
        show::WriteRequest request;
        request.format = show::Format::Pack;
        request.name = "Plug";
        request.meta.id = "plug-major";
        request.meta.version = "1";
        show::SourceItem json;
        json.kind = show::Kind::Plugin;
        json.name = "plugin.json";
        json.data = pluginJson("check-plugin", 9, 0);
        json.fromData = true;
        show::SourceItem lib;
        lib.kind = show::Kind::Plugin;
        lib.name = libraryName();
        lib.data = "binary";
        lib.fromData = true;
        request.items = {json, lib};
        const fs::path pack = base / "plug-major.nyxpack";
        CHECK(show::writePackage(request, pack, true, show::Limits::forPack()).ok);
        InstallOptions options;
        options.acceptNativePlugin = true;
        const InstallPlan plan = planFor(pack, layout, options);
        CHECK(plan.ok);
        CHECK(plan.blocked());
        CHECK(hasFinding(plan, Issue::PluginIncompatible, Severity::Blocking));
        CHECK(!applyInstall(pack, plan, layout, "s").ok);
        CHECK(!present(layout.plugins));

        request.items[0].data = pluginJson("check-plugin", 1, 9);
        CHECK(show::writePackage(request, pack, true, show::Limits::forPack()).ok);
        CHECK(hasFinding(planFor(pack, layout, options), Issue::PluginIncompatible, Severity::Blocking));

        request.items[0].data = std::string(20 * 1024, ' ') + pluginJson("check-plugin");
        CHECK(show::writePackage(request, pack, true, show::Limits::forPack()).ok);
        CHECK(hasFinding(planFor(pack, layout, options), Issue::PluginIncompatible, Severity::Blocking));

        request.items[0].data = "{ not json";
        CHECK(show::writePackage(request, pack, true, show::Limits::forPack()).ok);
        CHECK(hasFinding(planFor(pack, layout, options), Issue::PluginIncompatible, Severity::Blocking));

        request.items = {lib};
        CHECK(show::writePackage(request, pack, true, show::Limits::forPack()).ok);
        CHECK(hasFinding(planFor(pack, layout, options), Issue::PluginIncompatible, Severity::Blocking));

        request.items = {json};
        request.items[0].data = pluginJson("check-plugin");
        CHECK(show::writePackage(request, pack, true, show::Limits::forPack()).ok);
        CHECK(hasFinding(planFor(pack, layout, options), Issue::PluginIncompatible, Severity::Blocking));
    }

    {
        const fs::path root = base / "plug-platform";
        const Layout layout = Layout::underRoot(root);
        show::WriteRequest request;
        request.format = show::Format::Pack;
        request.name = "Foreign";
        request.meta.id = "foreign";
        request.meta.version = "1";
        show::SourceItem json;
        json.kind = show::Kind::Plugin;
        json.name = "plugin.json";
        json.data = pluginJson("foreign-plugin", 1, 0, true);
        json.fromData = true;
        show::SourceItem lib;
        lib.kind = show::Kind::Plugin;
        lib.name = "other.so";
        lib.data = "binary";
        lib.fromData = true;
        show::SourceItem preset;
        preset.kind = show::Kind::Preset;
        preset.name = "kept.json";
        preset.data = "{}";
        preset.fromData = true;
        request.items = {json, lib, preset};
        const fs::path pack = base / "foreign.nyxpack";
        CHECK(show::writePackage(request, pack, true, show::Limits::forPack()).ok);
        const InstallPlan plan = planFor(pack, layout);
        CHECK(plan.ok);
        CHECK(!plan.blocked());
        CHECK(!plan.needsConsent());
        CHECK(hasFinding(plan, Issue::PluginNotForPlatform, Severity::Notice));
        CHECK(plan.pluginId.empty());
        CHECK(applyInstall(pack, plan, layout, "s").ok);
        CHECK(present(root / "presets" / "kept.json"));
        CHECK(!present(layout.plugins));
    }

    {
        const fs::path root = base / "plug-taken";
        const Layout layout = Layout::underRoot(root);
        spec.plugin = pluginJson("taken-plugin");
        spec.id = "taken";
        const fs::path file = buildPack(base, spec);
        writeBytes(layout.plugins / "taken-plugin" / "plugin.json", pluginJson("taken-plugin"));
        InstallOptions options;
        options.acceptNativePlugin = true;
        InstallPlan plan = planFor(file, layout, options);
        CHECK(hasFinding(plan, Issue::PluginIdTaken, Severity::Blocking));
        CHECK(!plan.ready());
        fs::remove_all(layout.plugins);
        writeBytes(layout.plugins / "my-own-folder" / "plugin.json", pluginJson("taken-plugin"));
        plan = planFor(file, layout, options);
        CHECK(hasFinding(plan, Issue::PluginIdTaken, Severity::Blocking));
        CHECK(!present(layout.plugins / "taken-plugin"));
        fs::remove_all(layout.plugins);
        for (int i = 0; i < 16; ++i) {
            fs::create_directories(layout.plugins / ("folder" + std::to_string(i)));
        }
        plan = planFor(file, layout, options);
        CHECK(hasFinding(plan, Issue::TooManyPlugins, Severity::Blocking));
    }

    {
        const fs::path root = base / "plug-state";
        const Layout layout = Layout::underRoot(root);
        spec.plugin = pluginJson("state-plugin");
        spec.id = "state";
        const fs::path file = buildPack(base, spec);
        CHECK(addToPluginSafeList(root, {"state-plugin"}, "crashed", 123));
        const std::string safeBefore = readBytes(pluginSafeListPath(root));
        InstallOptions options;
        options.acceptNativePlugin = true;
        InstallPlan plan = planFor(file, layout, options);
        CHECK(hasFinding(plan, Issue::PluginBlockedBySafeMode, Severity::Notice));
        CHECK(plan.ready());
        CHECK(applyInstall(file, plan, layout, "s").ok);
        CHECK(readBytes(pluginSafeListPath(root)) == safeBefore);
        CHECK(!present(pluginConfigPath(root)));
        PluginConfig config;
        config.setEnabled("state-plugin", true);
        CHECK(writePluginConfig(root, config));
        const std::string configBefore = readBytes(pluginConfigPath(root));
        plan = planFor(file, layout, options);
        CHECK(hasFinding(plan, Issue::PluginStillOn, Severity::Blocking));
        CHECK(!applyInstall(file, plan, layout, "s2").ok);
        const RemovePlan removal = planRemove(layout, "state");
        CHECK(hasFinding(removal, Issue::PluginStillOn));
        CHECK(removal.blocked());
        CHECK(!applyRemove(layout, removal, "r").ok);
        CHECK(present(layout.plugins / "state-plugin" / "plugin.json"));
        CHECK(readBytes(pluginConfigPath(root)) == configBefore);
    }
}

void testVersionRules(const fs::path& base) {
    const fs::path root = base / "versions-data";
    const Layout layout = Layout::underRoot(root);
    Spec spec;
    spec.id = "ranged";
    spec.presets["r.json"] = "{}";
    spec.minApp = "0.30";
    spec.maxApp = "0.50";
    fs::path file = buildPack(base, spec);
    InstallOptions options;
    options.appVersion = "0.42.0";
    CHECK(!planFor(file, layout, options).blocked());
    options.appVersion = "0.30";
    CHECK(!planFor(file, layout, options).blocked());
    options.appVersion = "0.50.0";
    CHECK(!planFor(file, layout, options).blocked());
    options.appVersion = "0.29.9";
    CHECK(hasFinding(planFor(file, layout, options), Issue::IncompatibleApp, Severity::Blocking));
    options.appVersion = "0.51";
    CHECK(hasFinding(planFor(file, layout, options), Issue::IncompatibleApp, Severity::Blocking));
    options.appVersion = "0.51";
    const InstallPlan blocked = planFor(file, layout, options);
    CHECK(!applyInstall(file, blocked, layout, "s").ok);
    CHECK(!present(root / "presets" / "r.json"));

    Spec open;
    open.id = "open-range";
    open.presets["o.json"] = "{}";
    file = buildPack(base, open);
    options.appVersion = "9.9";
    CHECK(!planFor(file, layout, options).blocked());
}

void testDependencies(const fs::path& base) {
    const fs::path root = base / "deps-data";
    const Layout layout = Layout::underRoot(root);
    Spec core;
    core.id = "core-pack";
    core.version = "1.2";
    core.presets["core.json"] = "{\"core\":1}";
    const fs::path coreFile = buildPack(base, core);

    Spec addon;
    addon.id = "addon-pack";
    addon.presets["addon.json"] = "{\"addon\":1}";
    addon.dependencies = {{"core-pack", "1.1"}};
    const fs::path addonFile = buildPack(base, addon);

    InstallPlan plan = planFor(addonFile, layout);
    CHECK(hasFinding(plan, Issue::MissingDependency, Severity::Blocking));
    CHECK(!plan.ready());
    CHECK(!applyInstall(addonFile, plan, layout, "s").ok);
    CHECK(!present(root / "presets" / "addon.json"));

    CHECK(install(coreFile, layout).ok);
    plan = planFor(addonFile, layout);
    CHECK(!plan.blocked());
    CHECK(install(addonFile, layout, {}, "s2").ok);

    Spec strict;
    strict.id = "strict-pack";
    strict.presets["strict.json"] = "{}";
    strict.dependencies = {{"core-pack", "2.0"}};
    plan = planFor(buildPack(base, strict), layout);
    CHECK(hasFinding(plan, Issue::DependencyTooOld, Severity::Blocking));

    Spec downgradeCore = core;
    downgradeCore.version = "1.0";
    downgradeCore.presets["core.json"] = "{\"core\":0}";
    const fs::path oldCore = buildPack(base, downgradeCore);
    plan = planFor(oldCore, layout);
    CHECK(hasFinding(plan, Issue::Downgrade, Severity::Consent));
    CHECK(hasFinding(plan, Issue::BreaksDependent, Severity::Blocking));
    CHECK(plan.blocked());
    InstallOptions allow;
    allow.allowDowngrade = true;
    plan = planFor(oldCore, layout, allow);
    CHECK(hasFinding(plan, Issue::BreaksDependent, Severity::Blocking));
    CHECK(!plan.ready());

    const RemovePlan removal = planRemove(layout, "core-pack");
    CHECK(removal.ok);
    CHECK(hasFinding(removal, Issue::RequiredBy));
    CHECK(removal.blocked());
    CHECK(!applyRemove(layout, removal, "r").ok);
    CHECK(present(root / "presets" / "core.json"));

    const RemovePlan addonRemoval = planRemove(layout, "addon-pack");
    CHECK(!addonRemoval.blocked());
    CHECK(applyRemove(layout, addonRemoval, "r2").ok);
    CHECK(!present(root / "presets" / "addon.json"));
    CHECK(!planRemove(layout, "core-pack").blocked());
    CHECK(!planRemove(layout, "never-installed").ok);
}

void testUpdateAndRemoval(const fs::path& base) {
    const fs::path root = base / "update-data";
    const Layout layout = Layout::underRoot(root);
    writeBytes(root / "presets" / "user.json", "{\"user\":1}");
    Spec v1;
    v1.id = "evolving";
    v1.version = "1.0";
    v1.presets["keep.json"] = "{\"v\":1}";
    v1.presets["drop.json"] = "{\"drop\":1}";
    v1.presets["edited.json"] = "{\"edited\":1}";
    v1.shaders["old.frag"] = "void main() {}";
    const fs::path file1 = buildPack(base, v1);
    CHECK(install(file1, layout, {}, "first").ok);

    writeBytes(root / "presets" / "edited.json", "{\"edited\":\"by user\"}");

    Spec v2 = v1;
    v2.version = "1.1";
    v2.presets.erase("drop.json");
    v2.presets.erase("edited.json");
    v2.presets["keep.json"] = "{\"v\":2}";
    v2.presets["new.json"] = "{\"new\":1}";
    v2.shaders.clear();
    const fs::path file2 = buildPack(base, v2);
    InstallOptions options;
    InstallPlan plan = planFor(file2, layout, options);
    CHECK(plan.update);
    CHECK(plan.installedVersion == "1.0");
    CHECK(hasFinding(plan, Issue::Update, Severity::Notice));
    CHECK(plan.ready());
    CHECK(plan.obsolete.size() == 3);
    const InstallResult update = applyInstall(file2, plan, layout, "second");
    CHECK(update.ok);
    CHECK(update.updated == 1);
    CHECK(update.written == 1);
    CHECK(update.removed == 2);
    CHECK(update.keptModified == 1);
    CHECK(readBytes(root / "presets" / "keep.json") == "{\"v\":2}");
    CHECK(readBytes(root / "presets" / "new.json") == "{\"new\":1}");
    CHECK(!present(root / "presets" / "drop.json"));
    CHECK(!present(root / "shaders" / "old.frag"));
    CHECK(readBytes(root / "presets" / "edited.json") == "{\"edited\":\"by user\"}");
    CHECK(readBytes(root / "presets" / "user.json") == "{\"user\":1}");
    CHECK(readBytes(layout.backups / "second" / "presets" / "keep.json") == "{\"v\":1}");
    CHECK(readBytes(layout.backups / "second" / "presets" / "drop.json") == "{\"drop\":1}");
    const std::vector<Record> records = readRecords(layout);
    CHECK(records.size() == 1);
    CHECK(records[0].version == "1.1");
    CHECK(records[0].files.size() == 2);

    plan = planFor(file1, layout, options);
    CHECK(hasFinding(plan, Issue::Downgrade, Severity::Consent));
    CHECK(plan.needsConsent());
    CHECK(!applyInstall(file1, plan, layout, "no").ok);
    CHECK(readBytes(root / "presets" / "keep.json") == "{\"v\":2}");
    options.allowDowngrade = true;
    plan = planFor(file1, layout, options);
    CHECK(hasFinding(plan, Issue::FileConflict, Severity::Blocking));
    CHECK(!plan.ready());
    options.onConflict = ConflictPolicy::KeepMine;
    plan = planFor(file1, layout, options);
    CHECK(plan.ready());

    const RemovePlan removal = planRemove(layout, "evolving");
    CHECK(removal.ok);
    CHECK(!removal.blocked());
    CHECK(removal.count(RemoveState::Remove) == 2);
    const RemoveResult removed = applyRemove(layout, removal, "gone");
    CHECK(removed.ok);
    CHECK(removed.removed == 2);
    CHECK(!present(root / "presets" / "keep.json"));
    CHECK(!present(root / "presets" / "new.json"));
    CHECK(!present(layout.recordPath("evolving")));
    CHECK(!present(layout.licensePath("evolving")));
    CHECK(readBytes(layout.backups / "gone" / "presets" / "keep.json") == "{\"v\":2}");
    CHECK(readBytes(root / "presets" / "user.json") == "{\"user\":1}");
    CHECK(readRecords(layout).empty());
    CHECK(install(file2, layout, {}, "again").ok);
}

void testPluginUpdateAndRemoval(const fs::path& base) {
    const fs::path root = base / "plugin-update-data";
    const Layout layout = Layout::underRoot(root);
    Spec v1;
    v1.id = "with-plugin";
    v1.version = "1.0";
    v1.presets["p.json"] = "{}";
    v1.plugin = pluginJson("moving-plugin");
    const fs::path file1 = buildPack(base, v1);
    CHECK(install(file1, layout).ok);
    CHECK(present(layout.plugins / "moving-plugin" / "plugin.json"));
    Spec v2 = v1;
    v2.version = "2.0";
    v2.plugin = pluginJson("renamed-plugin");
    const fs::path file2 = buildPack(base, v2);
    const InstallResult update = install(file2, layout, {}, "up");
    CHECK(update.ok);
    CHECK(update.removed == 2);
    CHECK(!present(layout.plugins / "moving-plugin"));
    CHECK(present(layout.plugins / "renamed-plugin" / "plugin.json"));
    CHECK(present(layout.backups / "up" / "plugins" / "moving-plugin" / "plugin.json"));
    const RemovePlan removal = planRemove(layout, "with-plugin");
    CHECK(removal.ok);
    CHECK(!removal.blocked());
    CHECK(applyRemove(layout, removal, "rm").ok);
    CHECK(!present(layout.plugins / "renamed-plugin"));
    CHECK(present(layout.backups / "rm" / "plugins" / "renamed-plugin" / "plugin.json"));
}

void testConflicts(const fs::path& base) {
    const fs::path root = base / "conflict-data";
    const Layout layout = Layout::underRoot(root);
    writeBytes(root / "presets" / "clash.json", "{\"mine\":1}");
    writeBytes(root / "presets" / "same.json", "{\"same\":1}");
    writeBytes(root / "shaders" / "clash.frag", "// mine");
    Spec spec;
    spec.id = "clashing";
    spec.presets["clash.json"] = "{\"theirs\":1}";
    spec.presets["same.json"] = "{\"same\":1}";
    spec.presets["fresh.json"] = "{\"fresh\":1}";
    spec.shaders["clash.frag"] = "// theirs";
    const fs::path file = buildPack(base, spec);

    InstallPlan plan = planFor(file, layout);
    CHECK(plan.blocked());
    CHECK(plan.count(FileState::Conflict) == 2);
    CHECK(plan.count(FileState::Identical) == 1);
    CHECK(plan.count(FileState::New) == 2);
    CHECK(hasFinding(plan, Issue::FileConflict, Severity::Blocking));
    CHECK(!applyInstall(file, plan, layout, "s").ok);
    CHECK(readBytes(root / "presets" / "clash.json") == "{\"mine\":1}");
    CHECK(!present(root / "presets" / "fresh.json"));

    InstallOptions keep;
    keep.onConflict = ConflictPolicy::KeepMine;
    plan = planFor(file, layout, keep);
    CHECK(plan.ready());
    CHECK(hasFinding(plan, Issue::KeptMine, Severity::Notice));
    InstallResult result = applyInstall(file, plan, layout, "keep");
    CHECK(result.ok);
    CHECK(result.kept == 2);
    CHECK(readBytes(root / "presets" / "clash.json") == "{\"mine\":1}");
    CHECK(readBytes(root / "shaders" / "clash.frag") == "// mine");
    CHECK(readBytes(root / "presets" / "fresh.json") == "{\"fresh\":1}");
    std::vector<Record> records = readRecords(layout);
    CHECK(records.size() == 1);
    CHECK(records[0].files.size() == 1);
    CHECK(records[0].skipped.size() == 2);

    const RemovePlan removal = planRemove(layout, "clashing");
    CHECK(applyRemove(layout, removal, "rm").ok);
    CHECK(readBytes(root / "presets" / "clash.json") == "{\"mine\":1}");
    CHECK(readBytes(root / "presets" / "same.json") == "{\"same\":1}");
    CHECK(readBytes(root / "shaders" / "clash.frag") == "// mine");
    CHECK(!present(root / "presets" / "fresh.json"));

    InstallOptions replace;
    replace.onConflict = ConflictPolicy::Replace;
    plan = planFor(file, layout, replace);
    CHECK(plan.ready());
    result = applyInstall(file, plan, layout, "replace");
    CHECK(result.ok);
    CHECK(result.updated == 2);
    CHECK(readBytes(root / "presets" / "clash.json") == "{\"theirs\":1}");
    CHECK(readBytes(layout.backups / "replace" / "presets" / "clash.json") == "{\"mine\":1}");
    CHECK(readBytes(layout.backups / "replace" / "shaders" / "clash.frag") == "// mine");

    Spec other;
    other.id = "competitor";
    other.presets["fresh.json"] = "{\"other\":1}";
    const fs::path otherFile = buildPack(base, other);
    for (ConflictPolicy policy : {ConflictPolicy::Block, ConflictPolicy::KeepMine, ConflictPolicy::Replace}) {
        InstallOptions options;
        options.onConflict = policy;
        plan = planFor(otherFile, layout, options);
        CHECK(plan.blocked());
        CHECK(hasFinding(plan, Issue::OwnedByOther, Severity::Blocking));
        CHECK(!applyInstall(otherFile, plan, layout, "no").ok);
    }
    CHECK(readBytes(root / "presets" / "fresh.json") == "{\"fresh\":1}");

    fs::create_directories(root / "presets" / "dir.json");
    Spec folder;
    folder.id = "folder-way";
    folder.presets["dir.json"] = "{}";
    plan = planFor(buildPack(base, folder), layout);
    CHECK(hasFinding(plan, Issue::FolderInTheWay, Severity::Blocking));
    CHECK(plan.blocked());
}

void testRollback(const fs::path& base) {
    {
        const fs::path root = base / "rollback-fresh";
        Layout layout = Layout::underRoot(root);
        writeBytes(root / "presets" / "mine.json", "{\"mine\":1}");
        Spec spec;
        spec.id = "rolling";
        spec.presets["a.json"] = "{\"a\":1}";
        spec.presets["mine.json"] = "{\"theirs\":1}";
        spec.shaders["s.frag"] = "void main() {}";
        const fs::path file = buildPack(base, spec);
        InstallOptions options;
        options.onConflict = ConflictPolicy::Replace;
        const InstallPlan plan = planFor(file, layout, options);
        CHECK(plan.ready());
        writeBytes(root / "blocker", "a file where the records folder should be");
        layout.records = root / "blocker";
        const InstallResult result = applyInstall(file, plan, layout, "fail");
        CHECK(!result.ok);
        CHECK(result.rolledBack);
        CHECK(readBytes(root / "presets" / "mine.json") == "{\"mine\":1}");
        CHECK(!present(root / "presets" / "a.json"));
        CHECK(!present(root / "shaders" / "s.frag"));
        CHECK(countFiles(root / "presets") == 1);
        CHECK(countFiles(root / "shaders") == 0);
    }
    {
        const fs::path root = base / "rollback-update";
        const Layout layout = Layout::underRoot(root);
        Spec v1;
        v1.id = "rolling-up";
        v1.version = "1.0";
        v1.presets["a.json"] = "{\"a\":1}";
        v1.presets["gone.json"] = "{\"gone\":1}";
        v1.plugin = pluginJson("rolling-plugin");
        CHECK(install(buildPack(base, v1), layout).ok);
        const std::string recordBefore = readBytes(layout.recordPath("rolling-up"));
        Spec v2 = v1;
        v2.version = "2.0";
        v2.presets["a.json"] = "{\"a\":2}";
        v2.presets.erase("gone.json");
        v2.presets["b.json"] = "{\"b\":2}";
        const fs::path file2 = buildPack(base, v2);
        InstallOptions options;
        options.acceptNativePlugin = true;
        const InstallPlan plan = planFor(file2, layout, options);
        CHECK(plan.ready());
        fs::path temp = layout.recordPath("rolling-up");
        temp += ".nyxpack-new";
        fs::create_directories(temp);
        const InstallResult result = applyInstall(file2, plan, layout, "fail2");
        CHECK(!result.ok);
        CHECK(result.rolledBack);
        CHECK(readBytes(root / "presets" / "a.json") == "{\"a\":1}");
        CHECK(readBytes(root / "presets" / "gone.json") == "{\"gone\":1}");
        CHECK(!present(root / "presets" / "b.json"));
        CHECK(readBytes(layout.recordPath("rolling-up")) == recordBefore);
        CHECK(present(layout.plugins / "rolling-plugin" / "plugin.json"));
        CHECK(readBytes(layout.plugins / "rolling-plugin" / asPath(libraryName())) == "binary-1.0");
    }
}

void testCancelAndDamage(const fs::path& base) {
    const fs::path root = base / "damage-data";
    const Layout layout = Layout::underRoot(root);
    Spec spec;
    spec.id = "damaged";
    spec.presets["a.json"] = std::string(5000, 'a');
    spec.presets["b.json"] = std::string(5000, 'b');
    spec.shaders["c.frag"] = std::string(5000, 'c');
    const fs::path file = buildPack(base, spec);

    InstallPlan plan = planFor(file, layout);
    CHECK(plan.ready());
    int calls = 0;
    InstallResult cancelled = applyInstall(file, plan, layout, "c", [&](const show::Progress&) { return ++calls < 2; });
    CHECK(!cancelled.ok);
    CHECK(cancelled.cancelled);
    CHECK(countFiles(root) == 0);

    InstallOptions cancelPlan;
    const show::Inspection inspection = show::inspectPackage(file, show::Limits::forPack(), show::Format::Pack);
    const InstallPlan cancelledPlan = planInstall(file, inspection, layout, cancelPlan, [](const show::Progress&) { return false; });
    CHECK(!cancelledPlan.ok);
    CHECK(cancelledPlan.cancelled);

    std::string bytes = readBytes(file);
    const show::Item* target = nullptr;
    for (const show::Item& item : inspection.manifest.items) {
        if (item.name == "b.json") {
            target = &item;
        }
    }
    CHECK(target != nullptr);
    if (target != nullptr) {
        bytes[static_cast<size_t>(target->offset) + 100] ^= 0x55;
        const fs::path tampered = base / "tampered.nyxpack";
        writeBytes(tampered, bytes);
        const show::Inspection check = show::inspectPackage(tampered, show::Limits::forPack(), show::Format::Pack);
        CHECK(check.ok);
        const InstallPlan tamperedPlan = planInstall(tampered, check, layout, {});
        const InstallResult result = applyInstall(tampered, tamperedPlan, layout, "t");
        CHECK(!result.ok);
        CHECK(countFiles(root) == 0);
        CHECK(result.error.find("checksum") != std::string::npos || result.error.find("damaged") != std::string::npos);
    }

    const std::string whole = readBytes(file);
    for (size_t cut : {size_t(4), size_t(20), whole.size() / 2, whole.size() - 40, whole.size() - 1}) {
        const fs::path truncated = base / "truncated.nyxpack";
        writeBytes(truncated, whole.substr(0, cut));
        CHECK(!show::inspectPackage(truncated, show::Limits::forPack(), show::Format::Pack).ok);
    }
    std::string flipped = whole;
    flipped[flipped.size() - 5] ^= 0x01;
    const fs::path footerBroken = base / "footer.nyxpack";
    writeBytes(footerBroken, flipped);
    CHECK(!show::inspectPackage(footerBroken, show::Limits::forPack(), show::Format::Pack).ok);

    show::Limits tiny = show::Limits::forPack();
    tiny.maxTotalBytes = 1000;
    CHECK(!show::inspectPackage(file, tiny, show::Format::Pack).ok);
    tiny = show::Limits::forPack();
    tiny.maxEntries = 2;
    CHECK(!show::inspectPackage(file, tiny, show::Format::Pack).ok);
    tiny = show::Limits::forPack();
    tiny.maxManifestBytes = 100;
    CHECK(!show::inspectPackage(file, tiny, show::Format::Pack).ok);

    show::WriteRequest big;
    big.format = show::Format::Pack;
    big.name = "Big";
    big.meta.id = "big";
    big.meta.version = "1";
    show::SourceItem shader;
    shader.kind = show::Kind::Shader;
    shader.name = "huge.frag";
    shader.data = std::string(300 * 1024, 'x');
    shader.fromData = true;
    big.items.push_back(shader);
    CHECK(!show::writePackage(big, base / "big.nyxpack", true, show::Limits::forPack()).ok);

    show::WriteRequest incomplete;
    incomplete.format = show::Format::Pack;
    incomplete.name = "Incomplete";
    show::SourceItem preset;
    preset.kind = show::Kind::Preset;
    preset.name = "x.json";
    preset.data = "{}";
    preset.fromData = true;
    incomplete.items.push_back(preset);
    const fs::path incompleteFile = base / "incomplete.nyxpack";
    CHECK(show::writePackage(incomplete, incompleteFile, true, show::Limits::forPack()).ok);
    const InstallPlan bad = planFor(incompleteFile, layout);
    CHECK(!bad.ok);
    CHECK(!bad.error.empty());
    CHECK(!applyInstall(incompleteFile, bad, layout, "x").ok);
    CHECK(countFiles(root) == 0);

    const InstallPlan unplanned = planInstall(file, show::Inspection(), layout, {});
    CHECK(!unplanned.ok);
}

void testRecords(const fs::path& base) {
    const fs::path root = base / "records-data";
    const Layout layout = Layout::underRoot(root);
    Record record;
    record.id = "good";
    record.version = "1.2";
    record.name = "Good";
    record.files.push_back({show::Kind::Preset, "a.json", "abc", 3});
    writeBytes(layout.recordPath("good"), recordToJson(record));
    writeBytes(layout.records / "broken.json", "{ not json");
    writeBytes(layout.records / "mismatch.json", recordToJson(record));
    writeBytes(layout.records / "notes.txt", "ignored");
    Record bad = record;
    bad.id = "evil";
    nlohmann::json doc = nlohmann::json::parse(recordToJson(bad));
    doc["files"].push_back({{"kind", "preset"}, {"name", "../../outside.json"}, {"sha256", "x"}, {"size", 1}});
    doc["files"].push_back({{"kind", "plugin"}, {"name", "plugin.json"}, {"sha256", "x"}, {"size", 1}});
    doc["files"].push_back({{"kind", "media"}, {"name", "clip.mp4"}, {"sha256", "x"}, {"size", 1}});
    writeBytes(layout.recordPath("evil"), doc.dump());
    std::vector<std::string> problems;
    const std::vector<Record> records = readRecords(layout, &problems);
    CHECK(records.size() == 2);
    CHECK(problems.size() == 2);
    Record loaded;
    CHECK(findRecord(records, "evil", loaded));
    CHECK(loaded.files.size() == 1);
    CHECK(!findRecord(records, "mismatch", loaded));
    CHECK(readRecords(Layout::underRoot(base / "nothing-here")).empty());

    writeBytes(layout.records / "huge.json", std::string(kMaxRecordBytes + 10, ' '));
    problems.clear();
    readRecords(layout, &problems);
    CHECK(problems.size() == 3);
}

}

int main() {
    const fs::path base = scratchDirectory("nyrexvj-pack-tests");
    auto sub = [&](const char* name) {
        const fs::path path = base / name;
        std::error_code ec;
        fs::create_directories(path, ec);
        return path;
    };
    testVersions();
    testMetaValidation();
    testManifestSafety();
    testBuilderAndFormats(sub("formats"));
    testBuilderRejects(sub("rejects"));
    testFreshInstall(sub("fresh"));
    testPluginChecks(sub("plugins"));
    testVersionRules(sub("versions"));
    testDependencies(sub("deps"));
    testUpdateAndRemoval(sub("update"));
    testPluginUpdateAndRemoval(sub("pluginupdate"));
    testConflicts(sub("conflicts"));
    testRollback(sub("rollback"));
    testCancelAndDamage(sub("damage"));
    testRecords(sub("records"));
    return testing::finish();
}
