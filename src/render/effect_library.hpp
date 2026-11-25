#pragma once

#include "core/composition.hpp"
#include "render/shader_source.hpp"

#include <array>
#include <memory>
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
    bool isGraph = false;
    bool isIsf = false;
    bool usesMacros = false;
    int graphSlot = -1;
    int textureReads = 0;
    std::shared_ptr<const ShaderPlan> plan{};
    std::string path{};
    std::string error{};
    long long stamp = 0;
    unsigned long long bytes = 0;
    unsigned long long hash = 0;

    int paramCount() const;
};

struct NodeScan {
    std::vector<size_t> changed;
    int added = 0;
};

constexpr size_t kMaxIsfEffects = 64;

struct IsfScan {
    std::vector<size_t> changed;
    int added = 0;
};

NodeScan refreshNodeEffects(const std::string& forceName = std::string());
IsfScan refreshIsfEffects();
std::string isfEffectName(const std::string& stem);
std::string assembleEffectSource(const std::string& body);
int registerEffect(const std::string& name, const std::string& body, bool usesPrevious, const std::array<float, kParamCount>& defaults, bool usesMacros = false);

const std::vector<EffectDef>& effectLibrary();
int effectIndexByName(const std::string& name);
void setEffect(EffectSlot& slot, int effectIndex);
const std::string& effectFragmentSource(int effectIndex);
const char* gradeGlsl();

}
