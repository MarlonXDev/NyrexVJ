#pragma once

#include "core/composition.hpp"

#include <string>
#include <vector>

namespace nvj {

std::string compositionToJson(const Composition& comp);
bool compositionFromJson(const std::string& text, Composition& comp, std::string& error, std::vector<std::string>* warnings = nullptr);

const std::vector<Composition>& factoryPresets();
int factoryPresetIndex(const std::string& name);
const std::vector<int>& factoryBank();
Composition defaultComposition();

std::string sanitizePresetName(const std::string& name);
std::string presetsDirectory();
std::vector<std::string> listUserPresets();
bool saveUserPreset(const Composition& comp, const std::string& name, std::string& error);
bool loadUserPreset(const std::string& name, Composition& comp, std::string& error);
bool deleteUserPreset(const std::string& name);

bool saveSession(const Composition& comp);
bool loadSession(Composition& comp);

}
