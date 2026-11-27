#include "app/app.hpp"

#include "app/ui_common.hpp"

#include <imgui.h>

#include <algorithm>
#include <cstdio>
#include <string>

namespace nvj {

namespace {

double megabytes(size_t bytes) {
    return static_cast<double>(bytes) / (1024.0 * 1024.0);
}

std::string sourceText(const routing::Source& source) {
    char text[96];
    switch (source.kind) {
    case routing::SourceKind::Layer:
        std::snprintf(text, sizeof(text), tr("Layer %d"), source.index + 1);
        break;
    case routing::SourceKind::Group:
        std::snprintf(text, sizeof(text), tr("Layers 1-%d"), source.index);
        break;
    case routing::SourceKind::Master:
        std::snprintf(text, sizeof(text), "%s", tr("Master"));
        break;
    }
    std::string result = text;
    if (source.previousFrame) {
        std::snprintf(text, sizeof(text), tr("%s (previous frame)"), result.c_str());
        result = text;
    }
    return result;
}

const char* statusText(routing::Status status) {
    switch (status) {
    case routing::Status::Ok:
        return tr("Working");
    case routing::Status::Feedback:
        return tr("Feedback, one frame late");
    case routing::Status::Cycle:
        return tr("Blocked, this would be a loop");
    case routing::Status::Inactive:
        return tr("Source is not playing, drawing transparent");
    case routing::Status::Invalid:
        return tr("Not a valid choice");
    }
    return "";
}

ImVec4 statusColor(routing::Status status) {
    switch (status) {
    case routing::Status::Ok:
        return ImVec4(0.55f, 0.85f, 0.55f, 1.0f);
    case routing::Status::Feedback:
        return ImVec4(0.62f, 0.75f, 0.95f, 1.0f);
    case routing::Status::Inactive:
        return ImVec4(1.0f, 0.65f, 0.2f, 1.0f);
    case routing::Status::Cycle:
    case routing::Status::Invalid:
        break;
    }
    return ImVec4(1.0f, 0.38f, 0.32f, 1.0f);
}

}

bool App::senderRunning(int kind) const {
    switch (kind) {
    case 0:
        return spout_.active();
    case 1:
        return ndi_.active();
    case 2:
        return recorder_.recording();
    }
    return false;
}

void App::applySenderSources() {
    const std::string* keys[routing::kSenderCount] = {&settings_.senderSpout, &settings_.senderNdi, &settings_.senderRecord};
    for (int k = 0; k < routing::kSenderCount; ++k) {
        routing::Source source;
        if (!senderRunning(k) || !routing::parseSenderKey(*keys[k], source)) {
            source = routing::Source{};
        }
        engine_.setSenderSource(static_cast<routing::SenderKind>(k), source);
    }
    engine_.setRoutePreviewsWanted(routingOpen_);
}

void App::startRoutingDemo() {
    const int base = clipIndexByName("Julia");
    const int second = clipIndexByName("Plasma");
    const int other = second >= 0 ? second : base;
    const int group = clipIndexByName(routing::sourceName(routing::Source{routing::SourceKind::Group, 2, false}));
    const int feedback = clipIndexByName(routing::sourceName(routing::Source{routing::SourceKind::Layer, 0, true}));
    if (base < 0 || group < 0 || feedback < 0) {
        return;
    }
    Composition demo;
    demo.layers[0].setCell(0, base);
    demo.layers[0].activeColumn = 0;
    demo.layers[1].setCell(0, other);
    demo.layers[1].activeColumn = 0;
    demo.layers[1].blend = BlendAdd;
    demo.layers[1].opacity = 0.5f;
    demo.layers[2].setCell(0, group);
    demo.layers[2].activeColumn = 0;
    demo.layers[2].blend = BlendAdd;
    demo.layers[2].opacity = 0.35f;
    demo.layers[3].setCell(0, feedback);
    demo.layers[3].activeColumn = 0;
    demo.layers[3].blend = BlendAdd;
    demo.layers[3].opacity = 0.3f;
    engine_.applyComposition(demo);
    settings_.senderSpout = routing::senderKey(routing::Source{routing::SourceKind::Layer, 0, false});
    routingOpen_ = true;
}

void App::drawRoutingWindow() {
    if (!routingOpen_) {
        return;
    }
    ui::setNextWindowSize(ImVec2(660.0f, 720.0f), ImGuiCond_FirstUseEver);
    std::string title = std::string(tr("Video routing")) + "###routingWindow";
    if (!ui::beginTool(title.c_str(), &routingOpen_)) {
        ImGui::End();
        return;
    }

    ImGui::TextWrapped("%s", tr("A route clip shows the picture of another layer, of several layers together or of the master. Find them in the library under the Routing filter. The picture is copied on the graphics card and never goes through the processor."));

    ui::sectionHeader(tr("Sources"));
    constexpr float kTileWidth = 144.0f;
    constexpr float kTileHeight = 81.0f;
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const int perRow = std::max(1, static_cast<int>((ImGui::GetContentRegionAvail().x + spacing) / (kTileWidth + spacing)));
    for (int slot = 0; slot < routing::kSlotCount; ++slot) {
        if (slot % perRow != 0) {
            ImGui::SameLine();
        }
        const routing::Source source = routing::sourceOfSlot(slot);
        const RouteSlotStatus status = engine_.routeSlot(slot);
        ImGui::PushID(slot);
        ImGui::BeginGroup();
        ImGui::TextUnformatted(sourceText(source).c_str());
        if (status.preview && status.fresh) {
            ImGui::Image(ui::texId(status.preview), ImVec2(kTileWidth, kTileHeight), ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f));
        } else {
            const ImVec2 corner = ImGui::GetCursorScreenPos();
            ImGui::Dummy(ImVec2(kTileWidth, kTileHeight));
            ImGui::GetWindowDrawList()->AddRect(corner, ImVec2(corner.x + kTileWidth, corner.y + kTileHeight), IM_COL32(90, 90, 90, 255));
            ImGui::SetCursorScreenPos(ImVec2(corner.x + 8.0f, corner.y + kTileHeight * 0.5f - ImGui::GetTextLineHeight() * 0.5f));
            ImGui::TextDisabled("%s", tr("not playing"));
            ImGui::SetCursorScreenPos(ImVec2(corner.x, corner.y + kTileHeight + ImGui::GetStyle().ItemSpacing.y));
        }
        if (status.width > 0) {
            ImGui::TextDisabled("%d x %d", status.width, status.height);
        } else {
            ImGui::TextDisabled("-");
        }
        ImGui::Text("%s: %d", tr("Used by"), status.references);
        ImGui::Text("%s: %.1f MB", tr("Memory"), megabytes(status.bytes));
        ImGui::EndGroup();
        ImGui::PopID();
    }

