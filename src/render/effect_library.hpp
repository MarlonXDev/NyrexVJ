#pragma once

#include "core/composition.hpp"

#include <array>
#include <string>
#include <vector>

namespace nvj {

struct EffectDef {
    std::string name;
    std::array<const char*, kParamCount> paramNames;
    std::array<float, kParamCount> defaults;
    std::string body;
    bool is3d = false;
    bool usesPrevious = false;

    int paramCount() const;
};

const std::vector<EffectDef>& effectLibrary();
int effectIndexByName(const std::string& name);
void setEffect(EffectSlot& slot, int effectIndex);
const std::string& effectFragmentSource(int effectIndex);
const char* gradeGlsl();

}
