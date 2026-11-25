#include "core/generator_files.hpp"

#include "core/log.hpp"
#include "core/preset.hpp"

#include <algorithm>
#include <fstream>
#include <system_error>

namespace nvj::gen {

namespace fs = std::filesystem;

namespace {

std::string lowered(std::string text) {
    for (char& c : text) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    return text;
}

void trimToBoundary(std::string& text) {
    size_t i = text.size();
    while (i > 0 && (static_cast<unsigned char>(text[i - 1]) & 0xC0) == 0x80) {
        --i;
    }
    if (i == 0) {
        text.clear();
        return;
    }
    const unsigned char lead = static_cast<unsigned char>(text[i - 1]);
    size_t expected = 1;
    if (lead >= 0xF0) {
        expected = 4;
    } else if (lead >= 0xE0) {
        expected = 3;
    } else if (lead >= 0xC0) {
        expected = 2;
    }
    if (text.size() - (i - 1) < expected) {
        text.resize(i - 1);
    }
}

bool reservedDeviceName(const std::string& name) {
    std::string stem = lowered(name);
    const size_t dot = stem.find('.');
    if (dot != std::string::npos) {
        stem.resize(dot);
    }
    while (!stem.empty() && stem.back() == ' ') {
        stem.pop_back();
    }
    if (stem == "con" || stem == "prn" || stem == "aux" || stem == "nul") {
        return true;
    }
    if (stem.size() == 4 && (stem.compare(0, 3, "com") == 0 || stem.compare(0, 3, "lpt") == 0) && stem[3] >= '0' && stem[3] <= '9') {
        return true;
    }
    return false;
}

}

std::string cleanName(const std::string& name) {
    std::string out = sanitizePresetName(name);
    if (out.size() > kMaxNameBytes) {
        out.resize(kMaxNameBytes);
    }
    trimToBoundary(out);
    if (reservedDeviceName(out)) {
        out.insert(out.begin(), '_');
    }
    return out;
}

bool nameIsUsable(const std::string& name) {
    return !name.empty() && cleanName(name) == name;
}

std::string uniqueName(const std::string& wanted, const std::vector<std::string>& taken) {
    std::string base = cleanName(wanted);
    if (base.empty()) {
        base = "Untitled";
    }
    auto exists = [&](const std::string& candidate) {
        const std::string key = lowered(candidate);
        return std::any_of(taken.begin(), taken.end(), [&](const std::string& other) { return lowered(other) == key; });
    };
    if (!exists(base)) {
        return base;
    }
    for (int i = 2; i < 1000; ++i) {
        std::string suffix = " " + std::to_string(i);
        std::string trimmed = base;
        if (trimmed.size() + suffix.size() > kMaxNameBytes) {
            trimmed.resize(kMaxNameBytes - suffix.size());
            trimToBoundary(trimmed);
        }
        const std::string candidate = trimmed + suffix;
        if (!exists(candidate)) {
            return candidate;
        }
    }
    return base;
}

std::vector<std::string> listStems(const fs::path& folder, const std::string& extension) {
    std::vector<std::string> stems;
    std::error_code ec;
    for (fs::directory_iterator it(folder, ec), end; !ec && it != end && stems.size() < kMaxFiles; it.increment(ec)) {
        std::error_code entryEc;
        if (!it->is_regular_file(entryEc) || entryEc) {
            continue;
        }
        if (lowered(it->path().extension().string()) != extension) {
            continue;
        }
        const std::u8string stem = it->path().stem().u8string();
        std::string name(stem.begin(), stem.end());
        if (!nameIsUsable(name)) {
            continue;
        }
        stems.push_back(name);
    }
    std::sort(stems.begin(), stems.end(), [](const std::string& a, const std::string& b) { return lowered(a) < lowered(b); });
    return stems;
}

bool readLimited(const fs::path& path, std::string& text, std::string& error) {
    std::error_code ec;
    if (!fs::is_regular_file(path, ec) || ec) {
        error = "The file could not be found";
        return false;
    }
    const auto size = fs::file_size(path, ec);
    if (ec) {
        error = "The file could not be read";
        return false;
    }
    if (size > kMaxDocBytes) {
        error = "The file is too large";
        return false;
    }
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        error = "The file could not be read";
        return false;
    }
    text.assign(static_cast<size_t>(size), '\0');
    in.read(text.data(), static_cast<std::streamsize>(size));
    if (static_cast<size_t>(in.gcount()) != text.size()) {
        error = "The file could not be read";
        return false;
    }
    return true;
}

fs::path folderPath(const char* folder) {
    return fs::path(userDataDir()) / folder;
}

}
