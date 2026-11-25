#include "app/app.hpp"

#include "app/ui_common.hpp"
#include "core/log.hpp"
#include "core/preset.hpp"
#include "core/venue_setup.hpp"
#include "platform/platform.hpp"
#include "render/clip_library.hpp"
#include "scene3d/scene_pack.hpp"

#include <imgui.h>

#include <algorithm>
#include <cfloat>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
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

std::string withText(const char* format, const std::string& text) {
    char buffer[640];
    std::snprintf(buffer, sizeof(buffer), format, text.c_str());
    return buffer;
}

std::string withCount(const char* format, size_t count) {
    char buffer[320];
    std::snprintf(buffer, sizeof(buffer), format, count);
    return buffer;
}

std::string withCountAndSize(const char* format, size_t count, unsigned long long bytes) {
    char buffer[320];
    std::snprintf(buffer, sizeof(buffer), format, count, sizeText(bytes).c_str());
    return buffer;
}

std::string withTwoTexts(const char* format, const std::string& first, const std::string& second) {
    char buffer[640];
    std::snprintf(buffer, sizeof(buffer), format, first.c_str(), second.c_str());
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

const char* kindLabel(show::Kind kind) {
    switch (kind) {
        case show::Kind::Composition: return tr("The show");
        case show::Kind::Preset: return tr("Preset");
        case show::Kind::Media: return tr("Media");
        case show::Kind::Model: return tr("3D model");
        case show::Kind::ScenePack: return tr("Scene pack");
        case show::Kind::Shader: return tr("Shader");
        case show::Kind::Node: return tr("Node effect");
        case show::Kind::Venue: return tr("Venue setup");
        case show::Kind::Mask: return tr("Mask picture");
        case show::Kind::Cues: return tr("Cue list");
        case show::Kind::Timeline: return tr("Timeline");
        case show::Kind::Plugin:
        case show::Kind::Preview:
        case show::Kind::License: break;
    }
    return "";
}

bool canLeaveOut(show::Kind kind) {
    return kind == show::Kind::Media || kind == show::Kind::Model || kind == show::Kind::ScenePack;
}

std::string warningText(const show::Warning& warning) {
    switch (warning.code) {
        case show::Warn::UnknownClip:
            return withText(tr("The clip '%s' is not in your library, so it cannot be packed"), warning.subject);
        case show::Warn::UnknownEffect:
            return withText(tr("The effect '%s' is not in your library, so it cannot be packed"), warning.subject);
        case show::Warn::UnsafeExternalFile:
            return withText(tr("The shader '%s' points to a clip that was renamed. Its text was not changed, so check it"), warning.subject);
        case show::Warn::UnreadableFile:
            return withText(tr("'%s' could not be read to look for the files it needs"), warning.subject);
        case show::Warn::NewerApp:
            return withText(tr("This package was made with a newer NyrexVJ (%s). Some things may not work"), warning.subject);
        case show::Warn::PluginNeeded:
            return withText(tr("This show needs the plugin '%s', which is not available here"), warning.subject);
    }
    return std::string();
}

std::string problemText(show::Problem problem) {
    switch (problem) {
        case show::Problem::Missing: return tr("Not found, listed as missing");
        case show::Problem::TooLarge: return tr("Too large for a package");
        case show::Problem::UnsafeName: return tr("The name cannot be used");
        case show::Problem::None: break;
    }
    return std::string();
}

std::string actionText(const show::ImportEntry& entry) {
    using show::Action;
    using show::State;
    if (entry.item.kind == show::Kind::Composition) {
        return entry.action == Action::Apply ? tr("Opens as the current show") : tr("Not in the package");
    }
    switch (entry.state) {
        case State::New:
            return tr("Will be added");
        case State::Identical:
            return tr("Already here, same file");
        case State::Different:
            if (entry.action == Action::CopyRenamed) {
                return withText(tr("Added as %s"), entry.finalName);
            }
            if (entry.action == Action::ReplaceFile) {
                return tr("Replaces yours, yours is saved first");
            }
            return tr("You have a different file, yours is kept");
        case State::NotIncluded:
            return tr("Not in the package");
        case State::Blocked:
            return tr("A folder has this name, skipped");
        case State::NotApplied:
            return tr("Not applied, tick the box above to use it");
    }
    return std::string();
}

}

