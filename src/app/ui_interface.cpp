#include "app/app.hpp"

#include "app/shortcut_keys.hpp"
#include "app/theme.hpp"
#include "app/ui_common.hpp"
#include "core/log.hpp"
#include "core/workspace.hpp"

#include <GLFW/glfw3.h>
#include <imgui_impl_glfw.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>

namespace nvj {

namespace {

constexpr int kRecoveredLayout = 1;
constexpr int kRecoveredSettings = 2;
constexpr int kRecoveredShortcuts = 4;
constexpr int kShortcutsNewer = 8;
constexpr int kInterfaceReset = 16;
constexpr int kLayoutReset = 32;

constexpr float kMinScale = 0.7f;
constexpr float kMaxScale = 2.5f;

std::string readFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return std::string();
    }
    return std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
}

std::string translatedReason(const std::string& english) {
    return trMessage(english);
}

const char* shortcutLabel(shortcuts::Action action) {
    return tr(shortcuts::info(action).label);
}

}

void App::recoverInterfaceFiles(const StartOptions& options) {
    const std::filesystem::path dataDir = userDataDir();
    const std::filesystem::path layoutFile(iniPath_);
    const std::filesystem::path settingsFile = dataDir / "settings.json";

    if (options.resetLayout || options.resetInterface) {
        std::error_code ec;
        if (std::filesystem::exists(layoutFile, ec)) {
            workspace::quarantine(layoutFile);
        }
        recoveryFlags_ |= kLayoutReset;
    }
    if (options.resetInterface) {
        std::error_code ec;
        const std::filesystem::path shortcutFile = dataDir / "shortcuts.json";
        if (std::filesystem::exists(shortcutFile, ec)) {
            workspace::quarantine(shortcutFile);
        }
        recoveryFlags_ |= kInterfaceReset;
    }

    std::error_code ec;
    if (std::filesystem::exists(layoutFile, ec)) {
        const std::string text = readFile(layoutFile);
        const workspace::Check check = workspace::inspectLayout(text);
        if (!check.ok) {
            logLine("Saved window layout is not usable: " + check.reason);
            workspace::quarantine(layoutFile);
            recoveryFlags_ |= kRecoveredLayout;
        }
    }
    if (std::filesystem::exists(settingsFile, ec)) {
        const std::string text = readFile(settingsFile);
        const nlohmann::json doc = nlohmann::json::parse(text, nullptr, false);
        if (doc.is_discarded() || !doc.is_object()) {
            logLine("settings.json is not usable and was set aside");
            workspace::quarantine(settingsFile);
            recoveryFlags_ |= kRecoveredSettings;
        }
    }
    needsLayout_ = !std::filesystem::exists(layoutFile, ec);
}

void App::loadShortcuts() {
    shortcutsPath_ = (std::filesystem::path(userDataDir()) / "shortcuts.json").string();
    const shortcuts::LoadResult result = shortcuts::loadFile(shortcutsPath_, shortcuts_);
    if (result.state == shortcuts::LoadState::Damaged) {
        logLine("shortcuts.json is not usable: " + result.error);
        workspace::quarantine(shortcutsPath_);
        shortcuts_.resetAll();
        recoveryFlags_ |= kRecoveredShortcuts;
    } else if (result.state == shortcuts::LoadState::Newer) {
        shortcuts_.resetAll();
        recoveryFlags_ |= kShortcutsNewer;
        shortcutsNewerKept_ = true;
    }
    for (const std::string& note : result.notes) {
        logLine("shortcuts.json: " + note);
    }
}

void App::saveShortcuts() {
    if (shortcutsPath_.empty()) {
        return;
    }
    if (shortcutsNewerKept_) {
        workspace::quarantine(shortcutsPath_);
        shortcutsNewerKept_ = false;
    }
    if (!shortcuts::saveFile(shortcutsPath_, shortcuts_)) {
        toast(tr("The shortcuts could not be saved"));
    }
}

