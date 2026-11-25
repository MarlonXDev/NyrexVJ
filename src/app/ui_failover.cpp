#include "app/app.hpp"

#include "app/ui_common.hpp"
#include "core/failover.hpp"

#include <algorithm>
#include <cstdio>
#include <string>

namespace nvj {

namespace {

const ImVec4 kGood(0.45f, 0.85f, 0.45f, 1.0f);
const ImVec4 kBad(1.0f, 0.4f, 0.35f, 1.0f);
const ImVec4 kWarn(1.0f, 0.65f, 0.2f, 1.0f);

constexpr float kLabelColumn = 190.0f;

std::string fixed(double value, int digits) {
    char buffer[48];
    std::snprintf(buffer, sizeof(buffer), "%.*f", digits, value);
    return buffer;
}

const ImVec4& levelColor(failover::Level level) {
    return level == failover::Level::Ok ? kGood : (level == failover::Level::Warn ? kWarn : kBad);
}

const char* levelLabel(failover::Level level) {
    return level == failover::Level::Ok ? tr("Ready") : (level == failover::Level::Warn ? tr("Check") : tr("Not ready"));
}

const char* stateLabel(failover::State state) {
    switch (state) {
    case failover::State::Connecting:
        return tr("Waiting for the primary");
    case failover::State::Following:
        return tr("Following the primary");
    case failover::State::PrimaryUnhealthy:
        return tr("The primary reports a problem");
    case failover::State::PrimaryLost:
        return tr("The primary is lost");
    case failover::State::PrimaryEnded:
        return tr("The primary was closed on purpose");
    case failover::State::ConfirmPending:
        return tr("Waiting for your confirmation");
    case failover::State::Leader:
        return tr("This PC is the leader");
    default:
        return tr("Idle");
    }
}

const char* checkLabel(const std::string& id) {
    if (id == "link") {
        return tr("Connection to the primary");
    }
    if (id == "version") {
        return tr("Same program version");
    }
    if (id == "clock") {
        return tr("Clock");
    }
    if (id == "state") {
        return tr("Show state is current");
    }
    if (id == "assets") {
        return tr("Clips match the primary");
    }
    if (id == "files") {
        return tr("Clip files on this PC");
    }
    if (id == "show") {
        return tr("Same show as the primary");
    }
    if (id == "outputs") {
        return tr("Outputs");
    }
    if (id == "decoders") {
        return tr("Playing clips work");
    }
    if (id == "primary") {
        return tr("The primary reports no fault");
    }
    if (id == "snapshot") {
        return tr("Session snapshot");
    }
    return "";
}

std::string checkDetail(const failover::Check& check) {
    const std::string& id = check.id;
    if (id == "clock" && check.a > 0.0) {
        return fixed(check.a * 1000.0, 1) + " ms " + tr("round trip") + ", " + fixed(check.b * 1000.0, 1) + " ms " + tr("jitter");
    }
    if (id == "state" && check.a < 1000.0) {
        return fixed(check.a, 2) + " s " + tr("old");
    }
    if (id == "assets" && check.level != failover::Level::Ok) {
        return std::to_string(static_cast<int>(check.a)) + " " + tr("missing") + ", " + std::to_string(static_cast<int>(check.b)) + " " + tr("different");
    }
    if (id == "files" && check.a > 0.0) {
        return std::to_string(static_cast<int>(check.a)) + " " + tr("missing");
    }
    if (id == "outputs") {
        return std::to_string(static_cast<int>(check.a)) + " / " + std::to_string(static_cast<int>(check.b)) + " " + tr("open");
    }
    if (id == "decoders" && check.a > 0.0) {
        return std::to_string(static_cast<int>(check.a)) + " " + tr("failed");
    }
    if (id == "snapshot") {
        return check.a < 0.0 ? std::string(tr("none yet")) : fixed(check.a, 0) + " s " + tr("old");
    }
    return std::string();
}

void rowLabel(const char* label) {
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("%s", label);
    ImGui::SameLine(kLabelColumn);
}

void valueRow(const char* label, const std::string& value, const ImVec4* color = nullptr) {
    rowLabel(label);
    if (color != nullptr) {
        ImGui::PushStyleColor(ImGuiCol_Text, *color);
    }
    ImGui::TextUnformatted(value.c_str());
    if (color != nullptr) {
        ImGui::PopStyleColor();
    }
}

void warnText(const char* text) {
    ImGui::PushStyleColor(ImGuiCol_Text, kWarn);
    ImGui::TextWrapped("%s", text);
    ImGui::PopStyleColor();
}

const char* eventLabel(const std::string& kind) {
    if (kind == "state") {
        return tr("State");
    }
    if (kind == "readiness") {
        return tr("Readiness");
    }
    if (kind == "takeover-automatic") {
        return tr("Automatic takeover");
    }
    if (kind == "takeover-manual") {
        return tr("Takeover by hand");
    }
    if (kind == "takeover-forced") {
        return tr("Takeover while the primary answered");
    }
    if (kind == "serving") {
        return tr("Serving as leader");
    }
    if (kind == "old-primary-joined") {
        return tr("Old primary joined");
    }
    if (kind == "stepped-down") {
        return tr("Stepped down");
    }
    return tr("Event");
}

}

void App::drawFailoverWindow() {
    if (!failoverOpen_) {
        return;
    }
    ui::setNextWindowSize(ImVec2(620.0f, 760.0f), ImGuiCond_FirstUseEver);
    const std::string title = std::string(tr("Hot standby")) + "###failoverWindow";
    if (!ImGui::Begin(title.c_str(), &failoverOpen_)) {
        ImGui::End();
        return;
    }
    auto persist = [this]() { saveSettings(settings_); };
    const double now = steadyNowSeconds();

    ImGui::TextWrapped("%s", tr("Two PCs run the same show. The primary sends its state through the network sync and the standby follows it with its outputs held black. If the primary stops, the standby can take over. NyrexVJ does not switch projectors or network streams itself: a video switcher or router in front of the projectors has to move to the standby."));

    ImGui::SeparatorText(tr("Settings"));
    if (ImGui::Checkbox(trId("Use hot standby", "failoverEnabled"), &settings_.failoverEnabled)) {
        persist();
    }
    ImGui::SetItemTooltip("%s", tr("Turn this on on both PCs. The primary is the master of the network sync, the standby is the follower."));
    const netsync::Role role = netSync_.role();
    if (settings_.failoverEnabled && role == netsync::Role::Off) {
        warnText(tr("The network sync is off. Set one PC to master and the other to follower under Settings, Sync."));
    }
    if (ImGui::Checkbox(trId("Keep the outputs of the standby black", "failoverHold"), &settings_.failoverHoldOutputs)) {
        persist();
    }
    ImGui::SetItemTooltip("%s", tr("While this PC follows, its picture stays black, so nothing is shown twice. This also blacks the output preview of the standby."));
    float lost = static_cast<float>(settings_.failoverLostAfter);
    ImGui::SetNextItemWidth(ui::px(260.0f));
    if (ui::sliderFloat(trId("The primary counts as lost after (seconds)", "failoverLost"), &lost, 3.0f, 60.0f, "%.0f s")) {
        settings_.failoverLostAfter = failover::clampLostAfter(lost);
    }
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        persist();
    }
    ImGui::SetItemTooltip("%s", tr("A short time reacts faster but may take over after a network hiccup."));
    int leaderPort = settings_.failoverLeaderPort;
    ImGui::SetNextItemWidth(ui::px(120.0f));
    ImGui::InputInt(trId("Port as leader (0 = the port of the primary)", "failoverLeaderPort"), &leaderPort, 0, 0);
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        settings_.failoverLeaderPort = leaderPort >= 1024 && leaderPort <= 65535 ? leaderPort : 0;
        persist();
    }
    ImGui::SetItemTooltip("%s", tr("The port this PC opens after it took over. Leave it at 0 when both PCs use the same port, which is the normal case with two PCs. Use another port only for two programs on one PC."));
    if (ImGui::Checkbox(trId("Take over automatically when the primary is lost", "failoverAuto"), &settings_.failoverAutoTakeover)) {
        persist();
    }
    ImGui::SetItemTooltip("%s", tr("Only when this PC was ready for ten seconds before the loss, the primary did not close on purpose and nothing is wrong here. It happens once, then you arm it again."));
    if (settings_.failoverAutoTakeover) {
        warnText(tr("An automatic takeover opens the standby for the network and its outputs without asking. Make sure the projectors are not driven by both PCs at the same time."));
    }

    if (!settings_.failoverEnabled) {
        ImGui::TextDisabled("%s", tr("Off. Nothing is reported and nothing is held back."));
        ImGui::End();
        return;
    }

    ImGui::SeparatorText(tr("Status"));
    const failover::State state = failover_.state();
    const ImVec4* stateColor = state == failover::State::Following || state == failover::State::Leader ? &kGood : (state == failover::State::Connecting || state == failover::State::Idle ? nullptr : &kWarn);
    valueRow(tr("This PC"), failover_.standby() ? tr("Standby (follower)") : (failover_.primary() ? tr("Primary or leader (master)") : tr("No role")));
    valueRow(tr("State"), stateLabel(state), stateColor);
    valueRow(tr("Leader epoch"), std::to_string(netSync_.epoch()));
    if (failover_.decoderFaults() > 0) {
        std::string names;
        for (const std::string& name : failover_.faultNames()) {
            names += (names.empty() ? "" : ", ") + name;
        }
        valueRow(tr("Clips with a fault"), names, &kBad);
    }
    if (failover_.announcing()) {
        warnText((std::string(tr("Telling the old primary to step down: ")) + failover_.announceHost()).c_str());
    }

    if (failover_.standby()) {
        ImGui::SeparatorText(tr("Readiness of this PC"));
        const failover::Report& report = failover_.report();
        valueRow(tr("Overall"), levelLabel(report.overall), &levelColor(report.overall));
        if (ImGui::BeginTable("##failoverChecks", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
            for (const failover::Check& check : report.checks) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(checkLabel(check.id));
                ImGui::TableNextColumn();
                ImGui::TextColored(levelColor(check.level), "%s", levelLabel(check.level));
                ImGui::TableNextColumn();
                ImGui::TextDisabled("%s", checkDetail(check).c_str());
            }
            ImGui::EndTable();
        }
        valueRow(tr("Ready for"), fixed(failover_.machine().readySeconds(now), 0) + " s");

        ImGui::SeparatorText(tr("Takeover"));
        if (settings_.failoverAutoTakeover) {
            if (failover_.machine().autoUsed()) {
                warnText(tr("The automatic takeover was used once. Arm it again to allow another."));
                if (ImGui::Button(tr("Arm again"))) {
                    failover_.rearm();
                }
            } else if (failover_.machine().autoBlockedByEnd()) {
                warnText(tr("The primary was closed on purpose, so there is no automatic takeover. You can still take over by hand."));
            } else {
                ImGui::TextDisabled("%s", tr("Armed: this PC takes over by itself if the primary is lost and this PC is ready."));
            }
        }
        const bool canTake = state != failover::State::Leader && state != failover::State::Idle;
        ImGui::BeginDisabled(!canTake);
        if (ImGui::Button(tr("Take over..."))) {
            if (failover_.requestTakeover(now)) {
                failoverConfirm_ = true;
                ImGui::OpenPopup("##failoverConfirm");
            }
        }
        ImGui::EndDisabled();
        ImGui::SetItemTooltip("%s", tr("Makes this PC the leader. It asks again before it does."));
    } else if (failover_.primary()) {
        ImGui::SeparatorText(tr("Standby PC"));
        const StandbyView view = failover_.standbyView(now);
        if (!view.present) {
            ImGui::TextDisabled("%s", tr("No standby PC is connected."));
        } else {
            valueRow(tr("Name"), view.name);
            if (view.reports) {
                valueRow(tr("Readiness"), levelLabel(view.readiness), &levelColor(view.readiness));
                valueRow(tr("Same show"), view.showKnown ? (view.showMatches ? tr("Yes") : tr("No")) : tr("Unknown"), view.showKnown ? (view.showMatches ? &kGood : &kBad) : &kWarn);
                valueRow(tr("Report age"), fixed(view.age, 1) + " s");
            } else {
                warnText(tr("This follower does not send readiness reports. It runs an older version or has hot standby off."));
            }
        }
    }

    if (ImGui::BeginPopupModal("##failoverConfirm", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (!failover_.machine().requestOpen()) {
            failoverConfirm_ = false;
            ImGui::CloseCurrentPopup();
        } else {
            ImGui::TextUnformatted(tr("Take over as the leader?"));
            ImGui::Separator();
            ImGui::PushTextWrapPos(ImGui::GetFontSize() * 28.0f);
            ImGui::TextWrapped("%s", tr("This PC opens the network sync as master, stops holding its outputs and tells the old primary to step down."));
            if (failover_.machine().requestWarnsAlive()) {
                warnText(tr("The primary is still answering. Until it steps down both PCs may send a picture. Only continue if you want to switch on purpose."));
            }
            if (failover_.report().overall != failover::Level::Ok) {
                warnText(tr("This PC is not fully ready. Look at the table before you continue."));
            }
            ImGui::PopTextWrapPos();
            const double waited = now - failover_.machine().measure().requestedAt;
            ImGui::BeginDisabled(waited < 0.6);
            if (ui::accentButton(tr("Take over now"), true)) {
                failover_.confirmTakeover(now);
                failoverConfirm_ = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (ImGui::Button(tr("Cancel"))) {
                failover_.cancelTakeover();
                failoverConfirm_ = false;
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::EndPopup();
    }

    const failover::Measure& measure = failover_.machine().measure();
    ImGui::SeparatorText(tr("Last takeover (measured, not promised)"));
    if (!measure.taken) {
        ImGui::TextDisabled("%s", tr("No takeover yet."));
    } else {
        valueRow(tr("Kind"), measure.automatic ? tr("Automatic") : (measure.forced ? tr("By hand, primary still answering") : tr("By hand")));
        valueRow(tr("Loss detected after"), fixed(measure.detectionSeconds, 2) + " s");
        valueRow(tr("Decision after the loss"), fixed(measure.decisionSeconds, 2) + " s");
        valueRow(tr("Age of the last state"), fixed(measure.stateAgeAtTakeover, 2) + " s");
        valueRow(tr("Readiness before the loss"), levelLabel(measure.readinessBeforeLoss), &levelColor(measure.readinessBeforeLoss));
        valueRow(tr("Ready for before the loss"), fixed(measure.readySecondsBeforeLoss, 0) + " s");
        if (failover_.switchSeconds() > 0.0) {
            valueRow(tr("Serving as leader after"), fixed(failover_.switchSeconds(), 2) + " s");
        }
    }

    ImGui::SeparatorText(tr("Events"));
    const std::vector<FailoverEvent>& events = failover_.events();
    if (events.empty()) {
        ImGui::TextDisabled("%s", tr("Nothing happened yet."));
    }
    int shown = 0;
    for (auto it = events.rbegin(); it != events.rend() && shown < 14; ++it, ++shown) {
        const std::string line = fixed(std::max(now - it->time, 0.0), 0) + " s " + tr("ago") + "  " + eventLabel(it->kind) + "  " + it->detail;
        ImGui::TextUnformatted(line.c_str());
    }

    ImGui::End();
}

}
