#pragma once

#include "render/shader_source.hpp"

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace nvj {

constexpr size_t kMaxCustomTransitions = 32;

struct TransitionDef {
    std::string name;
    std::string path;
    long long stamp = 0;
    unsigned long long bytes = 0;
    unsigned long long version = 1;
    bool shadertoy = false;
    std::string error;
    std::shared_ptr<const ShaderPlan> plan;

    bool usable() const { return plan && error.empty(); }
};

struct TransitionScan {
    int added = 0;
    std::vector<size_t> changed;
};

std::string transitionClipName(const std::string& stem);
const std::vector<TransitionDef>& transitionLibrary();
TransitionScan refreshCustomTransitions();
int customTransitionIndex(const std::string& name);
const char* exampleTransitionText();
std::string createExampleTransition();

}
