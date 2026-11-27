#include "app/app.hpp"

#include "app/ui_common.hpp"
#include "core/log.hpp"
#include "media/media_image.hpp"
#include "render/asset_import.hpp"
#include "render/clip_library.hpp"
#include "scene3d/scene_pack.hpp"

#include <imgui.h>

#include <algorithm>
#include <cfloat>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <set>
#include <string>
#include <vector>

namespace nvj {

namespace {

const ImVec4 kGood(0.45f, 0.85f, 0.45f, 1.0f);
const ImVec4 kWarn(1.0f, 0.65f, 0.2f, 1.0f);
const ImVec4 kBad(1.0f, 0.4f, 0.35f, 1.0f);

void copyText(char* target, size_t size, const std::string& text) {
    std::snprintf(target, size, "%s", text.c_str());
}

std::string formatted(const char* format, ...) {
    char buffer[640];
    va_list arguments;
    va_start(arguments, format);
    std::vsnprintf(buffer, sizeof(buffer), format, arguments);
    va_end(arguments);
    return buffer;
}

std::string libraryClipName(const std::string& libraryFile) {
    const std::string stem = pathToUtf8(pathFromUtf8(libraryFile).stem());
    return isVideoFileName(libraryFile) ? videoClipName(stem) : imageClipName(stem);
}

std::string joined(const std::vector<std::string>& names) {
    std::string out;
    for (const std::string& name : names) {
        if (!out.empty()) {
            out += ", ";
        }
        out += name;
    }
    return out;
}

std::vector<std::string> folderList(const char* text) {
    std::vector<std::string> out;
    if (text[0] != '\0') {
        out.push_back(text);
    }
    return out;
}

}

void App::openResoPort(const std::string& path) {
    ResoPortDialog& d = resoPort_;
    d.open = true;
    d.focus = true;
    if (!path.empty()) {
        copyText(d.file, sizeof(d.file), path);
        startResoPortCheck();
    }
}

void App::startResoPortCheck() {
    ResoPortDialog& d = resoPort_;
    if (d.checkJob.valid() || d.importJob.valid()) {
        return;
    }
    d.checked = false;
    d.message.clear();
    if (d.file[0] == '\0') {
        d.message = tr("Enter the path of a Resolume composition (.avc) first");
        d.messageGood = false;
        return;
    }
    const std::string file = d.file;
    const std::vector<std::string> folders = folderList(d.folder);
    d.checkJob = std::async(std::launch::async, [file, folders]() {
        ResoPortCheck result;
        result.file = file;
        result.ok = resoport::loadDocument(file, result.document, result.error);
        if (result.ok) {
            resoport::MediaFinder finder(file, folders);
            result.resolved = resoport::resolveClips(result.document, finder);
        }
        return result;
    });
}

void App::startResoPortImport() {
    ResoPortDialog& d = resoPort_;
    if (d.checkJob.valid() || d.importJob.valid() || !d.checked || !d.check.ok) {
        return;
    }
    std::set<std::string> files;
    for (const resoport::ResolvedClip& clip : d.check.resolved) {
        if (!clip.file.empty()) {
            files.insert(clip.file);
        }
    }
    d.message.clear();
    d.importJob = std::async(std::launch::async, [files]() {
        ResoPortImport result;
        for (const std::string& file : files) {
            const ImportResult imported = importAsset(file);
            if (imported.kind == ImportKind::Media) {
                result.libraryNames[file] = imported.name;
                if (imported.added) {
                    ++result.copied;
                }
            } else {
                ++result.failed;
            }
        }
        return result;
    });
}

void App::serviceResoPort() {
    ResoPortDialog& d = resoPort_;
    if (d.checkJob.valid() && d.checkJob.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        d.check = d.checkJob.get();
        d.checked = d.check.ok;
        if (d.check.ok) {
            resoport::buildComposition(d.check.document, d.check.resolved, [](const std::string&) { return 0; }, d.preview);
            d.message.clear();
        } else {
            d.message = d.check.error == "The file does not exist" ? std::string(tr("The file does not exist")) : std::string(tr("This file cannot be read as a Resolume composition")) + " (" + d.check.error + ")";
            d.messageGood = false;
        }
    }
    if (d.importJob.valid() && d.importJob.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        const ResoPortImport imported = d.importJob.get();
        rescanLibrary();
        engine_.syncLibrary();
        assetsDirty_ = true;
        convertScanPending_ = true;
        std::unordered_map<std::string, int> indexes;
        for (const auto& item : imported.libraryNames) {
            indexes[item.first] = clipIndexByName(libraryClipName(item.second));
        }
        resoport::Report report;
        const Composition comp = resoport::buildComposition(d.check.document, d.check.resolved, [&indexes](const std::string& file) {
            const auto found = indexes.find(file);
            return found == indexes.end() ? -1 : found->second;
        }, report);
        loadComposition(comp, formatted(tr("Imported Resolume composition: %s"), comp.name.c_str()));
        d.preview = report;
        d.message = formatted(tr("Imported %d clips, %d media files copied into the library"), report.clipsImported, imported.copied);
        d.messageGood = true;
        logLine("ResoPort imported '" + comp.name + "': " + std::to_string(report.clipsImported) + " clips, " + std::to_string(report.clipsMissing) + " missing, " + std::to_string(imported.failed) + " copy failures");
    }
}

void App::drawResoPort() {
    serviceResoPort();
    ResoPortDialog& d = resoPort_;
    if (!d.open) {
        return;
    }
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ui::setNextWindowSize(ImVec2(std::min(700.0f, viewport->Size.x - 60.0f), std::min(620.0f, viewport->Size.y - 60.0f)), ImGuiCond_Appearing);
    ui::setNextWindowSizeConstraints(ImVec2(480.0f, 320.0f), ImVec2(FLT_MAX, FLT_MAX));
    if (d.focus) {
        ImGui::SetNextWindowFocus();
        d.focus = false;
    }
    if (!ui::beginTool(trId("Resolume import (ResoPort)", "resoPortWindow"), &d.open, ImGuiWindowFlags_NoDocking)) {
        ImGui::End();
        return;
    }
    const bool busy = d.checkJob.valid() || d.importJob.valid();
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("%s", tr("Reads a Resolume composition (.avc) and builds a NyrexVJ composition from it: media clips, layer blend modes, layer opacity and deck names. Effects, generators and other Resolume settings are not imported. You first see what would happen, nothing is changed until you press Import. You can also drop an .avc file onto the window."));
    ImGui::PopTextWrapPos();
    ImGui::Spacing();

    ImGui::BeginDisabled(busy);
    const char* checkLabel = tr("Check file");
    const float checkWidth = ImGui::CalcTextSize(checkLabel).x + ImGui::GetStyle().FramePadding.x * 2.0f;
    ImGui::SetNextItemWidth(-(checkWidth + ImGui::GetStyle().ItemSpacing.x));
    ImGui::InputTextWithHint("##resoPortFile", tr("Path of the .avc file"), d.file, sizeof(d.file));
    ImGui::SameLine();
    if (ImGui::Button(checkLabel)) {
        startResoPortCheck();
    }
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputTextWithHint("##resoPortFolder", tr("Also look for media in this folder (optional)"), d.folder, sizeof(d.folder));
    ImGui::EndDisabled();

    if (busy) {
        ImGui::TextDisabled("%s", d.importJob.valid() ? tr("Importing media...") : tr("Reading the composition..."));
    }
    if (!d.message.empty()) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(d.messageGood ? kGood : kBad, "%s", d.message.c_str());
        ImGui::PopTextWrapPos();
    }

