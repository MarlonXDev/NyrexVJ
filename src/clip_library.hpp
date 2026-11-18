#pragma once

#include <string>
#include <vector>

namespace nvj {

struct ClipDef {
    std::string name;
    std::string body;
    bool is3d = false;
};

const std::vector<ClipDef>& clipLibrary();
int clipIndexByName(const std::string& name);
const std::string& clipVertexSource();
std::string clipFragmentSource(const ClipDef& clip);

}
