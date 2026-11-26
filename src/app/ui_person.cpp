#include "app/app.hpp"

#include "app/ui_common.hpp"
#include "core/person_mask.hpp"
#include "render/person_session.hpp"

#include <imgui.h>

#include <algorithm>
#include <cstdio>
#include <string>

namespace nvj {

namespace {

const ImVec4 kPersonGood(0.45f, 0.85f, 0.45f, 1.0f);
const ImVec4 kPersonWarn(1.0f, 0.65f, 0.2f, 1.0f);

const char* modeLabel(person::Mode mode) {
    switch (mode) {
    case person::Mode::Difference:
        return tr("Background difference (no model)");
    default:
        return tr("Off");
    }
}

const char* tierLabel(person::Tier tier) {
    switch (tier) {
    case person::Tier::Fast:
        return tr("Fast");
    case person::Tier::Fine:
        return tr("Fine");
    default:
        return tr("Balanced");
    }
}

const char* inputLabel(person::Input input) {
    switch (input) {
    case person::Input::Ndi:
        return tr("NDI");
    case person::Input::Stream:
        return tr("Stream");
    default:
        return tr("Camera");
    }
}

std::string sourceLabel(person::Input input, int slot) {
    return std::string(inputLabel(input)) + " " + std::to_string(slot + 1);
}

const char* stateLabel(person::State state) {
    switch (state) {
    case person::State::Learning:
        return tr("Learning the empty scene");
    case person::State::Running:
        return tr("Watching the scene");
    case person::State::SourceLost:
        return tr("No picture from the source");
    case person::State::Failed:
        return tr("Person detection failed");
    default:
        return tr("Waiting for the first picture");
    }
}

std::string megabytesText(unsigned long long bytes) {
    char text[32];
    std::snprintf(text, sizeof(text), "%.2f MB", static_cast<double>(bytes) / (1024.0 * 1024.0));
    return text;
}

bool percentSlider(const char* label, int& value, const char* tooltip) {
    int edit = value;
    ImGui::SetNextItemWidth(ui::px(260.0f));
    const bool changed = ImGui::SliderInt(label, &edit, 0, 100, "%d %%");
    if (changed) {
        value = std::clamp(edit, 0, 100);
    }
    const bool done = ImGui::IsItemDeactivatedAfterEdit();
    ImGui::SetItemTooltip("%s", tooltip);
    return done;
}

}

void App::drawPersonSettings() {
    auto persist = [this]() { saveSettings(settings_); };

    ImGui::SeparatorText(tr("Person detection"));
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("%s", tr("Finds people in front of a camera without any AI model. It learns what the empty scene looks like and reports everything that differs, as a mask and as modulation sources. It is experimental and off by default, while it is off nothing extra runs and nothing is created."));
    ImGui::PopTextWrapPos();

    const person::Mode mode = person::modeFromName(settings_.personMode, person::Mode::Off);
    ImGui::SetNextItemWidth(ui::px(300.0f));
    if (ImGui::BeginCombo(trId("Person detection (experimental)", "personMode"), modeLabel(mode))) {
        for (int i = 0; i < person::kModeCount; ++i) {
            const person::Mode entry = static_cast<person::Mode>(i);
            if (ImGui::Selectable(modeLabel(entry), entry == mode)) {
                settings_.personMode = person::modeName(entry);
                persist();
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SetItemTooltip("%s", tr("Background difference compares every picture with a learned picture of the empty scene. It needs a fixed camera and nobody in the picture while it learns. It does not recognise people, it finds everything that differs from the background. No model is used, nothing is downloaded and everything runs on this computer."));
    if (mode == person::Mode::Off) {
        return;
    }

    person::Input input = person::Input::Camera;
    int slot = 0;
    if (!person::parseInput(settings_.personInput, input, slot)) {
        input = person::Input::Camera;
        slot = 0;
    }
    ImGui::SetNextItemWidth(ui::px(300.0f));
    if (ImGui::BeginCombo(trId("Picture source", "personSource"), sourceLabel(input, slot).c_str())) {
        for (int kind = 0; kind < person::kInputCount; ++kind) {
            for (int s = 0; s < 4; ++s) {
                const person::Input entry = static_cast<person::Input>(kind);
                const bool selected = entry == input && s == slot;
                if (ImGui::Selectable(sourceLabel(entry, s).c_str(), selected)) {
                    settings_.personInput = person::inputText(entry, s);
                    persist();
                }
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SetItemTooltip("%s", tr("The live input that is analysed. Set the camera, NDI source or stream of this slot on the Live page first. The same picture is still shown by the clips as usual."));

    const person::Tier tier = person::tierFromName(settings_.personQuality, person::Tier::Balanced);
    ImGui::SetNextItemWidth(ui::px(300.0f));
    if (ImGui::BeginCombo(trId("Quality", "personQuality"), tierLabel(tier))) {
        for (int i = 0; i < person::kTierCount; ++i) {
            const person::Tier entry = static_cast<person::Tier>(i);
            if (ImGui::Selectable(tierLabel(entry), entry == tier)) {
                settings_.personQuality = person::tierName(entry);
                persist();
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SetItemTooltip("%s", tr("The analysis works on a small copy of the picture on its own thread. Fast uses about 96 pixels width, Balanced about 160 and Fine about 256. If it cannot keep up it lowers the quality, then the rate, and switches off at the end instead of slowing the show down."));

    int rate = settings_.personRate;
    ImGui::SetNextItemWidth(ui::px(300.0f));
    if (ImGui::SliderInt(trId("Analysis rate (per second)", "personRate"), &rate, person::kMinRate, person::kMaxRate)) {
        settings_.personRate = person::clampRate(rate);
    }
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        persist();
    }
    ImGui::SetItemTooltip("%s", tr("How many pictures per second are analysed at most. Between two analyses the last mask and the last values stay as they are. A low rate saves processor time and makes the movement jumpier."));

    if (percentSlider(trId("Sensitivity", "personSensitivity"), settings_.personSensitivity, tr("Higher values find weaker differences, but also pick up more noise and shadows. Lower values need a clear difference from the background."))) {
        persist();
    }
    if (percentSlider(trId("Softness", "personSoftness"), settings_.personSoftness, tr("How soft the edge of the mask is. Higher values blur the edge, lower values keep it sharp and a little more jittery."))) {
        persist();
    }
    if (percentSlider(trId("Stability", "personStability"), settings_.personStability, tr("How strongly the mask is held between two analyses. Higher values flicker less and follow fast movement a little later."))) {
        persist();
    }

    PersonSession* session = engine_.personSession();
    if (ImGui::Button(trId("Learn background now", "personLearn")) && session != nullptr) {
        session->learn();
    }
    ImGui::SetItemTooltip("%s", tr("Forgets the learned background and starts again. Leave the picture and let nobody stand in it for a few seconds. Do this after the camera, the light or the stage has changed."));

    if (session == nullptr) {
        ImGui::TextDisabled("%s", tr("Waiting for the first picture"));
        return;
    }

    const PersonSessionInfo info = session->info();
    const bool good = info.state == person::State::Running;
    const bool plain = info.state == person::State::Off || info.state == person::State::Learning;
    ImGui::TextColored(good ? kPersonGood : (plain ? ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled) : kPersonWarn), "%s: %s", tr("State"), stateLabel(info.state));
    if (!info.note.empty()) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled("%s: %s", tr("Why"), trMessage(info.note).c_str());
        ImGui::PopTextWrapPos();
    }
    if (!engine_.personNote().empty()) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(kPersonWarn, "%s", trMessage(engine_.personNote()).c_str());
        ImGui::PopTextWrapPos();
    }
    if (info.worker.disabled) {
        ImGui::TextColored(kPersonWarn, "%s", tr("Switched off, the analysis was too slow."));
    }
    if (info.state == person::State::Running) {
        ImGui::Text("%s: %d", tr("People found"), info.persons);
    }
    if (info.worker.completed > 0) {
        ImGui::TextDisabled("%s: %.2f ms (%s %.2f ms)", tr("Analysis time"), info.worker.averageMs, tr("budget"), info.worker.budgetMs);
        ImGui::TextDisabled("%s: %d x %d, %s, %d/s", tr("Analysis size"), info.worker.width, info.worker.height, tierLabel(info.tier), info.rate);
    }
    ImGui::TextDisabled("%s: %s", tr("Graphics memory in use for person detection"), megabytesText(info.gpuBytes).c_str());
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("%s", tr("Method: background difference. No model, no download, no network, nothing is sent anywhere."));
    ImGui::PopTextWrapPos();

    if (info.maskWidth > 0 && session->maskReady()) {
        const float width = ui::px(240.0f);
        const float aspect = session->sourceAspect() > 0.1f ? session->sourceAspect() : 1.7778f;
        ImGui::Image(ui::texId(session->texture()), ImVec2(width, width / aspect));
        ImGui::SetItemTooltip("%s", tr("The mask as it is used by the Person Key effect. White is a person, black is the background."));
    }
}

}
