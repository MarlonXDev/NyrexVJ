#include "app/shortcut_keys.hpp"

#include "core/shortcuts.hpp"

#include <cstdlib>

namespace nvj {

ImGuiKey imguiKeyFor(const std::string& name) {
    if (name.size() == 1 && name[0] >= 'A' && name[0] <= 'Z') {
        return static_cast<ImGuiKey>(ImGuiKey_A + (name[0] - 'A'));
    }
    if (name.size() == 1 && name[0] >= '0' && name[0] <= '9') {
        return static_cast<ImGuiKey>(ImGuiKey_0 + (name[0] - '0'));
    }
    if (name.size() >= 2 && name.size() <= 3 && name[0] == 'F') {
        const int number = std::atoi(name.c_str() + 1);
        if (number >= 1 && number <= 12) {
            return static_cast<ImGuiKey>(ImGuiKey_F1 + (number - 1));
        }
    }
    if (name == "Space") {
        return ImGuiKey_Space;
    }
    if (name == "Comma") {
        return ImGuiKey_Comma;
    }
    if (name == "Period") {
        return ImGuiKey_Period;
    }
    if (name == "Minus") {
        return ImGuiKey_Minus;
    }
    if (name == "Equal") {
        return ImGuiKey_Equal;
    }
    if (name == "Slash") {
        return ImGuiKey_Slash;
    }
    if (name == "Semicolon") {
        return ImGuiKey_Semicolon;
    }
    if (name == "PageUp") {
        return ImGuiKey_PageUp;
    }
    if (name == "PageDown") {
        return ImGuiKey_PageDown;
    }
    if (name == "Home") {
        return ImGuiKey_Home;
    }
    if (name == "End") {
        return ImGuiKey_End;
    }
    if (name == "Insert") {
        return ImGuiKey_Insert;
    }
    return ImGuiKey_None;
}

std::string capturedKeyName(bool* escape) {
    if (escape) {
        *escape = false;
    }
    const ImGuiIO& io = ImGui::GetIO();
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) && !io.KeyCtrl && !io.KeyShift && !io.KeyAlt) {
        if (escape) {
            *escape = true;
        }
        return std::string();
    }
    for (const std::string& name : shortcuts::keyNames()) {
        const ImGuiKey key = imguiKeyFor(name);
        if (key != ImGuiKey_None && ImGui::IsKeyPressed(key, false)) {
            return name;
        }
    }
    return std::string();
}

}
