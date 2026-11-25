#pragma once

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace nvj {

struct ManualView {
    size_t chapter = 0;
    std::string section;
    bool scrollTop = true;
    bool scrollToSection = false;
    bool focusSearch = false;
    char search[96] = {};
    std::vector<std::pair<size_t, std::string>> history;
    std::string clicked;
};

}
