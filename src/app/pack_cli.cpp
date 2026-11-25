#include "app/pack_cli.hpp"

#include "core/pack_workflow.hpp"
#include "scene3d/scene_pack.hpp"
#include "update/changelog.hpp"

#include <cstdio>
#include <ctime>
#include <filesystem>

namespace nvj {

namespace {

std::string stampNow() {
    const std::time_t now = std::time(nullptr);
    std::tm parts{};
#ifdef _WIN32
    localtime_s(&parts, &now);
#else
    localtime_r(&now, &parts);
#endif
    char buffer[40];
    std::strftime(buffer, sizeof(buffer), "%Y%m%d-%H%M%S", &parts);
    return buffer;
}

std::string utcNow() {
    const std::time_t now = std::time(nullptr);
    std::tm parts{};
#ifdef _WIN32
    gmtime_s(&parts, &now);
#else
    gmtime_r(&now, &parts);
#endif
    char buffer[40];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &parts);
    return buffer;
}

const char* severityName(pack::Severity severity) {
    switch (severity) {
        case pack::Severity::Blocking: return "blocking";
        case pack::Severity::Consent: return "needs consent";
        case pack::Severity::Notice: return "notice";
    }
    return "";
}

}

int runPackBuild(const std::string& folder, const std::string& file, bool overwrite) {
    const pack::BuildResult result = pack::buildPackFromFolder(pathFromUtf8(folder), pathFromUtf8(file), overwrite, baseVersionString(NYREXVJ_VERSION), utcNow());
    if (!result.ok) {
        std::printf("Could not build the package: %s\n", result.error.c_str());
        for (const std::string& problem : result.problems) {
            std::printf("  - %s\n", problem.c_str());
        }
        return 1;
    }
    std::printf("Package written: %s (%llu bytes, %zu files)\n", file.c_str(), result.written.bytes, result.written.manifest.items.size());
    return 0;
}

int runPackInfo(const std::string& file) {
    const std::filesystem::path path = pathFromUtf8(file);
    const show::Inspection inspection = show::inspectPackage(path, show::Limits::forPack(), show::Format::Pack);
    if (!inspection.ok) {
        std::printf("Not a usable community package: %s\n", inspection.error.c_str());
        return 1;
    }
    pack::InstallOptions options;
    options.appVersion = baseVersionString(NYREXVJ_VERSION);
    options.now = utcNow();
    const pack::InstallPlan plan = pack::planInstall(path, inspection, pack::Layout::standard(), options);
    const show::Manifest& manifest = inspection.manifest;
    std::printf("Name: %s\nId: %s\nVersion: %s\nAuthor: %s\nLicense: %s\nNyrexVJ: %s to %s\nFingerprint (SHA-256): %s\n", manifest.name.c_str(), manifest.meta.id.c_str(), manifest.meta.version.c_str(), manifest.meta.author.c_str(), manifest.meta.license.c_str(), manifest.meta.minApp.empty() ? "-" : manifest.meta.minApp.c_str(), manifest.meta.maxApp.empty() ? "newest" : manifest.meta.maxApp.c_str(), plan.packageSha256.c_str());
    for (const show::Item& item : manifest.items) {
        std::printf("File: %s %s (%llu bytes)\n", show::kindName(item.kind), item.name.c_str(), item.size);
    }
    if (!plan.ok) {
        std::printf("Cannot be installed: %s\n", plan.error.c_str());
        return 1;
    }
    for (const pack::Finding& finding : plan.findings) {
        std::printf("%s: %s %s\n", severityName(finding.severity), pack::issueName(finding.issue), finding.subject.c_str());
    }
    std::printf(plan.blocked() ? "Result: blocked\n" : plan.needsConsent() ? "Result: possible after your consent\n" : "Result: can be installed\n");
    return plan.blocked() ? 1 : 0;
}

int runPackInstall(const PackInstallArgs& args) {
    const std::filesystem::path path = pathFromUtf8(args.file);
    const show::Inspection inspection = show::inspectPackage(path, show::Limits::forPack(), show::Format::Pack);
    if (!inspection.ok) {
        std::printf("Not a usable community package: %s\n", inspection.error.c_str());
        return 1;
    }
    pack::InstallOptions options;
    options.appVersion = baseVersionString(NYREXVJ_VERSION);
    options.now = utcNow();
    options.acceptNativePlugin = args.acceptPlugin;
    options.allowDowngrade = args.allowDowngrade;
    options.onConflict = args.conflict == 2 ? pack::ConflictPolicy::Replace : args.conflict == 1 ? pack::ConflictPolicy::KeepMine : pack::ConflictPolicy::Block;
    const pack::Layout layout = pack::Layout::standard();
    const pack::InstallPlan plan = pack::planInstall(path, inspection, layout, options);
    if (!plan.ok) {
        std::printf("Cannot be installed: %s\n", plan.error.c_str());
        return 1;
    }
    for (const pack::Finding& finding : plan.findings) {
        std::printf("%s: %s %s\n", severityName(finding.severity), pack::issueName(finding.issue), finding.subject.c_str());
    }
    if (!plan.ready()) {
        std::printf(plan.blocked() ? "Nothing was installed: something stands in the way\n" : "Nothing was installed: your consent is needed (--pack-accept-plugin, --pack-allow-downgrade)\n");
        return 1;
    }
    const pack::InstallResult result = pack::applyInstall(path, plan, layout, stampNow());
    if (!result.ok) {
        std::printf("Installation failed: %s%s\n", result.error.c_str(), result.rolledBack ? " (everything was put back)" : "");
        return 1;
    }
    std::printf("Installed %s %s: %zu added, %zu updated, %zu old files moved to the backup%s\n", inspection.manifest.meta.id.c_str(), inspection.manifest.meta.version.c_str(), result.written, result.updated, result.removed, result.pluginInstalled ? ", plugin installed but switched off" : "");
    return 0;
}

int runPackRemove(const std::string& id) {
    const pack::Layout layout = pack::Layout::standard();
    const pack::RemovePlan plan = pack::planRemove(layout, id);
    if (!plan.ok) {
        std::printf("Cannot remove: %s\n", plan.error.c_str());
        return 1;
    }
    for (const pack::Finding& finding : plan.findings) {
        std::printf("%s: %s %s\n", severityName(finding.severity), pack::issueName(finding.issue), finding.subject.c_str());
    }
    if (plan.blocked()) {
        std::printf("Nothing was removed: something stands in the way\n");
        return 1;
    }
    const pack::RemoveResult result = pack::applyRemove(layout, plan, stampNow());
    if (!result.ok) {
        std::printf("Removal failed: %s%s\n", result.error.c_str(), result.rolledBack ? " (everything was put back)" : "");
        return 1;
    }
    std::printf("Removed %s: %zu files moved to the backup, %zu changed files kept\n", id.c_str(), result.removed, result.keptModified);
    return 0;
}

}
