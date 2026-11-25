#pragma once

#include "core/composition.hpp"

#include <array>
#include <filesystem>
#include <string>
#include <vector>

namespace nvj::chain {

constexpr const char* kFormat = "nyrexvj-chain";
constexpr int kVersion = 1;
constexpr const char* kExtension = ".nyxchain";
constexpr const char* kFolder = "chains";
constexpr size_t kMaxDescriptionBytes = 200;

using Slots = std::array<EffectSlot, kEffectSlots>;

struct ChainDoc {
    std::string description;
    Slots slots{};
    std::vector<std::string> missing;
};

struct ChainInfo {
    std::string name;
    std::string description;
    std::vector<std::string> effects;
    bool readable = true;
};

std::string chainToJson(const Slots& slots, const std::string& description);
bool chainFromJson(const std::string& text, ChainDoc& doc, std::string& error, std::vector<std::string>* warnings = nullptr);

std::string chainsDirectory();
std::filesystem::path chainPath(const std::string& name);
std::vector<ChainInfo> listChains();
std::vector<ChainInfo> searchChains(const std::vector<ChainInfo>& chains, const std::string& query);
bool matchesQuery(const ChainInfo& info, const std::string& query);
bool saveChain(const std::string& name, const Slots& slots, const std::string& description, std::string& error);
bool loadChain(const std::string& name, ChainDoc& doc, std::string& error, std::vector<std::string>* warnings = nullptr);
bool deleteChain(const std::string& name);
bool exportChain(const std::string& name, const std::filesystem::path& target, std::string& error);
bool importChain(const std::filesystem::path& source, std::string& nameOut, std::string& error, std::vector<std::string>* warnings = nullptr);
int usedSlots(const Slots& slots);

}
