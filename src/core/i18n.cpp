#include "core/i18n.hpp"

#include "core/log.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace nvj {

namespace {

constexpr size_t kMaxLanguageFileBytes = 1024 * 1024;
constexpr size_t kMaxLanguageEntries = 4000;
constexpr size_t kMaxTextLength = 1000;
constexpr size_t kMaxUserLanguages = 16;

struct LanguageData {
    std::string code;
    std::string label;
    std::unordered_map<std::string, std::string> owned;
    std::unordered_map<std::string_view, const char*> map;
    std::vector<std::pair<std::string_view, const char*>> prefixes;
};

struct BuiltInInfo {
    const char* code;
    const char* label;
};

constexpr BuiltInInfo kBuiltIn[kBuiltInLanguageCount] = {
    {"en", "English"},
    {"de", "Deutsch"},
    {"es", "Espa\xC3\xB1ol"},
    {"fr", "Fran\xC3\xA7""ais"},
    {"it", "Italiano"},
    {"pt", "Portugu\xC3\xAA""s"},
    {"nl", "Nederlands"},
    {"ja", "Japanese"},
};

constexpr const char* kJapaneseNativeLabel = "\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E";

bool gNativeLabels = false;

Language gLanguage = Language::English;
LanguageData* gActive = nullptr;
std::unique_ptr<LanguageData> gBuiltInData[kBuiltInLanguageCount];
std::vector<std::unique_ptr<LanguageData>> gUserData;
std::vector<std::unique_ptr<LanguageData>> gRetired;

void addPrefix(LanguageData& data, std::string_view key, const char* text) {
    if (key.size() >= 3 && key.back() == ' ') {
        data.prefixes.emplace_back(key, text);
    }
}

LanguageData& builtInData(Language language) {
    const int index = static_cast<int>(language);
    if (!gBuiltInData[index]) {
        auto data = std::make_unique<LanguageData>();
        data->code = kBuiltIn[index].code;
        data->label = kBuiltIn[index].label;
        int count = 0;
        const Translation* table = builtInTable(language, count);
        for (int i = 0; i < count; ++i) {
            data->map.emplace(table[i].english, table[i].text);
            addPrefix(*data, table[i].english, table[i].text);
        }
        gBuiltInData[index] = std::move(data);
    }
    return *gBuiltInData[index];
}

LanguageData* dataFor(Language language) {
    const int index = static_cast<int>(language);
    if (index <= 0) {
        return nullptr;
    }
    if (index < kBuiltInLanguageCount) {
        return &builtInData(language);
    }
    const size_t user = static_cast<size_t>(index - kBuiltInLanguageCount);
    return user < gUserData.size() ? gUserData[user].get() : nullptr;
}

bool validCode(const std::string& code) {
    if (code.size() < 2 || code.size() > 12) {
        return false;
    }
    for (char c : code) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-';
        if (!ok) {
            return false;
        }
    }
    return true;
}

bool latinOnly(const std::string& text) {
    for (size_t i = 0; i < text.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        if (c < 0x80) {
            continue;
        }
        if ((c == 0xC2 || c == 0xC3) && i + 1 < text.size() && (static_cast<unsigned char>(text[i + 1]) & 0xC0) == 0x80) {
            ++i;
            continue;
        }
        return false;
    }
    return true;
}

std::string matchEdges(const std::string& key, std::string value) {
    while (!value.empty() && value.back() == ' ') {
        value.pop_back();
    }
    size_t lead = 0;
    while (lead < value.size() && value[lead] == ' ') {
        ++lead;
    }
    value.erase(0, lead);
    if (!key.empty() && key.front() == ' ') {
        value.insert(0, " ");
    }
    if (!key.empty() && key.back() == ' ') {
        value.push_back(' ');
    }
    return value;
}

}

void setLanguage(Language language) {
    gLanguage = language;
    gActive = dataFor(language);
    if (!gActive) {
        gLanguage = Language::English;
    }
}

Language currentLanguage() {
    return gLanguage;
}

