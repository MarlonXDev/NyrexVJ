#include "app/app.hpp"

#include "app/ui_common.hpp"
#include "core/color_lut.hpp"
#include "core/color_model.hpp"
#include "platform/platform.hpp"

#include <imgui.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <string>
#include <system_error>
#include <utility>

namespace nvj {

namespace {

const ImVec4 kColorWarn(1.0f, 0.65f, 0.2f, 1.0f);

}

void App::applyColorSettings() {
    std::optional<HdrToSdr> hdr;
    if (settings_.hdrToSdr) {
        HdrToSdr params;
        params.referenceWhite = settings_.hdrWhiteNits;
        params.peak = settings_.hdrPeakNits;
        params.mapper = toneMapperFromName(settings_.hdrTone, ToneMapper::Shoulder);
        hdr = sanitized(params);
    }
    engine_.setHdrToSdr(hdr);

    const std::string key = std::string(settings_.colorStage ? "1" : "0") + "|" + settings_.colorLut;
    if (key != colorApplied_) {
        colorApplied_ = key;
        colorLut_ = CubeLut{};
        colorLutError_.clear();
        const CubeLut* lut = nullptr;
        if (settings_.colorStage && !settings_.colorLut.empty()) {
            if (!safeCubeName(settings_.colorLut)) {
                colorLutError_ = "the file name is not allowed";
            } else {
                CubeParse parsed = loadCubeFile(lutDirectory() / settings_.colorLut);
                if (parsed.ok) {
                    colorLut_ = std::move(parsed.lut);
                    lut = &colorLut_;
                } else {
                    colorLutError_ = parsed.error;
                    if (parsed.line > 0) {
                        colorLutError_ += " (line " + std::to_string(parsed.line) + ")";
                    }
                }
            }
        }
        engine_.setColorStage(settings_.colorStage, lut, settings_.colorLutStrength);
        return;
    }
    engine_.setColorStrength(settings_.colorLutStrength);
}

void App::drawColorSettings() {
    auto persist = [this]() { saveSettings(settings_); };

    ui::sectionHeader(tr("Colour"));
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("%s", tr("Layers, mixing and the main picture are 8 bit and use the sRGB look. The tools below change the picture only when you switch them on, the default show looks exactly as before."));
    ImGui::PopTextWrapPos();

    if (ImGui::Checkbox(trId("Colour stage with LUT (experimental)", "optColorStage"), &settings_.colorStage)) {
        persist();
    }
    ImGui::SetItemTooltip("%s", tr("Runs one extra pass over the finished picture. It costs one more picture of graphics memory and a little graphics time. Turn it off and the picture is untouched."));

    if (settings_.colorStage) {
        const std::filesystem::path directory = lutDirectory();
        if (colorLutFiles_.empty() || ImGui::IsWindowAppearing()) {
            colorLutFiles_ = listCubeFiles(directory);
        }
        const char* preview = settings_.colorLut.empty() ? tr("No LUT") : settings_.colorLut.c_str();
        ImGui::SetNextItemWidth(ui::px(260.0f));
        if (ImGui::BeginCombo(trId("LUT file", "optColorLut"), preview)) {
            if (ImGui::Selectable(tr("No LUT"), settings_.colorLut.empty())) {
                settings_.colorLut.clear();
                persist();
            }
            for (const std::string& name : colorLutFiles_) {
                if (ImGui::Selectable(name.c_str(), name == settings_.colorLut)) {
                    settings_.colorLut = name;
                    persist();
                }
            }
            ImGui::EndCombo();
        }
        ImGui::SetItemTooltip("%s", tr("Only .cube files with a 3D table are read, from 2 to 65 points per side. 1D tables are not supported."));
        if (ImGui::SmallButton(tr("Open LUT folder"))) {
            std::error_code error;
            std::filesystem::create_directories(directory, error);
            platform::openFolder(directory.string());
        }
        ImGui::SameLine();
        if (ImGui::SmallButton(tr("Refresh list"))) {
            colorLutFiles_ = listCubeFiles(directory);
        }
        float strength = settings_.colorLutStrength;
        ImGui::SetNextItemWidth(ui::px(220.0f));
        if (ImGui::SliderFloat(trId("LUT strength", "optColorLutStrength"), &strength, 0.0f, 1.0f, "%.2f")) {
            settings_.colorLutStrength = std::clamp(strength, 0.0f, 1.0f);
        }
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            persist();
        }
        ImGui::SetItemTooltip("%s", tr("0 leaves the picture as it is, 1 applies the LUT completely."));

        const Engine::ColorStageStatus& status = engine_.colorStageStatus();
        if (!colorLutError_.empty()) {
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextColored(kColorWarn, "%s: %s", tr("The LUT could not be loaded"), trMessage(colorLutError_).c_str());
            ImGui::PopTextWrapPos();
        } else if (!status.error.empty()) {
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextColored(kColorWarn, "%s: %s", tr("Colour stage unavailable"), trMessage(status.error).c_str());
            ImGui::PopTextWrapPos();
        } else if (status.lutLoaded) {
            ImGui::TextDisabled("%s: %d x %d x %d, %s", tr("LUT loaded"), status.lutSize, status.lutSize, status.lutSize, status.active ? tr("active") : tr("strength is 0"));
        } else {
            ImGui::TextDisabled("%s", tr("No LUT chosen, the picture is not changed."));
        }
        ImGui::TextDisabled("%s: %.1f MB", tr("Extra graphics memory"), static_cast<double>(engine_.colorStageBytes()) / (1024.0 * 1024.0));
    }

