#pragma once

#include <string>

namespace nvj {

struct ThemeInfo {
    const char* id;
    const char* name;
    float r;
    float g;
    float b;
};

int themeCount();
const ThemeInfo& themeInfo(int index);
int themeIndex(const std::string& id);
void applyTheme(int index = 0);

}
