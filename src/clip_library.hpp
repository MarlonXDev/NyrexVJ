#pragma once

#include <string>
#include <vector>

namespace nvj {

struct ClipDef {
    std::string name;
    std::string body;
};

const std::vector<ClipDef>& clipLibrary();
const std::string& clipVertexSource();
std::string clipFragmentSource(const ClipDef& clip);

}
