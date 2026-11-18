#include "core/composition.hpp"
#include "core/i18n.hpp"
#include "render/clip_library.hpp"
#include "render/effect_library.hpp"
#include "update/changelog.hpp"
#include "check.hpp"

#include <set>
#include <string>

namespace {

void testTable() {
    int count = 0;
    const nvj::Translation* table = nvj::germanTable(count);
    CHECK(count > 200);
    std::set<std::string> seen;
    for (int i = 0; i < count; ++i) {
        const std::string english = table[i].english;
        const std::string german = table[i].german;
        CHECK(!english.empty());
        CHECK(!german.empty());
        CHECK(seen.insert(english).second);
        CHECK(english.back() == ' ' ? german.back() == ' ' : german.back() != ' ');
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
    nvj::setLanguage(nvj::Language::English);
}

void testLanguageCodes() {
    CHECK(nvj::languageFromCode("de") == nvj::Language::German);
    CHECK(nvj::languageFromCode("en") == nvj::Language::English);
    CHECK(nvj::languageFromCode("") == nvj::Language::English);
    CHECK(nvj::languageFromCode("fr") == nvj::Language::English);
    CHECK(std::string(nvj::languageCode(nvj::Language::German)) == "de");
    CHECK(std::string(nvj::languageCode(nvj::Language::English)) == "en");
    for (int i = 0; i < nvj::kLanguageCount; ++i) {
        const nvj::Language language = static_cast<nvj::Language>(i);
        CHECK(nvj::languageFromCode(nvj::languageCode(language)) == language);
        CHECK(std::string(nvj::languageLabel(language)).size() > 1);
    }
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
        CHECK(clip.body.find("void main()") != std::string::npos);
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
    testNameTables();
    testChangelogs();
    testWelcome();
    testLibraries();
    return testing::finish();
}