bool App::shortcutBlocked() const {
    const ImGuiIO& io = ImGui::GetIO();
    return io.WantTextInput || shortcutCapture_ >= 0;
}

bool App::shortcutPressed(shortcuts::Action action) {
    const shortcuts::Binding& binding = shortcuts_.get(action);
    if (binding.empty() || shortcutBlocked()) {
        return false;
    }
    const ImGuiKey key = imguiKeyFor(binding.key);
    if (key == ImGuiKey_None || !ImGui::IsKeyPressed(key, false)) {
        return false;
    }
    const ImGuiIO& io = ImGui::GetIO();
    if (settings_.uiKeyboardNav && io.NavActive && io.NavVisible && binding.key == "Space" && !binding.ctrl && !binding.shift && !binding.alt) {
        return false;
    }
    return shortcuts_.matches(action, binding.key, io.KeyCtrl, io.KeyShift, io.KeyAlt);
}

void App::updateInterfaceStyle() {
    ImGuiIO& io = ImGui::GetIO();
    float dpi = 1.0f;
    if (settings_.uiAutoScale) {
        dpi = dpiOverride_ > 0.0f ? dpiOverride_ : (window_ ? ImGui_ImplGlfw_GetContentScaleForWindow(window_) : 1.0f);
        dpi = std::clamp(dpi, 0.75f, 4.0f);
    }
    dpiScale_ = dpi;
    const float scale = std::clamp(settings_.uiScale, kMinScale, kMaxScale);
    const float sizeScale = scale * dpi;
    const bool high = settings_.uiContrast == "high";
    if (std::fabs(sizeScale - appliedSizeScale_) > 0.0001f || high != appliedHighContrast_ || settings_.theme != appliedTheme_) {
        ThemeOptions options;
        options.highContrast = high;
        options.sizeScale = sizeScale;
        ImGuiStyle& style = ImGui::GetStyle();
        style.FontScaleMain = scale;
        style.FontScaleDpi = dpi;
        applyTheme(themeIndex(settings_.theme), options);
        appliedSizeScale_ = sizeScale;
        appliedHighContrast_ = high;
        appliedTheme_ = settings_.theme;
    }
    ImGuiStyle& style = ImGui::GetStyle();
    style.FontScaleMain = scale;
    style.FontScaleDpi = dpi;
    if (settings_.uiKeyboardNav != appliedKeyboardNav_ || !keyboardNavSeen_) {
        if (settings_.uiKeyboardNav) {
            io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        } else {
            io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableKeyboard;
        }
        appliedKeyboardNav_ = settings_.uiKeyboardNav;
        keyboardNavSeen_ = true;
    }
}

void App::changeInterfaceScale(int direction) {
    float value = settings_.uiScale;
    if (direction == 0) {
        value = 1.0f;
    } else {
        value = std::round((value + 0.1f * static_cast<float>(direction)) * 100.0f) / 100.0f;
    }
    value = std::clamp(value, kMinScale, kMaxScale);
    settings_.uiScale = value;
    uiScaleEdit_ = value;
    saveSettings(settings_);
    char text[64];
    std::snprintf(text, sizeof(text), "%s: %d%%", tr("Interface size"), static_cast<int>(std::lround(value * 100.0f)));
    toast(text);
}

void App::refreshWorkspaces() {
    workspaces_ = workspace::list(userDataDir());
    workspacesStale_ = false;
}

void App::requestWorkspace(const std::string& name) {
    pendingWorkspace_ = name;
}

