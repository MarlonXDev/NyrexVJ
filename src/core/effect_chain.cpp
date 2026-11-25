#include "core/effect_chain.hpp"

#include "core/effect_json.hpp"
#include "core/generator_files.hpp"
#include "core/recovery.hpp"
#include "core/text_layout.hpp"
#include "render/effect_library.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <system_error>

namespace nvj::chain {

namespace {

using json = nlohmann::json;
namespace fs = std::filesystem;

std::string lowered(std::string text) {
    for (char& c : text) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    return text;
}

std::string cleanDescription(const std::string& raw, bool* changed) {
    const text::DecodeResult decoded = text::decodeUtf8(raw.substr(0, std::min<size_t>(raw.size(), 4 * kMaxDescriptionBytes)));
    std::string out;
    for (char32_t cp : decoded.text) {
        const std::string piece = text::encodeUtf8(std::u32string(1, cp == U'\n' ? U' ' : cp));
        if (out.size() + piece.size() > kMaxDescriptionBytes) {
            break;
        }
        out += piece;
    }
    if (changed) {
        *changed = decoded.hadInvalid || decoded.truncated || out != raw;
    }
    return out;
}

std::vector<std::string> effectNames(const json& slots) {
    std::vector<std::string> names;
    if (!slots.is_array()) {
        return names;
    }
    for (size_t i = 0; i < slots.size() && i < static_cast<size_t>(kEffectSlots); ++i) {
        auto it = slots[i].find("effect");
        if (slots[i].is_object() && it != slots[i].end() && it->is_string() && !it->get<std::string>().empty()) {
            names.push_back(it->get<std::string>());
        }
    }
    return names;
}

}

int usedSlots(const Slots& slots) {
    int used = 0;
    for (const EffectSlot& slot : slots) {
        if (slot.effect >= 0) {
            ++used;
        }
    }
    return used;
}

std::string chainToJson(const Slots& slots, const std::string& description) {
    json node;
    node["format"] = kFormat;
    node["version"] = kVersion;
    node["description"] = cleanDescription(description, nullptr);
    node["slots"] = effectsToJson(slots);
    return node.dump(2);
}

bool chainFromJson(const std::string& textIn, ChainDoc& doc, std::string& error, std::vector<std::string>* warnings) {
    if (textIn.size() > gen::kMaxDocBytes) {
        error = "The file is too large";
        return false;
    }
    json node = json::parse(textIn, nullptr, false, false);
    if (node.is_discarded() || !node.is_object()) {
        error = "The chain file is not valid";
        return false;
    }
    auto format = node.find("format");
    if (format == node.end() || !format->is_string() || format->get<std::string>() != kFormat) {
        error = "This is not a NyrexVJ effect chain";
        return false;
    }
    auto version = node.find("version");
    if (version == node.end() || !version->is_number_integer() || version->get<int>() < 1) {
        error = "The chain file has no version";
        return false;
    }
    if (version->get<int>() > kVersion) {
        error = "The chain file was made by a newer version";
        return false;
    }
    auto slots = node.find("slots");
    if (slots == node.end() || !slots->is_array()) {
        error = "The chain file has no effects";
        return false;
    }
    if (slots->size() > static_cast<size_t>(kEffectSlots) && warnings) {
        warnings->push_back("The chain has more than four effects and the rest was left out");
    }
    ChainDoc result;
    for (const json& entry : *slots) {
        if (!entry.is_object()) {
            continue;
        }
        auto name = entry.find("effect");
        if (name != entry.end() && name->is_string() && !name->get<std::string>().empty() && effectIndexByName(name->get<std::string>()) < 0) {
            std::string shown = name->get<std::string>();
            if (shown.size() > 80) {
                shown.resize(80);
            }
            result.missing.push_back(shown);
        }
    }
    effectsFromJson(*slots, result.slots, warnings);
    auto description = node.find("description");
    if (description != node.end() && description->is_string()) {
        bool changed = false;
        result.description = cleanDescription(description->get<std::string>(), &changed);
        if (changed && warnings) {
            warnings->push_back("The description was shortened or cleaned");
        }
    }
    doc = result;
    return true;
}

std::string chainsDirectory() {
    return gen::folderPath(kFolder).string();
}

fs::path chainPath(const std::string& name) {
    return gen::folderPath(kFolder) / (name + kExtension);
}

std::vector<ChainInfo> listChains() {
    std::vector<ChainInfo> list;
    for (const std::string& name : gen::listStems(gen::folderPath(kFolder), kExtension)) {
        ChainInfo info;
        info.name = name;
        std::string text;
        std::string error;
        if (!gen::readLimited(chainPath(name), text, error)) {
            info.readable = false;
            list.push_back(info);
            continue;
        }
        json node = json::parse(text, nullptr, false, false);
        if (node.is_discarded() || !node.is_object()) {
            info.readable = false;
            list.push_back(info);
            continue;
        }
        auto description = node.find("description");
        if (description != node.end() && description->is_string()) {
            info.description = cleanDescription(description->get<std::string>(), nullptr);
        }
        auto slots = node.find("slots");
        if (slots != node.end()) {
            info.effects = effectNames(*slots);
        }
        list.push_back(info);
    }
    return list;
}

bool matchesQuery(const ChainInfo& info, const std::string& query) {
    std::string needle = lowered(query);
    size_t first = needle.find_first_not_of(' ');
    if (first == std::string::npos) {
        return true;
    }
    needle = needle.substr(first, needle.find_last_not_of(' ') - first + 1);
    size_t start = 0;
    while (start <= needle.size()) {
        size_t end = needle.find(' ', start);
        if (end == std::string::npos) {
            end = needle.size();
        }
        const std::string word = needle.substr(start, end - start);
        if (!word.empty()) {
            bool found = lowered(info.name).find(word) != std::string::npos || lowered(info.description).find(word) != std::string::npos;
            for (const std::string& effect : info.effects) {
                if (found) {
                    break;
                }
                found = lowered(effect).find(word) != std::string::npos;
            }
            if (!found) {
                return false;
            }
        }
        if (end >= needle.size()) {
            break;
        }
        start = end + 1;
    }
    return true;
}

std::vector<ChainInfo> searchChains(const std::vector<ChainInfo>& chains, const std::string& query) {
    std::vector<ChainInfo> found;
    for (const ChainInfo& info : chains) {
        if (matchesQuery(info, query)) {
            found.push_back(info);
        }
    }
    return found;
}

bool saveChain(const std::string& name, const Slots& slots, const std::string& description, std::string& error) {
    if (!gen::nameIsUsable(name)) {
        error = "The name is not valid";
        return false;
    }
    if (usedSlots(slots) == 0) {
        error = "The chain has no effects";
        return false;
    }
    std::error_code ec;
    fs::create_directories(gen::folderPath(kFolder), ec);
    if (!writeFileAtomic(chainPath(name), chainToJson(slots, description))) {
        error = "The file could not be saved";
        return false;
    }
    return true;
}

bool loadChain(const std::string& name, ChainDoc& doc, std::string& error, std::vector<std::string>* warnings) {
    if (!gen::nameIsUsable(name)) {
        error = "The name is not valid";
        return false;
    }
    std::string text;
    if (!gen::readLimited(chainPath(name), text, error)) {
        return false;
    }
    return chainFromJson(text, doc, error, warnings);
}

bool deleteChain(const std::string& name) {
    if (!gen::nameIsUsable(name)) {
        return false;
    }
    std::error_code ec;
    return fs::remove(chainPath(name), ec) && !ec;
}

bool exportChain(const std::string& name, const fs::path& target, std::string& error) {
    ChainDoc doc;
    if (!loadChain(name, doc, error)) {
        return false;
    }
    fs::path destination = target;
    std::error_code ec;
    if (fs::is_directory(destination, ec)) {
        destination /= name + kExtension;
    }
    if (lowered(destination.extension().string()) != kExtension) {
        error = "The file name must end with .nyxchain";
        return false;
    }
    if (!writeFileAtomic(destination, chainToJson(doc.slots, doc.description))) {
        error = "The file could not be saved";
        return false;
    }
    return true;
}

bool importChain(const fs::path& source, std::string& nameOut, std::string& error, std::vector<std::string>* warnings) {
    if (lowered(source.extension().string()) != kExtension) {
        error = "The file name must end with .nyxchain";
        return false;
    }
    std::string text;
    if (!gen::readLimited(source, text, error)) {
        return false;
    }
    ChainDoc doc;
    if (!chainFromJson(text, doc, error, warnings)) {
        return false;
    }
    if (usedSlots(doc.slots) == 0) {
        error = "The chain has no effects that this version knows";
        return false;
    }
    const std::u8string stem = source.stem().u8string();
    std::vector<std::string> taken;
    for (const ChainInfo& info : listChains()) {
        taken.push_back(info.name);
    }
    if (taken.size() >= gen::kMaxFiles) {
        error = "There are too many chains already";
        return false;
    }
    const std::string name = gen::uniqueName(std::string(stem.begin(), stem.end()), taken);
    if (!saveChain(name, doc.slots, doc.description, error)) {
        return false;
    }
    nameOut = name;
    return true;
}

}
