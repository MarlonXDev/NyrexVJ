#pragma once

#include <string>

namespace nvj {

int runPackBuild(const std::string& folder, const std::string& file, bool overwrite);
int runPackInfo(const std::string& file);

struct PackInstallArgs {
    std::string file;
    bool acceptPlugin = false;
    bool allowDowngrade = false;
    int conflict = 0;
};

int runPackInstall(const PackInstallArgs& args);
int runPackRemove(const std::string& id);

}