    ImGui::Spacing();
    if (ImGui::Checkbox(trId("Tone map HDR videos to SDR", "optHdrToSdr"), &settings_.hdrToSdr)) {
        persist();
    }
    ImGui::SetItemTooltip("%s", tr("Videos marked as HDR (PQ or HLG) are turned into the normal SDR picture while the graphics card converts their colours. Without it they appear flat and grey. It works only while Convert video colours on the graphics card is on, videos that the processor converts are not changed. HAP videos are not affected."));
    if (settings_.hdrToSdr) {
        int white = settings_.hdrWhiteNits;
        ImGui::SetNextItemWidth(ui::px(220.0f));
        if (ImGui::SliderInt(trId("Reference white (nits)", "optHdrWhite"), &white, 80, 400)) {
            settings_.hdrWhiteNits = white;
        }
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            persist();
        }
        ImGui::SetItemTooltip("%s", tr("How bright normal white is inside the HDR video. 203 is the usual value."));
        int peak = settings_.hdrPeakNits;
        ImGui::SetNextItemWidth(ui::px(220.0f));
        if (ImGui::SliderInt(trId("Video peak (nits)", "optHdrPeak"), &peak, 400, 4000)) {
            settings_.hdrPeakNits = peak;
        }
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            persist();
        }
        ImGui::SetItemTooltip("%s", tr("The brightest value the video was mastered for. This is where the tone mapping reaches white."));
        const ToneMapper current = toneMapperFromName(settings_.hdrTone, ToneMapper::Shoulder);
        ImGui::SetNextItemWidth(ui::px(220.0f));
        if (ImGui::BeginCombo(trId("Tone mapping", "optHdrTone"), tr(toneMapperLabel(current)))) {
            for (int i = 0; i < kToneMapperCount; ++i) {
                const ToneMapper mapper = static_cast<ToneMapper>(i);
                if (ImGui::Selectable(tr(toneMapperLabel(mapper)), mapper == current)) {
                    settings_.hdrTone = toneMapperName(mapper);
                    persist();
                }
            }
            ImGui::EndCombo();
        }
        ImGui::SetItemTooltip("%s", tr("Soft shoulder keeps normal brightness untouched and rolls off only the brightest parts. Filmic gives a stronger contrast."));
    }

    const FramebufferDepth depth = engine_.outputDepth();
    ImGui::TextDisabled("%s: %s", tr("Main picture format"), depth.ok ? describeDepth(depth).c_str() : tr("unknown"));
    ImGui::SetItemTooltip("%s", tr("What the graphics card really reports for the main picture. Real 10 bit output or HDR display are not supported, see docs/COLOR.md."));
}

}