void App::openShowPackages(int tab) {
    showDialog_.open = true;
    showDialog_.focus = true;
    showDialog_.requestedTab = tab;
    if (!showDialog_.listsLoaded) {
        showDialog_.presets = listUserPresets();
        showDialog_.presetChosen.assign(showDialog_.presets.size(), 0);
        showDialog_.venues = listVenues(venuesDirectory());
        showDialog_.venueChosen.assign(showDialog_.venues.size(), 0);
        showDialog_.listsLoaded = true;
    }
    if (showDialog_.name[0] == '\0') {
        copyText(showDialog_.name, sizeof(showDialog_.name), engine_.comp.name);
    }
}

void App::importShowPackage(const std::string& path) {
    openShowPackages(1);
    copyText(showDialog_.packagePath, sizeof(showDialog_.packagePath), path);
    showDialog_.hasResult = false;
    showDialog_.importMessage.clear();
    startShowCheck();
}

void App::startShowJob(ShowJobKind kind, const std::function<void(ShowJobResult&, const show::ProgressFn&)>& body) {
    if (showDialog_.job.valid()) {
        return;
    }
    auto shared = std::make_shared<ShowJobShared>();
    showDialog_.shared = shared;
    showDialog_.running = kind;
    showDialog_.job = std::async(std::launch::async, [kind, body, shared]() {
        ShowJobResult result;
        result.kind = kind;
        body(result, shared->callback());
        return result;
    });
}

void App::startShowCheck() {
    ShowDialog& d = showDialog_;
    if (d.job.valid()) {
        return;
    }
    const fs::path package = pathFromUtf8(d.packagePath);
    show::ImportOptions options;
    options.policy = d.policy == 1 ? show::Policy::KeepBoth : d.policy == 2 ? show::Policy::Replace : show::Policy::KeepMine;
    options.applyCues = d.applyCues;
    options.applyTimeline = d.applyTimeline;
    options.appVersion = NYREXVJ_VERSION;
    const show::Layout layout = show::Layout::standard();
    const show::Catalogue catalogue = show::Catalogue::fromLibraries();
    const bool reuse = d.checked && d.inspection.ok;
    const show::Inspection known = d.inspection;
    d.replan = false;
    d.scanned = false;
    d.scan = show::RepairScan();
    startShowJob(ShowJobKind::Plan, [package, options, layout, catalogue, reuse, known](ShowJobResult& result, const show::ProgressFn& progress) {
        result.inspection = reuse ? known : show::inspectPackage(package);
        result.plan = show::planImport(package, result.inspection, layout, catalogue, options, progress);
    });
}

void App::serviceShowJob() {
    ShowDialog& d = showDialog_;
    if (!d.job.valid() || d.job.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
        return;
    }
    ShowJobResult result = d.job.get();
    d.running = ShowJobKind::None;
    switch (result.kind) {
        case ShowJobKind::Write:
            if (result.written.ok) {
                d.exportMessage = std::string(tr("Show package saved: ")) + result.destination + " (" + sizeText(result.written.bytes) + ")";
                toast(d.exportMessage);
            } else if (result.written.cancelled) {
                d.exportMessage = tr("Cancelled, nothing was written");
            } else {
                d.exportMessage = trMessage(result.written.error);
            }
            break;
        case ShowJobKind::Plan:
            d.inspection = result.inspection;
            d.importPlan = result.plan;
            d.checked = result.plan.ok;
            if (!result.plan.ok) {
                d.importMessage = result.plan.cancelled ? std::string(tr("Cancelled")) : trMessage(result.plan.error);
            } else {
                d.importMessage.clear();
            }
            break;
        case ShowJobKind::Apply: {
            d.imported = result.imported;
            d.hasResult = result.imported.ok;
            if (!result.imported.ok) {
                d.importMessage = result.imported.cancelled ? std::string(tr("Cancelled, nothing was changed")) : trMessage(result.imported.error);
                if (result.imported.rolledBack) {
                    d.importMessage += std::string(" ") + tr("Everything was put back as it was.");
                }
            } else {
                d.importMessage.clear();
                rescanLibrary();
                engine_.syncLibrary();
                syncNodeEffects();
                assetsDirty_ = true;
                convertScanPending_ = true;
                refreshUserPresets();
                showDialog_.presets = listUserPresets();
                showDialog_.presetChosen.assign(showDialog_.presets.size(), 0);
                showDialog_.venues = listVenues(venuesDirectory());
                showDialog_.venueChosen.assign(showDialog_.venues.size(), 0);
                venueListDirty_ = true;
                toast(tr("Show package imported"));
                d.replan = true;
            }
            break;
        }
        case ShowJobKind::Scan:
            d.scan = result.scan;
            d.scanned = true;
            d.repairAccept.assign(d.scan.candidates.size(), 0);
            for (size_t i = 0; i < d.scan.candidates.size(); ++i) {
                d.repairAccept[i] = d.scan.candidates[i].verified ? 1 : 0;
            }
            break;
        case ShowJobKind::Repair:
            if (result.repairOk) {
                d.repairMessage = withCount(tr("%zu files copied"), result.repaired.size());
                rescanLibrary();
                engine_.syncLibrary();
                assetsDirty_ = true;
                convertScanPending_ = true;
                d.replan = true;
            } else {
                d.repairMessage = trMessage(result.error);
            }
            break;
        case ShowJobKind::None:
            break;
    }
    d.shared.reset();
}