int languageCount() {
    return kBuiltInLanguageCount + static_cast<int>(gUserData.size());
}

bool isUserLanguage(Language language) {
    return static_cast<int>(language) >= kBuiltInLanguageCount;
}

const char* languageCode(Language language) {
    const int index = static_cast<int>(language);
    if (index >= 0 && index < kBuiltInLanguageCount) {
        return kBuiltIn[index].code;
    }
    LanguageData* data = dataFor(language);
    return data ? data->code.c_str() : kBuiltIn[0].code;
}

void setNativeLabelsAvailable(bool available) {
    gNativeLabels = available;
}

bool nativeLabelsAvailable() {
    return gNativeLabels;
}

bool languageNeedsCjkFont(Language language) {
    return language == Language::Japanese;
}

const char* languageLabel(Language language) {
    const int index = static_cast<int>(language);
    if (language == Language::Japanese && gNativeLabels) {
        return kJapaneseNativeLabel;
    }
    if (index >= 0 && index < kBuiltInLanguageCount) {
        return kBuiltIn[index].label;
    }
    LanguageData* data = dataFor(language);
    return data ? data->label.c_str() : kBuiltIn[0].label;
}

Language languageFromCode(const std::string& code) {
    for (int i = 0; i < languageCount(); ++i) {
        const Language language = static_cast<Language>(i);
        if (code == languageCode(language)) {
            return language;
        }
    }
    return Language::English;
}

const Translation* builtInTable(Language language, int& count) {
    switch (language) {
    case Language::German:
        return germanTable(count);
    case Language::Spanish:
        return spanishTable(count);
    case Language::French:
        return frenchTable(count);
    case Language::Italian:
        return italianTable(count);
    case Language::Portuguese:
        return portugueseTable(count);
    case Language::Dutch:
        return dutchTable(count);
    case Language::Japanese:
        return japaneseTable(count);
    default:
        count = 0;
        return nullptr;
    }
}

const char* tr(const char* english) {
    if (!gActive || !english) {
        return english;
    }
    auto it = gActive->map.find(std::string_view(english));
    return it == gActive->map.end() ? english : it->second;
}

