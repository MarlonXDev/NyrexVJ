#include "core/composition.hpp"
#include "core/i18n.hpp"
#include "render/clip_library.hpp"
#include "render/effect_library.hpp"
#include "update/changelog.hpp"
#include "check.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <string>

namespace {

bool latinOnly(const std::string& text) {
    for (size_t i = 0; i < text.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        if (c < 0x80) {
            continue;
        }
        if ((c == 0xC2 || c == 0xC3) && i + 1 < text.size()) {
            ++i;
            continue;
        }
        return false;
    }
    return true;
}

int percentCount(const std::string& text) {
    int total = 0;
    for (char c : text) {
        total += c == '%' ? 1 : 0;
    }
    return total;
}

void testTable() {
    int germanCount = 0;
    const nvj::Translation* german = nvj::germanTable(germanCount);
    CHECK(germanCount > 200);
    std::set<std::string> germanKeys;
    for (int i = 0; i < germanCount; ++i) {
        germanKeys.insert(german[i].english);
    }
    for (int l = 1; l < nvj::kBuiltInLanguageCount; ++l) {
        int count = 0;
        const nvj::Translation* table = nvj::builtInTable(static_cast<nvj::Language>(l), count);
        CHECK(count == germanCount);
        std::set<std::string> seen;
        for (int i = 0; i < count; ++i) {
            const std::string english = table[i].english;
            const std::string text = table[i].text;
            CHECK(!english.empty());
            CHECK(!text.empty());
            CHECK(seen.insert(english).second);
            CHECK(germanKeys.count(english) == 1);
            CHECK(english.back() == ' ' ? text.back() == ' ' : text.back() != ' ');
            CHECK(english.front() == ' ' ? text.front() == ' ' : text.front() != ' ');
            CHECK(latinOnly(text));
            CHECK(english == "Slowest 5%" || percentCount(english) == percentCount(text));
        }
    }
}

void testLookup() {
    nvj::setLanguage(nvj::Language::English);
    CHECK(std::string(nvj::tr("Save")) == "Save");
    CHECK(std::string(nvj::tr("Something unknown")) == "Something unknown");
    CHECK(std::string(nvj::trId("Save", "x")) == "Save###x");
    CHECK(nvj::trMessage("Update available: v1.0.0") == "Update available: v1.0.0");

    nvj::setLanguage(nvj::Language::German);
    CHECK(std::string(nvj::tr("Save")) == "Speichern");
    CHECK(std::string(nvj::tr("Something unknown")) == "Something unknown");
    CHECK(std::string(nvj::trId("Save", "x")) == "Speichern###x");
    CHECK(nvj::trMessage("Update available: v1.0.0") == "Update verfügbar: v1.0.0");
    CHECK(nvj::trMessage("Could not reach GitHub: HTTP 404") == "GitHub ist nicht erreichbar: HTTP 404");
    CHECK(nvj::trMessage("Audio device not found") == "Audiogerät nicht gefunden");
    CHECK(nvj::trMessage("Totally new message") == "Totally new message");
    CHECK(nvj::trMessage("").empty());

    nvj::setLanguage(nvj::Language::Spanish);
    CHECK(std::string(nvj::tr("Save")) == "Guardar");
    CHECK(std::string(nvj::tr("Something unknown")) == "Something unknown");
    CHECK(std::string(nvj::trId("Save", "x")) == "Guardar###x");
    CHECK(nvj::trMessage("Update available: v1.0.0") == "Actualización disponible: v1.0.0");
    CHECK(nvj::trMessage("Totally new message") == "Totally new message");

    nvj::setLanguage(nvj::Language::French);
    CHECK(std::string(nvj::tr("Save")) == "Enregistrer");
    CHECK(std::string(nvj::trId("Save", "x")) == "Enregistrer###x");
    CHECK(nvj::trMessage("Update available: v1.0.0") == "Mise à jour disponible : v1.0.0");
    CHECK(nvj::trMessage("Could not reach GitHub: HTTP 404") == "Impossible de joindre GitHub : HTTP 404");
    CHECK(nvj::trMessage("").empty());
    nvj::setLanguage(nvj::Language::English);
}

void testLanguageCodes() {
    CHECK(nvj::languageFromCode("de") == nvj::Language::German);
    CHECK(nvj::languageFromCode("en") == nvj::Language::English);
    CHECK(nvj::languageFromCode("es") == nvj::Language::Spanish);
    CHECK(nvj::languageFromCode("fr") == nvj::Language::French);
    CHECK(nvj::languageFromCode("") == nvj::Language::English);
    CHECK(nvj::languageFromCode("xx") == nvj::Language::English);
    CHECK(std::string(nvj::languageCode(nvj::Language::German)) == "de");
    CHECK(std::string(nvj::languageCode(nvj::Language::English)) == "en");
    CHECK(std::string(nvj::languageCode(nvj::Language::Spanish)) == "es");
    CHECK(std::string(nvj::languageCode(nvj::Language::French)) == "fr");
    CHECK(std::string(nvj::languageLabel(nvj::Language::Spanish)) == "Español");
    CHECK(std::string(nvj::languageLabel(nvj::Language::French)) == "Français");
    CHECK(nvj::languageCount() >= nvj::kBuiltInLanguageCount);
    for (int i = 0; i < nvj::languageCount(); ++i) {
        const nvj::Language language = static_cast<nvj::Language>(i);
        CHECK(nvj::languageFromCode(nvj::languageCode(language)) == language);
        CHECK(std::string(nvj::languageLabel(language)).size() > 1);
    }
}

void writeFile(const std::filesystem::path& path, const std::string& text) {
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream << text;
}

void testUserLanguages() {
    const std::filesystem::path folder = std::filesystem::temp_directory_path() / "nyrexvj_test_languages";
    std::error_code ec;
    std::filesystem::remove_all(folder, ec);
    std::filesystem::create_directories(folder);

    const nvj::UserLanguageReport none = nvj::loadUserLanguages(folder.string());
    CHECK(none.loaded == 0 && none.skipped == 0);
    CHECK(nvj::languageCount() == nvj::kBuiltInLanguageCount);

    std::string code;
    std::string name;
    std::string error;
    CHECK(!nvj::parseUserLanguage("not json", code, name, error));
    CHECK(!nvj::parseUserLanguage("[]", code, name, error));
    CHECK(!nvj::parseUserLanguage(R"({"name":"Italiano","translations":{}})", code, name, error));
    CHECK(!nvj::parseUserLanguage(R"({"code":"IT","name":"Italiano","translations":{}})", code, name, error));
    CHECK(!nvj::parseUserLanguage(R"({"code":"it","name":"","translations":{}})", code, name, error));
    CHECK(!nvj::parseUserLanguage(R"({"code":"it","name":"Italiano"})", code, name, error));
    CHECK(nvj::parseUserLanguage(R"({"code":"it","name":"Italiano","translations":{}})", code, name, error));
    CHECK(code == "it" && name == "Italiano");

    writeFile(folder / "italiano.json", R"({"code":"it","name":"Italiano","translations":{"Save":"Salva","Saved preset: ":"Preset salvato:","Close":"Chiudi","Open":"","Stop":"停止","Count":5}})");
    writeFile(folder / "broken.json", "{ nope");
    writeFile(folder / "clash.json", R"({"code":"de","name":"Doppelt","translations":{"Save":"x"}})");
    writeFile(folder / "notes.txt", "ignored");

    const nvj::UserLanguageReport report = nvj::loadUserLanguages(folder.string());
    CHECK(report.loaded == 1);
    CHECK(report.skipped == 2);
    CHECK(report.skippedTexts == 2);
    CHECK(!report.firstProblem.empty());
    CHECK(nvj::languageCount() == nvj::kBuiltInLanguageCount + 1);

    const nvj::Language italian = nvj::languageFromCode("it");
    CHECK(static_cast<int>(italian) == nvj::kBuiltInLanguageCount);
    CHECK(nvj::isUserLanguage(italian));
    CHECK(!nvj::isUserLanguage(nvj::Language::French));
    CHECK(std::string(nvj::languageCode(italian)) == "it");
    CHECK(std::string(nvj::languageLabel(italian)) == "Italiano");

    nvj::setLanguage(italian);
    CHECK(nvj::currentLanguage() == italian);
    CHECK(std::string(nvj::tr("Save")) == "Salva");
    CHECK(std::string(nvj::tr("Close")) == "Chiudi");
    CHECK(std::string(nvj::tr("Open")) == "Open");
    CHECK(std::string(nvj::tr("Stop")) == "Stop");
    CHECK(std::string(nvj::tr("Count")) == "Count");
    CHECK(std::string(nvj::tr("Unknown text")) == "Unknown text");
    CHECK(nvj::trMessage("Saved preset: demo") == "Preset salvato: demo");
    CHECK(std::string(nvj::trId("Save", "x")) == "Salva###x");

    const nvj::UserLanguageReport again = nvj::loadUserLanguages(folder.string());
    CHECK(again.loaded == 1);
    CHECK(nvj::currentLanguage() == italian);
    CHECK(std::string(nvj::tr("Save")) == "Salva");

    std::filesystem::remove(folder / "italiano.json");
    const nvj::UserLanguageReport gone = nvj::loadUserLanguages(folder.string());
    CHECK(gone.loaded == 0);
    CHECK(nvj::currentLanguage() == nvj::Language::English);
    CHECK(std::string(nvj::tr("Save")) == "Save");
    CHECK(nvj::languageFromCode("it") == nvj::Language::English);

    nvj::setLanguage(nvj::Language::German);
    nvj::loadUserLanguages(folder.string());
    CHECK(nvj::currentLanguage() == nvj::Language::German);
    CHECK(std::string(nvj::tr("Save")) == "Speichern");
    nvj::setLanguage(nvj::Language::English);

    std::string written;
    CHECK(nvj::writeLanguageTemplate(folder.string(), written));
    CHECK(std::filesystem::exists(written));
    std::ifstream stream(written, std::ios::binary);
    const std::string text((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    CHECK(nvj::parseUserLanguage(text, code, name, error));
    CHECK(text == nvj::languageTemplate());
    CHECK(text.find("\"Save\": \"Save\"") != std::string::npos);
    int germanCount = 0;
    nvj::germanTable(germanCount);
    size_t lines = 0;
    for (char c : text) {
        lines += c == '\n' ? 1 : 0;
    }
    CHECK(lines == static_cast<size_t>(germanCount) + 6);

    nvj::loadUserLanguages("");
    std::filesystem::remove_all(folder, ec);
}

void testNameTables() {
    nvj::setLanguage(nvj::Language::German);
    for (int i = 0; i < nvj::BlendModeCount; ++i) {
        CHECK(std::string(nvj::tr(nvj::blendName(i))).size() > 0);
    }
    CHECK(std::string(nvj::tr(nvj::audioBandName(nvj::BandHigh))) == "Höhen");
    CHECK(std::string(nvj::tr(nvj::modTargetName(nvj::TargetBrightness))) == "Helligkeit");
    for (const nvj::EffectDef& def : nvj::effectLibrary()) {
        for (const char* name : def.paramNames) {
            if (name) {
                CHECK(std::string(nvj::tr(name)) != "");
            }
        }
    }
    nvj::setLanguage(nvj::Language::English);
}

void testChangelogs() {
    const auto& english = nvj::changelog(false);
    const auto& german = nvj::changelog(true);
    CHECK(!english.empty());
    CHECK(english.size() == german.size());
    for (size_t i = 0; i < english.size() && i < german.size(); ++i) {
        CHECK(english[i].version == german[i].version);
        CHECK(english[i].date == german[i].date);
        CHECK(!english[i].items.empty());
        CHECK(english[i].items.size() == german[i].items.size());
    }
    for (size_t i = 1; i < english.size(); ++i) {
        CHECK(nvj::isNewerVersion(english[i - 1].version, english[i].version));
    }
}

void testWelcome() {
    CHECK(nvj::welcomeKind("", "0.3.0") == nvj::WelcomeKind::FirstRun);
    CHECK(nvj::welcomeKind("0.2.0", "0.3.0") == nvj::WelcomeKind::Update);
    CHECK(nvj::welcomeKind("0.3.0", "0.3.0") == nvj::WelcomeKind::None);
    CHECK(nvj::welcomeKind("0.3.0", "0.2.0") == nvj::WelcomeKind::None);
    CHECK(nvj::welcomeKind("0.3.0", "0.3.0-dev") == nvj::WelcomeKind::None);
    CHECK(nvj::welcomeKind("0.2.0", "0.3.0-dev") == nvj::WelcomeKind::Update);
}

void testLibraries() {
    std::set<std::string> names;
    for (const nvj::ClipDef& clip : nvj::clipLibrary()) {
        CHECK(!clip.name.empty());
        CHECK(names.insert(clip.name).second);
        CHECK(clip.isScene() || clip.isMedia() || clip.body.find("void main()") != std::string::npos);
        CHECK(nvj::clipIndexByName(clip.name) >= 0);
    }
    CHECK(nvj::clipLibrary().size() >= 30);
    names.clear();
    for (const nvj::EffectDef& def : nvj::effectLibrary()) {
        CHECK(!def.name.empty());
        CHECK(names.insert(def.name).second);
        CHECK(def.body.find("vec4 effect(vec2 uv, vec4 p)") != std::string::npos);
        CHECK(def.paramCount() >= 1);
        for (int p = 0; p < nvj::kParamCount; ++p) {
            CHECK(def.defaults[static_cast<size_t>(p)] >= 0.0f && def.defaults[static_cast<size_t>(p)] <= 1.0f);
        }
        CHECK(nvj::effectIndexByName(def.name) >= 0);
    }
    CHECK(nvj::effectLibrary().size() >= 50);
}

}

int main() {
    testTable();
    testLookup();
    testLanguageCodes();
    testUserLanguages();
    testNameTables();
    testChangelogs();
    testWelcome();
    testLibraries();
    return testing::finish();
}