void App::drawShowProgress() {
    ShowDialog& d = showDialog_;
    if (!d.job.valid() || !d.shared) {
        return;
    }
    const unsigned long long total = d.shared->total.load();
    const unsigned long long done = d.shared->done.load();
    const float fraction = total > 0 ? static_cast<float>(static_cast<double>(done) / static_cast<double>(total)) : 0.0f;
    const std::string current = d.shared->currentText();
    char label[96];
    std::snprintf(label, sizeof(label), "%s / %s", sizeText(done).c_str(), sizeText(total).c_str());
    ImGui::ProgressBar(std::clamp(fraction, 0.0f, 1.0f), ImVec2(-120.0f, 0.0f), total > 0 ? label : "");
    ImGui::SameLine();
    if (ImGui::Button(tr("Cancel"), ImVec2(110.0f, 0.0f))) {
        d.shared->cancel = true;
    }
    if (!current.empty()) {
        ImGui::TextDisabled("%s", current.c_str());
    }
}

void App::buildShowPlan() {
    ShowDialog& d = showDialog_;
    show::ExportRequest request;
    request.name = d.name;
    request.notes = d.notes;
    request.compositionJson = compositionToJson(engine_.comp);
    for (size_t i = 0; i < d.presets.size(); ++i) {
        if (d.presetChosen[i]) {
            request.presets.push_back(d.presets[i]);
        }
    }
    for (size_t i = 0; i < d.venues.size(); ++i) {
        if (d.venueChosen[i]) {
            request.venues.push_back(d.venues[i]);
        }
    }
    request.includeCues = d.includeCues;
    request.includeTimeline = d.includeTimeline;
    d.exportPlan = show::planExport(request, show::Catalogue::fromLibraries(), show::Layout::standard());
    d.licenses.assign(d.exportPlan.items.size(), std::array<char, 200>{});
    d.planValid = true;
    d.exportMessage.clear();
}

