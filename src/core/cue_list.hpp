#pragma once

#include <string>
#include <vector>

namespace nvj {

struct Cue {
    double seconds = 0.0;
    std::string target;
    float value = 1.0f;
};

constexpr size_t kMaxCues = 512;
constexpr double kCueMaxStep = 0.5;

void sortCues(std::vector<Cue>& cues);
std::vector<size_t> dueCues(const std::vector<Cue>& cues, double previous, double current);
std::string cuesToJson(const std::vector<Cue>& cues);
bool cuesFromJson(const std::string& text, std::vector<Cue>& cues);

class CueRunner {
public:
    void reset();
    std::vector<size_t> advance(const std::vector<Cue>& cues, bool valid, double seconds);
    bool tracking() const { return tracking_; }
    double position() const { return last_; }
    unsigned long long skipped() const { return skipped_; }

private:
    bool tracking_ = false;
    double last_ = 0.0;
    unsigned long long skipped_ = 0;
};

}
