#include "app/app.hpp"

#include "app/ui_common.hpp"
#include "core/log.hpp"
#include "core/plugin_manifest.hpp"
#include "core/preset.hpp"
#include "platform/platform.hpp"
#include "scene3d/image_decode.hpp"
#include "scene3d/scene_pack.hpp"
#include "update/changelog.hpp"

#include <glad/gl.h>

#include <imgui.h>

#include <algorithm>
#include <cfloat>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>
#include <vector>

namespace nvj {

namespace {

namespace fs = std::filesystem;

const ImVec4 kGood(0.45f, 0.85f, 0.45f, 1.0f);
const ImVec4 kWarn(1.0f, 0.65f, 0.2f, 1.0f);
const ImVec4 kBad(1.0f, 0.4f, 0.35f, 1.0f);

std::string sizeText(unsigned long long bytes) {
    char buffer[48];
    if (bytes >= 1024ull * 1024ull * 1024ull) {
        std::snprintf(buffer, sizeof(buffer), "%.2f GB", static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0));
    } else if (bytes >= 1024ull * 1024ull) {
        std::snprintf(buffer, sizeof(buffer), "%.1f MB", static_cast<double>(bytes) / (1024.0 * 1024.0));
    } else if (bytes >= 1024ull) {
        std::snprintf(buffer, sizeof(buffer), "%.0f KB", static_cast<double>(bytes) / 1024.0);
    } else {
        std::snprintf(buffer, sizeof(buffer), "%llu B", bytes);
    }
    return buffer;
}

std::string with1(const char* format, const std::string& a) {
    char buffer[960];
    std::snprintf(buffer, sizeof(buffer), format, a.c_str());
    return buffer;
}

std::string with2(const char* format, const std::string& a, const std::string& b) {
    char buffer[960];
    std::snprintf(buffer, sizeof(buffer), format, a.c_str(), b.c_str());
    return buffer;
}

std::string with3(const char* format, const std::string& a, const std::string& b, const std::string& c) {
    char buffer[960];
    std::snprintf(buffer, sizeof(buffer), format, a.c_str(), b.c_str(), c.c_str());
    return buffer;
}

std::string withCount(const char* format, size_t count) {
    char buffer[320];
    std::snprintf(buffer, sizeof(buffer), format, count);
    return buffer;
}

std::string utcStamp() {
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

std::string localStamp() {
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

std::string pathText(const fs::path& path) {
    const std::u8string text = path.u8string();
    return std::string(text.begin(), text.end());
}

void copyText(char* target, size_t size, const std::string& text) {
    std::snprintf(target, size, "%s", text.c_str());
}

std::string translateProblem(const std::string& text) {
    const std::string whole = trMessage(text);
    if (whole != text) {
        return whole;
    }
    const size_t colon = text.find(": ");
    if (colon == std::string::npos) {
        return text;
    }
    return trMessage(text.substr(0, colon)) + text.substr(colon);
}

const char* kindLabel(show::Kind kind) {
    switch (kind) {
        case show::Kind::Preset: return tr("Preset");
        case show::Kind::Shader: return tr("Shader");
        case show::Kind::Node: return tr("Node effect");
        case show::Kind::Venue: return tr("Venue setup");
        case show::Kind::Mask: return tr("Mask picture");
        case show::Kind::Plugin: return tr("Plugin");
        case show::Kind::License: return tr("Licence text");
        default: break;
    }
    return "";
}

std::string findingText(const pack::Finding& finding) {
    using pack::Issue;
    switch (finding.issue) {
        case Issue::IncompatibleApp:
            if (!finding.detail.empty() && !finding.other.empty()) {
                return with3(tr("This package is made for NyrexVJ %s up to %s. You have %s."), finding.detail, finding.other, finding.subject);
            }
            if (!finding.detail.empty()) {
                return with2(tr("This package needs NyrexVJ %s or newer. You have %s."), finding.detail, finding.subject);
            }
            return with2(tr("This package was made for NyrexVJ up to %s. You have %s."), finding.other, finding.subject);
        case Issue::MissingDependency:
            if (finding.detail.empty()) {
                return with1(tr("This package needs the package '%s', which is not installed."), finding.subject);
            }
            return with2(tr("This package needs the package '%s' (version %s or newer), which is not installed."), finding.subject, finding.detail);
        case Issue::DependencyTooOld:
            return with3(tr("This package needs '%s' in version %s or newer, but version %s is installed."), finding.subject, finding.other, finding.detail);
        case Issue::BreaksDependent:
            return with3(tr("The installed package '%s' needs this package in version %s or newer, but this one is version %s."), finding.subject, finding.detail, finding.other);
        case Issue::Downgrade:
            return with2(tr("Version %s is installed. This package is the older version %s."), finding.detail, finding.other);
        case Issue::SameVersion:
            return with1(tr("Version %s is already installed. Installing it again puts back files that are missing."), finding.detail);
        case Issue::Update:
            return with2(tr("Update from version %s to version %s."), finding.detail, finding.other);
        case Issue::NativePlugin:
            return with2(tr("This package contains the plugin '%s' (%s). A plugin is a program that runs inside NyrexVJ with the same rights as NyrexVJ. Only install it if you trust the author. It stays switched off until you switch it on yourself."), finding.subject, finding.detail);
        case Issue::PluginIncompatible:
            return with1(tr("The plugin in this package cannot be used with this version of NyrexVJ (%s)."), finding.detail);
        case Issue::PluginNotForPlatform:
            return with2(tr("The plugin '%s' has no version for this system (%s), so it is left out. The rest is installed."), finding.subject, finding.detail);
        case Issue::PluginIdTaken:
            return with2(tr("A plugin named '%s' already exists (%s). Remove it first."), finding.subject, finding.detail);
        case Issue::TooManyPlugins:
            return tr("The plugins folder is full, NyrexVJ looks at 16 plugin folders at most.");
        case Issue::PluginBlockedBySafeMode:
            return with1(tr("The plugin '%s' was stopped before because it crashed. It stays blocked until you clear it in the plugin settings."), finding.subject);
        case Issue::PluginStillOn:
            return with1(tr("Switch the plugin '%s' off in the plugin settings first, then try again."), finding.subject);
        case Issue::FileConflict:
            return with1(tr("You already have a different file named '%s'."), finding.subject);
        case Issue::KeptMine:
            return with1(tr("Your file '%s' is kept. The one from the package is not installed."), finding.subject);
        case Issue::OwnedByOther:
            return with2(tr("'%s' belongs to the installed package '%s'."), finding.subject, finding.detail);
        case Issue::FolderInTheWay:
            return with1(tr("A folder has the name '%s', so the file cannot be put there."), finding.subject);
        case Issue::ShaderCode:
            return with1(tr("This package contains %s shader files. A shader is a small program that runs on your graphics card. A faulty one can make the picture stutter or freeze. Only install shaders from authors you trust."), finding.subject);
        case Issue::NoLicense:
            return with1(tr("The package '%s' does not state a licence. Ask the author what you may do with it."), finding.subject);
        case Issue::RequiredBy:
            return with1(tr("These installed packages need it: %s"), finding.subject);
        case Issue::ModifiedKept:
            return with1(tr("%s files were changed by you and stay where they are."), finding.subject);
    }
    return std::string();
}

std::string stateText(const pack::FileEntry& entry, int conflict) {
    using pack::FileState;
    switch (entry.state) {
        case FileState::New: return tr("Will be added");
        case FileState::Update: return tr("Will be updated");
        case FileState::Same: return tr("Already installed, unchanged");
        case FileState::Identical: return tr("You already have this file, left alone");
        case FileState::Conflict:
            if (conflict == 2) {
                return tr("Replaces yours, yours is saved first");
            }
            if (conflict == 1) {
                return tr("You have a different file, yours is kept");
            }
            return tr("You have a different file with this name");
        case FileState::OwnedByOther: return with1(tr("Belongs to the package '%s'"), entry.owner);
        case FileState::FolderInWay: return tr("A folder has this name");
        case FileState::PlatformSkipped: return tr("Not for this system, left out");
    }
    return std::string();
}

ImVec4 severityColor(pack::Severity severity) {
    switch (severity) {
        case pack::Severity::Blocking: return kBad;
        case pack::Severity::Consent: return kWarn;
        case pack::Severity::Notice: return ImVec4(0.8f, 0.8f, 0.55f, 1.0f);
    }
    return kWarn;
}

}

void App::openCommunityPackages(int tab) {
    packDialog_.open = true;
    packDialog_.focus = true;
    packDialog_.requestedTab = tab;
    packDialog_.recordsDirty = true;
}

void App::importCommunityPackage(const std::string& path) {
    openCommunityPackages(1);
    copyText(packDialog_.packagePath, sizeof(packDialog_.packagePath), path);
    packDialog_.hasResult = false;
    packDialog_.installMessage.clear();
    packDialog_.checked = false;
    packDialog_.conflict = 0;
    packDialog_.acceptPlugin = false;
    packDialog_.allowDowngrade = false;
    startPackCheck();
}

void App::startPackJob(PackJobKind kind, const std::function<void(PackJobResult&, const show::ProgressFn&)>& body) {
    if (packDialog_.job.valid()) {
        return;
    }
    auto shared = std::make_shared<ShowJobShared>();
    packDialog_.shared = shared;
    packDialog_.running = kind;
    packDialog_.job = std::async(std::launch::async, [kind, body, shared]() {
        PackJobResult result;
        result.kind = kind;
        body(result, shared->callback());
        return result;
    });
}

void App::startPackCheck() {
    PackDialog& d = packDialog_;
    if (d.job.valid()) {
        return;
    }
    const fs::path package = pathFromUtf8(d.packagePath);
    pack::InstallOptions options;
    options.onConflict = d.conflict == 2 ? pack::ConflictPolicy::Replace : d.conflict == 1 ? pack::ConflictPolicy::KeepMine : pack::ConflictPolicy::Block;
    options.acceptNativePlugin = d.acceptPlugin;
    options.allowDowngrade = d.allowDowngrade;
    options.appVersion = baseVersionString(NYREXVJ_VERSION);
    options.now = utcStamp();
    const bool fresh = !(d.checked && d.plan.ok);
    if (!fresh) {
        options.knownSha256 = d.plan.packageSha256;
    }
    const show::Inspection known = d.inspection;
    const pack::Layout layout = pack::Layout::standard();
    d.replan = false;
    startPackJob(PackJobKind::Check, [package, options, layout, fresh, known](PackJobResult& result, const show::ProgressFn& progress) {
        result.fresh = fresh;
        result.inspection = fresh ? show::inspectPackage(package, show::Limits::forPack(), show::Format::Pack) : known;
        result.plan = pack::planInstall(package, result.inspection, layout, options, progress);
        if (fresh && result.plan.ok) {
            std::string error;
            if (result.plan.hasPreview) {
                std::string bytes;
                if (show::readItemData(package, result.plan.preview, 1024 * 1024, bytes, error)) {
                    result.previewBytes.assign(bytes.begin(), bytes.end());
                }
            }
            if (result.plan.hasLicenseText) {
                show::readItemData(package, result.plan.licenseText, 64 * 1024, result.licenseText, error);
            }
        }
    });
}

void App::afterPackChange() {
    rescanLibrary();
    engine_.syncLibrary();
    syncNodeEffects();
    assetsDirty_ = true;
    convertScanPending_ = true;
    refreshUserPresets();
    venueListDirty_ = true;
    if (plugins_) {
        plugins_->rescan();
    }
    packDialog_.recordsDirty = true;
}

void App::servicePackJob() {
    PackDialog& d = packDialog_;
    if (!d.job.valid() || d.job.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
        return;
    }
    PackJobResult result = d.job.get();
    d.running = PackJobKind::None;
    switch (result.kind) {
        case PackJobKind::Check:
            d.plan = result.plan;
            if (result.fresh) {
                d.inspection = result.inspection;
                d.licenseText = result.licenseText;
                if (d.previewTexture != 0) {
                    glDeleteTextures(1, &d.previewTexture);
                    d.previewTexture = 0;
                }
                d.previewWidth = 0;
                d.previewHeight = 0;
                if (!result.previewBytes.empty()) {
                    SceneImage image;
                    std::string error;
                    if (decodeMediaImage(result.previewBytes.data(), result.previewBytes.size(), 2048, 16u * 1024u * 1024u, image, error) && image.width > 0 && image.height > 0) {
                        glGenTextures(1, &d.previewTexture);
                        glBindTexture(GL_TEXTURE_2D, d.previewTexture);
                        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
                        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, image.width, image.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, image.rgba.data());
                        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                        glBindTexture(GL_TEXTURE_2D, 0);
                        d.previewWidth = image.width;
                        d.previewHeight = image.height;
                    }
                }
            }
            d.checked = result.plan.ok;
            if (!result.plan.ok) {
                d.installMessage = result.plan.cancelled ? std::string(tr("Cancelled")) : translateProblem(result.plan.error);
            } else if (!d.hasResult) {
                d.installMessage.clear();
            }
            break;
        case PackJobKind::Install:
            d.installed = result.installed;
            d.hasResult = result.installed.ok;
            if (result.installed.ok) {
                d.installMessage.clear();
                afterPackChange();
                toast(tr("Community package installed"));
                d.replan = true;
            } else {
                d.installMessage = result.installed.cancelled ? std::string(tr("Cancelled, nothing was changed")) : translateProblem(result.installed.error);
                if (result.installed.rolledBack) {
                    d.installMessage += std::string(" ") + tr("Everything was put back as it was.");
                }
            }
            break;
        case PackJobKind::PlanRemove:
            d.removal = result.removal;
            d.removalPlanned = result.removal.ok;
            d.removeMessage = result.removal.ok ? std::string() : translateProblem(result.removal.error);
            break;
        case PackJobKind::Remove:
            if (result.removed.ok) {
                d.removeMessage = with1(tr("Removed. The files were moved to %s"), pathText(result.removed.backupFolder));
                d.removalPlanned = false;
                d.selected.clear();
                afterPackChange();
                toast(tr("Community package removed"));
            } else {
                d.removeMessage = translateProblem(result.removed.error);
                if (result.removed.rolledBack) {
                    d.removeMessage += std::string(" ") + tr("Everything was put back as it was.");
                }
            }
            break;
        case PackJobKind::Build:
            d.buildOk = result.built.ok;
            d.buildProblems.clear();
            if (result.built.ok) {
                d.buildMessage = with2(tr("Package saved: %s (%s)"), result.destination, sizeText(result.built.written.bytes));
                toast(d.buildMessage);
            } else if (result.built.cancelled) {
                d.buildMessage = tr("Cancelled, nothing was written");
            } else {
                d.buildMessage = translateProblem(result.built.error);
                for (const std::string& problem : result.built.problems) {
                    d.buildProblems.push_back(translateProblem(problem));
                }
            }
            break;
        case PackJobKind::None:
            break;
    }
    d.shared.reset();
}

void App::drawPackProgress() {
    PackDialog& d = packDialog_;
    if (!d.job.valid() || !d.shared) {
        return;
    }
    const unsigned long long total = d.shared->total.load();
    const unsigned long long done = d.shared->done.load();
    const float fraction = total > 0 ? static_cast<float>(static_cast<double>(done) / static_cast<double>(total)) : 0.0f;
    char label[96];
    std::snprintf(label, sizeof(label), "%s / %s", sizeText(done).c_str(), sizeText(total).c_str());
    ImGui::ProgressBar(std::clamp(fraction, 0.0f, 1.0f), ImVec2(-120.0f, 0.0f), total > 0 ? label : "");
    ImGui::SameLine();
    if (ImGui::Button(tr("Cancel"), ImVec2(110.0f, 0.0f))) {
        d.shared->cancel = true;
    }
    const std::string current = d.shared->currentText();
    if (!current.empty()) {
        ImGui::TextDisabled("%s", current.c_str());
    }
}

void App::drawPackInstall() {
    PackDialog& d = packDialog_;
    const bool busy = d.job.valid();
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("%s", tr("Installs a community package: presets, shaders, node effects, masks, venue setups and sometimes a plugin made by someone else. You first see what would happen, nothing is changed until you press Install. Nothing is switched on by itself and your own files are never replaced without asking. You can also drop a .nyxpack file onto the window."));
    ImGui::PopTextWrapPos();
    ImGui::BeginDisabled(busy);
    const char* checkLabel = tr("Check package");
    const float checkWidth = ImGui::CalcTextSize(checkLabel).x + ImGui::GetStyle().FramePadding.x * 2.0f;
    ImGui::SetNextItemWidth(-(checkWidth + ImGui::GetStyle().ItemSpacing.x));
    ImGui::InputText("##packPackagePath", d.packagePath, sizeof(d.packagePath));
    ImGui::SameLine();
    if (ImGui::Button(checkLabel)) {
        d.checked = false;
        d.hasResult = false;
        d.installMessage.clear();
        d.conflict = 0;
        d.acceptPlugin = false;
        d.allowDowngrade = false;
        startPackCheck();
    }
    ImGui::EndDisabled();
    if (!d.installMessage.empty()) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(d.hasResult ? kGood : kBad, "%s", d.installMessage.c_str());
        ImGui::PopTextWrapPos();
    }
    if (d.replan && !busy && d.checked) {
        startPackCheck();
    }
    drawPackProgress();
    if (!d.checked || !d.plan.ok) {
        return;
    }