void App::drawShowExport() {
    ShowDialog& d = showDialog_;
    const bool busy = d.job.valid();
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("%s", tr("Packs the current show with every file it uses into one file you can copy to another computer. Plugins are never put into a package."));
    ImGui::PopTextWrapPos();
    ImGui::BeginDisabled(busy);
    ImGui::TextUnformatted(tr("Name"));
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputText("##showName", d.name, sizeof(d.name));
    ImGui::TextUnformatted(tr("Notes for whoever opens the package"));
    ImGui::InputTextMultiline("##showNotes", d.notes, sizeof(d.notes), ImVec2(-FLT_MIN, ImGui::GetTextLineHeight() * 3.5f));
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("%s", tr("Where to save the package, for example a USB stick. The ending .nyxshow is added if it is missing."));
    ImGui::PopTextWrapPos();
    const char* suggestLabel = tr("Suggest a place");
    const float suggestWidth = ImGui::CalcTextSize(suggestLabel).x + ImGui::GetStyle().FramePadding.x * 2.0f;
    ImGui::SetNextItemWidth(-(suggestWidth + ImGui::GetStyle().ItemSpacing.x));
    if (ImGui::InputText("##showDestination", d.destination, sizeof(d.destination))) {
        d.exportMessage.clear();
    }
    ImGui::SameLine();
    if (ImGui::Button(suggestLabel)) {
        const std::string clean = sanitizePresetName(d.name);
        const fs::path folder = pathFromUtf8(userDataDir()) / "shows";
        copyText(d.destination, sizeof(d.destination), pathText(folder / pathFromUtf8((clean.empty() ? std::string("show") : clean) + ".nyxshow")));
        d.exportMessage.clear();
    }
    ImGui::Checkbox(tr("Replace the file if it exists"), &d.overwrite);

    if (ImGui::TreeNode(tr("Presets to pack too"))) {
        if (d.presets.empty()) {
            ImGui::TextDisabled("%s", tr("(none saved)"));
        }
        for (size_t i = 0; i < d.presets.size(); ++i) {
            bool chosen = d.presetChosen[i] != 0;
            if (ImGui::Checkbox((d.presets[i] + "##preset" + std::to_string(i)).c_str(), &chosen)) {
                d.presetChosen[i] = chosen ? 1 : 0;
                d.planValid = false;
            }
        }
        ImGui::TreePop();
    }
    if (ImGui::TreeNode(tr("Venue setups to pack too"))) {
        if (d.venues.empty()) {
            ImGui::TextDisabled("%s", tr("(none saved)"));
        }
        for (size_t i = 0; i < d.venues.size(); ++i) {
            bool chosen = d.venueChosen[i] != 0;
            if (ImGui::Checkbox((d.venues[i] + "##venue" + std::to_string(i)).c_str(), &chosen)) {
                d.venueChosen[i] = chosen ? 1 : 0;
                d.planValid = false;
            }
        }
        ImGui::TreePop();
    }
    if (ImGui::Checkbox(tr("Pack the cue list"), &d.includeCues)) {
        d.planValid = false;
    }
    ImGui::SameLine();
    if (ImGui::Checkbox(tr("Pack the timeline"), &d.includeTimeline)) {
        d.planValid = false;
    }
    if (ImGui::Button(tr("Check what goes in"))) {
        buildShowPlan();
    }
    ImGui::EndDisabled();

    if (d.planValid) {
        const show::ExportPlan& plan = d.exportPlan;
        unsigned long long bytes = 0;
        size_t files = 0;
        for (const show::PlanItem& item : plan.items) {
            if (item.problem == show::Problem::None && item.source.include) {
                bytes += item.size;
                ++files;
            }
        }
        ImGui::Spacing();
        ImGui::Text("%s", withCountAndSize(tr("%zu files, %s in the package"), files, bytes).c_str());
        if (plan.missingCount() > 0) {
            ImGui::TextColored(kWarn, "%s", withCount(tr("%zu files were not found. They are listed as missing and can be found again after the import."), plan.missingCount()).c_str());
        }
        if (plan.blockedCount() > 0) {
            ImGui::TextColored(kBad, "%s", withCount(tr("%zu files cannot be packed (too large or an unusable name)."), plan.blockedCount()).c_str());
        }
        for (const show::Warning& warning : plan.warnings) {
            ImGui::TextColored(kWarn, "%s", warningText(warning).c_str());
        }
        if (!plan.plugins.empty()) {
            ImGui::TextColored(kWarn, "%s", tr("The other computer needs these plugins installed itself:"));
            for (const std::string& plugin : plan.plugins) {
                ImGui::BulletText("%s", plugin.c_str());
            }
        }
        if (!plan.live.empty()) {
            ImGui::TextDisabled("%s", tr("Live sources the show uses (they come from that computer, not from the package):"));
            for (const std::string& live : plan.live) {
                ImGui::BulletText("%s", live.c_str());
            }
        }
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled("%s", tr("NyrexVJ cannot know whether you may pass a file on. Write where a file comes from or its licence in the last column. It is stored in the package and shown to whoever imports it."));
        ImGui::PopTextWrapPos();
        d.licenses.resize(d.exportPlan.items.size());
        if (ImGui::BeginTable("##showItems", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV, ImVec2(0.0f, 190.0f))) {
            ImGui::TableSetupColumn(tr("Pack"), ImGuiTableColumnFlags_WidthFixed, 44.0f);
            ImGui::TableSetupColumn(tr("Kind"), ImGuiTableColumnFlags_WidthFixed, 96.0f);
            ImGui::TableSetupColumn(tr("File"), ImGuiTableColumnFlags_WidthStretch, 1.4f);
            ImGui::TableSetupColumn(tr("Size"), ImGuiTableColumnFlags_WidthFixed, 74.0f);
            ImGui::TableSetupColumn(tr("Source or licence"), ImGuiTableColumnFlags_WidthStretch, 1.0f);
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableHeadersRow();
            for (size_t i = 0; i < d.exportPlan.items.size(); ++i) {
                show::PlanItem& item = d.exportPlan.items[i];
                ImGui::PushID(static_cast<int>(i));
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                if (item.problem == show::Problem::None && canLeaveOut(item.source.kind)) {
                    ImGui::Checkbox("##pack", &item.source.include);
                } else if (item.problem == show::Problem::None) {
                    ImGui::TextDisabled("%s", tr("yes"));
                } else {
                    ImGui::TextDisabled("-");
                }
                ImGui::TableSetColumnIndex(1);
                ImGui::TextUnformatted(kindLabel(item.source.kind));
                ImGui::TableSetColumnIndex(2);
                ImGui::TextUnformatted(item.source.name.c_str());
                if (item.problem != show::Problem::None) {
                    ImGui::SameLine();
                    ImGui::TextColored(item.problem == show::Problem::Missing ? kWarn : kBad, "(%s)", problemText(item.problem).c_str());
                } else if (!item.source.include) {
                    ImGui::SameLine();
                    ImGui::TextDisabled("(%s)", tr("only listed with a checksum"));
                }
                ImGui::TableSetColumnIndex(3);
                ImGui::TextUnformatted(sizeText(item.size).c_str());
                ImGui::TableSetColumnIndex(4);
                if (item.problem == show::Problem::None && item.source.kind != show::Kind::Composition && item.source.kind != show::Kind::Cues && item.source.kind != show::Kind::Timeline) {
                    ImGui::SetNextItemWidth(-FLT_MIN);
                    ImGui::InputText("##license", d.licenses[i].data(), d.licenses[i].size());
                }
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        if (plan.withoutLicenseCount() > 0) {
            ImGui::TextColored(kWarn, "%s", withCount(tr("%zu media files have no note about where they come from."), plan.withoutLicenseCount()).c_str());
        }

        ImGui::BeginDisabled(busy || d.destination[0] == '\0' || d.name[0] == '\0');
        if (ImGui::Button(tr("Create the package"))) {
            std::string destination = d.destination;
            if (destination.size() < 8 || destination.compare(destination.size() - 8, 8, ".nyxshow") != 0) {
                destination += ".nyxshow";
            }
            for (size_t i = 0; i < d.exportPlan.items.size() && i < d.licenses.size(); ++i) {
                d.exportPlan.items[i].source.license = d.licenses[i].data();
            }
            const show::WriteRequest request = d.exportPlan.toRequest(d.name, d.notes, NYREXVJ_VERSION, utcStamp());
            const bool overwrite = d.overwrite;
            d.exportMessage.clear();
            startShowJob(ShowJobKind::Write, [request, destination, overwrite](ShowJobResult& result, const show::ProgressFn& progress) {
                result.destination = destination;
                result.written = show::writePackage(request, pathFromUtf8(destination), overwrite, show::Limits{}, progress);
            });
        }
        ImGui::EndDisabled();
    }
    drawShowProgress();
    if (!d.exportMessage.empty()) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextUnformatted(d.exportMessage.c_str());
        ImGui::PopTextWrapPos();
    }
}

void App::drawShowImport() {
    ShowDialog& d = showDialog_;
    const bool busy = d.job.valid();
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("%s", tr("Opens a show package from another computer. You first see what would happen, nothing is changed until you press Import. You can also drop a .nyxshow file onto the window."));
    ImGui::PopTextWrapPos();
    ImGui::BeginDisabled(busy);
    const char* checkLabel = tr("Check package");
    const float checkWidth = ImGui::CalcTextSize(checkLabel).x + ImGui::GetStyle().FramePadding.x * 2.0f;
    ImGui::SetNextItemWidth(-(checkWidth + ImGui::GetStyle().ItemSpacing.x));
    ImGui::InputText("##showPackagePath", d.packagePath, sizeof(d.packagePath));
    ImGui::SameLine();
    if (ImGui::Button(checkLabel)) {
        d.checked = false;
        d.hasResult = false;
        d.importMessage.clear();
        startShowCheck();
    }
    ImGui::EndDisabled();
    if (!d.importMessage.empty()) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(d.hasResult ? kGood : kBad, "%s", d.importMessage.c_str());
        ImGui::PopTextWrapPos();
    }
    if (d.replan && !busy && d.checked) {
        startShowCheck();
    }
    if (d.checked && d.importPlan.ok) {
        const show::Manifest& manifest = d.importPlan.manifest;
        ImGui::Spacing();
        ImGui::SeparatorText(manifest.name.empty() ? tr("Show package") : manifest.name.c_str());
        if (!manifest.created.empty()) {
            ImGui::TextDisabled("%s", (manifest.appVersion.empty() ? withText(tr("Made %s"), manifest.created) : withTwoTexts(tr("Made %s with NyrexVJ %s"), manifest.created, manifest.appVersion)).c_str());
        }
        if (!manifest.notes.empty()) {
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextUnformatted(manifest.notes.c_str());
            ImGui::PopTextWrapPos();
        }
        for (const show::Warning& warning : d.importPlan.warnings) {
            ImGui::TextColored(kWarn, "%s", warningText(warning).c_str());
        }
        if (!d.inspection.warnings.empty()) {
            ImGui::TextColored(kWarn, "%s", withCount(tr("%zu entries of a kind this version does not know were ignored."), d.inspection.warnings.size()).c_str());
        }
        for (const std::string& live : manifest.live) {
            ImGui::TextDisabled("%s", (std::string(tr("Uses a live source: ")) + live).c_str());
        }

        ImGui::BeginDisabled(busy);
        int policy = d.policy;
        ImGui::TextUnformatted(tr("If a file with the same name is already here:"));
        bool changed = false;
        changed |= ImGui::RadioButton(tr("Keep mine"), &policy, 0);
        ImGui::SameLine();
        changed |= ImGui::RadioButton(tr("Keep both"), &policy, 1);
        ImGui::SameLine();
        changed |= ImGui::RadioButton(tr("Replace mine, save it first"), &policy, 2);
        d.policy = policy;
        changed |= ImGui::Checkbox(tr("Also replace my cue list"), &d.applyCues);
        ImGui::SameLine();
        changed |= ImGui::Checkbox(tr("Also replace my timeline"), &d.applyTimeline);
        ImGui::EndDisabled();
        if (changed) {
            d.replan = true;
        }
        if (d.applyCues || d.applyTimeline) {
            ImGui::TextDisabled("%s", tr("A replaced cue list or timeline is used after the next start of NyrexVJ."));
        }

        if (ImGui::BeginTable("##showEntries", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV, ImVec2(0.0f, 190.0f))) {
            ImGui::TableSetupColumn(tr("Kind"), ImGuiTableColumnFlags_WidthFixed, 96.0f);
            ImGui::TableSetupColumn(tr("File"), ImGuiTableColumnFlags_WidthStretch, 1.2f);
            ImGui::TableSetupColumn(tr("Size"), ImGuiTableColumnFlags_WidthFixed, 74.0f);
            ImGui::TableSetupColumn(tr("What happens"), ImGuiTableColumnFlags_WidthStretch, 1.4f);
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableHeadersRow();
            for (const show::ImportEntry& entry : d.importPlan.entries) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted(kindLabel(entry.item.kind));
                ImGui::TableSetColumnIndex(1);
                ImGui::TextUnformatted(entry.item.name.c_str());
                if (!entry.item.license.empty() && ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("%s", (std::string(tr("Source or licence: ")) + entry.item.license).c_str());
                }
                if (!entry.item.license.empty()) {
                    ImGui::SameLine();
                    ImGui::TextDisabled("(%s)", entry.item.license.c_str());
                }
                ImGui::TableSetColumnIndex(2);
                ImGui::TextUnformatted(sizeText(entry.item.size).c_str());
                ImGui::TableSetColumnIndex(3);
                const bool warn = entry.state == show::State::NotIncluded || entry.state == show::State::Blocked || (entry.state == show::State::Different && entry.action == show::Action::Skip);
                ImGui::TextColored(warn ? kWarn : ImGui::GetStyleColorVec4(ImGuiCol_Text), "%s", actionText(entry).c_str());
            }
            ImGui::EndTable();
        }
        {
            const size_t copies = d.importPlan.count(show::Action::Copy) + d.importPlan.count(show::Action::CopyRenamed) + d.importPlan.count(show::Action::ReplaceFile);
            char line[400];
            std::snprintf(line, sizeof(line), tr("%zu files will be copied, %s in total, %s free"), copies, sizeText(d.importPlan.copyBytes).c_str(), sizeText(d.importPlan.freeBytes).c_str());
            ImGui::TextUnformatted(line);
        }
        if (!d.importPlan.enoughSpace) {
            ImGui::TextColored(kBad, "%s", tr("There is not enough free space for this show."));
        }
        if (!d.importPlan.unresolved.empty()) {
            ImGui::TextColored(kWarn, "%s", withCount(tr("%zu clips or effects of the show will be missing after the import. They stay empty, nothing is replaced behind your back:"), d.importPlan.unresolved.size()).c_str());
            size_t shown = 0;
            for (const std::string& name : d.importPlan.unresolved) {
                if (++shown > 12) {
                    ImGui::TextDisabled("...");
                    break;
                }
                ImGui::BulletText("%s", name.c_str());
            }
        }

        ImGui::BeginDisabled(busy || !d.importPlan.enoughSpace);
        if (ImGui::Button(tr("Import"))) {
            const fs::path package = pathFromUtf8(d.packagePath);
            const show::ImportPlan plan = d.importPlan;
            const show::Layout layout = show::Layout::standard();
            const std::string stamp = localStamp();
            d.importMessage.clear();
            d.hasResult = false;
            startShowJob(ShowJobKind::Apply, [package, plan, layout, stamp](ShowJobResult& result, const show::ProgressFn& progress) {
                result.imported = show::applyImport(package, plan, layout, stamp, progress);
            });
        }
        ImGui::EndDisabled();
        drawShowProgress();

        if (d.hasResult) {
            ImGui::Spacing();
            ImGui::SeparatorText(tr("Imported"));
            ImGui::TextUnformatted(withCount(tr("%zu files added"), d.imported.copied).c_str());
            if (d.imported.renamed > 0) {
                ImGui::TextUnformatted(withCount(tr("%zu added under a new name"), d.imported.renamed).c_str());
            }
            if (d.imported.replaced > 0) {
                ImGui::TextUnformatted(withCount(tr("%zu replaced, your old files are saved"), d.imported.replaced).c_str());
            }
            if (d.imported.kept > 0) {
                ImGui::TextUnformatted(withCount(tr("%zu of your files kept as they were"), d.imported.kept).c_str());
            }
            if (!d.imported.backupFolder.empty()) {
                if (ImGui::Button(tr("Open the folder with the saved files"))) {
                    platform::openFolder(pathText(d.imported.backupFolder));
                }
            }
            if (!d.imported.compositionJson.empty()) {
                if (ImGui::Button(tr("Open the show"))) {
                    Composition comp;
                    std::string error;
                    std::vector<std::string> warnings;
                    if (compositionFromJson(d.imported.compositionJson, comp, error, &warnings)) {
                        loadComposition(comp, warnings.empty() ? std::string(tr("The show was opened")) : withCount(tr("The show was opened, %zu things in it are missing"), warnings.size()));
                    } else {
                        toast(trMessage(error));
                    }
                }
            }
        }

        const bool anyMissing = d.importPlan.count(show::State::NotIncluded) > 0;
        if (anyMissing) {
            ImGui::Spacing();
            ImGui::SeparatorText(tr("Find missing files"));
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextDisabled("%s", tr("Some files are not in the package. Point to a folder where they might be, for example the original media drive. Files are only taken when they are the same as the ones the package knew."));
            ImGui::PopTextWrapPos();
            ImGui::BeginDisabled(busy);
            const char* searchLabel = tr("Search the folder");
            const float searchWidth = ImGui::CalcTextSize(searchLabel).x + ImGui::GetStyle().FramePadding.x * 2.0f;
            ImGui::SetNextItemWidth(-(searchWidth + ImGui::GetStyle().ItemSpacing.x));
            ImGui::InputText("##showRepairFolder", d.repairFolder, sizeof(d.repairFolder));
            ImGui::SameLine();
            if (ImGui::Button(searchLabel) && d.repairFolder[0] != '\0') {
                const show::ImportPlan plan = d.importPlan;
                const fs::path folder = pathFromUtf8(d.repairFolder);
                d.repairMessage.clear();
                startShowJob(ShowJobKind::Scan, [plan, folder](ShowJobResult& result, const show::ProgressFn& progress) {
                    result.scan = show::findMissingFiles(plan, folder, progress);
                });
            }
            ImGui::EndDisabled();
            if (d.scanned) {
                if (d.scan.candidates.empty()) {
                    ImGui::TextDisabled("%s", tr("Nothing matching was found."));
                }
                if (d.scan.truncated) {
                    ImGui::TextColored(kWarn, "%s", tr("The folder has very many files, only the first ones were searched."));
                }
                for (size_t i = 0; i < d.scan.candidates.size(); ++i) {
                    const show::RepairCandidate& candidate = d.scan.candidates[i];
                    bool accept = d.repairAccept[i] != 0;
                    const std::string label = d.importPlan.entries[candidate.entry].item.name + "##repair" + std::to_string(i);
                    if (ImGui::Checkbox(label.c_str(), &accept)) {
                        d.repairAccept[i] = accept ? 1 : 0;
                    }
                    ImGui::SameLine();
                    ImGui::TextColored(candidate.verified ? kGood : kWarn, "%s", candidate.verified ? tr("same file, checked") : tr("same name, content cannot be checked"));
                    ImGui::TextDisabled("    %s", pathText(candidate.found).c_str());
                }
                if (!d.scan.candidates.empty()) {
                    ImGui::BeginDisabled(busy);
                    if (ImGui::Button(tr("Copy the ticked files"))) {
                        std::vector<show::RepairCandidate> accepted;
                        for (size_t i = 0; i < d.scan.candidates.size(); ++i) {
                            if (d.repairAccept[i]) {
                                accepted.push_back(d.scan.candidates[i]);
                            }
                        }
                        const show::ImportPlan plan = d.importPlan;
                        d.repairMessage.clear();
                        startShowJob(ShowJobKind::Repair, [plan, accepted](ShowJobResult& result, const show::ProgressFn& progress) {
                            result.repairOk = show::applyRepair(plan, accepted, result.repaired, result.error, progress);
                        });
                    }
                    ImGui::EndDisabled();
                }
            }
            if (!d.repairMessage.empty()) {
                ImGui::TextUnformatted(d.repairMessage.c_str());
            }
        }
    }
}

