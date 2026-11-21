#pragma once

#include <cstdint>
#include <functional>
#include <string>

namespace nvj::redirect {

struct Hooks {
    std::function<std::string(const std::string&)> lookup;
    std::function<void(const std::string&, const std::string&)> markBad;
    std::function<std::uint64_t()> generation;
};

void install(Hooks hooks);
void uninstall();
bool installed();
std::string lookup(const std::string& original);
void markBad(const std::string& original, const std::string& reason);
std::uint64_t generation();

}
