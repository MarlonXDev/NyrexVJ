#pragma once

#include "core/show_package.hpp"

#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace nvj::show {

struct Layout {
    std::filesystem::path root;
    std::filesystem::path media;
    std::filesystem::path models;
    std::filesystem::path scenePacks;
    std::filesystem::path shaders;
    std::filesystem::path nodes;
    std::filesystem::path venues;
    std::filesystem::path masks;
    std::filesystem::path presets;
    std::filesystem::path cues;
    std::filesystem::path timeline;

    static Layout underRoot(const std::filesystem::path& root);
    static Layout standard();
    bool placesFile(Kind kind) const;
    std::filesystem::path folderFor(Kind kind) const;
    std::filesystem::path pathFor(Kind kind, const std::string& name) const;
};

struct CatalogueEntry {
    Kind kind = Kind::Media;
    std::string fileName;
    std::filesystem::path path;
};

struct Catalogue {
    std::map<std::string, CatalogueEntry> clips;
    std::map<std::string, CatalogueEntry> effects;
    std::set<std::string> builtInClips;
    std::set<std::string> builtInEffects;
    std::map<std::string, std::string> liveClips;
    std::map<std::string, std::string> pluginClips;

    static Catalogue fromLibraries();
};

struct References {
    std::set<std::string> clips;
    std::set<std::string> effects;
};

using NameMap = std::map<std::string, std::string>;

bool collectReferences(const std::string& jsonText, References& out, std::string& error);
bool renameReferences(const std::string& jsonText, const NameMap& clips, const NameMap& effects, std::string& out, std::string& error);
void collectShaderMediaReferences(const std::string& shaderText, std::set<std::string>& clips);
std::string clipNameForFile(Kind kind, const std::string& fileName);
std::string effectNameForFile(Kind kind, const std::string& fileName);
std::vector<std::string> gltfExternalFiles(const std::string& gltfText);
std::vector<std::string> venueMaskImages(const std::string& venueText);
int compareVersions(const std::string& left, const std::string& right);

enum class Warn {
    UnknownClip,
    UnknownEffect,
    UnsafeExternalFile,
    UnreadableFile,
    NewerApp,
    PluginNeeded
};

struct Warning {
    Warn code = Warn::UnknownClip;
    std::string subject;
};

enum class Problem {
    None,
    Missing,
    TooLarge,
    UnsafeName
};

struct ExportRequest {
    std::string name;
    std::string notes;
    std::string compositionJson;
    std::vector<std::string> presets;
    std::vector<std::string> venues;
    bool includeCues = false;
    bool includeTimeline = false;
};

struct PlanItem {
    SourceItem source;
    unsigned long long size = 0;
    Problem problem = Problem::None;
    std::string detail;
    std::set<std::string> neededBy;
};

struct ExportPlan {
    std::vector<PlanItem> items;
    std::vector<std::string> plugins;
    std::vector<std::string> live;
    std::vector<Warning> warnings;

    unsigned long long includedBytes() const;
    size_t includedCount() const;
    size_t missingCount() const;
    size_t blockedCount() const;
    size_t withoutLicenseCount() const;
    WriteRequest toRequest(const std::string& name, const std::string& notes, const std::string& appVersion, const std::string& created) const;
};

ExportPlan planExport(const ExportRequest& request, const Catalogue& catalogue, const Layout& layout, const Limits& limits = {});

enum class Policy {
    KeepMine,
    KeepBoth,
    Replace
};

enum class State {
    New,
    Identical,
    Different,
    NotIncluded,
    Blocked,
    NotApplied
};

enum class Action {
    Copy,
    Skip,
    CopyRenamed,
    ReplaceFile,
    Apply
};

struct ImportOptions {
    Policy policy = Policy::KeepMine;
    bool applyCues = false;
    bool applyTimeline = false;
    std::string appVersion;
};

struct ImportEntry {
    Item item;
    std::filesystem::path target;
    std::string finalName;
    State state = State::New;
    Action action = Action::Skip;
};

struct ImportPlan {
    bool ok = false;
    bool cancelled = false;
    std::string error;
    Manifest manifest;
    std::vector<ImportEntry> entries;
    unsigned long long copyBytes = 0;
    unsigned long long freeBytes = 0;
    bool enoughSpace = true;
    NameMap clipRenames;
    NameMap effectRenames;
    std::vector<std::string> unresolved;
    std::vector<Warning> warnings;

    size_t count(State state) const;
    size_t count(Action action) const;
};

ImportPlan planImport(const std::filesystem::path& package, const Inspection& inspection, const Layout& layout, const Catalogue& catalogue, const ImportOptions& options, const ProgressFn& progress = nullptr);

struct ImportResult {
    bool ok = false;
    bool cancelled = false;
    bool rolledBack = false;
    std::string error;
    std::string compositionJson;
    std::filesystem::path backupFolder;
    size_t copied = 0;
    size_t renamed = 0;
    size_t replaced = 0;
    size_t kept = 0;
    size_t applied = 0;
    std::vector<std::string> unresolved;
    std::vector<std::string> written;
};

ImportResult applyImport(const std::filesystem::path& package, const ImportPlan& plan, const Layout& layout, const std::string& backupStamp, const ProgressFn& progress = nullptr);

struct RepairCandidate {
    size_t entry = 0;
    std::filesystem::path found;
    bool verified = false;
};

struct RepairScan {
    std::vector<RepairCandidate> candidates;
    size_t scanned = 0;
    bool truncated = false;
    bool cancelled = false;
};

constexpr int kRepairMaxDepth = 6;
constexpr size_t kRepairMaxEntries = 20000;

RepairScan findMissingFiles(const ImportPlan& plan, const std::filesystem::path& folder, const ProgressFn& progress = nullptr);
bool applyRepair(const ImportPlan& plan, const std::vector<RepairCandidate>& accepted, std::vector<std::string>& copied, std::string& error, const ProgressFn& progress = nullptr);

}