    ui::sectionHeader(tr("Route clips in the layers"));
    bool any = false;
    for (const routing::Resolved& resolved : engine_.routePlan().consumers) {
        if (resolved.consumer.kind != routing::ConsumerKind::Layer) {
            continue;
        }
        any = true;
        ImGui::Text(tr("Layer %d shows"), resolved.consumer.index + 1);
        ImGui::SameLine();
        ImGui::TextUnformatted(sourceText(resolved.consumer.source).c_str());
        ImGui::SameLine();
        ImGui::TextColored(statusColor(resolved.status), "- %s", statusText(resolved.status));
    }
    if (!any) {
        ImGui::TextDisabled("%s", tr("No layer plays a route clip right now."));
    }

    ui::sectionHeader(tr("Pictures sent to other programs"));
    const char* senderNames[routing::kSenderCount] = {tr("Spout"), tr("NDI"), tr("Recording")};
    std::string* keys[routing::kSenderCount] = {&settings_.senderSpout, &settings_.senderNdi, &settings_.senderRecord};
    for (int k = 0; k < routing::kSenderCount; ++k) {
        routing::Source current;
        if (!routing::parseSenderKey(*keys[k], current)) {
            current = routing::Source{};
        }
        ImGui::PushID(k);
        ImGui::SetNextItemWidth(ui::px(220.0f));
        if (ImGui::BeginCombo(senderNames[k], sourceText(current).c_str())) {
            for (const routing::Source& choice : routing::senderChoices()) {
                const bool selected = choice == current;
                if (ImGui::Selectable(sourceText(choice).c_str(), selected)) {
                    *keys[k] = routing::senderKey(choice);
                    saveSettings(settings_);
                }
                if (selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
        const routing::SenderKind kind = static_cast<routing::SenderKind>(k);
        if (current.kind != routing::SourceKind::Master) {
            ImGui::SameLine();
            if (!senderRunning(k)) {
                ImGui::TextDisabled("%s", tr("not running"));
            } else if (engine_.senderSourceLive(kind)) {
                ImGui::TextColored(statusColor(routing::Status::Ok), "%s", tr("sending"));
            } else {
                ImGui::TextColored(statusColor(routing::Status::Inactive), "%s", tr("source not playing, sending black"));
            }
        }
        ImGui::PopID();
    }
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("%s", tr("Master sends the finished picture. Any other source sends an opaque copy of that layer or group without the master effects."));
    ImGui::PopTextWrapPos();

    ui::sectionHeader(tr("Memory"));
    ImGui::Text(tr("%.1f MB for routed pictures in %d copies"), megabytes(engine_.routeBytes()), engine_.routeCapturesAlive());
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("%s", tr("A copy is only made while a route clip or a sender needs it, and it is released about a second and a half after the last user is gone."));
    ImGui::PopTextWrapPos();

    ImGui::End();
}

}