    const pack::InstallPlan& plan = d.plan;
    const show::Manifest& manifest = plan.manifest;
    const show::PackMeta& meta = manifest.meta;
    ImGui::Spacing();
    ui::sectionHeader((manifest.name + "  " + meta.version).c_str());
    if (d.previewTexture != 0) {
        const float width = std::min(260.0f, ImGui::GetContentRegionAvail().x);
        const float height = width * static_cast<float>(d.previewHeight) / static_cast<float>(std::max(1, d.previewWidth));
        ImGui::Image(ui::texId(d.previewTexture), ImVec2(width, std::min(height, 200.0f)));
    }
    if (!meta.author.empty()) {
        ImGui::TextUnformatted(with1(tr("By %s"), meta.author).c_str());
    }
    if (!meta.description.empty()) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextUnformatted(meta.description.c_str());
        ImGui::PopTextWrapPos();
    }
    ImGui::TextDisabled("%s", with1(tr("Id: %s"), meta.id).c_str());
    ImGui::TextDisabled("%s", with1(tr("Licence: %s"), meta.license.empty() ? std::string(tr("not stated")) : meta.license).c_str());
    if (!meta.homepage.empty()) {
        ImGui::TextDisabled("%s", with1(tr("Homepage: %s"), meta.homepage).c_str());
    }
    if (!meta.minApp.empty() || !meta.maxApp.empty()) {
        const std::string range = meta.minApp.empty() ? std::string("-") : meta.minApp;
        ImGui::TextDisabled("%s", with2(tr("Works with NyrexVJ: %s to %s"), range, meta.maxApp.empty() ? std::string(tr("newest")) : meta.maxApp).c_str());
    }
    for (const show::PackDependency& dependency : meta.dependencies) {
        ImGui::TextDisabled("%s", (dependency.min.empty() ? with1(tr("Needs the package: %s"), dependency.id) : with2(tr("Needs the package: %s (version %s or newer)"), dependency.id, dependency.min)).c_str());
    }
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("%s", with1(tr("Fingerprint of the file (SHA-256): %s"), plan.packageSha256).c_str());
    ImGui::TextDisabled("%s", tr("If the author published a fingerprint, it should be the same. Different means the file is not the one the author made."));
    ImGui::PopTextWrapPos();
    if (!d.licenseText.empty() && ImGui::TreeNode(tr("Licence text"))) {
        ImGui::BeginChild("##packLicense", ImVec2(0.0f, ImGui::GetTextLineHeight() * 8.0f), ImGuiChildFlags_Borders);
        ImGui::TextUnformatted(d.licenseText.c_str());
        ImGui::EndChild();
        ImGui::TreePop();
    }
    if (!d.inspection.warnings.empty()) {
        ImGui::TextColored(kWarn, "%s", withCount(tr("%zu entries of a kind this version does not know were ignored."), d.inspection.warnings.size()).c_str());
    }

    ImGui::Spacing();
    ui::sectionHeader(tr("What will happen"));
    for (const pack::Finding& finding : plan.findings) {
        ImVec4 color = severityColor(finding.severity);
        if (finding.severity == pack::Severity::Consent && finding.accepted) {
            color = kGood;
        }
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(color, "%s", findingText(finding).c_str());
        ImGui::PopTextWrapPos();
    }
    if (!plan.enoughSpace) {
        ImGui::TextColored(kBad, "%s", tr("There is not enough free space for this package"));
    }
    if (plan.findings.empty() && plan.enoughSpace) {
        ImGui::TextColored(kGood, "%s", tr("Nothing stands in the way."));
    }

    if (ImGui::BeginTable("##packFiles", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn(tr("Kind"), ImGuiTableColumnFlags_WidthFixed, 110.0f);
        ImGui::TableSetupColumn(tr("File"), ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn(tr("Result"), ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();
        for (const pack::FileEntry& entry : plan.files) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(kindLabel(entry.item.kind));
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(entry.item.name.c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(stateText(entry, d.conflict).c_str());
        }
        ImGui::EndTable();
    }

    if (plan.count(pack::FileState::Conflict) > 0) {
        ImGui::Spacing();
        ImGui::TextUnformatted(tr("You already have files with the same names but different content:"));
        bool changed = false;
        changed |= ImGui::RadioButton(tr("Do not install until I decide"), &d.conflict, 0);
        changed |= ImGui::RadioButton(tr("Keep my files, install the rest"), &d.conflict, 1);
        changed |= ImGui::RadioButton(tr("Replace my files, mine are saved first"), &d.conflict, 2);
        if (changed) {
            d.replan = true;
        }
    }
    for (const pack::Finding& finding : plan.findings) {
        if (finding.severity != pack::Severity::Consent) {
            continue;
        }
        if (finding.issue == pack::Issue::NativePlugin) {
            if (ImGui::Checkbox(tr("I trust the author and want to install this plugin"), &d.acceptPlugin)) {
                d.replan = true;
            }
        } else if (finding.issue == pack::Issue::Downgrade) {
            if (ImGui::Checkbox(tr("Install the older version anyway"), &d.allowDowngrade)) {
                d.replan = true;
            }
        }
    }

    ImGui::Spacing();
    ImGui::BeginDisabled(busy || !plan.ready());
    if (ImGui::Button(plan.update ? tr("Update package") : tr("Install package"), ImVec2(200.0f, 0.0f))) {
        const fs::path package = pathFromUtf8(d.packagePath);
        const pack::InstallPlan copy = plan;
        const pack::Layout layout = pack::Layout::standard();
        const std::string stamp = localStamp();
        d.installMessage.clear();
        startPackJob(PackJobKind::Install, [package, copy, layout, stamp](PackJobResult& result, const show::ProgressFn& progress) {
            result.installed = pack::applyInstall(package, copy, layout, stamp, progress);
        });
    }
    ImGui::EndDisabled();
    if (d.hasResult) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(kGood, "%s", withCount(tr("Done. %zu files were added or updated."), d.installed.written + d.installed.updated).c_str());
        if (d.installed.removed > 0) {
            ImGui::TextDisabled("%s", withCount(tr("%zu old files of the package were moved to the backup."), d.installed.removed).c_str());
        }
        if (d.installed.keptModified > 0) {
            ImGui::TextDisabled("%s", withCount(tr("%zu old files stay because you changed them."), d.installed.keptModified).c_str());
        }
        if (d.installed.pluginInstalled) {
            ImGui::TextColored(kWarn, "%s", with1(tr("The plugin '%s' is installed but switched off. You can switch it on in the plugin settings if you trust it."), d.installed.pluginId).c_str());
        }
        if (!d.installed.backupFolder.empty()) {
            ImGui::TextDisabled("%s", with1(tr("Replaced files were saved in %s"), pathText(d.installed.backupFolder)).c_str());
        }
        ImGui::PopTextWrapPos();
        if (d.installed.pluginInstalled && ImGui::Button(tr("Open plugin settings"))) {
            openSettings(SettingsPlugins);
        }
    }
}

void App::drawPackInstalled() {
    PackDialog& d = packDialog_;
    const bool busy = d.job.valid();
    if (d.recordsDirty) {
        d.records = pack::readRecords(pack::Layout::standard());
        d.recordsDirty = false;
        d.installedLicenseFor.clear();
        if (!d.selected.empty() && std::none_of(d.records.begin(), d.records.end(), [&](const pack::Record& r) { return r.id == d.selected; })) {
            d.selected.clear();
            d.removalPlanned = false;
        }
    }
    if (d.selected.empty() && !d.records.empty()) {
        d.selected = d.records.front().id;
    }
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("%s", tr("The community packages that are installed. Removing one moves its files into a backup folder. Files you changed yourself stay where they are."));
    ImGui::PopTextWrapPos();
    if (d.records.empty()) {
        ImGui::Spacing();
        ImGui::TextUnformatted(tr("No community package is installed yet. Use the Install tab to add one."));
    }
    const float listHeight = std::min(ImGui::GetTextLineHeightWithSpacing() * 6.0f, ImGui::GetContentRegionAvail().y * 0.4f);
    if (!d.records.empty()) {
        ImGui::BeginChild("##packInstalledList", ImVec2(0.0f, listHeight), ImGuiChildFlags_Borders);
        for (const pack::Record& record : d.records) {
            const std::string label = record.name + "  " + record.version + "##" + record.id;
            if (ImGui::Selectable(label.c_str(), d.selected == record.id) && !busy) {
                d.selected = record.id;
                d.removalPlanned = false;
                d.removeMessage.clear();
            }
        }
        ImGui::EndChild();
    }
    if (!d.removeMessage.empty()) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(d.removalPlanned ? kBad : kGood, "%s", d.removeMessage.c_str());
        ImGui::PopTextWrapPos();
    }
    drawPackProgress();
    const auto selected = std::find_if(d.records.begin(), d.records.end(), [&](const pack::Record& r) { return r.id == d.selected; });
    if (selected == d.records.end()) {
        return;
    }
    const pack::Record& record = *selected;
    ImGui::Spacing();
    ui::sectionHeader(record.name.c_str());
    ImGui::TextDisabled("%s", with1(tr("Id: %s"), record.id).c_str());
    ImGui::TextUnformatted(with1(tr("Version %s"), record.version).c_str());
    if (!record.author.empty()) {
        ImGui::TextUnformatted(with1(tr("By %s"), record.author).c_str());
    }
    ImGui::TextDisabled("%s", with1(tr("Licence: %s"), record.license.empty() ? std::string(tr("not stated")) : record.license).c_str());
    ImGui::TextDisabled("%s", with2(tr("Installed %s with NyrexVJ %s"), record.installedAt, record.appVersion).c_str());
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("%s", with1(tr("Fingerprint of the file (SHA-256): %s"), record.packageSha256).c_str());
    ImGui::PopTextWrapPos();
    if (!record.pluginId.empty()) {
        ImGui::TextColored(kWarn, "%s", with1(tr("Contains the plugin '%s'"), record.pluginId).c_str());
    }
    for (const show::PackDependency& dependency : record.dependencies) {
        ImGui::TextDisabled("%s", (dependency.min.empty() ? with1(tr("Needs the package: %s"), dependency.id) : with2(tr("Needs the package: %s (version %s or newer)"), dependency.id, dependency.min)).c_str());
    }
    if (ImGui::TreeNode(tr("Installed files"))) {
        for (const pack::RecordFile& file : record.files) {
            ImGui::TextUnformatted((std::string(kindLabel(file.kind)) + ": " + file.name).c_str());
        }
        for (const pack::RecordFile& file : record.skipped) {
            ImGui::TextDisabled("%s", with1(tr("Not installed, you kept your own: %s"), file.name).c_str());
        }
        ImGui::TreePop();
    }
    if (d.installedLicenseFor != record.id) {
        d.installedLicenseFor = record.id;
        d.installedLicense.clear();
        std::error_code ec;
        const fs::path license = pack::Layout::standard().licensePath(record.id);
        if (fs::is_regular_file(license, ec) && fs::file_size(license, ec) <= 64 * 1024) {
            std::ifstream in(license, std::ios::binary);
            d.installedLicense.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        }
    }
    if (!d.installedLicense.empty() && ImGui::TreeNode(tr("Licence text"))) {
        ImGui::BeginChild("##packInstalledLicense", ImVec2(0.0f, ImGui::GetTextLineHeight() * 8.0f), ImGuiChildFlags_Borders);
        ImGui::TextUnformatted(d.installedLicense.c_str());
        ImGui::EndChild();
        ImGui::TreePop();
    }

    ImGui::Spacing();
    if (d.removalPlanned && d.removal.record.id == record.id) {
        for (const pack::Finding& finding : d.removal.findings) {
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextColored(severityColor(finding.severity), "%s", findingText(finding).c_str());
            ImGui::PopTextWrapPos();
        }
        ImGui::TextUnformatted(withCount(tr("%zu files will be moved to the backup folder."), d.removal.count(pack::RemoveState::Remove)).c_str());
        ImGui::BeginDisabled(busy || d.removal.blocked());
        if (ImGui::Button(tr("Remove package"), ImVec2(200.0f, 0.0f))) {
            const pack::Layout layout = pack::Layout::standard();
            const pack::RemovePlan plan = d.removal;
            const std::string stamp = localStamp();
            startPackJob(PackJobKind::Remove, [layout, plan, stamp](PackJobResult& result, const show::ProgressFn&) {
                result.removed = pack::applyRemove(layout, plan, stamp);
            });
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button(tr("Keep it"))) {
            d.removalPlanned = false;
        }
    } else {
        ImGui::BeginDisabled(busy);
        if (ImGui::Button(tr("Remove..."), ImVec2(200.0f, 0.0f))) {
            const pack::Layout layout = pack::Layout::standard();
            const std::string id = record.id;
            d.removeMessage.clear();
            startPackJob(PackJobKind::PlanRemove, [layout, id](PackJobResult& result, const show::ProgressFn&) {
                result.removal = pack::planRemove(layout, id);
            });
        }
        ImGui::EndDisabled();
    }
    ImGui::SameLine();
    if (ImGui::Button(tr("Open backup folder"))) {
        const fs::path folder = pack::Layout::standard().backups;
        std::error_code ec;
        fs::create_directories(folder, ec);
        platform::openFolder(pathText(folder));
    }
}

