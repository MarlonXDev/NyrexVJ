#pragma once

#include <string>

namespace nvj {

enum class Language {
    English = 0,
    German = 1,
};

constexpr int kLanguageCount = 2;

void setLanguage(Language language);
Language currentLanguage();
const char* languageCode(Language language);
const char* languageLabel(Language language);
Language languageFromCode(const std::string& code);

const char* tr(const char* english);
const char* trId(const char* english, const char* id);
std::string trMessage(const std::string& english);

struct Translation {
    const char* english;
    const char* german;
};

const Translation* germanTable(int& count);

}
