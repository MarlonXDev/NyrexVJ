#pragma once

#include <string>

namespace nvj::api {

struct PageAsset {
    const char* path;
    const char* type;
    const char* body;
};

const PageAsset* findPageAsset(const std::string& path);
std::string pageLanguageJson();

}
