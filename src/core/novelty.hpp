#pragma once

#include <string>

namespace nvj {

bool isNewClip(const std::string& name);
bool isNewEffect(const std::string& name);
bool isNewPreset(const std::string& name);

bool isNewClipIn(const std::string& name, const std::string& appVersion);
bool isNewEffectIn(const std::string& name, const std::string& appVersion);
bool isNewPresetIn(const std::string& name, const std::string& appVersion);

}