void App::drawShowPackages() {
    serviceShowJob();
    ShowDialog& d = showDialog_;
    if (!d.open) {
        return;
    }
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(std::min(760.0f, viewport->Size.x - 60.0f), std::min(640.0f, viewport->Size.y - 60.0f)), ImGuiCond_Appearing);
    ImGui::SetNextWindowSizeConstraints(ImVec2(520.0f, 360.0f), ImVec2(FLT_MAX, FLT_MAX));
    if (d.focus) {
        ImGui::SetNextWindowFocus();
        d.focus = false;
    }
    const std::string title = std::string(trId("Show package", "showPackageWindow"));
    if (!ImGui::Begin(title.c_str(), &d.open, ImGuiWindowFlags_NoDocking)) {
        ImGui::End();
        return;
    }
    if (ImGui::BeginTabBar("##showTabs")) {
        ImGuiTabItemFlags exportFlags = d.requestedTab == 0 ? ImGuiTabItemFlags_SetSelected : 0;
        ImGuiTabItemFlags importFlags = d.requestedTab == 1 ? ImGuiTabItemFlags_SetSelected : 0;
        d.requestedTab = -1;
        if (ImGui::BeginTabItem(tr("Export"), nullptr, exportFlags)) {
            drawShowExport();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem(tr("Import"), nullptr, importFlags)) {
            drawShowImport();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::End();
}

}
