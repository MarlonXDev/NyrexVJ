#pragma once

#include <imgui.h>

#include <string>

namespace nvj {

ImGuiKey imguiKeyFor(const std::string& name);
std::string capturedKeyName(bool* escape);

}
