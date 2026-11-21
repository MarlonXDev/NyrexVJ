#include "core/cue_list.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>

namespace nvj {

void sortCues(std::vector<Cue>& cues) {
    std::stable_sort(cues.begin(), cues.end(), [](const Cue& a, const Cue& b) { return a.seconds < b.seconds; });
}

std::vector<size_t> dueCues(const std::vector<Cue>& cues, double previous, double current) {
    std::vector<size_t> due;
    if (!(current > previous)) {
        return due;
    }
    for (size_t i = 0; i < cues.size(); ++i) {
        if (cues[i].seconds > previous && cues[i].seconds <= current) {
            due.push_back(i);
        }
    }
    return due;
}

std::string cuesToJson(const std::vector<Cue>& cues) {
    nlohmann::json doc;
    doc["version"] = 1;
    nlohmann::json list = nlohmann::json::array();
    for (const Cue& cue : cues) {
        nlohmann::json item;
        item["seconds"] = cue.seconds;
        item["target"] = cue.target;
        item["value"] = cue.value;
        list.push_back(item);
    }
    doc["cues"] = list;
    return doc.dump(2);
}

bool cuesFromJson(const std::string& text, std::vector<Cue>& cues) {
    const nlohmann::json doc = nlohmann::json::parse(text, nullptr, false);
    if (!doc.is_object() || !doc.contains("cues") || !doc["cues"].is_array()) {
        return false;
    }
    std::vector<Cue> parsed;
    for (const nlohmann::json& item : doc["cues"]) {
        if (!item.is_object() || parsed.size() >= kMaxCues) {
            continue;
        }
        const auto seconds = item.find("seconds");
        const auto target = item.find("target");
        if (seconds == item.end() || !seconds->is_number() || target == item.end() || !target->is_string()) {
            continue;
        }
        Cue cue;
        cue.seconds = seconds->get<double>();
        cue.target = target->get<std::string>();
        const auto value = item.find("value");
        if (value != item.end() && value->is_number()) {
            cue.value = std::clamp(value->get<float>(), 0.0f, 1.0f);
        }
        if (!std::isfinite(cue.seconds) || cue.seconds < 0.0 || cue.target.empty()) {
            continue;
        }
        parsed.push_back(cue);
    }
    sortCues(parsed);
    cues = parsed;
    return true;
}

void CueRunner::reset() {
    tracking_ = false;
    last_ = 0.0;
}

std::vector<size_t> CueRunner::advance(const std::vector<Cue>& cues, bool valid, double seconds) {
    std::vector<size_t> due;
    if (!valid || !std::isfinite(seconds)) {
        tracking_ = false;
        return due;
    }
    if (tracking_) {
        const double step = seconds - last_;
        if (step > 0.0 && step <= kCueMaxStep) {
            due = dueCues(cues, last_, seconds);
        } else if (step < 0.0 || step > kCueMaxStep) {
            ++skipped_;
        }
    }
    tracking_ = true;
    last_ = seconds;
    return due;
}

}
