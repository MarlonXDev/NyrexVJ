#pragma once

#include "core/resoport.hpp"

#include <future>
#include <string>
#include <unordered_map>
#include <vector>

namespace nvj {

struct ResoPortCheck {
    resoport::Document document;
    std::vector<resoport::ResolvedClip> resolved;
    std::string file;
    std::string error;
    bool ok = false;
};

struct ResoPortImport {
    std::unordered_map<std::string, std::string> libraryNames;
    int copied = 0;
    int failed = 0;
};

struct ResoPortDialog {
    bool open = false;
    bool focus = false;
    bool checked = false;
    char file[512] = {};
    char folder[512] = {};
    std::string message;
    bool messageGood = false;
    ResoPortCheck check;
    resoport::Report preview;
    std::future<ResoPortCheck> checkJob;
    std::future<ResoPortImport> importJob;
};

}