    if (d.checked && d.check.ok) {
        const resoport::Document& doc = d.check.document;
        const resoport::Report& r = d.preview;
        ImGui::Spacing();
        ui::sectionHeader(doc.name.empty() ? tr("Resolume composition") : doc.name.c_str());
        if (!doc.application.empty()) {
            ImGui::TextDisabled("%s", formatted(tr("Made with %s %d.%d"), doc.application.c_str(), doc.versionMajor, doc.versionMinor).c_str());
        }
        ImGui::BeginChild("##resoPortReport", ImVec2(0.0f, -ImGui::GetFrameHeightWithSpacing() * 2.2f), ImGuiChildFlags_None);
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(r.clipsImported > 0 ? kGood : kWarn, "%s", formatted(tr("%d clips will be imported"), r.clipsImported).c_str());
        if (r.clipsMissing > 0) {
            ImGui::TextColored(kWarn, "%s", formatted(tr("%d media files were not found, those cells stay empty"), r.clipsMissing).c_str());
            ImGui::TextDisabled("%s", joined(r.missing).c_str());
            ImGui::TextDisabled("%s", tr("Put the media next to the .avc file or enter its folder above, then check again."));
        }
        if (r.clipsUnsupported > 0) {
            ImGui::TextColored(kWarn, "%s", formatted(tr("%d clips use a file type NyrexVJ cannot play"), r.clipsUnsupported).c_str());
            ImGui::TextDisabled("%s", joined(r.unsupported).c_str());
        }
        if (r.clipsGenerator > 0) {
            ImGui::TextColored(kWarn, "%s", formatted(tr("%d generator clips are not imported"), r.clipsGenerator).c_str());
        }
        if (r.clipsRouter + r.clipsOther > 0) {
            ImGui::TextColored(kWarn, "%s", formatted(tr("%d clips from other sources are not imported"), r.clipsRouter + r.clipsOther).c_str());
        }
        if (r.clipsSkippedPosition > 0) {
            ImGui::TextColored(kWarn, "%s", formatted(tr("%d clips lie outside the 4 layers and 8 columns"), r.clipsSkippedPosition).c_str());
        }
        if (r.layersSkipped > 0) {
            ImGui::TextColored(kWarn, "%s", formatted(tr("%d layers beyond the fourth are ignored"), r.layersSkipped).c_str());
        }
        if (r.decksSkipped > 0) {
            ImGui::TextColored(kWarn, "%s", formatted(tr("%d decks beyond the sixteenth are ignored"), r.decksSkipped).c_str());
        }
        if (r.effectPasses > 0) {
            ImGui::TextColored(kWarn, "%s", formatted(tr("%d effects are not imported"), r.effectPasses).c_str());
        }
        for (const std::string& name : r.unknownBlends) {
            ImGui::TextColored(kWarn, "%s", formatted(tr("Blend mode %s is unknown, Normal is used"), name.c_str()).c_str());
        }
        if (doc.width > 0 && doc.height > 0 && !r.resolutionMatched) {
            const Resolution& chosen = kResolutions[static_cast<size_t>(resolutionIndexFor(doc.width, doc.height))];
            ImGui::TextColored(kWarn, "%s", formatted(tr("Resolution %d x %d is not available, %s is used"), doc.width, doc.height, chosen.label).c_str());
        }
        if (r.tempoTaken) {
            ImGui::TextDisabled("%s", formatted(tr("Tempo %.1f BPM is taken over"), doc.bpm).c_str());
        }
        ImGui::PopTextWrapPos();
        ImGui::EndChild();
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled("%s", tr("Replaces the current composition. The media files are copied into the library."));
        ImGui::PopTextWrapPos();
        ImGui::BeginDisabled(busy);
        if (ImGui::Button(tr("Import"))) {
            startResoPortImport();
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button(tr("Close"))) {
            d.open = false;
        }
    }
    ImGui::End();
}

}