std::string trMessage(const std::string& english) {
    if (!gActive || english.empty()) {
        return english;
    }
    auto exact = gActive->map.find(std::string_view(english));
    if (exact != gActive->map.end()) {
        return exact->second;
    }
    const std::pair<std::string_view, const char*>* best = nullptr;
    size_t bestLength = 0;
    for (const auto& entry : gActive->prefixes) {
        if (entry.first.size() > bestLength && english.compare(0, entry.first.size(), entry.first) == 0) {
            best = &entry;
            bestLength = entry.first.size();
        }
    }
    if (best) {
        return std::string(best->second) + trMessage(english.substr(bestLength));
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

bool parseUserLanguage(const std::string& jsonText, std::string& code, std::string& name, std::string& error) {
    nlohmann::json doc = nlohmann::json::parse(jsonText, nullptr, false);
    if (doc.is_discarded() || !doc.is_object()) {
        error = "not a JSON object";
        return false;
    }
    auto codeIt = doc.find("code");
    auto nameIt = doc.find("name");
    auto textsIt = doc.find("translations");
    if (codeIt == doc.end() || !codeIt->is_string() || !validCode(codeIt->get<std::string>())) {
        error = "missing or invalid \"code\" (2 to 12 characters: a-z, 0-9, -)";
        return false;
    }
    if (nameIt == doc.end() || !nameIt->is_string() || nameIt->get<std::string>().empty() || nameIt->get<std::string>().size() > 32) {
        error = "missing or invalid \"name\" (1 to 32 characters)";
        return false;
    }
    if (textsIt == doc.end() || !textsIt->is_object()) {
        error = "missing \"translations\" object";
        return false;
    }
    code = codeIt->get<std::string>();
    name = nameIt->get<std::string>();
    return true;
}

UserLanguageReport loadUserLanguages(const std::string& folder) {
    UserLanguageReport report;
    const Language before = gLanguage;
    const std::string beforeCode = languageCode(before);
    gActive = nullptr;
    for (auto& old : gUserData) {
        gRetired.push_back(std::move(old));
    }
    gUserData.clear();

    std::error_code ec;
    std::vector<std::filesystem::path> files;
    if (!folder.empty() && std::filesystem::is_directory(folder, ec)) {
        for (const auto& entry : std::filesystem::directory_iterator(folder, ec)) {
            if (entry.is_regular_file(ec) && entry.path().extension() == ".json") {
                files.push_back(entry.path());
            }
        }
    }
    std::sort(files.begin(), files.end());

    auto problem = [&report](const std::filesystem::path& path, const std::string& text) {
        ++report.skipped;
        if (report.firstProblem.empty()) {
            report.firstProblem = path.filename().string() + ": " + text;
        }
    };

    for (const auto& path : files) {
        if (gUserData.size() >= kMaxUserLanguages) {
            problem(path, "too many language files");
            continue;
        }
        const auto size = std::filesystem::file_size(path, ec);
        if (ec || size > kMaxLanguageFileBytes) {
            problem(path, "file is too large or unreadable");
            continue;
        }
        std::ifstream stream(path, std::ios::binary);
        std::stringstream buffer;
        buffer << stream.rdbuf();
        const std::string text = buffer.str();
        std::string code;
        std::string name;
        std::string error;
        if (!parseUserLanguage(text, code, name, error)) {
            problem(path, error);
            continue;
        }
        bool duplicate = false;
        for (int i = 0; i < kBuiltInLanguageCount; ++i) {
            duplicate = duplicate || code == kBuiltIn[i].code;
        }
        for (const auto& existing : gUserData) {
            duplicate = duplicate || code == existing->code;
        }
        if (duplicate || !latinOnly(name)) {
            problem(path, duplicate ? "language code already in use" : "name uses characters the font cannot show");
            continue;
        }
        const nlohmann::json doc = nlohmann::json::parse(text, nullptr, false);
        auto data = std::make_unique<LanguageData>();
        data->code = code;
        data->label = name;
        for (auto it = doc["translations"].begin(); it != doc["translations"].end(); ++it) {
            if (data->owned.size() >= kMaxLanguageEntries) {
                break;
            }
            if (!it.value().is_string()) {
                continue;
            }
            const std::string& key = it.key();
            std::string value = it.value().get<std::string>();
            if (key.empty() || key.size() > kMaxTextLength || value.size() > kMaxTextLength || value.empty() || !latinOnly(value)) {
                ++report.skippedTexts;
                continue;
            }
            value = matchEdges(key, value);
            data->owned[key] = value;
        }
        for (const auto& entry : data->owned) {
            data->map.emplace(std::string_view(entry.first), entry.second.c_str());
            addPrefix(*data, std::string_view(entry.first), entry.second.c_str());
        }
        gUserData.push_back(std::move(data));
        ++report.loaded;
    }

    setLanguage(languageFromCode(beforeCode));
    return report;
}

std::string languageTemplate() {
    int count = 0;
    const Translation* table = germanTable(count);
    nlohmann::json translations = nlohmann::json::object();
    for (int i = 0; i < count; ++i) {
        translations[table[i].english] = table[i].english;
    }
    nlohmann::json doc;
    doc["code"] = "xx";
    doc["name"] = "My language";
    doc["translations"] = translations;
    return doc.dump(2) + "\n";
}

std::string userLanguageDirectory() {
    return (std::filesystem::path(userDataDir()) / "languages").string();
}

bool writeLanguageTemplate(const std::string& folder, std::string& writtenPath) {
    std::error_code ec;
    std::filesystem::create_directories(folder, ec);
    const std::filesystem::path path = std::filesystem::path(folder) / "template.json";
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream) {
        return false;
    }
    const std::string text = languageTemplate();
    stream.write(text.data(), static_cast<std::streamsize>(text.size()));
    stream.close();
    if (!stream) {
        return false;
    }
    writtenPath = path.string();
    return true;
}

}
