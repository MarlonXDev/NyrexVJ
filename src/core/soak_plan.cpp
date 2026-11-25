#include "core/soak_plan.hpp"

#include "core/frame_pacer.hpp"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdlib>

namespace nvj {

namespace {

bool parseNumber(const std::string& text, double& value) {
    if (text.empty() || text.find_first_not_of("0123456789.eE+-") != std::string::npos) {
        return false;
    }
    char* end = nullptr;
    errno = 0;
    const double parsed = std::strtod(text.c_str(), &end);
    if (end == text.c_str() || *end != '\0' || errno == ERANGE || !std::isfinite(parsed)) {
        return false;
    }
    value = parsed;
    return true;
}

}

double soakSampleSeconds(double seconds) {
    return std::clamp(seconds / 30.0, 0.5, 60.0);
}

bool parseSoakSpec(const std::string& text, SoakPlan& plan, std::string& error) {
    plan = SoakPlan{};
    const size_t comma = text.find(',');
    const std::string secondsText = text.substr(0, comma);
    double seconds = 0.0;
    if (!parseNumber(secondsText, seconds)) {
        error = "the duration is not a number";
        return false;
    }
    if (seconds < kMinSoakSeconds || seconds > kMaxSoakSeconds) {
        error = "the duration must be between 5 and 86400 seconds";
        return false;
    }
    plan.seconds = seconds;
    if (comma != std::string::npos) {
        double fps = 0.0;
        if (!parseNumber(text.substr(comma + 1), fps) || fps != std::floor(fps)) {
            error = "the frame rate is not a whole number";
            return false;
        }
        if (fps < 1.0 || fps > 1000.0 || clampFpsLimit(static_cast<int>(fps)) != static_cast<int>(fps)) {
            error = "the frame rate must be 30, 60, 90, 120 or 144";
            return false;
        }
        plan.fps = static_cast<int>(fps);
    }
    plan.sampleSeconds = soakSampleSeconds(plan.seconds);
    return true;
}

SoakEvents soakEventsAt(const SoakPlan& plan, int second) {
    SoakEvents events;
    if (second <= 0) {
        return events;
    }
    events.look = plan.lookEvery > 0 && second % plan.lookEvery == 0;
    events.deck = plan.deckEvery > 0 && second % plan.deckEvery == 0;
    events.quality = plan.qualityEvery > 0 && second % plan.qualityEvery == 0;
    return events;
}

}
