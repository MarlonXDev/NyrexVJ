#include "app/app.hpp"

#include "app/node_editor.hpp"
#include "app/theme.hpp"
#include "app/ui_common.hpp"
#include "core/log.hpp"
#include "core/shortcuts.hpp"
#include "core/workspace.hpp"
#include "render/clip_library.hpp"
#include "scene3d/scene_pack.hpp"

#include <GLFW/glfw3.h>
#include <glad/gl.h>

#include <imgui.h>
#include <imgui_internal.h>
#include <stb_image_write.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace nvj {

namespace {

namespace fs = std::filesystem;
namespace sc = shortcuts;

constexpr int kMaxFrames = 20000;

const char* const kPanels[] = {"###deck", "###output", "###preview", "###master", "###clip", "###browser", "###trigger", "###audio", "###stripMaster", "###stripAudio", "###stripNetwork", "###stripMidi", "###stripOsc"};
constexpr size_t kPanelCount = sizeof(kPanels) / sizeof(kPanels[0]);

struct Rects {
    std::array<ImVec4, kPanelCount> value;
    std::array<ImGuiID, kPanelCount> dock;
    bool valid = false;
};

ImGuiWindow* panel(size_t index) {
    return ImGui::FindWindowByName(kPanels[index]);
}

Rects captureRects() {
    Rects rects;
    rects.valid = true;
    for (size_t i = 0; i < kPanelCount; ++i) {
        const ImGuiWindow* window = panel(i);
        if (!window) {
            rects.valid = false;
            continue;
        }
        rects.value[i] = ImVec4(window->Pos.x, window->Pos.y, window->Size.x, window->Size.y);
        rects.dock[i] = window->DockId;
    }
    return rects;
}

bool sameRects(const Rects& a, const Rects& b, float tolerance, std::string& detail) {
    if (!a.valid || !b.valid) {
        detail = "a panel is missing";
        return false;
    }
    for (size_t i = 0; i < kPanelCount; ++i) {
        const ImVec4& p = a.value[i];
        const ImVec4& q = b.value[i];
        if (std::fabs(p.x - q.x) > tolerance || std::fabs(p.y - q.y) > tolerance || std::fabs(p.z - q.z) > tolerance || std::fabs(p.w - q.w) > tolerance || a.dock[i] != b.dock[i]) {
            detail = std::string(kPanels[i] + 3) + " differs: " + std::to_string(static_cast<int>(p.x)) + "," + std::to_string(static_cast<int>(p.y)) + " " + std::to_string(static_cast<int>(p.z)) + "x" + std::to_string(static_cast<int>(p.w)) + " against " + std::to_string(static_cast<int>(q.x)) + "," + std::to_string(static_cast<int>(q.y)) + " " + std::to_string(static_cast<int>(q.z)) + "x" + std::to_string(static_cast<int>(q.w));
            return false;
        }
    }
    detail = "all " + std::to_string(kPanelCount) + " panels match";
    return true;
}

bool insideMainWindow(const ImGuiWindow* window, float tolerance) {
    if (!window) {
        return false;
    }
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    return window->Pos.x >= viewport->Pos.x - tolerance && window->Pos.y >= viewport->Pos.y - tolerance && window->Pos.x + window->Size.x <= viewport->Pos.x + viewport->Size.x + tolerance && window->Pos.y + window->Size.y <= viewport->Pos.y + viewport->Size.y + tolerance;
}

std::string describe(const ImGuiWindow* window) {
    if (!window) {
        return "missing";
    }
    return std::to_string(static_cast<int>(window->Pos.x)) + "," + std::to_string(static_cast<int>(window->Pos.y)) + " " + std::to_string(static_cast<int>(window->Size.x)) + "x" + std::to_string(static_cast<int>(window->Size.y));
}

bool allPanelsUsable(std::string& detail) {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    for (size_t i = 0; i < kPanelCount; ++i) {
        const ImGuiWindow* window = panel(i);
        if (window && window->DockNode && window->DockNode->Windows.Size > 1 && !window->DockTabIsVisible) {
            continue;
        }
        if (!window || window->Size.x < 24.0f || window->Size.y < 24.0f || !insideMainWindow(window, 2.0f)) {
            detail = std::string(kPanels[i] + 3) + " " + describe(window) + " in " + std::to_string(static_cast<int>(viewport->Size.x)) + "x" + std::to_string(static_cast<int>(viewport->Size.y));
            return false;
        }
    }
    detail = std::to_string(static_cast<int>(viewport->Size.x)) + "x" + std::to_string(static_cast<int>(viewport->Size.y));
    return true;
}

float measuredFrameHeight() {
    const ImGuiWindow* window = ImGui::FindWindowByName("###deck");
    const float font = window ? window->FontRefSize : ImGui::GetFontSize();
    return font + ImGui::GetStyle().FramePadding.y * 2.0f;
}

std::string readText(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

void writeText(const fs::path& path, const std::string& text) {
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << text;
}

bool saveShot(const std::string& name) {
    const char* folder = std::getenv("NYREXVJ_UITEST_SHOTS");
    if (!folder || !*folder) {
        return false;
    }
    int width = 0;
    int height = 0;
    GLFWwindow* window = glfwGetCurrentContext();
    if (!window) {
        return false;
    }
    glfwGetFramebufferSize(window, &width, &height);
    if (width <= 0 || height <= 0) {
        return false;
    }
    std::vector<unsigned char> pixels(static_cast<size_t>(width) * static_cast<size_t>(height) * 4);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glReadBuffer(GL_BACK);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    for (size_t i = 3; i < pixels.size(); i += 4) {
        pixels[i] = 255;
    }
    std::error_code ec;
    fs::create_directories(folder, ec);
    stbi_flip_vertically_on_write(1);
    const int written = stbi_write_png((fs::path(folder) / (name + ".png")).string().c_str(), width, height, 4, pixels.data(), width * 4);
    stbi_flip_vertically_on_write(0);
    return written != 0;
}

}

void App::startInterfaceTest() {
    interfaceTest_ = ShaderEditorUiTest{};
    interfaceTest_.active = true;
}

std::string App::interfaceTestReport() const {
    nlohmann::json report;
    nlohmann::json checks = nlohmann::json::array();
    for (const ShaderEditorUiTest::Item& item : interfaceTest_.items) {
        nlohmann::json entry;
        entry["name"] = item.name;
        entry["passed"] = item.passed;
        entry["detail"] = item.detail;
        checks.push_back(entry);
    }
    report["checks"] = checks;
    report["failed"] = interfaceTest_.failed;
    report["passed"] = interfaceTest_.failed == 0 && !interfaceTest_.items.empty();
    report["frames"] = interfaceTest_.totalFrames;
    report["version"] = NYREXVJ_VERSION;
    return report.dump(2);
}

int App::tickInterfaceTest() {
    ShaderEditorUiTest& t = interfaceTest_;
    if (!t.active) {
        return 0;
    }
    if (t.steps.empty()) {
        buildInterfaceTest();
    }
    ++t.totalFrames;
    if (t.totalFrames > kMaxFrames) {
        t.items.push_back({"the script finishes in time", "stopped at step " + std::to_string(t.index), false});
        ++t.failed;
        t.active = false;
        return 2;
    }
    if (t.index >= t.steps.size()) {
        t.active = false;
        return t.failed == 0 ? 1 : 2;
    }
    ShaderEditorUiTest::Step& step = t.steps[t.index];
    if (!t.started) {
        t.started = true;
        t.waited = 0;
        if (step.act) {
            step.act();
        }
        return 0;
    }
    ++t.waited;
    const bool done = step.until ? (step.until() || t.waited >= step.frames) : t.waited >= step.frames;
    if (done) {
        if (step.check) {
            step.check();
        }
        ++t.index;
        t.started = false;
    }
    return 0;
}

void App::buildInterfaceTest() {
    ShaderEditorUiTest& t = interfaceTest_;
    using Step = ShaderEditorUiTest::Step;

    const auto record = [this](const std::string& name, bool passed, const std::string& detail) {
        interfaceTest_.items.push_back({name, detail, passed});
        interfaceTest_.failed += passed ? 0 : 1;
        logLine("Interface UI test " + name + ": " + (passed ? "passed" : "FAILED") + " (" + detail + ")");
    };
    const auto add = [&t](std::function<void()> act, int frames, std::function<bool()> until = nullptr, std::function<void()> check = nullptr) {
        Step step;
        step.act = std::move(act);
        step.frames = frames;
        step.until = std::move(until);
        step.check = std::move(check);
        t.steps.push_back(std::move(step));
    };
    const auto press = [&add](ImGuiKey key, bool ctrl, bool shift) {
        add([key, ctrl, shift]() {
            ImGuiIO& io = ImGui::GetIO();
            if (ctrl) {
                io.AddKeyEvent(ImGuiMod_Ctrl, true);
            }
            if (shift) {
                io.AddKeyEvent(ImGuiMod_Shift, true);
            }
            io.AddKeyEvent(key, true);
            io.AddKeyEvent(key, false);
            if (shift) {
                io.AddKeyEvent(ImGuiMod_Shift, false);
            }
            if (ctrl) {
                io.AddKeyEvent(ImGuiMod_Ctrl, false);
            }
        }, 4);
    };
    const auto shot = [&add](const std::string& name) {
        add([name]() { saveShot(name); }, 1);
    };
    const auto resizeTo = [this, &add](int width, int height) {
        add([this, width, height]() { glfwSetWindowSize(window_, width, height); }, 40, [width, height]() {
            const ImVec2 size = ImGui::GetIO().DisplaySize;
            return std::fabs(size.x - static_cast<float>(width)) < 1.5f && std::fabs(size.y - static_cast<float>(height)) < 1.5f;
        });
        add(nullptr, 4);
    };
    const auto original = std::make_shared<Rects>();
    const auto messed = std::make_shared<Rects>();
    const auto baseFrame = std::make_shared<float>(0.0f);
    const auto basePadding = std::make_shared<float>(0.0f);

    add(nullptr, 20);
    add([this]() {
        settings_.uiScale = 1.0f;
        settings_.uiAutoScale = false;
        settings_.uiContrast = "standard";
        settings_.uiKeyboardNav = false;
        dpiOverride_ = 0.0f;
        shortcuts_.resetAll();
        needsLayout_ = true;
    }, 8);
    add(nullptr, 6, nullptr, [original, baseFrame, basePadding, record]() {
        *original = captureRects();
        std::string detail;
        const bool usable = allPanelsUsable(detail);
        record("the default layout shows the six panels inside the window", usable, detail);
        const ImGuiWindow* master = ImGui::FindWindowByName("###master");
        const ImGuiWindow* layer = ImGui::FindWindowByName("###layer");
        const ImGuiWindow* clip = ImGui::FindWindowByName("###clip");
        record("Master and Layer are tabs of one panel", layer && master && layer->DockId == master->DockId && master->DockId != 0, master ? "dock " + std::to_string(master->DockId) : "missing");
        record("the Clip panel sits to the right of Layer and Master", clip && master && clip->DockId != master->DockId && clip->Pos.x >= master->Pos.x + master->Size.x - 2.0f, clip ? describe(clip) : "missing");
        *baseFrame = measuredFrameHeight();
        *basePadding = ImGui::GetStyle().FramePadding.y;
    });
    shot("default-100");

    add([this]() {
        workspaceName_[0] = '\0';
        saveWorkspaceAs("uitest-one");
    }, 3, nullptr, [this, record]() {
        refreshWorkspaces();
        bool found = false;
        for (const workspace::Entry& entry : workspaces_) {
            if (entry.name == "uitest-one" && entry.valid && entry.width > 0 && entry.height > 0) {
                found = true;
            }
        }
        record("a workspace is saved and listed with its screen size", found, std::to_string(workspaces_.size()) + " entries");
    });

    add([messed]() {
        const ImGuiWindow* deck = panel(0);
        ImGui::DockBuilderDockWindow("###browser", deck->DockId);
        *messed = Rects{};
    }, 6, nullptr, [original, messed, record]() {
        *messed = captureRects();
        const ImGuiWindow* deck = panel(0);
        const ImGuiWindow* browser = panel(5);
        record("moving a panel into another dock changes the layout", deck && browser && browser->DockId == deck->DockId && browser->DockId != original->dock[5], browser ? "browser dock " + std::to_string(browser->DockId) : "missing");
    });
    shot("rearranged");

    add([this]() { requestWorkspace("uitest-one"); }, 8, nullptr, [original, record]() {
        std::string detail;
        const Rects now = captureRects();
        record("loading the workspace puts every panel back where it was", sameRects(*original, now, 2.0f, detail), detail);
    });

    add([this]() {
        writeText(fs::path(userDataDir()) / "workspaces" / "damaged.ini", "this is not a layout\n\x01\x02 garbage = = =\n");
        workspacesStale_ = true;
        refreshWorkspaces();
        requestWorkspace("damaged");
    }, 6, nullptr, [this, original, record]() {
        bool listedAsDamaged = false;
        for (const workspace::Entry& entry : workspaces_) {
            if (entry.name == "damaged" && !entry.valid && !entry.problem.empty()) {
                listedAsDamaged = true;
            }
        }
        std::string detail;
        const Rects now = captureRects();
        record("a damaged workspace file is listed as damaged", listedAsDamaged, std::to_string(workspaces_.size()) + " entries");
        record("loading it is refused and the layout stays", sameRects(*original, now, 2.0f, detail) && !workspaceMessage_.empty(), workspaceMessage_.empty() ? detail : workspaceMessage_);
    });

    add([this]() {
        ImGui::DockBuilderDockWindow("###browser", panel(0)->DockId);
    }, 6);
    add([this]() { resetWindowLayout(); }, 8, nullptr, [original, record]() {
        std::string detail;
        const Rects now = captureRects();
        record("Reset window layout restores the default arrangement", sameRects(*original, now, 2.0f, detail), detail);
    });

    add([this]() {
        timelineOpen_ = true;
        timelineDockRequest_ = 1;
    }, 10, nullptr, [record]() {
        const ImGuiWindow* timeline = ImGui::FindWindowByName("###timeline");
        const ImGuiWindow* deck = panel(0);
        const bool docked = timeline && timeline->DockId != 0 && deck && timeline->Pos.y >= deck->Pos.y + deck->Size.y - 2.0f && insideMainWindow(timeline, 2.0f);
        std::string detail;
        record("the timeline docks at the bottom of the window", docked && allPanelsUsable(detail), timeline ? describe(timeline) : "missing");
    });
    shot("timeline-bottom");
    add([this]() { timelineDockRequest_ = 2; }, 10, nullptr, [record]() {
        const ImGuiWindow* timeline = ImGui::FindWindowByName("###timeline");
        const ImGuiWindow* deck = panel(0);
        const bool docked = timeline && timeline->DockId != 0 && deck && timeline->Pos.x >= deck->Pos.x + deck->Size.x - 2.0f && insideMainWindow(timeline, 2.0f);
        std::string detail;
        record("the timeline docks on the right of the window", docked && allPanelsUsable(detail), timeline ? describe(timeline) : "missing");
    });
    shot("timeline-right");
    add([this]() { timelineDockRequest_ = 3; }, 10, nullptr, [original, record]() {
        const ImGuiWindow* timeline = ImGui::FindWindowByName("###timeline");
        std::string detail;
        record("the timeline can float again as a window", timeline && timeline->DockId == 0 && insideMainWindow(timeline, 2.0f) && allPanelsUsable(detail), timeline ? describe(timeline) : "missing");
        std::string sizes;
        const Rects now = captureRects();
        record("the other panels get their old sizes back when the timeline leaves its dock", sameRects(*original, now, 8.0f, sizes), sizes);
    });
    add([this]() { timelineDockRequest_ = 1; }, 10);
    add([this]() { timelineOpen_ = false; }, 10, nullptr, [original, record]() {
        std::string sizes;
        const Rects now = captureRects();
        record("the other panels get their old sizes back when the docked timeline is closed", sameRects(*original, now, 8.0f, sizes), sizes);
    });
    add([this]() {
        timelineOpen_ = false;
        resetWindowLayout();
    }, 8, nullptr, [original, record]() {
        std::string detail;
        const Rects now = captureRects();
        record("Reset window layout also takes the timeline out of the layout", sameRects(*original, now, 2.0f, detail), detail);
    });

    add([]() {
        ImGui::SetWindowFocus("###stripMaster");
    }, 4);
    add([this]() {
        const ImGuiWindow* strip = ImGui::FindWindowByName("###stripMaster");
        if (strip) {
            const float label = std::clamp((strip->ContentRegionRect.GetWidth()) * 0.36f, ui::px(56.0f), ui::px(150.0f));
            zoomMouse_ = ImVec2(strip->ContentRegionRect.Min.x + label + ui::px(140.0f), strip->ContentRegionRect.Min.y + ImGui::GetFrameHeight() * 0.5f);
            ImGui::GetIO().AddMousePosEvent(zoomMouse_.x, zoomMouse_.y);
        }
    }, 4);
    add([]() {
        ImGuiIO& io = ImGui::GetIO();
        io.AddKeyEvent(ImGuiMod_Ctrl, true);
        io.AddMouseButtonEvent(0, true);
    }, 2);
    add([]() {
        ImGuiIO& io = ImGui::GetIO();
        io.AddMouseButtonEvent(0, false);
        io.AddKeyEvent(ImGuiMod_Ctrl, false);
    }, 4, nullptr, [record]() {
        const ImGuiID active = ImGui::GetActiveID();
        record("Ctrl+click on a slider opens the field for typing a value", active != 0 && ImGui::TempInputIsActive(active) && ImGui::GetIO().WantTextInput, "active id " + std::to_string(active));
    });
    shot("slider-typing");
    press(ImGuiKey_Escape, false, false);
    add(nullptr, 4, nullptr, [record]() {
        record("Esc closes the field and leaves the value as it was", ImGui::GetActiveID() == 0 || !ImGui::TempInputIsActive(ImGui::GetActiveID()), "active id " + std::to_string(ImGui::GetActiveID()));
    });
    add([this]() {
        engine_.comp.master.level = 0.5f;
        const ImGuiWindow* strip = ImGui::FindWindowByName("###stripMaster");
        if (strip) {
            const float label = std::clamp((strip->ContentRegionRect.GetWidth()) * 0.36f, ui::px(56.0f), ui::px(150.0f));
            const float valueWidth = std::max(ui::px(52.0f), ImGui::CalcTextSize("0.50").x + ui::px(14.0f));
            zoomMouse_ = ImVec2(strip->ContentRegionRect.Min.x + label + valueWidth + ui::px(33.0f), strip->ContentRegionRect.Min.y + ImGui::GetFrameHeight() * 0.5f);
            ImGui::GetIO().AddMousePosEvent(zoomMouse_.x, zoomMouse_.y);
        }
    }, 4);
    add([]() { ImGui::GetIO().AddMouseButtonEvent(0, true); }, 2);
    add([]() { ImGui::GetIO().AddMouseButtonEvent(0, false); }, 4, nullptr, [this, record]() {
        const float level = engine_.comp.master.level;
        record("the plus field next to a slider raises it by one percent of the range or more", level > 0.505f && level < 0.9f, std::to_string(level));
    });
    add([this]() { engine_.comp.master.level = 1.0f; }, 2);

    add([this]() { openSettings(SettingsAppearance); }, 8, nullptr, [this, record]() {
        const ImGuiWindow* window = ImGui::FindWindowByName("###settingsWindow");
        record("the Settings window opens inside the main window", window && insideMainWindow(window, 2.0f), describe(window));
        record("the Settings window has no close button in its title bar", window && !window->HasCloseButton, describe(window));
    });
    shot("settings-appearance-100");
    add([]() { ImGui::SetWindowFocus("###settingsWindow"); }, 3);
    press(ImGuiKey_Escape, false, false);
    add(nullptr, 3, nullptr, [this, record]() {
        record("Esc closes the Settings window", !settingsOpen_, settingsOpen_ ? "still open" : "closed");
    });
    add([this]() { settingsOpen_ = false; }, 3);

    add([this]() {
        blackoutBefore_ = engine_.blackout;
    }, 1);
    press(ImGuiKey_B, false, false);
    add(nullptr, 2, nullptr, [this, record]() {
        record("the key B toggles the blackout with the default shortcut", engine_.blackout != blackoutBefore_, engine_.blackout ? "on" : "off");
    });
    press(ImGuiKey_B, false, false);
    add(nullptr, 2, nullptr, [this, record]() {
        record("pressing B again switches it back", engine_.blackout == blackoutBefore_, engine_.blackout ? "on" : "off");
    });

    add([this]() {
        sc::Binding binding;
        binding.key = "K";
        binding.ctrl = true;
        shortcuts_.set(sc::Action::Blackout, binding);
        blackoutBefore_ = engine_.blackout;
    }, 1);
    press(ImGuiKey_B, false, false);
    add(nullptr, 2, nullptr, [this, record]() {
        record("after moving Blackout to Ctrl+K the old key B does nothing", engine_.blackout == blackoutBefore_, engine_.blackout ? "on" : "off");
    });
    press(ImGuiKey_K, false, false);
    add(nullptr, 2, nullptr, [this, record]() {
        record("K without Ctrl does nothing", engine_.blackout == blackoutBefore_, engine_.blackout ? "on" : "off");
    });
    press(ImGuiKey_K, true, true);
    add(nullptr, 2, nullptr, [this, record]() {
        record("Ctrl+Shift+K does nothing because the modifiers must match", engine_.blackout == blackoutBefore_, engine_.blackout ? "on" : "off");
    });
    press(ImGuiKey_K, true, false);
    add(nullptr, 2, nullptr, [this, record]() {
        record("Ctrl+K toggles the blackout", engine_.blackout != blackoutBefore_, engine_.blackout ? "on" : "off");
    });
    press(ImGuiKey_K, true, false);
    add([this]() { shortcuts_.resetAll(); }, 2, nullptr, [this, record]() {
        record("Ctrl+K again switches it off and the default can be restored", !engine_.blackout && shortcuts_.isDefault(), engine_.blackout ? "on" : "off");
    });

    add([this]() {
        fs::remove(fs::path(shortcutsPath_));
        openSettings(SettingsShortcuts);
    }, 8);
    shot("settings-shortcuts-100");
    add([this]() {
        shortcutCapture_ = static_cast<int>(sc::Action::Blackout);
    }, 3);
    press(ImGuiKey_J, true, false);
    add(nullptr, 4, nullptr, [this, record]() {
        const sc::Binding& binding = shortcuts_.get(sc::Action::Blackout);
        record("capturing a key combination in the Shortcuts page sets it", binding.key == "J" && binding.ctrl && !binding.shift && !binding.alt && shortcutCapture_ == -1, sc::format(binding));
        const std::string file = readText(fs::path(shortcutsPath_));
        record("the new shortcut is saved to shortcuts.json", file.find("Ctrl+J") != std::string::npos, std::to_string(file.size()) + " bytes");
    });
    add([this]() { shortcutCapture_ = static_cast<int>(sc::Action::Tap); }, 3);
    press(ImGuiKey_J, true, false);
    add(nullptr, 4, nullptr, [this, record]() {
        const sc::Binding& binding = shortcuts_.get(sc::Action::Tap);
        record("a shortcut that is already used is refused with a message", binding.key == "Space" && !shortcutMessage_.empty(), shortcutMessage_);
    });
    add([this]() { shortcutCapture_ = static_cast<int>(sc::Action::Tap); }, 3);
    press(ImGuiKey_3, false, false);
    add(nullptr, 4, nullptr, [this, record]() {
        const sc::Binding& binding = shortcuts_.get(sc::Action::Tap);
        record("a reserved key (the number keys) is refused", binding.key == "Space" && !shortcutMessage_.empty(), shortcutMessage_);
    });
    add([this]() { shortcutCapture_ = static_cast<int>(sc::Action::Tap); }, 3);
    press(ImGuiKey_Escape, false, false);
    add(nullptr, 4, nullptr, [this, record]() {
        record("Esc cancels the capture and keeps the shortcut", shortcutCapture_ == -1 && shortcuts_.get(sc::Action::Tap).key == "Space", sc::format(shortcuts_.get(sc::Action::Tap)));
    });
    add([this]() {
        shortcuts_.resetAll();
        saveShortcuts();
        settingsOpen_ = false;
        shortcutCapture_ = -1;
        shortcutMessage_.clear();
    }, 3);

    add([this, record]() {
        const std::string newer = "{\"version\": 999, \"bindings\": {\"blackout\": \"K\"}}\n";
        writeText(fs::path(shortcutsPath_), newer);
        loadShortcuts();
        const bool defaults = shortcuts_.isDefault();
        sc::Binding binding;
        binding.key = "J";
        binding.ctrl = true;
        shortcuts_.set(sc::Action::Blackout, binding);
        saveShortcuts();
        const fs::path copy = fs::path(shortcutsPath_ + ".bad");
        const bool kept = fs::exists(copy) && readText(copy) == newer;
        const bool written = readText(fs::path(shortcutsPath_)).find("\"version\": 1") != std::string::npos;
        record("a shortcut file from a newer version is ignored and the defaults apply", defaults, defaults ? "defaults" : "not defaults");
        record("changing a shortcut first keeps a copy of the newer file as shortcuts.json.bad", kept && written, std::string("copy ") + (kept ? "kept" : "missing") + ", new file " + (written ? "written" : "missing"));
        shortcuts_.resetAll();
        saveShortcuts();
        std::error_code ec;
        fs::remove(copy, ec);
    }, 3);

    add([this]() { settings_.uiScale = 1.0f; uiScaleEdit_ = 1.0f; }, 3);
    press(ImGuiKey_Equal, true, false);
    add(nullptr, 3, nullptr, [this, record]() {
        record("Ctrl+= makes the interface larger by 10 percent points", std::fabs(settings_.uiScale - 1.1f) < 0.001f, std::to_string(settings_.uiScale));
    });
    press(ImGuiKey_Minus, true, false);
    press(ImGuiKey_Minus, true, false);
    add(nullptr, 3, nullptr, [this, record]() {
        record("Ctrl+- twice makes it smaller", std::fabs(settings_.uiScale - 0.9f) < 0.001f, std::to_string(settings_.uiScale));
    });
    press(ImGuiKey_0, true, false);
    add(nullptr, 3, nullptr, [this, record]() {
        record("Ctrl+0 brings it back to 100 percent", std::fabs(settings_.uiScale - 1.0f) < 0.001f, std::to_string(settings_.uiScale));
    });

    for (const float scale : {1.25f, 1.5f, 2.0f}) {
        add([this, scale]() {
            settings_.uiScale = scale;
            settings_.uiAutoScale = false;
            uiScaleEdit_ = scale;
        }, 4, nullptr, [baseFrame, basePadding, scale, record]() {
            const ImGuiStyle& style = ImGui::GetStyle();
            const float frame = measuredFrameHeight();
            const float ratio = *baseFrame > 0.0f ? frame / *baseFrame : 0.0f;
            const float paddingRatio = *basePadding > 0.0f ? style.FramePadding.y / *basePadding : 0.0f;
            char text[160];
            std::snprintf(text, sizeof(text), "frame %.1f against %.1f is x%.2f, padding x%.2f", frame, *baseFrame, ratio, paddingRatio);
            record("interface size " + std::to_string(static_cast<int>(scale * 100.0f)) + " percent scales the controls", std::fabs(ratio - scale) < scale * 0.06f && std::fabs(style.FramePadding.y - *basePadding * scale) <= 1.0f && std::fabs(style.FontScaleMain - scale) < 0.001f, text);
        });
    }
    add([this]() { settings_.uiScale = 1.0f; uiScaleEdit_ = 1.0f; }, 3);

    for (const float dpi : {1.25f, 1.5f, 2.0f}) {
        add([this, dpi]() {
            settings_.uiAutoScale = true;
            dpiOverride_ = dpi;
        }, 4, nullptr, [this, baseFrame, basePadding, dpi, record]() {
            const ImGuiStyle& style = ImGui::GetStyle();
            const float frame = measuredFrameHeight();
            const float ratio = *baseFrame > 0.0f ? frame / *baseFrame : 0.0f;
            const float paddingRatio = *basePadding > 0.0f ? style.FramePadding.y / *basePadding : 0.0f;
            char text[160];
            std::snprintf(text, sizeof(text), "display scale %.2f, frame x%.2f, padding x%.2f", dpiScale_, ratio, paddingRatio);
            record("a display scale of " + std::to_string(static_cast<int>(dpi * 100.0f)) + " percent is followed when Match the display scale is on", std::fabs(dpiScale_ - dpi) < 0.001f && std::fabs(ratio - dpi) < dpi * 0.06f && std::fabs(style.FramePadding.y - *basePadding * dpi) <= 1.0f, text);
        });
    }
    add([this]() {
        settings_.uiAutoScale = false;
        dpiOverride_ = 0.0f;
    }, 4, nullptr, [this, record]() {
        record("with Match the display scale off the display scale is ignored", std::fabs(ImGui::GetStyle().FontScaleDpi - 1.0f) < 0.001f, std::to_string(ImGui::GetStyle().FontScaleDpi));
    });

    struct Combo {
        int width;
        int height;
        float scale;
    };
    const Combo combos[] = {
        {1024, 600, 1.0f}, {1024, 600, 1.5f}, {1024, 600, 2.0f},
        {1280, 720, 1.0f}, {1280, 720, 1.5f}, {1280, 720, 2.0f},
        {1600, 900, 1.25f}, {1600, 900, 2.0f},
        {1920, 1080, 1.0f}, {1920, 1080, 1.5f}, {1920, 1080, 2.0f},
        {2560, 1440, 1.5f}, {2560, 1440, 2.0f},
    };
    for (const Combo& combo : combos) {
        const std::string label = std::to_string(combo.width) + "x" + std::to_string(combo.height) + " at " + std::to_string(static_cast<int>(combo.scale * 100.0f)) + " percent";
        resizeTo(combo.width, combo.height);
        add([this, combo]() {
            settings_.uiScale = combo.scale;
            uiScaleEdit_ = combo.scale;
            resetWindowLayout();
        }, 8, nullptr, [label, record]() {
            std::string detail;
            const bool usable = allPanelsUsable(detail);
            record("the default layout is usable at " + label, usable, detail);
        });
        add([this]() { openSettings(SettingsAppearance); }, 8, nullptr, [label, record]() {
            const ImGuiWindow* window = ImGui::FindWindowByName("###settingsWindow");
            record("the Settings window fits at " + label, window && insideMainWindow(window, 2.0f) && window->Size.x >= 200.0f && window->Size.y >= 120.0f, describe(window));
        });
        shot("settings-" + std::to_string(combo.width) + "x" + std::to_string(combo.height) + "-" + std::to_string(static_cast<int>(combo.scale * 100.0f)));
        add([this]() { settingsOpen_ = false; openManual(std::string()); }, 8, nullptr, [label, record]() {
            const ImGuiWindow* window = ImGui::FindWindowByName("###manualWindow");
            record("the manual window fits at " + label, window && insideMainWindow(window, 2.0f) && window->Size.x >= 200.0f && window->Size.y >= 120.0f, describe(window));
        });
        add([this]() { manualOpen_ = false; }, 3);
        shot("main-" + std::to_string(combo.width) + "x" + std::to_string(combo.height) + "-" + std::to_string(static_cast<int>(combo.scale * 100.0f)));
    }
    add([this]() { settings_.uiScale = 1.0f; uiScaleEdit_ = 1.0f; }, 3);
    resizeTo(1100, 650);
    add([this]() { requestWorkspace("uitest-one"); }, 10, nullptr, [record]() {
        std::string detail;
        const bool usable = allPanelsUsable(detail);
        record("a workspace saved on a larger window loads into a smaller one and stays inside it", usable, detail);
    });
    resizeTo(1600, 900);
    add([this]() { resetWindowLayout(); }, 8);

    add([this]() {
        settings_.uiContrast = "high";
    }, 4, nullptr, [record]() {
        const ImGuiStyle& style = ImGui::GetStyle();
        record("the high contrast option switches the style", ui::kHighContrast && style.Colors[ImGuiCol_Text].x > 0.99f && style.Colors[ImGuiCol_NavCursor].x > 0.9f && style.Colors[ImGuiCol_NavCursor].z < 0.5f, "text " + std::to_string(style.Colors[ImGuiCol_Text].x) + ", focus ring blue " + std::to_string(style.Colors[ImGuiCol_NavCursor].z));
    });
    add([this]() { openSettings(SettingsAppearance); }, 8);
    shot("settings-high-contrast-100");
    add([this]() { settingsOpen_ = false; settings_.uiContrast = "standard"; }, 4, nullptr, [record]() {
        record("and switching it off restores the standard style", !ui::kHighContrast && ImGui::GetStyle().Colors[ImGuiCol_Text].x < 0.95f, std::to_string(ImGui::GetStyle().Colors[ImGuiCol_Text].x));
    });

    add([this]() {
        settings_.uiKeyboardNav = true;
        shortcuts_.set(sc::Action::Tap, sc::Binding());
        sc::Binding space;
        space.key = "Space";
        shortcuts_.set(sc::Action::Blackout, space);
        ImGui::SetWindowFocus("###trigger");
        blackoutBefore_ = engine_.blackout;
        navSeen_.clear();
    }, 6, nullptr, [record]() {
        record("keyboard navigation turns the navigation flag on", (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_NavEnableKeyboard) != 0, "flags " + std::to_string(ImGui::GetIO().ConfigFlags));
    });
    for (int i = 0; i < 6; ++i) {
        press(ImGuiKey_Tab, false, false);
        add([this]() {
            navSeen_.push_back(ImGui::GetCurrentContext()->NavId);
        }, 1);
    }
    add(nullptr, 2, nullptr, [this, record]() {
        std::set<ImGuiID> distinct(navSeen_.begin(), navSeen_.end());
        distinct.erase(0);
        record("Tab moves the focus through different controls", distinct.size() >= 4, std::to_string(distinct.size()) + " different controls in 6 presses" + " in " + std::string(ImGui::GetCurrentContext()->NavWindow ? ImGui::GetCurrentContext()->NavWindow->Name : "no window"));
        record("the focus ring is visible while navigating with the keyboard", ImGui::GetIO().NavActive && ImGui::GetIO().NavVisible, std::string("active ") + (ImGui::GetIO().NavActive ? "yes" : "no") + ", visible " + (ImGui::GetIO().NavVisible ? "yes" : "no"));
    });
    shot("keyboard-focus");
    add([this]() { blackoutBefore_ = engine_.blackout; }, 1);
    press(ImGuiKey_Space, false, false);
    add(nullptr, 3, nullptr, [this, record]() {
        record("Space does not fire a shortcut while a control has the keyboard focus", engine_.blackout == blackoutBefore_, engine_.blackout ? "blackout on" : "blackout off");
    });
    auto afterFirstEsc = std::make_shared<std::string>();
    press(ImGuiKey_Escape, false, false);
    add(nullptr, 3, nullptr, [afterFirstEsc]() {
        const ImGuiIO& io = ImGui::GetIO();
        *afterFirstEsc = std::string("after the first Esc: text input ") + (io.WantTextInput ? "yes" : "no") + ", focus ring " + (io.NavVisible ? "yes" : "no");
    });
    press(ImGuiKey_Escape, false, false);
    add([this]() { blackoutBefore_ = engine_.blackout; }, 3);
    press(ImGuiKey_Space, false, false);
    add(nullptr, 3, nullptr, [this, record, afterFirstEsc]() {
        record("after Esc leaves the control Space fires the shortcut again", engine_.blackout != blackoutBefore_, std::string(engine_.blackout ? "blackout on" : "blackout off") + "; " + *afterFirstEsc);
    });
    add([this]() {
        engine_.blackout = false;
        settings_.uiKeyboardNav = false;
        shortcuts_.resetAll();
    }, 6, nullptr, [record]() {
        record("turning keyboard navigation off clears the navigation flag", (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_NavEnableKeyboard) == 0, "flags " + std::to_string(ImGui::GetIO().ConfigFlags));
    });

    add([this]() {
        openNodeEditorNew();
    }, 20, [this]() { return nodes_ && nodes_->canvasSize.x > 50.0f && nodes_->hasGraph; });
    add(nullptr, 6);
    add(nullptr, 1, nullptr, [this, record]() {
        record("the node editor opens with a canvas at 100 percent zoom", nodes_ && nodes_->hasGraph && std::fabs(nodes_->zoom - 1.0f) < 0.001f, nodes_ ? std::to_string(nodes_->zoom) : "no editor");
    });
    shot("node-editor-100");
    add([this]() {
        const NodeEditorState& s = *nodes_;
        zoomMouse_ = ImVec2(s.canvasOrigin.x + s.canvasSize.x * 0.3f, s.canvasOrigin.y + s.canvasSize.y * 0.4f);
        const float k = std::clamp(ImGui::GetFontSize() / 16.0f, 0.75f, 2.5f) * s.zoom;
        zoomPoint_ = ImVec2((zoomMouse_.x - s.canvasOrigin.x - s.pan.x) / k, (zoomMouse_.y - s.canvasOrigin.y - s.pan.y) / k);
        ImGuiIO& io = ImGui::GetIO();
        io.AddMousePosEvent(zoomMouse_.x, zoomMouse_.y);
    }, 4);
    add([]() {
        ImGuiIO& io = ImGui::GetIO();
        io.AddKeyEvent(ImGuiMod_Ctrl, true);
        io.AddMouseWheelEvent(0.0f, 1.0f);
    }, 3);
    add([]() { ImGui::GetIO().AddKeyEvent(ImGuiMod_Ctrl, false); }, 3, nullptr, [this, record]() {
        const NodeEditorState& s = *nodes_;
        const float k = std::clamp(ImGui::GetFontSize() / 16.0f, 0.75f, 2.5f) * s.zoom;
        const ImVec2 now((zoomMouse_.x - s.canvasOrigin.x - s.pan.x) / k, (zoomMouse_.y - s.canvasOrigin.y - s.pan.y) / k);
        record("Ctrl and the mouse wheel zoom the node canvas in", std::fabs(s.zoom - 1.1f) < 0.01f, "zoom " + std::to_string(s.zoom));
        record("the point under the mouse stays under the mouse while zooming", std::fabs(now.x - zoomPoint_.x) < 1.0f && std::fabs(now.y - zoomPoint_.y) < 1.0f, "moved by " + std::to_string(now.x - zoomPoint_.x) + ", " + std::to_string(now.y - zoomPoint_.y));
    });
    shot("node-editor-zoomed-in");
    add([]() {
        ImGuiIO& io = ImGui::GetIO();
        io.AddKeyEvent(ImGuiMod_Ctrl, true);
        for (int i = 0; i < 30; ++i) {
            io.AddMouseWheelEvent(0.0f, -1.0f);
        }
    }, 40);
    add([]() { ImGui::GetIO().AddKeyEvent(ImGuiMod_Ctrl, false); }, 3, nullptr, [this, record]() {
        record("zooming out stops at the smallest zoom", std::fabs(nodes_->zoom - kNodeZoomMin) < 0.001f, "zoom " + std::to_string(nodes_->zoom));
    });
    shot("node-editor-zoomed-out");
    add([]() {
        ImGuiIO& io = ImGui::GetIO();
        io.AddKeyEvent(ImGuiMod_Ctrl, true);
        for (int i = 0; i < 60; ++i) {
            io.AddMouseWheelEvent(0.0f, 1.0f);
        }
    }, 70);
    add([]() { ImGui::GetIO().AddKeyEvent(ImGuiMod_Ctrl, false); }, 3, nullptr, [this, record]() {
        record("zooming in stops at the largest zoom", std::fabs(nodes_->zoom - kNodeZoomMax) < 0.001f, "zoom " + std::to_string(nodes_->zoom));
    });
    press(ImGuiKey_0, false, false);
    add(nullptr, 3, nullptr, [this, record]() {
        record("the key 0 resets the canvas zoom to 100 percent", std::fabs(nodes_->zoom - 1.0f) < 0.001f, "zoom " + std::to_string(nodes_->zoom));
    });
    press(ImGuiKey_Minus, false, false);
    add(nullptr, 3, nullptr, [this, record]() {
        record("the key - zooms the canvas out", nodes_->zoom < 1.0f, "zoom " + std::to_string(nodes_->zoom));
    });
    press(ImGuiKey_Equal, false, false);
    press(ImGuiKey_Equal, false, false);
    add(nullptr, 3, nullptr, [this, record]() {
        record("the key = zooms the canvas in", nodes_->zoom > 1.0f, "zoom " + std::to_string(nodes_->zoom));
    });
    add([this]() {
        nodesOpen_ = false;
        const fs::path graphs = fs::path(userDataDir()) / "nodes";
        std::error_code ec;
        fs::remove(graphs / "New effect.nyxfx", ec);
    }, 4);

    const auto resoDir = std::make_shared<fs::path>(fs::path(userDataDir()) / "resoport_uitest");
    add([this, resoDir]() {
        std::error_code ec;
        fs::remove_all(*resoDir, ec);
        fs::create_directories(*resoDir, ec);
        std::vector<unsigned char> pixels(8 * 8 * 3, 0);
        for (size_t i = 0; i < pixels.size(); i += 3) {
            pixels[i] = 220;
        }
        stbi_write_png(pathToUtf8(*resoDir / "Red Square.png").c_str(), 8, 8, 3, pixels.data(), 24);
        std::ofstream avc(*resoDir / "demo.avc", std::ios::binary);
        avc << "<?xml version='1.0' encoding='utf-8'?>\n"
               "<Composition name=\"Demo Import\" numDecks=\"1\" numLayers=\"2\" numColumns=\"3\">\n"
               "<versionInfo name=\"Resolume Arena\" majorVersion=\"7\" minorVersion=\"22\" />\n"
               "<CompositionInfo name=\"Demo Import\" width=\"1280\" height=\"720\" />\n"
               "<Layer layerIndex=\"0\"><VideoTrack><ChoosableMixer name=\"Blend\"><RenderPass type=\"Add\" baseType=\"Mixer\"><Params><ParamRange name=\"Opacity\" value=\"0.5\" /></Params></RenderPass></ChoosableMixer></VideoTrack></Layer>\n"
               "<Layer layerIndex=\"1\"><VideoTrack><ChoosableMixer name=\"Blend\"><RenderPass type=\"Alpha\" baseType=\"Mixer\" /></ChoosableMixer></VideoTrack></Layer>\n"
               "<Deck><Params><Param name=\"Name\" value=\"Opener\" /></Params>\n"
               "<Clip layerIndex=\"0\" columnIndex=\"0\"><VideoTrack><PrimarySource><VideoSource type=\"VideoFormatReaderSource\" fileName=\"C:\\Elsewhere\\Red Square.png\" /></PrimarySource></VideoTrack></Clip>\n"
               "<Clip layerIndex=\"1\" columnIndex=\"1\"><VideoTrack><PrimarySource><VideoSource type=\"VideoFormatReaderSource\" fileName=\"C:\\Elsewhere\\gone.mp4\" /></PrimarySource></VideoTrack></Clip>\n"
               "<Clip layerIndex=\"1\" columnIndex=\"2\"><VideoTrack><PrimarySource><VideoSource type=\"GeneratorVideoSource\" /></PrimarySource></VideoTrack></Clip>\n"
               "</Deck></Composition>\n";
        avc.close();
        queueDroppedFiles({pathToUtf8(*resoDir / "demo.avc")});
    }, 400, [this]() { return resoPort_.checked; }, [this, record]() {
        const resoport::Report& r = resoPort_.preview;
        record("dropping an .avc file opens ResoPort and checks it", resoPort_.open && resoPort_.checked && resoPort_.check.document.name == "Demo Import", resoPort_.message);
        saveShot("resoport-check");
        record("ResoPort finds the media next to the file and reports what is missing", r.clipsImported == 1 && r.clipsMissing == 1 && r.clipsGenerator == 1 && r.missing.size() == 1 && r.missing[0] == "gone.mp4", std::to_string(r.clipsImported) + " found, " + std::to_string(r.clipsMissing) + " missing, " + std::to_string(r.clipsGenerator) + " generators");
    });
    add([this]() {
        startResoPortImport();
    }, 600, [this]() { return !resoPort_.importJob.valid() && resoPort_.messageGood; }, [this, record, resoDir]() {
        saveShot("resoport-imported");
        const Composition& comp = engine_.comp;
        const int clip = clipIndexByName(imageClipName("Red Square"));
        const bool placed = clip >= 0 && comp.layers[0].cells[0] == clip;
        record("importing a Resolume composition builds the composition", comp.name == "Demo Import" && placed && comp.layers[1].cells[1] == -1 && comp.deckNames[0] == "Opener", comp.name + ", clip " + std::to_string(clip));
        record("the Resolume layer blend modes and opacity are taken over", comp.layers[0].blend == BlendAdd && std::fabs(comp.layers[0].opacity - 0.5f) < 0.001f && comp.layers[1].blend == BlendNormal && comp.resolution == resolutionIndexFor(1280, 720), "blend " + std::to_string(comp.layers[0].blend) + ", opacity " + std::to_string(comp.layers[0].opacity));
        std::error_code ec;
        record("the media file was copied into the library", fs::exists(pathFromUtf8(mediaDirectory()) / "Red Square.png", ec), mediaDirectory());
    });
    add([this, resoDir]() {
        resoPort_.open = false;
        loadComposition(Composition{}, std::string());
        std::error_code ec;
        fs::remove_all(*resoDir, ec);
        fs::remove(pathFromUtf8(mediaDirectory()) / "Red Square.png", ec);
    }, 6);

    const auto hidden = std::make_shared<std::array<double, 2>>();
    add([this, hidden]() {
        (*hidden)[0] = static_cast<double>(engine_.frameCounter());
        (*hidden)[1] = engine_.time();
        for (int i = 0; i < 30; ++i) {
            frame(1.0 / 60.0, false);
        }
    }, 3, nullptr, [this, hidden, record]() {
        const double frames = static_cast<double>(engine_.frameCounter()) - (*hidden)[0];
        const double seconds = engine_.time() - (*hidden)[1];
        record("the engine keeps rendering while no interface is drawn", frames >= 30.0, std::to_string(static_cast<int>(frames)) + " frames");
        record("the show clock keeps running while no interface is drawn", seconds > 0.4, std::to_string(seconds) + " s");
    });
    add([this]() {
        std::error_code ec;
        fs::remove(fs::path(userDataDir()) / "workspaces" / "damaged.ini", ec);
        fs::remove(fs::path(userDataDir()) / "workspaces" / "uitest-one.ini", ec);
        workspacesStale_ = true;
        settings_.uiScale = 1.0f;
        settings_.uiAutoScale = false;
        saveSettings(settings_);
    }, 4);
}

}
