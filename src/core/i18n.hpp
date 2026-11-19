#pragma once

#include <string>

namespace nvj {

enum class Language {
    English = 0,
    German = 1,
    Spanish = 2,
    French = 3,
};

constexpr int kBuiltInLanguageCount = 4;

void setLanguage(Language language);
Language currentLanguage();
int languageCount();
const char* languageCode(Language language);
const char* languageLabel(Language language);
Language languageFromCode(const std::string& code);
bool isUserLanguage(Language language);

const char* tr(const char* english);
const char* trId(const char* english, const char* id);
std::string trMessage(const std::string& english);

struct Translation {
    const char* english;
    const char* text;
};

const Translation* germanTable(int& count);
const Translation* spanishTable(int& count);
const Translation* frenchTable(int& count);
const Translation* builtInTable(Language language, int& count);

struct UserLanguageReport {
    int loaded = 0;
    int skipped = 0;
    int skippedTexts = 0;
    std::string firstProblem;
};

UserLanguageReport loadUserLanguages(const std::string& folder);
bool parseUserLanguage(const std::string& jsonText, std::string& code, std::string& name, std::string& error);
std::string languageTemplate();
std::string userLanguageDirectory();
bool writeLanguageTemplate(const std::string& folder, std::string& writtenPath);

}
