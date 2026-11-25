#pragma once

#include "core/show_package.hpp"
#include "core/show_workflow.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace nvj::pack {

constexpr size_t kMaxRecords = 512;
constexpr size_t kMaxRecordBytes = 1024 * 1024;
constexpr size_t kMaxPackJsonBytes = 64 * 1024;

struct Layout {
    show::Layout files;
    std::filesystem::path plugins;
    std::filesystem::path records;
    std::filesystem::path backups;

    static Layout underRoot(const std::filesystem::path& root);
    static Layout standard();
    std::filesystem::path recordPath(const std::string& id) const;
    std::filesystem::path licensePath(const std::string& id) const;
};

struct Version {
    unsigned part[4] = {0, 0, 0, 0};
};

bool parseVersion(const std::string& text, Version& out);
int compareVersion(const Version& left, const Version& right);
std::string versionText(const Version& version);

std::vector<std::string> validateMeta(const show::Manifest& manifest);

struct RecordFile {
    show::Kind kind = show::Kind::Preset;
    std::string name;
    std::string sha256;
    unsigned long long size = 0;
};

struct Record {
    std::string id;
    std::string version;
    std::string name;
    std::string author;
    std::string license;
    std::string installedAt;
    std::string appVersion;
    std::string packageSha256;
    std::string pluginId;
    std::vector<show::PackDependency> dependencies;
    std::vector<RecordFile> files;
    std::vector<RecordFile> skipped;
};

std::string recordToJson(const Record& record);
bool recordFromJson(const std::string& text, Record& record);
std::vector<Record> readRecords(const Layout& layout, std::vector<std::string>* problems = nullptr);
bool findRecord(const std::vector<Record>& records, const std::string& id, Record& out);

enum class Issue {
    IncompatibleApp,
    MissingDependency,
    DependencyTooOld,
    BreaksDependent,
    Downgrade,
    SameVersion,
    Update,
    NativePlugin,
    PluginIncompatible,
    PluginNotForPlatform,
    PluginIdTaken,
    TooManyPlugins,
    PluginBlockedBySafeMode,
    FileConflict,
    OwnedByOther,
    FolderInTheWay,
    KeptMine,
    ShaderCode,
    NoLicense,
    RequiredBy,
    PluginStillOn,
    ModifiedKept
};

const char* issueName(Issue issue);

enum class Severity {
    Blocking,
    Consent,
    Notice
};

struct Finding {
    Issue issue = Issue::Update;
    Severity severity = Severity::Notice;
    std::string subject;
    std::string detail;
    std::string other;
    bool accepted = false;
};

enum class ConflictPolicy {
    Block,
    KeepMine,
    Replace
};

enum class FileState {
    New,
    Update,
    Same,
    Identical,
    Conflict,
    OwnedByOther,
    FolderInWay,
    PlatformSkipped
};

enum class FileAction {
    Write,
    Keep,
    Skip
};

struct FileEntry {
    show::Item item;
    std::filesystem::path target;
    FileState state = FileState::New;
    FileAction action = FileAction::Write;
    std::string owner;
};

struct ObsoleteFile {
    RecordFile file;
    std::filesystem::path path;
};

struct InstallOptions {
    ConflictPolicy onConflict = ConflictPolicy::Block;
    std::string knownSha256;
    bool acceptNativePlugin = false;
    bool allowDowngrade = false;
    std::string appVersion;
    std::string now;
};

struct InstallPlan {
    bool ok = false;
    bool cancelled = false;
    std::string error;
    show::Manifest manifest;
    std::string packageSha256;
    std::string appVersion;
    std::string installedAt;
    std::vector<FileEntry> files;
    std::vector<Finding> findings;
    std::vector<ObsoleteFile> obsolete;
    bool update = false;
    std::string installedVersion;
    std::string pluginId;
    std::string pluginName;
    std::vector<std::string> pluginLibraries;
    bool hasPreview = false;
    show::Item preview;
    bool hasLicenseText = false;
    show::Item licenseText;
    unsigned long long writeBytes = 0;
    unsigned long long freeBytes = 0;
    bool enoughSpace = true;

    size_t count(Severity severity) const;
    size_t count(FileState state) const;
    bool blocked() const;
    bool needsConsent() const;
    bool ready() const;
};

InstallPlan planInstall(const std::filesystem::path& package, const show::Inspection& inspection, const Layout& layout, const InstallOptions& options, const show::ProgressFn& progress = nullptr);

struct InstallResult {
    bool ok = false;
    bool cancelled = false;
    bool rolledBack = false;
    std::string error;
    std::filesystem::path backupFolder;
    size_t written = 0;
    size_t updated = 0;
    size_t kept = 0;
    size_t removed = 0;
    size_t keptModified = 0;
    bool pluginInstalled = false;
    std::string pluginId;
};

InstallResult applyInstall(const std::filesystem::path& package, const InstallPlan& plan, const Layout& layout, const std::string& backupStamp, const show::ProgressFn& progress = nullptr);

enum class RemoveState {
    Remove,
    KeepModified,
    Missing
};

struct RemoveEntry {
    RecordFile file;
    std::filesystem::path path;
    RemoveState state = RemoveState::Remove;
};

struct RemovePlan {
    bool ok = false;
    std::string error;
    Record record;
    std::vector<RemoveEntry> entries;
    std::vector<Finding> findings;

    size_t count(RemoveState state) const;
    bool blocked() const;
};

RemovePlan planRemove(const Layout& layout, const std::string& id);

struct RemoveResult {
    bool ok = false;
    bool rolledBack = false;
    std::string error;
    std::filesystem::path backupFolder;
    size_t removed = 0;
    size_t keptModified = 0;
};

RemoveResult applyRemove(const Layout& layout, const RemovePlan& plan, const std::string& backupStamp);

struct BuildResult {
    bool ok = false;
    bool cancelled = false;
    std::string error;
    std::vector<std::string> problems;
    show::WriteResult written;
};

BuildResult buildPackFromFolder(const std::filesystem::path& folder, const std::filesystem::path& destination, bool overwrite, const std::string& appVersion, const std::string& created, const show::ProgressFn& progress = nullptr);

}
