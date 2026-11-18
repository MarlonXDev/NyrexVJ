#include "core/i18n.hpp"

#include <cstdio>
#include <string_view>
#include <unordered_map>

namespace nvj {

namespace {

Language gLanguage = Language::English;

const std::unordered_map<std::string_view, const char*>& germanMap() {
    static const std::unordered_map<std::string_view, const char*> map = [] {
        std::unordered_map<std::string_view, const char*> result;
        int count = 0;
        const Translation* table = germanTable(count);
        for (int i = 0; i < count; ++i) {
            result.emplace(table[i].english, table[i].german);
        }
        return result;
    }();
    return map;
}

}

void setLanguage(Language language) {
    gLanguage = language;
}

Language currentLanguage() {
    return gLanguage;
}

const char* languageCode(Language language) {
    return language == Language::German ? "de" : "en";
}

const char* languageLabel(Language language) {
    return language == Language::German ? "Deutsch" : "English";
}

Language languageFromCode(const std::string& code) {
    return code == "de" ? Language::German : Language::English;
}

const char* tr(const char* english) {
    if (gLanguage == Language::English || !english) {
        return english;
    }
    const auto& map = germanMap();
    auto it = map.find(std::string_view(english));
    return it == map.end() ? english : it->second;
}

std::string trMessage(const std::string& english) {
    if (gLanguage == Language::English || english.empty()) {
        return english;
    }
    const auto& map = germanMap();
    auto exact = map.find(std::string_view(english));
    if (exact != map.end()) {
        return exact->second;
    }
    int count = 0;
    const Translation* table = germanTable(count);
    const Translation* best = nullptr;
    size_t bestLength = 0;
    for (int i = 0; i < count; ++i) {
        const std::string_view key(table[i].english);
        if (key.size() > bestLength && key.size() >= 3 && key.back() == ' ' && english.compare(0, key.size(), key) == 0) {
            best = &table[i];
            bestLength = key.size();
        }
    }
    if (best) {
        return std::string(best->german) + trMessage(english.substr(bestLength));
    }
    return english;
}

const char* trId(const char* english, const char* id) {
    constexpr int kSlots = 32;
    constexpr int kSize = 192;
    static char buffers[kSlots][kSize];
    static int next = 0;
    char* buffer = buffers[next];
    next = (next + 1) % kSlots;
    std::snprintf(buffer, kSize, "%s###%s", tr(english), id);
    return buffer;
}

}