void App::applyPendingWorkspace() {
    if (!pendingWorkspace_.empty()) {
        const std::string name = pendingWorkspace_;
        pendingWorkspace_.clear();
        std::string layout;
        workspace::Header header;
        const workspace::Result result = workspace::load(userDataDir(), name, layout, &header);
        if (!result.ok) {
            workspaceMessage_ = translatedReason(result.error);
            toast(workspaceMessage_);
        } else {
            ImGui::LoadIniSettingsFromMemory(layout.c_str(), layout.size());
            needsLayout_ = false;
            const ImVec2 size = ImGui::GetMainViewport()->Size;
            const bool differs = header.width > 0 && header.height > 0 && (std::abs(header.width - static_cast<int>(size.x)) > 40 || std::abs(header.height - static_cast<int>(size.y)) > 40);
            char text[192];
            if (differs) {
                std::snprintf(text, sizeof(text), "%s: %s (%dx%d)", tr("Workspace loaded"), name.c_str(), header.width, header.height);
            } else {
                std::snprintf(text, sizeof(text), "%s: %s", tr("Workspace loaded"), name.c_str());
            }
            workspaceMessage_ = text;
            toast(text);
        }
    }
    if (recoveryFlags_ != 0) {
        std::string note;
        auto add = [&note](const char* text) {
            if (!note.empty()) {
                note += " ";
            }
            note += text;
        };
        if (recoveryFlags_ & kRecoveredLayout) {
            add(tr("The saved window layout was damaged. A copy was kept as layout_v6.ini.bad and the default layout was restored."));
        }
        if (recoveryFlags_ & kRecoveredSettings) {
            add(tr("settings.json was damaged. A copy was kept as settings.json.bad and the defaults are used."));
        }
        if (recoveryFlags_ & kRecoveredShortcuts) {
            add(tr("shortcuts.json was damaged. A copy was kept as shortcuts.json.bad and the default shortcuts are used."));
        }
        if (recoveryFlags_ & kShortcutsNewer) {
            add(tr("shortcuts.json comes from a newer version of NyrexVJ and was not changed. The default shortcuts are used."));
        }
        if (recoveryFlags_ & kInterfaceReset) {
            add(tr("Interface settings, layout and shortcuts were reset to the defaults."));
        } else if (recoveryFlags_ & kLayoutReset) {
            add(tr("The window layout was reset to the default."));
        }
        recoveryFlags_ = 0;
        toast(note);
        toastUntil_ = ImGui::GetTime() + 12.0;
    }
}

void App::saveWorkspaceAs(const std::string& name) {
    size_t size = 0;
    const char* data = ImGui::SaveIniSettingsToMemory(&size);
    const std::string layout(data ? data : "", data ? size : 0);
    const ImVec2 view = ImGui::GetMainViewport()->Size;
    const workspace::Result result = workspace::save(userDataDir(), name, layout, static_cast<int>(view.x), static_cast<int>(view.y), std::string("NyrexVJ ") + NYREXVJ_VERSION);
    workspacesStale_ = true;
    if (result.ok) {
        workspaceMessage_ = std::string(tr("Workspace saved")) + ": " + workspace::cleanName(name);
        workspaceName_[0] = '\0';
    } else {
        workspaceMessage_ = translatedReason(result.error);
    }
    toast(workspaceMessage_);
}

void App::deleteWorkspace(const std::string& name) {
    const workspace::Result result = workspace::remove(userDataDir(), name);
    workspacesStale_ = true;
    workspaceDeleteArmed_.clear();
    workspaceMessage_ = result.ok ? std::string(tr("Workspace deleted")) + ": " + name : translatedReason(result.error);
}

void App::resetWindowLayout() {
    needsLayout_ = true;
    toast(tr("Window layout reset"));
}