void App::drawPackCreate() {
    PackDialog& d = packDialog_;
    const bool busy = d.job.valid();
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("%s", tr("Packs a folder into a community package you can share. The folder needs a pack.json with the id, name, version and licence, and may have the folders presets, shaders, nodes, masks, venues and plugin, a preview.png and a LICENSE.txt. The manual shows an example."));
    ImGui::PopTextWrapPos();
    ImGui::BeginDisabled(busy);
    ImGui::TextUnformatted(tr("Folder of the package"));
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputText("##packBuildFolder", d.buildFolder, sizeof(d.buildFolder));
    ImGui::TextUnformatted(tr("Where to save the package. The ending .nyxpack is added if it is missing."));
    const char* suggestLabel = tr("Suggest a place");
    const float suggestWidth = ImGui::CalcTextSize(suggestLabel).x + ImGui::GetStyle().FramePadding.x * 2.0f;
    ImGui::SetNextItemWidth(-(suggestWidth + ImGui::GetStyle().ItemSpacing.x));
    ImGui::InputText("##packBuildDestination", d.buildDestination, sizeof(d.buildDestination));
    ImGui::SameLine();
    if (ImGui::Button(suggestLabel)) {
        const fs::path folder = pathFromUtf8(d.buildFolder);
        std::string stem = pathText(folder.filename());
        if (stem.empty()) {
            stem = "package";
        }
        copyText(d.buildDestination, sizeof(d.buildDestination), pathText(pathFromUtf8(userDataDir()) / "packages_out" / pathFromUtf8(stem + ".nyxpack")));
    }
    ImGui::Checkbox(tr("Replace the file if it exists"), &d.buildOverwrite);
    const bool ready = d.buildFolder[0] != '\0' && d.buildDestination[0] != '\0';
    ImGui::BeginDisabled(!ready);
    if (ImGui::Button(tr("Create package"), ImVec2(200.0f, 0.0f))) {
        const fs::path folder = pathFromUtf8(d.buildFolder);
        std::string destinationText = d.buildDestination;
        fs::path destination = pathFromUtf8(destinationText);
        if (destination.extension() != ".nyxpack") {
            destination += ".nyxpack";
        }
        const bool overwrite = d.buildOverwrite;
        const std::string version = baseVersionString(NYREXVJ_VERSION);
        const std::string created = utcStamp();
        d.buildMessage.clear();
        d.buildProblems.clear();
        startPackJob(PackJobKind::Build, [folder, destination, overwrite, version, created](PackJobResult& result, const show::ProgressFn& progress) {
            result.destination = pathText(destination);
            result.built = pack::buildPackFromFolder(folder, destination, overwrite, version, created, progress);
        });
    }
    ImGui::EndDisabled();
    ImGui::EndDisabled();
    drawPackProgress();
    if (!d.buildMessage.empty()) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(d.buildOk ? kGood : kBad, "%s", d.buildMessage.c_str());
        ImGui::PopTextWrapPos();
    }
    for (const std::string& problem : d.buildProblems) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(kWarn, "%s", problem.c_str());
        ImGui::PopTextWrapPos();
    }
}

