#include "render/transition_library.hpp"

#include "core/log.hpp"
#include "render/clip_library.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>

namespace nvj {

namespace {

namespace fs = std::filesystem;

std::vector<TransitionDef>& mutableLibrary() {
    static std::vector<TransitionDef> library;
    return library;
}

std::map<std::string, std::pair<long long, unsigned long long>>& otherFiles() {
    static std::map<std::string, std::pair<long long, unsigned long long>> files;
    return files;
}

std::string utf8Text(const fs::path& path) {
    const std::u8string text = path.u8string();
    return std::string(text.begin(), text.end());
}

fs::path pathOf(const std::string& text) {
    return fs::path(std::u8string(text.begin(), text.end()));
}

void stampOf(const fs::path& file, long long& stamp, unsigned long long& bytes) {
    std::error_code ec;
    const auto size = fs::file_size(file, ec);
    bytes = ec ? 0 : static_cast<unsigned long long>(size);
    const auto time = fs::last_write_time(file, ec);
    stamp = ec ? 0 : static_cast<long long>(time.time_since_epoch().count());
}

bool sniffsTransition(const std::string& text) {
    return text.find("nyrexvj transition") != std::string::npos || (text.find("startImage") != std::string::npos && text.find("endImage") != std::string::npos);
}

enum class Read { Transition, Other, Broken };

Read readFile(const fs::path& file, TransitionDef& def) {
    stampOf(file, def.stamp, def.bytes);
    def.error.clear();
    def.plan.reset();
    def.shadertoy = false;
    if (def.bytes > kMaxShaderFileBytes) {
        def.error = "The shader file is larger than 256 KiB";
        return Read::Broken;
    }
    std::ifstream stream(file, std::ios::binary);
    if (!stream) {
        def.error = "The shader file cannot be read";
        return Read::Broken;
    }
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    const std::string text = buffer.str();
    const ShaderSource prepared = prepareShaderSource(text);
    if (!prepared.ok) {
        if (sniffsTransition(text)) {
            def.error = prepared.error;
            return Read::Broken;
        }
        return Read::Other;
    }
    if (!prepared.plan || !prepared.plan->transition) {
        return Read::Other;
    }
    def.plan = prepared.plan;
    def.shadertoy = prepared.shadertoy;
    return Read::Transition;
}

}

std::string transitionClipName(const std::string& stem) {
    return "Transition: " + stem;
}

const std::vector<TransitionDef>& transitionLibrary() {
    return mutableLibrary();
}

int customTransitionIndex(const std::string& name) {
    const std::vector<TransitionDef>& library = mutableLibrary();
    for (size_t i = 0; i < library.size(); ++i) {
        if (library[i].name == name) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

TransitionScan refreshCustomTransitions() {
    TransitionScan scan;
    std::vector<TransitionDef>& library = mutableLibrary();
    std::error_code ec;
    std::vector<fs::path> files;
    const fs::path directory = pathOf(shaderDirectory());
    for (fs::directory_iterator it(directory, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code entryEc;
        if (it->is_regular_file(entryEc) && !entryEc && isShaderFileName(utf8Text(it->path().filename()))) {
            files.push_back(it->path());
        }
    }
    std::sort(files.begin(), files.end());

    for (size_t i = 0; i < library.size(); ++i) {
        TransitionDef& def = library[i];
        const fs::path* match = nullptr;
        for (const fs::path& file : files) {
            if (transitionClipName(utf8Text(file.stem())) == def.name) {
                match = &file;
                break;
            }
        }
        if (!match) {
            if (def.error != "The transition file is gone") {
                def.error = "The transition file is gone";
                def.plan.reset();
                ++def.version;
                scan.changed.push_back(i);
            }
            continue;
        }
        long long stamp = 0;
        unsigned long long bytes = 0;
        stampOf(*match, stamp, bytes);
        if (stamp == def.stamp && bytes == def.bytes && def.error != "The transition file is gone") {
            continue;
        }
        TransitionDef fresh;
        fresh.name = def.name;
        fresh.path = utf8Text(*match);
        const Read state = readFile(*match, fresh);
        if (state == Read::Other) {
            fresh.error = "This file is not a transition any more";
        }
        fresh.version = def.version + 1;
        def = std::move(fresh);
        scan.changed.push_back(i);
    }

    for (const fs::path& file : files) {
        if (library.size() >= kMaxCustomTransitions) {
            break;
        }
        const std::string name = transitionClipName(utf8Text(file.stem()));
        if (customTransitionIndex(name) >= 0) {
            continue;
        }
        long long stamp = 0;
        unsigned long long bytes = 0;
        stampOf(file, stamp, bytes);
        const std::string key = utf8Text(file);
        const auto known = otherFiles().find(key);
        if (known != otherFiles().end() && known->second == std::make_pair(stamp, bytes)) {
            continue;
        }
        TransitionDef def;
        def.name = name;
        def.path = key;
        const Read state = readFile(file, def);
        if (state == Read::Other) {
            otherFiles()[key] = {stamp, bytes};
            continue;
        }
        otherFiles().erase(key);
        if (!def.error.empty()) {
            logLine("Transition file '" + def.name + "': " + def.error);
        }
        library.push_back(std::move(def));
        scan.changed.push_back(library.size() - 1);
        ++scan.added;
    }
    return scan;
}

const char* exampleTransitionText() {
    return R"glsl(#pragma nyrexvj transition
#pragma nyrexvj param softness float "Softness" 0 0.5 0.15
#pragma nyrexvj param angle float "Angle" 0 360 30
void main() {
    float a = radians(angle);
    vec2 dir = vec2(cos(a), sin(a));
    float x = dot(vUv - 0.5, dir) / (abs(dir.x) + abs(dir.y)) + 0.5;
    float t = mix(-softness, 1.0 + softness, uProgress);
    float m = 1.0 - ss(t - softness, t + softness, x);
    fragColor = mix(colFrom(vUv), colTo(vUv), m);
}
)glsl";
}

std::string createExampleTransition() {
    std::error_code ec;
    const fs::path directory = pathOf(shaderDirectory());
    fs::create_directories(directory, ec);
    fs::path target = directory / "example_transition.glsl";
    for (int i = 2; fs::exists(target, ec) && i < 1000; ++i) {
        target = directory / pathOf("example_transition (" + std::to_string(i) + ").glsl");
    }
    if (fs::exists(target, ec)) {
        return std::string();
    }
    std::ofstream out(target, std::ios::binary | std::ios::trunc);
    if (!out) {
        return std::string();
    }
    out << exampleTransitionText();
    out.close();
    if (!out) {
        return std::string();
    }
    return utf8Text(target.filename());
}

}