void App::drawViewMenu() {
    if (workspacesStale_) {
        refreshWorkspaces();
    }
    if (ImGui::BeginMenu(trId("View", "menuView"))) {
        if (ImGui::BeginMenu(trId("Workspaces", "menuWorkspaces"))) {
            bool any = false;
            for (const workspace::Entry& entry : workspaces_) {
                if (!entry.valid) {
                    continue;
                }
                any = true;
                if (ImGui::MenuItem(entry.name.c_str())) {
                    requestWorkspace(entry.name);
                }
            }
            if (!any) {
                ImGui::TextDisabled("%s", tr("No saved workspaces yet"));
            }
            ImGui::Separator();
            if (ImGui::MenuItem(tr("Save current layout as..."))) {
                workspacePopupOpen_ = true;
            }
            if (ImGui::MenuItem(tr("Manage workspaces..."))) {
                openSettings(SettingsAppearance);
            }
            ImGui::EndMenu();
        }
        if (ImGui::MenuItem(tr("Reset window layout"))) {
            resetWindowLayout();
        }
        ImGui::Separator();
        const std::string larger = shortcuts::format(shortcuts_.get(shortcuts::Action::InterfaceLarger));
        const std::string smaller = shortcuts::format(shortcuts_.get(shortcuts::Action::InterfaceSmaller));
        const std::string normal = shortcuts::format(shortcuts_.get(shortcuts::Action::InterfaceReset));
        if (ImGui::MenuItem(tr("Larger interface"), larger.c_str(), false, settings_.uiScale < kMaxScale - 0.001f)) {
            changeInterfaceScale(1);
        }
        if (ImGui::MenuItem(tr("Smaller interface"), smaller.c_str(), false, settings_.uiScale > kMinScale + 0.001f)) {
            changeInterfaceScale(-1);
        }
        if (ImGui::MenuItem(tr("Normal interface size"), normal.c_str())) {
            changeInterfaceScale(0);
        }
        ImGui::Separator();
        bool high = settings_.uiContrast == "high";
        if (ImGui::MenuItem(tr("High contrast"), nullptr, &high)) {
            settings_.uiContrast = high ? "high" : "standard";
            saveSettings(settings_);
        }
        if (ImGui::MenuItem(tr("Keyboard navigation"), nullptr, &settings_.uiKeyboardNav)) {
            saveSettings(settings_);
        }
        if (ImGui::MenuItem(tr("Show status bar"), nullptr, &settings_.showStatusBar)) {
            saveSettings(settings_);
        }
        ImGui::EndMenu();
    }
    if (workspacePopupOpen_) {
        ImGui::OpenPopup("###saveWorkspace");
        workspacePopupOpen_ = false;
    }
    if (ImGui::BeginPopupModal(trId("Save workspace", "saveWorkspace"), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        const float em = ImGui::GetFontSize();
        ImGui::TextWrapped("%s", tr("Saves where every panel sits and how big it is."));
        ImGui::SetNextItemWidth(em * 18.0f);
        const bool entered = ImGui::InputTextWithHint("##workspaceModalName", tr("Workspace name"), workspaceName_, sizeof(workspaceName_), ImGuiInputTextFlags_EnterReturnsTrue);
        if (ImGui::IsWindowAppearing()) {
            ImGui::SetKeyboardFocusHere(-1);
        }
        const bool nameOk = workspaceName_[0] != '\0';
        ImGui::BeginDisabled(!nameOk);
        const bool save = ImGui::Button(trId("Save", "workspaceModalSave"));
        ImGui::EndDisabled();
        if ((save || (entered && nameOk))) {
            saveWorkspaceAs(workspaceName_);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button(trId("Cancel", "workspaceModalCancel"))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void App::drawWorkspacePanel() {
    if (workspacesStale_) {
        refreshWorkspaces();
    }
    const float em = ImGui::GetFontSize();
    ImGui::TextWrapped("%s", tr("A workspace stores where every panel sits and how big it is. Load one to switch the whole layout at once."));
    ImGui::SetNextItemWidth(em * 16.0f);
    ImGui::InputTextWithHint("##workspaceName", tr("Workspace name"), workspaceName_, sizeof(workspaceName_));
    ImGui::SameLine();
    ImGui::BeginDisabled(workspaceName_[0] == '\0');
    if (ImGui::Button(trId("Save current layout", "workspaceSave"))) {
        saveWorkspaceAs(workspaceName_);
    }
    ImGui::EndDisabled();
    if (!workspaceMessage_.empty()) {
        ImGui::TextDisabled("%s", workspaceMessage_.c_str());
    }
    if (workspaces_.empty()) {
        ImGui::TextDisabled("%s", tr("No saved workspaces yet"));
        return;
    }
    for (size_t i = 0; i < workspaces_.size(); ++i) {
        const workspace::Entry& entry = workspaces_[i];
        ImGui::PushID(static_cast<int>(i));
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(entry.name.c_str());
        if (entry.valid && entry.width > 0 && entry.height > 0) {
            ImGui::SameLine();
            ImGui::TextDisabled("%dx%d", entry.width, entry.height);
        }
        ImGui::SameLine(em * 18.0f);
        ImGui::BeginDisabled(!entry.valid);
        if (ImGui::Button(tr("Load"))) {
            requestWorkspace(entry.name);
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        const bool armed = workspaceDeleteArmed_ == entry.name;
        if (ImGui::Button(armed ? tr("Really delete?") : tr("Delete"))) {
            if (armed) {
                deleteWorkspace(entry.name);
            } else {
                workspaceDeleteArmed_ = entry.name;
            }
        }
        if (!entry.valid) {
            ImGui::SameLine();
            ImGui::TextDisabled("%s: %s", tr("Damaged"), translatedReason(entry.problem).c_str());
        }
        ImGui::PopID();
    }
}

void App::drawInterfaceOptions() {
    const float em = ImGui::GetFontSize();
    ImGui::SeparatorText(tr("Scale"));
    ImGui::SetNextItemWidth(em * 18.0f);
    ui::sliderFloat(trId("Interface size", "optScale"), &uiScaleEdit_, kMinScale, kMaxScale, "%.2f");
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        settings_.uiScale = uiScaleEdit_;
        saveSettings(settings_);
    }
    ImGui::SameLine();
    if (ImGui::SmallButton(trId("Reset", "optScaleReset"))) {
        uiScaleEdit_ = 1.0f;
        settings_.uiScale = 1.0f;
        saveSettings(settings_);
    }
    if (ImGui::Checkbox(trId("Match the display scale", "optAutoScale"), &settings_.uiAutoScale)) {
        saveSettings(settings_);
    }
    ImGui::SetItemTooltip("%s", tr("Follows the scale your operating system sets for this screen, on top of the interface size above."));
    if (settings_.uiAutoScale) {
        ImGui::SameLine();
        ImGui::TextDisabled("%s: %d%%", tr("Display scale"), static_cast<int>(std::lround(dpiScale_ * 100.0f)));
    }
    ImGui::SeparatorText(tr("Contrast"));
    bool high = settings_.uiContrast == "high";
    if (ImGui::Checkbox(trId("High contrast", "optHighContrast"), &high)) {
        settings_.uiContrast = high ? "high" : "standard";
        saveSettings(settings_);
    }
    ImGui::SetItemTooltip("%s", tr("Brighter text, stronger borders and a yellow focus ring."));
    ImGui::SeparatorText(tr("Keyboard"));
    if (ImGui::Checkbox(trId("Keyboard navigation", "optKeyboardNav"), &settings_.uiKeyboardNav)) {
        saveSettings(settings_);
    }
    ImGui::SetItemTooltip("%s", tr("Move between controls with Tab and the arrow keys. The focused control gets a visible ring. Esc leaves it."));
    if (settings_.uiKeyboardNav) {
        ImGui::TextDisabled("%s", tr("While a control is focused, Space presses it instead of tapping the tempo."));
    }
}

void App::drawShortcutsPage() {
    ImGui::TextWrapped("%s", tr("Click a shortcut, then press the new key combination. Esc cancels."));
    ImGuiIO& io = ImGui::GetIO();
    const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp;
    if (ImGui::BeginTable("shortcutTable", 3, flags)) {
        ImGui::TableSetupColumn(tr("Action"), ImGuiTableColumnFlags_WidthStretch, 2.0f);
        ImGui::TableSetupColumn(tr("Shortcut"), ImGuiTableColumnFlags_WidthStretch, 2.0f);
        ImGui::TableSetupColumn("##shortcutButtons", ImGuiTableColumnFlags_WidthStretch, 1.5f);
        ImGui::TableHeadersRow();
        for (int i = 0; i < shortcuts::kActionCount; ++i) {
            const shortcuts::Action action = static_cast<shortcuts::Action>(i);
            ImGui::PushID(i);
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(shortcutLabel(action));
            ImGui::TableSetColumnIndex(1);
            const bool capturing = shortcutCapture_ == i;
            const shortcuts::Binding& binding = shortcuts_.get(action);
            std::string text;
            if (capturing) {
                text = tr("Press a key combination...");
            } else if (binding.empty()) {
                text = tr("Not assigned");
            } else {
                text = shortcuts::format(binding);
            }
            text += "###shortcutButton";
            if (ImGui::Button(text.c_str(), ImVec2(-FLT_MIN, 0.0f))) {
                shortcutCapture_ = capturing ? -1 : i;
                shortcutMessage_.clear();
            }
            if (capturing) {
                bool escape = false;
                const std::string key = capturedKeyName(&escape);
                if (escape) {
                    shortcutCapture_ = -1;
                } else if (!key.empty()) {
                    shortcuts::Binding wanted;
                    wanted.key = key;
                    wanted.ctrl = io.KeyCtrl;
                    wanted.shift = io.KeyShift;
                    wanted.alt = io.KeyAlt;
                    const shortcuts::SetResult result = shortcuts_.set(action, wanted);
                    if (result.ok) {
                        saveShortcuts();
                        shortcutMessage_.clear();
                    } else {
                        shortcutMessage_ = translatedReason(result.error);
                        if (result.conflict >= 0) {
                            shortcutMessage_ += ": ";
                            shortcutMessage_ += shortcutLabel(static_cast<shortcuts::Action>(result.conflict));
                        }
                        shortcutMessage_ += " (" + shortcuts::format(wanted) + ")";
                    }
                    shortcutCapture_ = -1;
                }
            }
            ImGui::TableSetColumnIndex(2);
            ImGui::BeginDisabled(binding.empty());
            if (ImGui::SmallButton(tr("Clear"))) {
                shortcuts_.set(action, shortcuts::Binding());
                saveShortcuts();
                shortcutMessage_.clear();
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::BeginDisabled(shortcuts_.isDefault(action));
            if (ImGui::SmallButton(tr("Default"))) {
                const shortcuts::Binding fallback = shortcuts::defaultBinding(action);
                const int other = shortcuts_.find(fallback);
                if (other >= 0 && other != i) {
                    shortcutMessage_ = std::string(tr("That shortcut is already used")) + ": " + shortcutLabel(static_cast<shortcuts::Action>(other)) + " (" + shortcuts::format(fallback) + ")";
                } else {
                    shortcuts_.reset(action);
                    saveShortcuts();
                    shortcutMessage_.clear();
                }
            }
            ImGui::EndDisabled();
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    if (!shortcutMessage_.empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.45f, 1.0f), "%s", shortcutMessage_.c_str());
    }
    ImGui::BeginDisabled(shortcuts_.isDefault());
    if (ImGui::Button(trId("Reset all shortcuts", "shortcutsResetAll"))) {
        shortcuts_.resetAll();
        saveShortcuts();
        shortcutMessage_.clear();
        shortcutCapture_ = -1;
    }
    ImGui::EndDisabled();
    ImGui::SeparatorText(tr("Fixed shortcuts"));
    ImGui::BulletText("%s", tr("1 - 8  trigger a column"));
    ImGui::BulletText("%s", tr("Ctrl + 1 - 8  deck 1 - 8"));
    ImGui::BulletText("%s", tr("Ctrl + Shift + 1 - 8  deck 9 - 16"));
    ImGui::BulletText("%s", tr("Ctrl + Shift + Z  redo"));
    ImGui::BulletText("%s", tr("Right click a slider  reset effect value"));
}

}
