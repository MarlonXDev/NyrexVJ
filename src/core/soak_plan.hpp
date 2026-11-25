#pragma once

#include <string>

namespace nvj {

constexpr double kMinSoakSeconds = 5.0;
constexpr double kMaxSoakSeconds = 86400.0;
constexpr double kMinJudgedSoakSeconds = 60.0;

struct SoakPlan {
    double seconds = 60.0;
    int fps = 60;
    double sampleSeconds = 2.0;
    int lookEvery = 20;
    int deckEvery = 7;
    int qualityEvery = 11;
    bool media = false;
    int mediaEvery = 2;
    int clearEvery = 9;
};

struct SoakEvents {
    bool look = false;
    bool deck = false;
    bool quality = false;
    bool media = false;
    bool clear = false;
};

bool parseSoakSpec(const std::string& text, SoakPlan& plan, std::string& error);
double soakSampleSeconds(double seconds);
SoakEvents soakEventsAt(const SoakPlan& plan, int second);

}
