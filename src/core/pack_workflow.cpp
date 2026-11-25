#include "core/pack_workflow.hpp"

#include "core/plugin_config.hpp"
#include "core/plugin_manifest.hpp"
#include "core/sha256.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <map>
#include <set>

namespace nvj::pack {

namespace fs = std::filesystem;

namespace {

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

bool movePath(const fs::path& from, const fs::path& to) {
    std::error_code ec;
    if (to.has_parent_path()) {
        fs::create_directories(to.parent_path(), ec);
    }
    ec.clear();
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

bool readSmallFile(const fs::path& file, size_t maxBytes, std::string& out) {
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
    out.resize(static_cast<size_t>(size));
    in.read(out.data(), static_cast<std::streamsize>(size));
    return static_cast<unsigned long long>(in.gcount()) == size;
}

bool shaOf(const fs::path& file, std::string& hex) {
    return sha256FileHex(file, hex);
}

std::string stringOf(const nlohmann::json& doc, const char* key, size_t maxBytes) {
    auto it = doc.find(key);
    if (it == doc.end() || !it->is_string()) {
        return std::string();
    }
    std::string text = it->get<std::string>();
    if (text.size() > maxBytes) {
        text.resize(maxBytes);
    }
    return text;
}

bool installableKind(show::Kind kind) {
    switch (kind) {
        case show::Kind::Preset:
        case show::Kind::Shader:
        case show::Kind::Node:
        case show::Kind::Venue:
        case show::Kind::Mask:
        case show::Kind::Plugin:
            return true;
        default:
            return false;
    }
}

std::string fileKey(show::Kind kind, const std::string& pluginId, const std::string& name) {
    if (kind == show::Kind::Plugin) {
        return "plugin:" + lower(pluginId) + "/" + lower(name);
    }
    return std::string(show::kindName(kind)) + ":" + lower(name);
}

fs::path targetFor(const Layout& layout, show::Kind kind, const std::string& pluginId, const std::string& name) {
    if (kind == show::Kind::Plugin) {
        return layout.plugins / u8path(pluginId) / u8path(name);
    }
    return layout.files.pathFor(kind, name);
}

std::string joinWithComma(const std::vector<std::string>& values) {
    std::string out;
    for (const std::string& value : values) {
        if (!out.empty()) {
            out += ", ";
        }
        out += value;
    }
    return out;
}

}

Layout Layout::underRoot(const fs::path& root) {
    Layout layout;
    layout.files = show::Layout::underRoot(root);
    layout.plugins = pluginsFolder(root);
    layout.records = root / "packages";
    layout.backups = root / "package_backups";
    return layout;
}

Layout Layout::standard() {
    Layout layout;
    layout.files = show::Layout::standard();
    layout.plugins = pluginsFolder(layout.files.root);
    layout.records = layout.files.root / "packages";
    layout.backups = layout.files.root / "package_backups";
    return layout;
}

fs::path Layout::recordPath(const std::string& id) const {
    return records / u8path(id + ".json");
}

fs::path Layout::licensePath(const std::string& id) const {
    return records / u8path(id + ".license.txt");
}

bool parseVersion(const std::string& text, Version& out) {
    out = Version();
    if (text.empty() || text.size() > 40) {
        return false;
    }
    size_t index = 0;
    size_t position = 0;
    while (true) {
        if (index >= 4) {
            return false;
        }
        size_t digits = 0;
        unsigned value = 0;
        const size_t start = position;
        while (position < text.size() && text[position] >= '0' && text[position] <= '9') {
            value = value * 10 + static_cast<unsigned>(text[position] - '0');
            ++position;
            ++digits;
            if (digits > 6) {
                return false;
            }
        }
        if (digits == 0 || (digits > 1 && text[start] == '0')) {
            return false;
        }
        out.part[index++] = value;
        if (position == text.size()) {
            return true;
        }
        if (text[position] != '.') {
            return false;
        }
        ++position;
    }
}

int compareVersion(const Version& left, const Version& right) {
    for (int i = 0; i < 4; ++i) {
        if (left.part[i] != right.part[i]) {
            return left.part[i] < right.part[i] ? -1 : 1;
        }
    }
    return 0;
}

std::string versionText(const Version& version) {
    int last = 3;
    while (last > 0 && version.part[last] == 0) {
        --last;
    }
    std::string text;
    for (int i = 0; i <= last; ++i) {
        if (i > 0) {
            text += '.';
        }
        text += std::to_string(version.part[i]);
    }
    return text;
}

std::vector<std::string> validateMeta(const show::Manifest& manifest) {
    std::vector<std::string> problems;
    const show::PackMeta& meta = manifest.meta;
    if (!validPluginId(meta.id)) {
        problems.push_back("The package has no valid id");
    }
    Version version;
    if (!parseVersion(meta.version, version)) {
        problems.push_back("The package has no valid version number");
    }
    if (manifest.name.empty()) {
        problems.push_back("The package has no name");
    }
    Version low;
    Version high;
    const bool hasLow = !meta.minApp.empty();
    const bool hasHigh = !meta.maxApp.empty();
    if ((hasLow && !parseVersion(meta.minApp, low)) || (hasHigh && !parseVersion(meta.maxApp, high)) || (hasLow && hasHigh && compareVersion(low, high) > 0)) {
        problems.push_back("The NyrexVJ versions named by the package are not valid");
    }
    std::set<std::string> seen;
    for (const show::PackDependency& dependency : meta.dependencies) {
        Version minimum;
        if (!validPluginId(dependency.id) || (!dependency.min.empty() && !parseVersion(dependency.min, minimum))) {
            problems.push_back("The package names a dependency that is not valid");
            break;
        }
        if (dependency.id == meta.id) {
            problems.push_back("The package depends on itself");
            break;
        }
        if (!seen.insert(dependency.id).second) {
            problems.push_back("The package names the same dependency twice");
            break;
        }
    }
    return problems;
}

std::string recordToJson(const Record& record) {
    nlohmann::json doc;
    doc["format"] = "nyrexvj-installed-pack";
    doc["version"] = 1;
    doc["id"] = record.id;
    doc["packageVersion"] = record.version;
    doc["name"] = record.name;
    doc["author"] = record.author;
    doc["license"] = record.license;
    doc["installedAt"] = record.installedAt;
    doc["appVersion"] = record.appVersion;
    doc["packageSha256"] = record.packageSha256;
    doc["pluginId"] = record.pluginId;
    nlohmann::json dependencies = nlohmann::json::array();
    for (const show::PackDependency& dependency : record.dependencies) {
        dependencies.push_back({{"id", dependency.id}, {"min", dependency.min}});
    }
    doc["dependencies"] = dependencies;
    auto fileList = [](const std::vector<RecordFile>& list) {
        nlohmann::json out = nlohmann::json::array();
        for (const RecordFile& file : list) {
            out.push_back({{"kind", show::kindName(file.kind)}, {"name", file.name}, {"sha256", file.sha256}, {"size", file.size}});
        }
        return out;
    };
    doc["files"] = fileList(record.files);
    doc["skipped"] = fileList(record.skipped);
    return doc.dump(2) + "\n";
}

bool recordFromJson(const std::string& text, Record& record) {
    record = Record();
    nlohmann::json doc = nlohmann::json::parse(text, nullptr, false);
    if (doc.is_discarded() || !doc.is_object() || doc.value("format", std::string()) != "nyrexvj-installed-pack" || doc.value("version", 0) != 1) {
        return false;
    }
    record.id = stringOf(doc, "id", 80);
    record.version = stringOf(doc, "packageVersion", 40);
    record.name = stringOf(doc, "name", show::kMaxShowNameBytes);
    record.author = stringOf(doc, "author", show::kMaxPackShortBytes);
    record.license = stringOf(doc, "license", show::kMaxPackShortBytes);
    record.installedAt = stringOf(doc, "installedAt", 40);
    record.appVersion = stringOf(doc, "appVersion", 40);
    record.packageSha256 = stringOf(doc, "packageSha256", 64);
    record.pluginId = stringOf(doc, "pluginId", 80);
    Version version;
    if (!validPluginId(record.id) || !parseVersion(record.version, version) || (!record.pluginId.empty() && !validPluginId(record.pluginId))) {
        return false;
    }
    auto deps = doc.find("dependencies");
    if (deps != doc.end() && deps->is_array()) {
        for (const nlohmann::json& entry : *deps) {
            if (!entry.is_object() || record.dependencies.size() >= show::kMaxPackDependencies) {
                continue;
            }
            show::PackDependency dependency;
            dependency.id = stringOf(entry, "id", 80);
            dependency.min = stringOf(entry, "min", 40);
            if (validPluginId(dependency.id)) {
                record.dependencies.push_back(dependency);
            }
        }
    }
    auto readFiles = [&](const char* key, std::vector<RecordFile>& out) {
        auto list = doc.find(key);
        if (list == doc.end() || !list->is_array()) {
            return;
        }
        for (const nlohmann::json& entry : *list) {
            if (!entry.is_object() || out.size() >= 4096) {
                continue;
            }
            RecordFile file;
            if (!show::kindFromName(stringOf(entry, "kind", 20), file.kind) || !installableKind(file.kind)) {
                continue;
            }
            file.name = stringOf(entry, "name", show::kMaxNameBytes);
            file.sha256 = stringOf(entry, "sha256", 64);
            auto size = entry.find("size");
            if (size != entry.end() && size->is_number_unsigned()) {
                file.size = size->get<unsigned long long>();
            }
            if (!show::isSafeItemName(file.kind, file.name)) {
                continue;
            }
            if (file.kind == show::Kind::Plugin && record.pluginId.empty()) {
                continue;
            }
            out.push_back(file);
        }
    };
    readFiles("files", record.files);
    readFiles("skipped", record.skipped);
    return true;
}

std::vector<Record> readRecords(const Layout& layout, std::vector<std::string>* problems) {
    std::vector<Record> records;
    std::error_code ec;
    if (!fs::is_directory(layout.records, ec)) {
        return records;
    }
    std::vector<fs::path> files;
    for (fs::directory_iterator it(layout.records, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code entryEc;
        if (it->is_regular_file(entryEc) && lower(u8text(it->path().extension())) == ".json") {
            files.push_back(it->path());
        }
    }
    std::sort(files.begin(), files.end());
    for (const fs::path& file : files) {
        if (records.size() >= kMaxRecords) {
            break;
        }
        std::string text;
        Record record;
        if (!readSmallFile(file, kMaxRecordBytes, text) || !recordFromJson(text, record) || u8text(file.stem()) != record.id) {
            if (problems) {
                problems->push_back(u8text(file.filename()));
            }
            continue;
        }
        records.push_back(record);
    }
    return records;
}

bool findRecord(const std::vector<Record>& records, const std::string& id, Record& out) {
    for (const Record& record : records) {
        if (record.id == id) {
            out = record;
            return true;
        }
    }
    return false;
}

const char* issueName(Issue issue) {
    switch (issue) {
        case Issue::IncompatibleApp: return "incompatible-app";
        case Issue::MissingDependency: return "missing-dependency";
        case Issue::DependencyTooOld: return "dependency-too-old";
        case Issue::BreaksDependent: return "breaks-dependent";
        case Issue::Downgrade: return "downgrade";
        case Issue::SameVersion: return "same-version";
        case Issue::Update: return "update";
        case Issue::NativePlugin: return "native-plugin";
        case Issue::PluginIncompatible: return "plugin-incompatible";
        case Issue::PluginNotForPlatform: return "plugin-not-for-platform";
        case Issue::PluginIdTaken: return "plugin-id-taken";
        case Issue::TooManyPlugins: return "too-many-plugins";
        case Issue::PluginBlockedBySafeMode: return "plugin-safe-mode";
        case Issue::FileConflict: return "file-conflict";
        case Issue::OwnedByOther: return "owned-by-other-package";
        case Issue::FolderInTheWay: return "folder-in-the-way";
        case Issue::KeptMine: return "kept-yours";
        case Issue::ShaderCode: return "shader-code";
        case Issue::NoLicense: return "no-license";
        case Issue::RequiredBy: return "required-by";
        case Issue::PluginStillOn: return "plugin-still-on";
        case Issue::ModifiedKept: return "modified-kept";
    }
    return "unknown";
}

size_t InstallPlan::count(Severity severity) const {
    return static_cast<size_t>(std::count_if(findings.begin(), findings.end(), [&](const Finding& f) { return f.severity == severity; }));
}

size_t InstallPlan::count(FileState state) const {
    return static_cast<size_t>(std::count_if(files.begin(), files.end(), [&](const FileEntry& f) { return f.state == state; }));
}

bool InstallPlan::blocked() const {
    return count(Severity::Blocking) > 0 || !enoughSpace;
}

bool InstallPlan::needsConsent() const {
    return std::any_of(findings.begin(), findings.end(), [](const Finding& f) { return f.severity == Severity::Consent && !f.accepted; });
}

bool InstallPlan::ready() const {
    return ok && !blocked() && !needsConsent();
}

size_t RemovePlan::count(RemoveState state) const {
    return static_cast<size_t>(std::count_if(entries.begin(), entries.end(), [&](const RemoveEntry& e) { return e.state == state; }));
}

bool RemovePlan::blocked() const {
    return std::any_of(findings.begin(), findings.end(), [](const Finding& f) { return f.severity == Severity::Blocking; });
}

namespace {

void addFinding(InstallPlan& plan, Issue issue, Severity severity, const std::string& subject, const std::string& detail = std::string(), const std::string& other = std::string(), bool accepted = false) {
    Finding finding;
    finding.issue = issue;
    finding.severity = severity;
    finding.subject = subject;
    finding.detail = detail;
    finding.other = other;
    finding.accepted = accepted;
    plan.findings.push_back(finding);
}

void addFinding(RemovePlan& plan, Issue issue, Severity severity, const std::string& subject, const std::string& detail = std::string()) {
    Finding finding;
    finding.issue = issue;
    finding.severity = severity;
    finding.subject = subject;
    finding.detail = detail;
    plan.findings.push_back(finding);
}

}

InstallPlan planInstall(const fs::path& package, const show::Inspection& inspection, const Layout& layout, const InstallOptions& options, const show::ProgressFn& progress) {
    InstallPlan plan;
    plan.appVersion = options.appVersion;
    plan.installedAt = options.now;
    if (!inspection.ok) {
        plan.error = inspection.error.empty() ? "The package could not be read" : inspection.error;
        return plan;
    }
    plan.manifest = inspection.manifest;
    if (plan.manifest.format != show::Format::Pack) {
        plan.error = "This is not a community package";
        return plan;
    }
    const std::vector<std::string> metaProblems = validateMeta(plan.manifest);
    if (!metaProblems.empty()) {
        plan.error = metaProblems.front();
        return plan;
    }
    const show::PackMeta& meta = plan.manifest.meta;

    if (!options.knownSha256.empty()) {
        plan.packageSha256 = options.knownSha256;
    } else {
        bool cancelled = false;
        std::string hex;
        const bool done = sha256FileHex(package, hex, [&](unsigned long long bytes) {
            show::Progress state;
            state.done = bytes;
            state.total = inspection.fileBytes;
            state.current = u8text(package.filename());
            if (progress && !progress(state)) {
                cancelled = true;
                return false;
            }
            return true;
        });
        if (cancelled) {
            plan.cancelled = true;
            plan.error = "Cancelled";
            return plan;
        }
        if (!done) {
            plan.error = "The package could not be read";
            return plan;
        }
        plan.packageSha256 = hex;
    }

    Version appVersion;
    const bool knowsApp = parseVersion(options.appVersion, appVersion);
    if (knowsApp) {
        Version low;
        Version high;
        const bool tooOld = !meta.minApp.empty() && parseVersion(meta.minApp, low) && compareVersion(appVersion, low) < 0;
        const bool tooNew = !meta.maxApp.empty() && parseVersion(meta.maxApp, high) && compareVersion(appVersion, high) > 0;
        if (tooOld || tooNew) {
            addFinding(plan, Issue::IncompatibleApp, Severity::Blocking, options.appVersion, meta.minApp, meta.maxApp);
        }
    }

    std::vector<std::string> recordProblems;
    const std::vector<Record> records = readRecords(layout, &recordProblems);
    Record existing;
    plan.update = findRecord(records, meta.id, existing);
    Version newVersion;
    parseVersion(meta.version, newVersion);
    if (plan.update) {
        plan.installedVersion = existing.version;
        Version oldVersion;
        parseVersion(existing.version, oldVersion);
        const int relation = compareVersion(newVersion, oldVersion);
        if (relation < 0) {
            addFinding(plan, Issue::Downgrade, Severity::Consent, meta.id, existing.version, meta.version, options.allowDowngrade);
        } else if (relation == 0) {
            addFinding(plan, Issue::SameVersion, Severity::Notice, meta.id, existing.version);
        } else {
            addFinding(plan, Issue::Update, Severity::Notice, meta.id, existing.version, meta.version);
        }
    }

    for (const show::PackDependency& dependency : meta.dependencies) {
        Record installed;
        if (!findRecord(records, dependency.id, installed)) {
            addFinding(plan, Issue::MissingDependency, Severity::Blocking, dependency.id, dependency.min);
            continue;
        }
        Version have;
        Version need;
        parseVersion(installed.version, have);
        if (!dependency.min.empty() && parseVersion(dependency.min, need) && compareVersion(have, need) < 0) {
            addFinding(plan, Issue::DependencyTooOld, Severity::Blocking, dependency.id, installed.version, dependency.min);
        }
    }
    for (const Record& other : records) {
        if (other.id == meta.id) {
            continue;
        }
        for (const show::PackDependency& dependency : other.dependencies) {
            Version need;
            if (dependency.id == meta.id && !dependency.min.empty() && parseVersion(dependency.min, need) && compareVersion(newVersion, need) < 0) {
                addFinding(plan, Issue::BreaksDependent, Severity::Blocking, other.id, dependency.min, meta.version);
            }
        }
    }

    std::map<std::string, std::string> ownerOf;
    std::map<std::string, std::string> recordedSha;
    for (const Record& record : records) {
        for (const RecordFile& file : record.files) {
            const std::string key = fileKey(file.kind, record.pluginId, file.name);
            ownerOf[key] = record.id;
            if (record.id == meta.id) {
                recordedSha[key] = file.sha256;
            }
        }
    }

    const show::Item* pluginJson = nullptr;
    size_t pluginItems = 0;
    size_t shaderItems = 0;
    for (const show::Item& item : plan.manifest.items) {
        if (!item.included) {
            continue;
        }
        if (item.kind == show::Kind::Plugin) {
            ++pluginItems;
            if (item.name == "plugin.json") {
                pluginJson = &item;
            }
        } else if (item.kind == show::Kind::Shader) {
            ++shaderItems;
        }
    }
    std::string pluginExtension;
    if (pluginItems > 0) {
        std::string text;
        std::string error;
        if (pluginJson == nullptr) {
            addFinding(plan, Issue::PluginIncompatible, Severity::Blocking, meta.id, "plugin.json is missing");
        } else if (!show::readItemData(package, *pluginJson, kMaxManifestBytes, text, error)) {
            addFinding(plan, Issue::PluginIncompatible, Severity::Blocking, meta.id, error);
        } else {
            PluginEntry entry = parseManifest(text, fs::path(), kPluginApiMajor, kPluginApiMinor, pluginPlatformKey());
            if (entry.problem == PluginProblem::NoLibrary) {
                addFinding(plan, Issue::PluginNotForPlatform, Severity::Notice, entry.manifest.name.empty() ? entry.manifest.id : entry.manifest.name, pluginPlatformKey());
            } else if (entry.problem != PluginProblem::None && entry.problem != PluginProblem::MissingLibrary) {
                addFinding(plan, Issue::PluginIncompatible, Severity::Blocking, entry.manifest.id.empty() ? meta.id : entry.manifest.id, pluginProblemName(entry.problem), entry.detail);
            } else {
                const std::string library = entry.manifest.library;
                const bool shipped = std::any_of(plan.manifest.items.begin(), plan.manifest.items.end(), [&](const show::Item& item) {
                    return item.included && item.kind == show::Kind::Plugin && item.name == library;
                });
                if (!shipped) {
                    addFinding(plan, Issue::PluginIncompatible, Severity::Blocking, entry.manifest.id, "The library file is not in the package", library);
                } else {
                    plan.pluginId = entry.manifest.id;
                    plan.pluginName = entry.manifest.name;
                    pluginExtension = extensionOf(library);
                }
            }
        }
    }

    if (!plan.pluginId.empty()) {
        std::vector<std::string> libraries;
        for (const show::Item& item : plan.manifest.items) {
            if (item.included && item.kind == show::Kind::Plugin && item.name != "plugin.json" && extensionOf(item.name) == pluginExtension) {
                libraries.push_back(item.name);
            }
        }
        plan.pluginLibraries = libraries;
        addFinding(plan, Issue::NativePlugin, Severity::Consent, plan.pluginName, joinWithComma(libraries), plan.pluginId, options.acceptNativePlugin);

        const bool ownPlugin = plan.update && existing.pluginId == plan.pluginId;
        const fs::path folder = layout.plugins / u8path(plan.pluginId);
        std::error_code ec;
        if (!ownPlugin) {
            std::string taken;
            for (const Record& record : records) {
                if (record.id != meta.id && record.pluginId == plan.pluginId) {
                    taken = record.id;
                }
            }
            if (taken.empty() && fs::exists(folder, ec)) {
                taken = plan.pluginId;
            }
            if (taken.empty()) {
                for (const PluginEntry& entry : scanPlugins(layout.plugins)) {
                    if (entry.manifest.id == plan.pluginId) {
                        taken = u8text(entry.directory.filename());
                        break;
                    }
                }
            }
            if (!taken.empty()) {
                addFinding(plan, Issue::PluginIdTaken, Severity::Blocking, plan.pluginId, taken);
            } else {
                size_t folders = 0;
                for (fs::directory_iterator it(layout.plugins, ec), end; !ec && it != end; it.increment(ec)) {
                    std::error_code entryEc;
                    if (it->is_directory(entryEc)) {
                        ++folders;
                    }
                }
                if (folders >= kMaxPluginFolders) {
                    addFinding(plan, Issue::TooManyPlugins, Severity::Blocking, plan.pluginId);
                }
            }
        }
        if (readPluginConfig(layout.files.root).isEnabled(plan.pluginId)) {
            addFinding(plan, Issue::PluginStillOn, Severity::Blocking, plan.pluginName);
        }
        if (inPluginSafeList(readPluginSafeList(layout.files.root), plan.pluginId)) {
            addFinding(plan, Issue::PluginBlockedBySafeMode, Severity::Notice, plan.pluginName);
        }
    }
    if (shaderItems > 0) {
        addFinding(plan, Issue::ShaderCode, Severity::Notice, std::to_string(shaderItems));
    }

    bool hasLicenseFile = false;
    std::set<std::string> newKeys;
    std::error_code ec;
    for (const show::Item& item : plan.manifest.items) {
        if (!item.included) {
            continue;
        }
        if (item.kind == show::Kind::Preview) {
            plan.hasPreview = true;
            plan.preview = item;
            continue;
        }
        FileEntry entry;
        entry.item = item;
        if (item.kind == show::Kind::License) {
            hasLicenseFile = true;
            plan.hasLicenseText = true;
            plan.licenseText = item;
            entry.target = layout.licensePath(meta.id);
            entry.state = fs::exists(entry.target, ec) ? FileState::Update : FileState::New;
            if (fs::is_directory(entry.target, ec)) {
                entry.state = FileState::FolderInWay;
                entry.action = FileAction::Skip;
                addFinding(plan, Issue::FolderInTheWay, Severity::Blocking, item.name);
            }
            plan.files.push_back(entry);
            continue;
        }
        if (!installableKind(item.kind)) {
            continue;
        }
        if (item.kind == show::Kind::Plugin) {
            const bool wanted = !plan.pluginId.empty() && (item.name == "plugin.json" || extensionOf(item.name) == pluginExtension);
            if (!wanted) {
                if (!plan.pluginId.empty() || pluginItems > 0) {
                    entry.state = FileState::PlatformSkipped;
                    entry.action = FileAction::Skip;
                    plan.files.push_back(entry);
                }
                continue;
            }
        }
        const std::string key = fileKey(item.kind, plan.pluginId, item.name);
        newKeys.insert(key);
        entry.target = targetFor(layout, item.kind, plan.pluginId, item.name);
        if (fs::is_directory(entry.target, ec)) {
            entry.state = FileState::FolderInWay;
            entry.action = FileAction::Skip;
            addFinding(plan, Issue::FolderInTheWay, Severity::Blocking, item.name);
            plan.files.push_back(entry);
            continue;
        }
        if (!fs::exists(entry.target, ec)) {
            entry.state = FileState::New;
            entry.action = FileAction::Write;
            plan.files.push_back(entry);
            continue;
        }
        std::string disk;
        if (!shaOf(entry.target, disk)) {
            disk.clear();
        }
        const auto owner = ownerOf.find(key);
        bool conflict = false;
        if (owner != ownerOf.end() && owner->second != meta.id) {
            entry.state = FileState::OwnedByOther;
            entry.action = FileAction::Skip;
            entry.owner = owner->second;
            addFinding(plan, Issue::OwnedByOther, Severity::Blocking, item.name, owner->second);
        } else if (owner != ownerOf.end()) {
            const std::string& known = recordedSha[key];
            if (disk == known) {
                entry.state = disk == item.sha256 ? FileState::Same : FileState::Update;
                entry.action = disk == item.sha256 ? FileAction::Keep : FileAction::Write;
            } else if (disk == item.sha256) {
                entry.state = FileState::Same;
                entry.action = FileAction::Keep;
            } else {
                conflict = true;
            }
        } else if (disk == item.sha256) {
            entry.state = FileState::Identical;
            entry.action = FileAction::Skip;
        } else {
            conflict = true;
        }
        if (conflict) {
            entry.state = FileState::Conflict;
            if (options.onConflict == ConflictPolicy::Replace) {
                entry.action = FileAction::Write;
            } else {
                entry.action = FileAction::Skip;
                if (options.onConflict == ConflictPolicy::KeepMine) {
                    addFinding(plan, Issue::KeptMine, Severity::Notice, item.name);
                } else {
                    addFinding(plan, Issue::FileConflict, Severity::Blocking, item.name);
                }
            }
        }
        plan.files.push_back(entry);
    }
    if (meta.license.empty() && !hasLicenseFile) {
        addFinding(plan, Issue::NoLicense, Severity::Notice, plan.manifest.name);
    }

    if (plan.update) {
        for (const RecordFile& file : existing.files) {
            if (newKeys.count(fileKey(file.kind, existing.pluginId, file.name)) == 0) {
                ObsoleteFile obsolete;
                obsolete.file = file;
                obsolete.path = targetFor(layout, file.kind, existing.pluginId, file.name);
                plan.obsolete.push_back(obsolete);
            }
        }
    }

    for (const FileEntry& entry : plan.files) {
        if (entry.action == FileAction::Write) {
            plan.writeBytes += entry.item.size;
        }
    }
    fs::path probe = layout.files.root;
    std::error_code spaceEc;
    while (!probe.empty() && !fs::exists(probe, spaceEc) && probe.has_parent_path() && probe.parent_path() != probe) {
        probe = probe.parent_path();
    }
    const fs::space_info space = fs::space(probe, spaceEc);
    if (!spaceEc) {
        plan.freeBytes = static_cast<unsigned long long>(space.available);
        plan.enoughSpace = plan.writeBytes + plan.writeBytes / 20 + 1024 * 1024 <= plan.freeBytes;
    }
    plan.ok = true;
    return plan;
}

namespace {

struct Staged {
    const FileEntry* entry = nullptr;
    fs::path temporary;
};

struct Operation {
    fs::path target;
    fs::path backup;
    bool hadBackup = false;
    bool placed = false;
};

fs::path uniqueBackupRoot(const Layout& layout, const std::string& stamp) {
    std::error_code ec;
    fs::path root = layout.backups / u8path(stamp);
    int suffix = 2;
    while (fs::exists(root, ec)) {
        root = layout.backups / u8path(stamp + "-" + std::to_string(suffix++));
    }
    return root;
}

fs::path backupSlot(const fs::path& root, const fs::path& base, const fs::path& target) {
    std::error_code ec;
    fs::path relative = fs::relative(target, base, ec);
    if (ec || relative.empty() || relative.native().find(fs::path("..").native()) == 0) {
        relative = target.filename();
    }
    return root / relative;
}

void undo(std::vector<Operation>& done) {
    std::error_code ec;
    for (auto it = done.rbegin(); it != done.rend(); ++it) {
        if (it->placed) {
            fs::remove(it->target, ec);
        }
        if (it->hadBackup) {
            movePath(it->backup, it->target);
        }
    }
    done.clear();
}

void dropEmptyFolders(std::vector<fs::path>& folders) {
    std::error_code ec;
    for (auto it = folders.rbegin(); it != folders.rend(); ++it) {
        fs::remove(*it, ec);
    }
    folders.clear();
}

}

InstallResult applyInstall(const fs::path& package, const InstallPlan& plan, const Layout& layout, const std::string& backupStamp, const show::ProgressFn& progress) {
    InstallResult result;
    if (!plan.ok) {
        result.error = plan.error.empty() ? "The installation was not planned" : plan.error;
        return result;
    }
    if (plan.blocked()) {
        result.error = plan.enoughSpace ? "The package cannot be installed as it is" : "There is not enough free space for this package";
        return result;
    }
    if (plan.needsConsent()) {
        result.error = "The package needs your permission first";
        return result;
    }
    if (!plan.pluginId.empty() && readPluginConfig(layout.files.root).isEnabled(plan.pluginId)) {
        result.error = "Switch the plugin off before it is changed";
        return result;
    }

    std::vector<Staged> staged;
    std::vector<fs::path> createdFolders;
    show::Progress state;
    state.total = plan.writeBytes;
    unsigned long long base = 0;
    bool cancelled = false;
    auto relay = [&](const show::Progress& inner) {
        state.done = base + inner.done;
        state.current = inner.current;
        if (progress && !progress(state)) {
            cancelled = true;
            return false;
        }
        return true;
    };
    auto discard = [&]() {
        std::error_code ec;
        for (const Staged& item : staged) {
            if (!item.temporary.empty()) {
                fs::remove(item.temporary, ec);
            }
        }
        staged.clear();
        dropEmptyFolders(createdFolders);
    };

    std::error_code ec;
    if (!plan.pluginId.empty()) {
        const fs::path folder = layout.plugins / u8path(plan.pluginId);
        if (!fs::exists(folder, ec)) {
            if (!fs::exists(layout.plugins, ec)) {
                createdFolders.push_back(layout.plugins);
            }
            createdFolders.push_back(folder);
            fs::create_directories(folder, ec);
        }
    }

    for (const FileEntry& entry : plan.files) {
        if (entry.action != FileAction::Write) {
            continue;
        }
        fs::path temporary = entry.target;
        temporary += ".nyxpack-new";
        std::string error;
        if (!show::extractItem(package, entry.item, temporary, true, relay, error)) {
            std::error_code removeEc;
            fs::remove(temporary, removeEc);
            discard();
            result.cancelled = cancelled;
            result.error = cancelled ? "Cancelled" : error;
            return result;
        }
        staged.push_back({&entry, temporary});
        base += entry.item.size;
    }

    fs::path backupRoot;
    std::vector<Operation> done;
    auto fail = [&](const char* message) {
        discard();
        undo(done);
        result.rolledBack = true;
        result.error = message;
        return result;
    };
    auto backupFor = [&](const fs::path& target, const fs::path& rootOfKind) {
        if (backupRoot.empty()) {
            backupRoot = uniqueBackupRoot(layout, backupStamp);
        }
        return backupSlot(backupRoot, rootOfKind, target);
    };

    for (Staged& item : staged) {
        const FileEntry& entry = *item.entry;
        Operation op;
        op.target = entry.target;
        if (fs::exists(entry.target, ec)) {
            const fs::path kindRoot = entry.item.kind == show::Kind::Plugin ? layout.plugins.parent_path() : layout.files.root;
            op.backup = backupFor(entry.target, kindRoot);
            if (!movePath(entry.target, op.backup)) {
                return fail("An existing file could not be saved before it was replaced");
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
            return fail("A file could not be moved to its place");
        }
        item.temporary.clear();
        op.placed = true;
        done.push_back(op);
        if (entry.item.kind == show::Kind::License) {
            continue;
        }
        if (entry.state == FileState::New) {
            ++result.written;
        } else {
            ++result.updated;
        }
    }

    Record record;
    record.id = plan.manifest.meta.id;
    record.version = plan.manifest.meta.version;
    record.name = plan.manifest.name;
    record.author = plan.manifest.meta.author;
    record.license = plan.manifest.meta.license;
    record.installedAt = plan.installedAt;
    record.appVersion = plan.appVersion;
    record.packageSha256 = plan.packageSha256;
    record.pluginId = plan.pluginId;
    record.dependencies = plan.manifest.meta.dependencies;
    for (const FileEntry& entry : plan.files) {
        if (entry.item.kind == show::Kind::License || entry.state == FileState::PlatformSkipped) {
            continue;
        }
        RecordFile file;
        file.kind = entry.item.kind;
        file.name = entry.item.name;
        file.sha256 = entry.item.sha256;
        file.size = entry.item.size;
        if (entry.action == FileAction::Write || entry.action == FileAction::Keep) {
            record.files.push_back(file);
        } else if (entry.state == FileState::Conflict) {
            record.skipped.push_back(file);
            ++result.kept;
        }
    }

    std::vector<Record> previous = readRecords(layout);
    Record old;
    findRecord(previous, record.id, old);
    for (const ObsoleteFile& obsolete : plan.obsolete) {
        std::string disk;
        if (!fs::exists(obsolete.path, ec)) {
            continue;
        }
        if (!shaOf(obsolete.path, disk) || disk != obsolete.file.sha256) {
            ++result.keptModified;
            continue;
        }
        Operation op;
        op.target = obsolete.path;
        op.backup = backupFor(obsolete.path, obsolete.file.kind == show::Kind::Plugin ? layout.plugins.parent_path() : layout.files.root);
        if (!movePath(obsolete.path, op.backup)) {
            return fail("An old file of the package could not be saved");
        }
        op.hadBackup = true;
        done.push_back(op);
        ++result.removed;
    }

    const fs::path recordFile = layout.recordPath(record.id);
    Operation recordOp;
    recordOp.target = recordFile;
    if (fs::exists(recordFile, ec)) {
        recordOp.backup = backupFor(recordFile, layout.files.root);
        if (!movePath(recordFile, recordOp.backup)) {
            return fail("The list of installed files could not be saved");
        }
        recordOp.hadBackup = true;
    }
    fs::path recordTemp = recordFile;
    recordTemp += ".nyxpack-new";
    if (!writeBinary(recordTemp, recordToJson(record))) {
        std::error_code removeEc;
        fs::remove(recordTemp, removeEc);
        done.push_back(recordOp);
        return fail("The list of installed files could not be written");
    }
    fs::rename(recordTemp, recordFile, ec);
    if (ec) {
        std::error_code removeEc;
        fs::remove(recordTemp, removeEc);
        done.push_back(recordOp);
        return fail("The list of installed files could not be written");
    }
    recordOp.placed = true;
    done.push_back(recordOp);

    if (!old.pluginId.empty() && old.pluginId != record.pluginId) {
        std::error_code removeEc;
        fs::remove(layout.plugins / u8path(old.pluginId), removeEc);
    }
    if (!backupRoot.empty() && result.updated == 0 && result.removed == 0) {
        std::error_code removeEc;
        fs::remove_all(backupRoot, removeEc);
        backupRoot.clear();
    }
    result.backupFolder = backupRoot;
    result.pluginInstalled = !plan.pluginId.empty();
    result.pluginId = plan.pluginId;
    result.ok = true;
    return result;
}

RemovePlan planRemove(const Layout& layout, const std::string& id) {
    RemovePlan plan;
    const std::vector<Record> records = readRecords(layout);
    if (!findRecord(records, id, plan.record)) {
        plan.error = "This package is not installed";
        return plan;
    }
    std::error_code ec;
    for (const RecordFile& file : plan.record.files) {
        RemoveEntry entry;
        entry.file = file;
        entry.path = targetFor(layout, file.kind, plan.record.pluginId, file.name);
        std::string disk;
        if (!fs::is_regular_file(entry.path, ec)) {
            entry.state = RemoveState::Missing;
        } else if (!shaOf(entry.path, disk) || disk != file.sha256) {
            entry.state = RemoveState::KeepModified;
        } else {
            entry.state = RemoveState::Remove;
        }
        plan.entries.push_back(entry);
    }
    std::vector<std::string> dependents;
    for (const Record& other : records) {
        if (other.id == id) {
            continue;
        }
        for (const show::PackDependency& dependency : other.dependencies) {
            if (dependency.id == id) {
                dependents.push_back(other.id);
                break;
            }
        }
    }
    if (!dependents.empty()) {
        addFinding(plan, Issue::RequiredBy, Severity::Blocking, joinWithComma(dependents));
    }
    if (!plan.record.pluginId.empty() && readPluginConfig(layout.files.root).isEnabled(plan.record.pluginId)) {
        addFinding(plan, Issue::PluginStillOn, Severity::Blocking, plan.record.name);
    }
    const size_t modified = plan.count(RemoveState::KeepModified);
    if (modified > 0) {
        addFinding(plan, Issue::ModifiedKept, Severity::Notice, std::to_string(modified));
    }
    plan.ok = true;
    return plan;
}

RemoveResult applyRemove(const Layout& layout, const RemovePlan& plan, const std::string& backupStamp) {
    RemoveResult result;
    if (!plan.ok) {
        result.error = plan.error.empty() ? "The removal was not planned" : plan.error;
        return result;
    }
    if (plan.blocked()) {
        result.error = "The package cannot be removed right now";
        return result;
    }
    if (!plan.record.pluginId.empty() && readPluginConfig(layout.files.root).isEnabled(plan.record.pluginId)) {
        result.error = "Switch the plugin off before it is changed";
        return result;
    }
    std::vector<Operation> done;
    fs::path backupRoot = uniqueBackupRoot(layout, backupStamp);
    std::error_code ec;
    auto fail = [&](const char* message) {
        undo(done);
        result.rolledBack = true;
        result.error = message;
        return result;
    };
    for (const RemoveEntry& entry : plan.entries) {
        if (entry.state == RemoveState::KeepModified) {
            ++result.keptModified;
            continue;
        }
        if (entry.state != RemoveState::Remove) {
            continue;
        }
        const fs::path base = entry.file.kind == show::Kind::Plugin ? layout.plugins.parent_path() : layout.files.root;
        Operation op;
        op.target = entry.path;
        op.backup = backupSlot(backupRoot, base, entry.path);
        if (!movePath(entry.path, op.backup)) {
            return fail("A file of the package could not be moved away");
        }
        op.hadBackup = true;
        done.push_back(op);
        ++result.removed;
    }
    const fs::path license = layout.licensePath(plan.record.id);
    if (fs::is_regular_file(license, ec)) {
        Operation op;
        op.target = license;
        op.backup = backupSlot(backupRoot, layout.files.root, license);
        if (!movePath(license, op.backup)) {
            return fail("The licence text of the package could not be moved away");
        }
        op.hadBackup = true;
        done.push_back(op);
    }
    const fs::path recordFile = layout.recordPath(plan.record.id);
    Operation recordOp;
    recordOp.target = recordFile;
    recordOp.backup = backupSlot(backupRoot, layout.files.root, recordFile);
    if (!movePath(recordFile, recordOp.backup)) {
        return fail("The list of installed files could not be moved away");
    }
    recordOp.hadBackup = true;
    done.push_back(recordOp);
    if (!plan.record.pluginId.empty()) {
        std::error_code removeEc;
        fs::remove(layout.plugins / u8path(plan.record.pluginId), removeEc);
    }
    result.backupFolder = backupRoot;
    result.ok = true;
    return result;
}

namespace {

void addProblem(BuildResult& result, const std::string& text) {
    if (result.problems.size() < 50) {
        result.problems.push_back(text);
    }
}

}

BuildResult buildPackFromFolder(const fs::path& folder, const fs::path& destination, bool overwrite, const std::string& appVersion, const std::string& created, const show::ProgressFn& progress) {
    BuildResult result;
    std::error_code ec;
    if (!fs::is_directory(folder, ec)) {
        result.error = "The folder of the package could not be found";
        return result;
    }
    std::string text;
    if (!readSmallFile(folder / "pack.json", kMaxPackJsonBytes, text)) {
        result.error = "The folder has no readable pack.json";
        return result;
    }
    nlohmann::json doc = nlohmann::json::parse(text, nullptr, false);
    if (doc.is_discarded() || !doc.is_object()) {
        result.error = "The pack.json is not valid";
        return result;
    }
    show::WriteRequest request;
    request.format = show::Format::Pack;
    request.name = stringOf(doc, "name", show::kMaxShowNameBytes);
    request.notes = stringOf(doc, "notes", show::kMaxNotesBytes);
    request.appVersion = appVersion;
    request.created = created;
    show::PackMeta& meta = request.meta;
    meta.id = stringOf(doc, "id", 80);
    meta.version = stringOf(doc, "version", 40);
    meta.author = stringOf(doc, "author", show::kMaxPackShortBytes);
    meta.description = stringOf(doc, "description", show::kMaxPackTextBytes);
    meta.license = stringOf(doc, "license", show::kMaxPackShortBytes);
    meta.homepage = stringOf(doc, "homepage", 200);
    auto range = doc.find("nyrexvj");
    if (range != doc.end() && range->is_object()) {
        meta.minApp = stringOf(*range, "min", 40);
        meta.maxApp = stringOf(*range, "max", 40);
    }
    auto dependencies = doc.find("dependencies");
    if (dependencies != doc.end() && dependencies->is_array()) {
        for (const nlohmann::json& entry : *dependencies) {
            if (!entry.is_object()) {
                continue;
            }
            show::PackDependency dependency;
            dependency.id = stringOf(entry, "id", 80);
            dependency.min = stringOf(entry, "min", 40);
            meta.dependencies.push_back(dependency);
        }
    }
    show::Manifest probe;
    probe.format = show::Format::Pack;
    probe.name = request.name;
    probe.meta = meta;
    for (const std::string& problem : validateMeta(probe)) {
        addProblem(result, problem);
    }

    struct Source {
        const char* folder;
        show::Kind kind;
    };
    const Source sources[] = {
        {"presets", show::Kind::Preset},
        {"shaders", show::Kind::Shader},
        {"nodes", show::Kind::Node},
        {"masks", show::Kind::Mask},
        {"venues", show::Kind::Venue},
        {"plugin", show::Kind::Plugin},
    };
    bool hasLibrary = false;
    bool hasPluginJson = false;
    for (const Source& source : sources) {
        const fs::path sub = folder / source.folder;
        if (!fs::is_directory(sub, ec)) {
            continue;
        }
        std::vector<fs::path> names;
        for (fs::directory_iterator it(sub, ec), end; !ec && it != end; it.increment(ec)) {
            names.push_back(it->path());
        }
        std::sort(names.begin(), names.end());
        for (const fs::path& path : names) {
            const std::string name = u8text(path.filename());
            std::error_code statusEc;
            const fs::file_status status = fs::symlink_status(path, statusEc);
            if (statusEc || fs::is_symlink(status)) {
                addProblem(result, "A link was found and left out: " + name);
                continue;
            }
            if (!fs::is_regular_file(status)) {
                addProblem(result, "A folder was found and left out: " + name);
                continue;
            }
            std::string reason;
            if (!show::isSafeItemName(source.kind, name, &reason)) {
                addProblem(result, reason + ": " + name);
                continue;
            }
            show::SourceItem item;
            item.kind = source.kind;
            item.name = name;
            item.file = path;
            if (source.kind == show::Kind::Plugin) {
                if (name == "plugin.json") {
                    hasPluginJson = true;
                    std::string manifestText;
                    if (!readSmallFile(path, kMaxManifestBytes, manifestText)) {
                        addProblem(result, "The plugin.json is too large or cannot be read");
                    } else {
                        const PluginEntry entry = parseManifest(manifestText, fs::path(), kPluginApiMajor, kPluginApiMinor, pluginPlatformKey());
                        if (entry.problem != PluginProblem::None && entry.problem != PluginProblem::MissingLibrary && entry.problem != PluginProblem::NoLibrary) {
                            addProblem(result, std::string("The plugin.json is not usable: ") + pluginProblemName(entry.problem));
                        }
                    }
                } else {
                    hasLibrary = true;
                }
            }
            request.items.push_back(item);
        }
    }
    if (hasPluginJson != hasLibrary) {
        addProblem(result, hasPluginJson ? "The plugin folder has no library file" : "The plugin folder has no plugin.json");
    }
    for (const char* name : {"preview.png", "preview.jpg", "preview.jpeg"}) {
        const fs::path path = folder / name;
        if (fs::is_regular_file(path, ec)) {
            show::SourceItem item;
            item.kind = show::Kind::Preview;
            item.name = name;
            item.file = path;
            request.items.push_back(item);
            meta.preview = name;
            break;
        }
    }
    const fs::path licensePath = folder / "LICENSE.txt";
    if (fs::is_regular_file(licensePath, ec)) {
        show::SourceItem item;
        item.kind = show::Kind::License;
        item.name = "LICENSE.txt";
        item.file = licensePath;
        request.items.push_back(item);
    }
    if (request.items.empty()) {
        addProblem(result, "The folder has nothing to put into a package");
    }
    if (!result.problems.empty()) {
        result.error = result.problems.front();
        return result;
    }
    result.written = show::writePackage(request, destination, overwrite, show::Limits::forPack(), progress);
    result.cancelled = result.written.cancelled;
    result.ok = result.written.ok;
    if (!result.ok) {
        result.error = result.written.error;
    }
    return result;
}

}
