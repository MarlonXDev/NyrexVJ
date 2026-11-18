#pragma once

#include <string>

namespace nvj {

enum class ImportKind { Media, Model, Unsupported, Missing, TooLarge, Failed };

struct ImportResult {
    ImportKind kind = ImportKind::Failed;
    std::string name;
    bool added = false;
    std::string reason;
};

ImportResult importAsset(const std::string& sourceUtf8);

}
