#include "app/app.hpp"

#include "app/ui_common.hpp"
#include "core/stage.hpp"
#include "render/dmx_strip.hpp"

#include <algorithm>
#include <cfloat>
#include <cstdio>
#include <string>

namespace nvj {

namespace {

const ImVec4 kGood(0.45f, 0.85f, 0.45f, 1.0f);
const ImVec4 kBad(1.0f, 0.4f, 0.35f, 1.0f);
const ImVec4 kWarn(1.0f, 0.65f, 0.2f, 1.0f);

void warnText(const char* text) {
    ImGui::PushStyleColor(ImGuiCol_Text, kWarn);
    ImGui::TextWrapped("%s", text);
    ImGui::PopStyleColor();
}

bool sourceCombo(const char* id, int& source) {
    bool changed = false;
    ImGui::SetNextItemWidth(ui::px(110.0f));
    if (ImGui::BeginCombo(id, source == 0 ? tr("Fixed") : tr(audioBandName(source)))) {
        if (ImGui::Selectable(tr("Fixed"), source == 0)) {
            source = 0;
            changed = true;
        }
        for (int i = 1; i < kStageSourceCount; ++i) {
            if (ImGui::Selectable(tr(audioBandName(i)), source == i)) {
                source = i;
                changed = true;
            }
        }
        ImGui::EndCombo();
    }
    return changed;
}

bool bindingEditor(SourceBinding& binding) {
    bool changed = false;
    changed |= sourceCombo("##source", binding.source);
    ImGui::SameLine();
    if (binding.source == 0) {
        ImGui::SetNextItemWidth(ui::px(150.0f));
        changed |= ui::sliderFloat("##fixed", &binding.fixed, 0.0f, 1.0f, "%.2f");
    } else {
        ImGui::SetNextItemWidth(ui::px(70.0f));
        changed |= ImGui::DragFloat("##low", &binding.low, 0.005f, 0.0f, 1.0f, "%.2f");
        ImGui::SetItemTooltip("%s", tr("Value when the source is at 0"));
        ImGui::SameLine();
        ImGui::SetNextItemWidth(ui::px(70.0f));
        changed |= ImGui::DragFloat("##high", &binding.high, 0.005f, 0.0f, 1.0f, "%.2f");
        ImGui::SetItemTooltip("%s", tr("Value when the source is at 1"));
        ImGui::SameLine();
        changed |= ImGui::Checkbox(tr("Invert"), &binding.invert);
    }
    return changed;
}

bool textField(const char* id, std::string& value, size_t limit, float width) {
    char buffer[80] = {};
    std::snprintf(buffer, sizeof(buffer), "%s", value.c_str());
    ImGui::SetNextItemWidth(width);
    if (ImGui::InputText(id, buffer, std::min(sizeof(buffer), limit + 1))) {
        value = buffer;
        return true;
    }
    return false;
}

}

void App::drawStagePage() {
    auto persist = [this]() { stageDirty_ = true; };
    DmxConfig& dmx = settings_.dmx;
    VrchatConfig& vrchat = settings_.vrchat;

    ImGui::SeparatorText(tr("Art-Net output"));
    ImGui::TextWrapped("%s", tr("Sends the channels below as Art-Net DMX, for example to HNode, QLC+ or a lighting console."));
    if (ImGui::Checkbox(trId("Send Art-Net", "dmxArtnet"), &dmx.artnetEnabled)) {
        persist();
    }
    ImGui::BeginDisabled(!dmx.artnetEnabled);
    if (textField("##artnetHost", dmx.artnetHost, 64, 180.0f)) {
        persist();
    }
    ImGui::SameLine();
    ImGui::TextUnformatted(tr("Address"));
    ImGui::SetNextItemWidth(ui::px(110.0f));
    if (ImGui::InputInt(trId("Port", "dmxPort"), &dmx.artnetPort, 0, 0)) {
        persist();
    }
    ImGui::SetNextItemWidth(ui::px(110.0f));
    if (ImGui::InputInt(trId("Net", "dmxNet"), &dmx.artnetNet, 0, 0)) {
        persist();
    }
    ImGui::SetNextItemWidth(ui::px(110.0f));
    if (ImGui::InputInt(trId("First universe", "dmxFirst"), &dmx.artnetFirstUniverse, 0, 0)) {
        persist();
    }
    ImGui::SetItemTooltip("%s", tr("Universe 1 of this program is sent as this Art-Net universe, the next ones follow."));
    ImGui::SetNextItemWidth(ui::px(160.0f));
    if (ui::sliderInt(trId("Packets per second", "dmxRate"), &dmx.artnetRate, 1, 44)) {
        persist();
    }
    ImGui::EndDisabled();
    if (!dmx.artnetEnabled) {
        ImGui::TextDisabled("%s", tr("Art-Net is off"));
    } else if (stage_.artnetSending()) {
        ImGui::TextColored(kGood, "%s%s:%d", tr("Sending to "), dmx.artnetHost.c_str(), dmx.artnetPort);
        ImGui::SameLine();
        ImGui::TextDisabled("(%llu %s)", stage_.artnetPackets(), tr("packets"));
    } else if (!stage_.artnetError().empty()) {
        ImGui::TextColored(kBad, "%s", trMessage(stage_.artnetError()).c_str());
    }
    if (dmx.artnetEnabled && dmx.artnetHost != "127.0.0.1" && dmx.artnetHost != "localhost") {
        warnText(tr("Data is sent to another address. Only use addresses in a network you trust."));
    }

    ImGui::SeparatorText(tr("DMX strip in the video (VRSL)"));
    ImGui::TextWrapped("%s", tr("Draws the channels as a row of grey blocks into the output video. Spout, NDI and the output windows all carry it, so VRSL in VRChat can read it from the stream."));
    if (ImGui::Checkbox(trId("Draw the strip into the video", "dmxStrip"), &dmx.stripEnabled)) {
        persist();
    }
    ImGui::BeginDisabled(!dmx.stripEnabled);
    ImGui::SetNextItemWidth(ui::px(160.0f));
    if (ImGui::BeginCombo(trId("Position", "dmxStripPos"), dmx.stripAtTop ? tr("Top edge") : tr("Bottom edge"))) {
        if (ImGui::Selectable(tr("Top edge"), dmx.stripAtTop)) {
            dmx.stripAtTop = true;
            persist();
        }
        if (ImGui::Selectable(tr("Bottom edge"), !dmx.stripAtTop)) {
            dmx.stripAtTop = false;
            persist();
        }
        ImGui::EndCombo();
    }
    ImGui::SetNextItemWidth(ui::px(300.0f));
    if (ImGui::BeginCombo(trId("Channel layout", "dmxStripLayout"), dmx.stripPadUniverses ? tr("40 columns per universe (520 cells)") : tr("Universes back to back (512 cells)"))) {
        if (ImGui::Selectable(tr("Universes back to back (512 cells)"), !dmx.stripPadUniverses)) {
            dmx.stripPadUniverses = false;
            persist();
        }
        if (ImGui::Selectable(tr("40 columns per universe (520 cells)"), dmx.stripPadUniverses)) {
            dmx.stripPadUniverses = true;
            persist();
        }
        ImGui::EndCombo();
    }
    ImGui::EndDisabled();
    const int width = engine_.width();
    const int height = engine_.height();
    const int stripHeight = DmxStrip::stripHeight(width, height);
    ImGui::TextDisabled("%s %d x %d px (%d x %d %s)", tr("Strip size:"), width, stripHeight, kVrslColumns, kVrslRows, tr("blocks"));
    if (width % kVrslColumns != 0) {
        warnText(tr("The output width is not a multiple of 120, so the blocks will not be whole pixels. 1920 x 1080 fits exactly."));
    }
    warnText(tr("The channel to block order follows the public VRSL description and is not yet checked against a real VRSL world. Use the test patterns to compare."));

    ImGui::SeparatorText(tr("Test patterns"));
    const StageTest current = stage_.test();
    const struct {
        StageTest test;
        const char* label;
    } tests[] = {{StageTest::Off, "Off"}, {StageTest::Full, "All 255"}, {StageTest::Ramp, "Ramp"}, {StageTest::Chase, "Chase"}};
    for (size_t i = 0; i < 4; ++i) {
        if (i > 0) {
            ImGui::SameLine();
        }
        const bool active = current == tests[i].test;
        if (active) {
            ImGui::PushStyleColor(ImGuiCol_Button, ui::kAccentDim);
        }
        if (ImGui::Button(tr(tests[i].label))) {
            stage_.setTest(tests[i].test);
        }
        if (active) {
            ImGui::PopStyleColor();
        }
    }
    ImGui::SetItemTooltip("%s", tr("Overrides the channels with a test signal on Art-Net and the strip until you pick Off."));
    if (current != StageTest::Off) {
        warnText(tr("A test pattern is running. The channels below are ignored."));
    }

    ImGui::SeparatorText(tr("Channels"));
    ImGui::TextWrapped("%s", tr("Each row drives one DMX channel from a fixed value or a modulation source such as an audio band, an LFO, a macro or the beat."));
    int removeLane = -1;
    if (ImGui::BeginChild("dmxLanes", ImVec2(0.0f, std::min(260.0f, 36.0f + 30.0f * static_cast<float>(dmx.lanes.size()))), ImGuiChildFlags_Borders)) {
        for (size_t i = 0; i < dmx.lanes.size(); ++i) {
            DmxLane& lane = dmx.lanes[i];
            ImGui::PushID(static_cast<int>(i));
            bool changed = false;
            changed |= textField("##name", lane.name, 48, 110.0f);
            ImGui::SetItemTooltip("%s", tr("Name"));
            ImGui::SameLine();
            ImGui::SetNextItemWidth(ui::px(48.0f));
            changed |= ImGui::DragInt("##universe", &lane.universe, 0.1f, 1, kDmxUniverses, "U%d");
            ImGui::SetItemTooltip("%s", tr("Universe"));
            ImGui::SameLine();
            ImGui::SetNextItemWidth(ui::px(58.0f));
            changed |= ImGui::DragInt("##channel", &lane.channel, 0.3f, 1, kDmxChannels, "%d");
            ImGui::SetItemTooltip("%s", tr("Channel"));
            ImGui::SameLine();
            changed |= ImGui::Checkbox("16##fine", &lane.fine);
            ImGui::SetItemTooltip("%s", tr("16 bit: uses this channel and the next one"));
            ImGui::SameLine();
            changed |= bindingEditor(lane.binding);
            ImGui::SameLine();
            const std::string place = vrslPlace(lane.universe, lane.channel, dmx.stripPadUniverses);
            ImGui::TextDisabled("%s", place.empty() ? "-" : place.c_str());
            ImGui::SetItemTooltip("%s", tr("Block in the strip (column / row)"));
            ImGui::SameLine();
            if (ImGui::SmallButton("X")) {
                removeLane = static_cast<int>(i);
            }
            ImGui::SetItemTooltip("%s", tr("Remove"));
            if (changed) {
                persist();
            }
            ImGui::PopID();
        }
    }
    ImGui::EndChild();
    if (removeLane >= 0) {
        dmx.lanes.erase(dmx.lanes.begin() + removeLane);
        persist();
    }
    if (ImGui::Button(trId("Add channel", "dmxAdd")) && dmx.lanes.size() < kMaxDmxLanes) {
        DmxLane lane;
        if (!dmx.lanes.empty()) {
            lane.universe = dmx.lanes.back().universe;
            lane.channel = std::min(dmx.lanes.back().channel + (dmx.lanes.back().fine ? 2 : 1), kDmxChannels);
        }
        lane.name = std::string(tr("Channel")) + " " + std::to_string(dmx.lanes.size() + 1);
        dmx.lanes.push_back(lane);
        persist();
    }
    ImGui::SameLine();
    if (ImGui::Button(trId("Remove all", "dmxClear")) && !dmx.lanes.empty()) {
        dmx.lanes.clear();
        persist();
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%zu / %zu", dmx.lanes.size(), kMaxDmxLanes);

    ImGui::SeparatorText(tr("VRChat avatar parameters"));
    ImGui::TextWrapped("%s", tr("Sends values to VRChat over OSC as /avatar/parameters/<name>, so an avatar or a world prop can follow the music or the beat. VRChat must have OSC enabled."));
    if (ImGui::Checkbox(trId("Send to VRChat", "vrcSend"), &vrchat.enabled)) {
        persist();
    }
    ImGui::BeginDisabled(!vrchat.enabled);
    if (textField("##vrcHost", vrchat.host, 64, 180.0f)) {
        persist();
    }
    ImGui::SameLine();
    ImGui::TextUnformatted(tr("Address"));
    ImGui::SetNextItemWidth(ui::px(110.0f));
    if (ImGui::InputInt(trId("Port", "vrcPort"), &vrchat.port, 0, 0)) {
        persist();
    }
    ImGui::SetNextItemWidth(ui::px(160.0f));
    if (ui::sliderInt(trId("Updates per second", "vrcRate"), &vrchat.rate, 1, 60)) {
        persist();
    }
    ImGui::EndDisabled();
    if (!vrchat.enabled) {
        ImGui::TextDisabled("%s", tr("VRChat output is off"));
    } else if (stage_.vrchatSending()) {
        ImGui::TextColored(kGood, "%s%s:%d", tr("Sending to "), vrchat.host.c_str(), vrchat.port);
        ImGui::SameLine();
        ImGui::TextDisabled("(%llu %s)", stage_.vrchatPackets(), tr("packets"));
    } else if (!stage_.vrchatError().empty()) {
        ImGui::TextColored(kBad, "%s", trMessage(stage_.vrchatError()).c_str());
    }
    if (vrchat.enabled && vrchat.host != "127.0.0.1" && vrchat.host != "localhost") {
        warnText(tr("Data is sent to another address. Only use addresses in a network you trust."));
    }

    int removeParam = -1;
    if (ImGui::BeginChild("vrcParams", ImVec2(0.0f, std::min(220.0f, 36.0f + 30.0f * static_cast<float>(vrchat.params.size()))), ImGuiChildFlags_Borders)) {
        for (size_t i = 0; i < vrchat.params.size(); ++i) {
            VrchatParam& param = vrchat.params[i];
            ImGui::PushID(static_cast<int>(i));
            bool changed = false;
            changed |= textField("##pname", param.name, 64, 150.0f);
            ImGui::SetItemTooltip("%s", tr("Parameter name in the avatar"));
            ImGui::SameLine();
            ImGui::SetNextItemWidth(ui::px(70.0f));
            const char* typeNames[] = {"Float", "Bool", "Int"};
            int type = static_cast<int>(param.type);
            if (ImGui::Combo("##ptype", &type, typeNames, 3)) {
                param.type = static_cast<VrchatType>(type);
                changed = true;
            }
            ImGui::SameLine();
            changed |= bindingEditor(param.binding);
            ImGui::SameLine();
            if (ImGui::SmallButton("X")) {
                removeParam = static_cast<int>(i);
            }
            ImGui::SetItemTooltip("%s", tr("Remove"));
            if (!validVrchatName(param.name)) {
                ImGui::SameLine();
                ImGui::TextColored(kBad, "%s", tr("Invalid name"));
            }
            if (changed) {
                persist();
            }
            ImGui::PopID();
        }
    }
    ImGui::EndChild();
    if (removeParam >= 0) {
        vrchat.params.erase(vrchat.params.begin() + removeParam);
        persist();
    }
    if (ImGui::Button(trId("Add parameter", "vrcAdd")) && vrchat.params.size() < kMaxVrchatParams) {
        VrchatParam param;
        param.name = "Nyrex" + std::to_string(vrchat.params.size() + 1);
        param.binding.source = 4;
        vrchat.params.push_back(param);
        persist();
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%zu / %zu", vrchat.params.size(), kMaxVrchatParams);

    if (stageDirty_ && !ImGui::IsAnyItemActive()) {
        stageDirty_ = false;
        saveSettings(settings_);
    }
}

}
