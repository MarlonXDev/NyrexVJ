#pragma once

#include "core/effect_chain.hpp"
#include "core/particles.hpp"
#include "core/text_doc.hpp"
#include "core/text_font.hpp"

#include <string>
#include <vector>

namespace nvj {

struct GeneratorDialog {
    bool open = false;
    bool focus = false;
    int requestedTab = -1;
    double clock = 0.0;
    double lastTime = -1.0;
    bool playing = true;

    std::vector<std::string> textFiles;
    bool textFilesDirty = true;
    std::vector<text::FontEntry> fonts;
    bool fontsDirty = true;
    text::TextDoc textDoc = text::exampleTextDoc();
    char textBody[2100] = {};
    char textName[80] = {};
    bool textReady = false;
    std::string textStatus;
    bool textStatusBad = false;
    std::vector<std::string> textWarnings;

    std::vector<std::string> particleFiles;
    bool particleFilesDirty = true;
    particles::Params params;
    char particleName[80] = {};
    std::string particleStatus;
    bool particleStatusBad = false;
    std::vector<std::string> particleWarnings;
    int builtinPick = -1;
};

struct ChainDialog {
    bool open = false;
    bool focus = false;
    int target = 0;
    char filter[80] = {};
    char saveName[80] = {};
    char description[208] = {};
    char exportPath[512] = {};
    char importPath[512] = {};
    std::vector<chain::ChainInfo> chains;
    bool dirty = true;
    std::string selected;
    bool preview = true;
    double clock = 0.0;
    double lastTime = -1.0;
    std::string status;
    bool statusBad = false;
    std::vector<std::string> warnings;
    bool confirmDelete = false;
};

}
