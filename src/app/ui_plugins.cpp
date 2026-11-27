#include "app/app.hpp"

#include "app/ui_common.hpp"
#include "core/log.hpp"
#include "platform/platform.hpp"

#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

namespace nvj {

namespace {

const ImVec4 kPluginWarn(1.0f, 0.55f, 0.2f, 1.0f);
const ImVec4 kPluginBad(1.0f, 0.4f, 0.35f, 1.0f);

struct SourceChoice {
    std::string key;
    std::string label;
};

std::string labelFor(const PluginHost& host, const std::string& key) {
    const PluginSourceDesc* source = host.findSource(key);
    if (source == nullptr) {
        return key;
    }
    std::string pluginId;
    std::string sourceId;
    splitPluginSourceKey(key, pluginId, sourceId);
    const PluginInfo* info = host.find(pluginId);
    return (info != nullptr ? info->entry.manifest.name : pluginId) + ": " + source->name;
}

std::vector<SourceChoice> loadedChoices(const PluginHost& host, int kind) {
    std::vector<SourceChoice> choices;
    for (const PluginInfo& info : host.plugins()) {
        if (info.state != PluginState::Loaded) {
            continue;
        }
        for (const PluginSourceDesc& source : info.sources) {
            if (source.kind == kind) {
                choices.push_back({source.key, info.entry.manifest.name + ": " + source.name});
            }
        }
    }
    return choices;
}

bool pickSource(PluginHost& host, const char* id, int kind, const std::string& current, std::string& chosen, float width) {
    std::string preview = tr("None");
    if (!current.empty()) {
        preview = labelFor(host, current);
        if (host.findSource(current) == nullptr) {
            preview += std::string(" (") + tr("not available") + ")";
        }
    }
    bool changed = false;
    ImGui::SetNextItemWidth(width);
    if (ui::beginCombo(id, preview.c_str())) {
        if (ImGui::Selectable(tr("None"), current.empty())) {
            chosen.clear();
            changed = true;
        }
        for (const SourceChoice& choice : loadedChoices(host, kind)) {
            if (ImGui::Selectable(choice.label.c_str(), current == choice.key)) {
                chosen = choice.key;
                changed = true;
            }
        }
        ImGui::EndCombo();
    }
    return changed;
}

void drawParameters(PluginHost& host, const std::string& key) {
    const PluginSourceDesc* source = host.findSource(key);
    if (source == nullptr || source->parameterCount <= 0) {
        return;
    }
    std::array<float, kPluginParameters> values = host.config().parametersFor(key);
    ImGui::PushID(key.c_str());
    ImGui::Indent();
    for (int p = 0; p < source->parameterCount && p < kPluginParameters; ++p) {
        ImGui::PushID(p);
        float value = values[static_cast<size_t>(p)];
        ImGui::SetNextItemWidth(ui::px(220.0f));
        if (ui::sliderFloat(source->parameterNames[static_cast<size_t>(p)].c_str(), &value, 0.0f, 1.0f, "%.2f")) {
            host.setParameter(key, p, value);
        }
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            host.saveConfig();
        }
        ImGui::PopID();
    }
    ImGui::Unindent();
    ImGui::PopID();
}

const char* errorKindLabel(PluginErrorKind kind) {
    switch (kind) {
    case PluginErrorKind::Load:
        return tr("Load error");
    case PluginErrorKind::Incompatible:
        return tr("Not compatible");
    case PluginErrorKind::CallFailed:
        return tr("Call failed");
    case PluginErrorKind::Exception:
        return tr("Exception");
    case PluginErrorKind::Timeout:
        return tr("Time budget exceeded");
    case PluginErrorKind::BadOutput:
        return tr("Invalid result");
    case PluginErrorKind::Guard:
        return tr("Memory guard");
    case PluginErrorKind::Limit:
        return tr("Limit reached");
    default:
        return tr("Failed");
    }
}

const char* stateLabel(PluginState state) {
    switch (state) {
    case PluginState::Loading:
        return tr("Loading");
    case PluginState::Loaded:
        return tr("Loaded");
    case PluginState::Failed:
        return tr("Failed");
    case PluginState::SafeMode:
        return tr("Safe mode: the plugin was running when NyrexVJ crashed.");
    case PluginState::Incompatible:
        return tr("Not compatible");
    default:
        return tr("Not enabled");
    }
}

ImVec4 stateColor(PluginState state) {
    switch (state) {
    case PluginState::Loaded:
        return ui::kAccent;
    case PluginState::Failed:
    case PluginState::SafeMode:
    case PluginState::Incompatible:
        return kPluginBad;
    default:
        return ImVec4(0.7f, 0.7f, 0.7f, 1.0f);
    }
}

void drawPluginEntry(PluginHost& host, const PluginInfo& info) {
    const std::string id = info.entry.manifest.id;
    const std::string folder = info.entry.directory.filename().string();
    ImGui::PushID(folder.c_str());
    ImGui::TextUnformatted(info.entry.manifest.name.empty() ? folder.c_str() : info.entry.manifest.name.c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("%s", info.entry.manifest.version.c_str());
    ImGui::SetItemTooltip("%s\nAPI %u.%u", info.entry.directory.string().c_str(), info.entry.manifest.apiMajor, info.entry.manifest.apiMinor);
    const bool blocked = info.state == PluginState::SafeMode || info.state == PluginState::Incompatible;
    ImGui::BeginDisabled(blocked);
    bool on = host.config().isEnabled(id) && !blocked;
    if (ImGui::Checkbox(tr("Enable"), &on)) {
        host.setEnabled(id, on);
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::TextColored(stateColor(info.state), "%s", stateLabel(info.state));
    if (info.state == PluginState::SafeMode) {
        ImGui::SameLine();
        if (ImGui::SmallButton(tr("Enable again"))) {
            host.clearSafeMode(id);
        }
    }
    if (!info.message.empty() && (info.state == PluginState::Failed || info.state == PluginState::Incompatible)) {
        ImGui::PushStyleColor(ImGuiCol_Text, kPluginWarn);
        ImGui::TextWrapped("[%s] %s", errorKindLabel(info.errorKind), info.message.c_str());
        ImGui::PopStyleColor();
    }
    if (info.state == PluginState::Failed) {
        ImGui::TextDisabled("%s", tr("A failed plugin stays switched off until NyrexVJ is restarted."));
    }
    if (info.state == PluginState::Loaded) {
        for (const PluginSourceDesc& source : info.sources) {
            ImGui::BulletText("%s  (%s)", source.name.c_str(), source.kind == kPluginSourceKindFrame ? tr("Image source") : tr("Modulation source"));
        }
    }
    ImGui::Spacing();
    ImGui::PopID();
}

}

void App::drawPluginFrameSlot(int slot) {
    if (!plugins_ || slot < 0 || slot >= kPluginFrameSlots) {
        return;
    }
    PluginHost& host = *plugins_;
    const std::string current = host.config().frames[static_cast<size_t>(slot)];
    std::string chosen;
    ImGui::PushID("pluginFrame");
    if (pickSource(host, trId("Plugin source", "pluginFramePick"), kPluginSourceKindFrame, current, chosen, -90.0f)) {
        host.assignFrame(slot, chosen);
    }
    drawParameters(host, host.config().frames[static_cast<size_t>(slot)]);
    ImGui::PopID();
    ImGui::TextDisabled("%s", tr("Plugins are switched on in Settings, Plugins."));
    if (ImGui::SmallButton(tr("Open plugin settings"))) {
        openSettings(SettingsPlugins);
    }
}

void App::drawPluginsPage() {
    if (!plugins_) {
        return;
    }
    PluginHost& host = *plugins_;
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextColored(kPluginWarn, "%s", tr("Plugins are native code and run with the same rights as NyrexVJ. There is no sandbox. Enable only plugins from sources you trust."));
    if (!host.allowLoading()) {
        ImGui::TextDisabled("%s", tr("Plugins are switched off for this session."));
    }
    ImGui::PopTextWrapPos();

    ui::sectionHeader(tr("Installed plugins"));
    if (ImGui::Button(tr("Open plugins folder"))) {
        const std::filesystem::path folder = pluginsFolder(userDataDir());
        std::error_code ec;
        std::filesystem::create_directories(folder, ec);
        platform::openFolder(folder.string());
    }
    ImGui::SameLine();
    if (ImGui::Button(tr("Rescan"))) {
        host.rescan();
    }
    if (host.plugins().empty()) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled("%s", tr("No plugins found. Put a plugin folder with a plugin.json file into the plugins folder."));
        ImGui::PopTextWrapPos();
    }
    for (const PluginInfo& info : host.plugins()) {
        drawPluginEntry(host, info);
    }

    ui::sectionHeader(tr("Modulation sources"));
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("%s", tr("Pick a plugin source for Plugin 1 to Plugin 8. Then choose it as the source of a modulation or an effect reaction."));
    ImGui::PopTextWrapPos();
    for (int slot = 0; slot < kPluginModulationSlots; ++slot) {
        ImGui::PushID(slot);
        const std::string current = host.config().modulation[static_cast<size_t>(slot)];
        std::string chosen;
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(tr(audioBandName(BandPlugin1 + slot)));
        ImGui::SameLine(90.0f);
        if (pickSource(host, "##modulationSource", kPluginSourceKindModulation, current, chosen, 260.0f)) {
            host.assignModulation(slot, chosen);
        }
        if (!current.empty()) {
            const PluginRunStats stats = host.modulationStats(slot);
            ImGui::SameLine();
            ImGui::ProgressBar(stats.value, ImVec2(70.0f, 0.0f), "");
            ImGui::SameLine();
            if (stats.failed) {
                ImGui::TextColored(kPluginBad, "%s", tr("Failed"));
            } else if (stats.running) {
                ImGui::TextDisabled("%s %.2f ms", tr("slowest call"), stats.maxCallMs);
                if (stats.throttled) {
                    ImGui::SameLine();
                    ImGui::TextColored(kPluginWarn, "%s", tr("Slowed down"));
                }
            } else {
                ImGui::TextDisabled("%s", tr("Idle"));
            }
            if (!stats.error.empty() && (stats.failed || stats.errorKind == PluginErrorKind::Limit)) {
                ImGui::PushStyleColor(ImGuiCol_Text, kPluginWarn);
                ImGui::TextWrapped("[%s] %s", errorKindLabel(stats.errorKind), stats.error.c_str());
                ImGui::PopStyleColor();
            }
            drawParameters(host, current);
        }
        ImGui::PopID();
    }

    ui::sectionHeader(tr("Image sources"));
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("%s", tr("These feed the clips Plugin Source 1 to Plugin Source 4."));
    ImGui::PopTextWrapPos();
    for (int slot = 0; slot < kPluginFrameSlots; ++slot) {
        ImGui::PushID(100 + slot);
        const std::string current = host.config().frames[static_cast<size_t>(slot)];
        std::string chosen;
        ImGui::AlignTextToFramePadding();
        ImGui::Text("%s %d", tr("Source"), slot + 1);
        ImGui::SameLine(90.0f);
        if (pickSource(host, "##frameSource", kPluginSourceKindFrame, current, chosen, 260.0f)) {
            host.assignFrame(slot, chosen);
        }
        if (!current.empty()) {
            const PluginRunStats stats = host.runnerStats(current);
            if (stats.throttled && !stats.failed) {
                ImGui::SameLine();
                ImGui::TextColored(kPluginWarn, "%s", tr("Slowed down"));
            }
            if (stats.failed && !stats.error.empty()) {
                ImGui::PushStyleColor(ImGuiCol_Text, kPluginWarn);
                ImGui::TextWrapped("[%s] %s", errorKindLabel(stats.errorKind), stats.error.c_str());
                ImGui::PopStyleColor();
            }
            drawParameters(host, current);
        }
        ImGui::PopID();
    }
}

}