void App::drawCommunityPackages() {
    servicePackJob();
    PackDialog& d = packDialog_;
    if (!d.open) {
        return;
    }
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ui::setNextWindowSize(ImVec2(std::min(780.0f, viewport->Size.x - 60.0f), std::min(660.0f, viewport->Size.y - 60.0f)), ImGuiCond_Appearing);
    ui::setNextWindowSizeConstraints(ImVec2(540.0f, 380.0f), ImVec2(FLT_MAX, FLT_MAX));
    if (d.focus) {
        ImGui::SetNextWindowFocus();
        d.focus = false;
    }
    const std::string title = std::string(trId("Community packages", "communityPackagesWindow"));
    if (!ui::beginTool(title.c_str(), &d.open, ImGuiWindowFlags_NoDocking)) {
        ImGui::End();
        return;
    }
    if (ImGui::BeginTabBar("##packTabs")) {
        const int wanted = d.requestedTab;
        d.requestedTab = -1;
        if (ImGui::BeginTabItem(tr("Installed"), nullptr, wanted == 0 ? ImGuiTabItemFlags_SetSelected : 0)) {
            drawPackInstalled();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem(tr("Install"), nullptr, wanted == 1 ? ImGuiTabItemFlags_SetSelected : 0)) {
            drawPackInstall();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem(tr("Create"), nullptr, wanted == 2 ? ImGuiTabItemFlags_SetSelected : 0)) {
            drawPackCreate();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::End();
}

}
