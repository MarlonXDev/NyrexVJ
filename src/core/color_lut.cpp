#include "core/color_lut.hpp"

#include "core/log.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <fstream>
#include <sstream>
#include <system_error>

namespace nvj {

namespace {

std::string trimmed(const std::string& s) {
    size_t begin = 0;
    size_t end = s.size();
    while (begin < end && (s[begin] == ' ' || s[begin] == '\t' || s[begin] == '\r' || s[begin] == '\n')) {
        ++begin;
    }
    while (end > begin && (s[end - 1] == ' ' || s[end - 1] == '\t' || s[end - 1] == '\r' || s[end - 1] == '\n')) {
        --end;
    }
    return s.substr(begin, end - begin);
}

std::vector<std::string> split(const std::string& line) {
    std::vector<std::string> out;
    std::string current;
    for (char c : line) {
        if (c == ' ' || c == '\t') {
            if (!current.empty()) {
                out.push_back(current);
                current.clear();
            }
        } else {
            current.push_back(c);
        }
    }
    if (!current.empty()) {
        out.push_back(current);
    }
    return out;
}

bool number(const std::string& token, double& value) {
    const char* begin = token.data();
    const char* end = begin + token.size();
    if (begin != end && *begin == '+') {
        ++begin;
    }
    if (begin == end) {
        return false;
    }
    const auto result = std::from_chars(begin, end, value);
    return result.ec == std::errc() && result.ptr == end && std::isfinite(value);
}

bool integer(const std::string& token, long long& value) {
    const auto result = std::from_chars(token.data(), token.data() + token.size(), value);
    return result.ec == std::errc() && result.ptr == token.data() + token.size();
}

CubeParse fail(int line, const std::string& message) {
    CubeParse result;
    result.ok = false;
    result.line = line;
    result.error = message;
    return result;
}

std::string upper(std::string s) {
    for (char& c : s) {
        if (c >= 'a' && c <= 'z') {
            c = static_cast<char>(c - 'a' + 'A');
        }
    }
    return s;
}

}

bool CubeLut::valid() const {
    if (size < kMinCubeSize || size > kMaxCubeSize || rgb.size() != entries() * 3) {
        return false;
    }
    for (size_t i = 0; i < 3; ++i) {
        if (!std::isfinite(domainMin[i]) || !std::isfinite(domainMax[i]) || !(domainMax[i] > domainMin[i])) {
            return false;
        }
    }
    for (float v : rgb) {
        if (!std::isfinite(v) || std::fabs(v) > kMaxCubeValue) {
            return false;
        }
    }
    return true;
}

CubeParse parseCube(const std::string& text) {
    CubeLut lut;
    long long size = 0;
    bool sizeSeen = false;
    size_t expected = 0;
    std::istringstream stream(text);
    std::string raw;
    int lineNumber = 0;
    while (std::getline(stream, raw)) {
        ++lineNumber;
        std::string line = trimmed(raw);
        if (lineNumber == 1 && line.size() >= 3 && static_cast<unsigned char>(line[0]) == 0xEF && static_cast<unsigned char>(line[1]) == 0xBB && static_cast<unsigned char>(line[2]) == 0xBF) {
            line = trimmed(line.substr(3));
        }
        if (line.empty() || line[0] == '#') {
            continue;
        }
        const std::vector<std::string> tokens = split(line);
        if (tokens.empty()) {
            continue;
        }
        const std::string keyword = upper(tokens[0]);
        const bool numeric = (tokens[0][0] >= '0' && tokens[0][0] <= '9') || tokens[0][0] == '-' || tokens[0][0] == '+' || tokens[0][0] == '.';
        if (!numeric) {
            if (keyword == "TITLE") {
                const size_t quote = line.find('"');
                const size_t last = line.rfind('"');
                lut.title = quote != std::string::npos && last > quote ? line.substr(quote + 1, last - quote - 1) : trimmed(line.substr(5));
                if (lut.title.size() > kMaxCubeNameBytes) {
                    lut.title.resize(kMaxCubeNameBytes);
                }
            } else if (keyword == "LUT_1D_SIZE" || keyword == "LUT_1D_INPUT_RANGE") {
                return fail(lineNumber, "1D LUTs are not supported, use a 3D LUT");
            } else if (keyword == "LUT_3D_SIZE") {
                if (sizeSeen) {
                    return fail(lineNumber, "LUT_3D_SIZE appears twice");
                }
                if (tokens.size() != 2 || !integer(tokens[1], size)) {
                    return fail(lineNumber, "LUT_3D_SIZE needs one whole number");
                }
                if (size < kMinCubeSize || size > kMaxCubeSize) {
                    return fail(lineNumber, "LUT_3D_SIZE must be between " + std::to_string(kMinCubeSize) + " and " + std::to_string(kMaxCubeSize));
                }
                sizeSeen = true;
                lut.size = static_cast<int>(size);
                expected = lut.entries();
                lut.rgb.reserve(expected * 3);
            } else if (keyword == "DOMAIN_MIN" || keyword == "DOMAIN_MAX") {
                double v[3];
                if (tokens.size() != 4 || !number(tokens[1], v[0]) || !number(tokens[2], v[1]) || !number(tokens[3], v[2])) {
                    return fail(lineNumber, keyword + " needs three numbers");
                }
                for (size_t i = 0; i < 3; ++i) {
                    (keyword == "DOMAIN_MIN" ? lut.domainMin : lut.domainMax)[i] = static_cast<float>(v[i]);
                }
            } else if (keyword == "LUT_3D_INPUT_RANGE") {
                double lo = 0.0;
                double hi = 1.0;
                if (tokens.size() != 3 || !number(tokens[1], lo) || !number(tokens[2], hi)) {
                    return fail(lineNumber, "LUT_3D_INPUT_RANGE needs two numbers");
                }
                for (size_t i = 0; i < 3; ++i) {
                    lut.domainMin[i] = static_cast<float>(lo);
                    lut.domainMax[i] = static_cast<float>(hi);
                }
            } else {
                return fail(lineNumber, "unknown line: " + tokens[0].substr(0, 40));
            }
            continue;
        }
        if (!sizeSeen) {
            return fail(lineNumber, "numbers appear before LUT_3D_SIZE");
        }
        double v[3];
        if (tokens.size() != 3 || !number(tokens[0], v[0]) || !number(tokens[1], v[1]) || !number(tokens[2], v[2])) {
            return fail(lineNumber, "a table line needs three finite numbers");
        }
        if (lut.rgb.size() >= expected * 3) {
            return fail(lineNumber, "more table lines than LUT_3D_SIZE promises");
        }
        for (double c : v) {
            if (std::fabs(c) > kMaxCubeValue) {
                return fail(lineNumber, "a table value is outside the range of -16 to 16");
            }
            lut.rgb.push_back(static_cast<float>(c));
        }
    }
    if (!sizeSeen) {
        return fail(lineNumber, "LUT_3D_SIZE is missing");
    }
    if (lut.rgb.size() != expected * 3) {
        return fail(lineNumber, "the table has " + std::to_string(lut.rgb.size() / 3) + " lines but LUT_3D_SIZE " + std::to_string(lut.size) + " needs " + std::to_string(expected));
    }
    if (!lut.valid()) {
        return fail(lineNumber, "the domain is not usable (DOMAIN_MAX must be larger than DOMAIN_MIN)");
    }
    CubeParse result;
    result.ok = true;
    result.lut = std::move(lut);
    return result;
}

CubeParse loadCubeFile(const std::filesystem::path& path) {
    std::error_code ec;
    const auto size = std::filesystem::file_size(path, ec);
    if (ec) {
        return fail(0, "the file cannot be read");
    }
    if (size > kMaxCubeFileBytes) {
        return fail(0, "the file is larger than 64 MB");
    }
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return fail(0, "the file cannot be opened");
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return parseCube(buffer.str());
}

CubeLut makeIdentityLut(int size) {
    CubeLut lut;
    lut.size = std::clamp(size, kMinCubeSize, kMaxCubeSize);
    lut.title = "identity";
    lut.rgb.reserve(lut.entries() * 3);
    const float last = static_cast<float>(lut.size - 1);
    for (int b = 0; b < lut.size; ++b) {
        for (int g = 0; g < lut.size; ++g) {
            for (int r = 0; r < lut.size; ++r) {
                lut.rgb.push_back(static_cast<float>(r) / last);
                lut.rgb.push_back(static_cast<float>(g) / last);
                lut.rgb.push_back(static_cast<float>(b) / last);
            }
        }
    }
    return lut;
}

Color3 sampleCube(const CubeLut& lut, const Color3& rgb) {
    if (!lut.valid()) {
        return rgb;
    }
    const int n = lut.size;
    double position[3];
    int base[3];
    double fraction[3];
    for (size_t i = 0; i < 3; ++i) {
        const double lo = lut.domainMin[i];
        const double hi = lut.domainMax[i];
        const double t = std::clamp((rgb[i] - lo) / (hi - lo), 0.0, 1.0);
        position[i] = t * (n - 1);
        base[i] = std::min(static_cast<int>(std::floor(position[i])), n - 2);
        fraction[i] = position[i] - base[i];
    }
    const auto at = [&](int r, int g, int b, size_t channel) {
        return static_cast<double>(lut.rgb[(static_cast<size_t>(b) * n * n + static_cast<size_t>(g) * n + static_cast<size_t>(r)) * 3 + channel]);
    };
    Color3 out{};
    for (size_t channel = 0; channel < 3; ++channel) {
        double value = 0.0;
        for (int corner = 0; corner < 8; ++corner) {
            const int dr = corner & 1;
            const int dg = (corner >> 1) & 1;
            const int db = (corner >> 2) & 1;
            const double weight = (dr ? fraction[0] : 1.0 - fraction[0]) * (dg ? fraction[1] : 1.0 - fraction[1]) * (db ? fraction[2] : 1.0 - fraction[2]);
            value += weight * at(base[0] + dr, base[1] + dg, base[2] + db, channel);
        }
        out[channel] = value;
    }
    return out;
}

double maxIdentityError(const CubeLut& lut) {
    if (!lut.valid()) {
        return 1.0e9;
    }
    const int n = lut.size;
    double worst = 0.0;
    for (int b = 0; b < n; ++b) {
        for (int g = 0; g < n; ++g) {
            for (int r = 0; r < n; ++r) {
                const size_t index = (static_cast<size_t>(b) * n * n + static_cast<size_t>(g) * n + static_cast<size_t>(r)) * 3;
                const double expected[3] = {
                    lut.domainMin[0] + (lut.domainMax[0] - lut.domainMin[0]) * r / (n - 1),
                    lut.domainMin[1] + (lut.domainMax[1] - lut.domainMin[1]) * g / (n - 1),
                    lut.domainMin[2] + (lut.domainMax[2] - lut.domainMin[2]) * b / (n - 1)};
                for (size_t c = 0; c < 3; ++c) {
                    worst = std::max(worst, std::fabs(static_cast<double>(lut.rgb[index + c]) - expected[c]));
                }
            }
        }
    }
    return worst;
}

bool isIdentityLut(const CubeLut& lut, double tolerance) {
    return lut.valid() && maxIdentityError(lut) <= tolerance;
}

std::uint64_t cubeGpuBytes(int size) {
    const std::uint64_t n = static_cast<std::uint64_t>(std::clamp(size, 0, kMaxCubeSize));
    return n * n * n * 6ull;
}

std::uint64_t colorStageBytes(int width, int height, int lutSize) {
    return static_cast<std::uint64_t>(std::max(width, 0)) * static_cast<std::uint64_t>(std::max(height, 0)) * 4ull + cubeGpuBytes(lutSize);
}

std::filesystem::path lutDirectory() {
    return std::filesystem::path(userDataDir()) / "luts";
}

bool safeCubeName(const std::string& name) {
    if (name.empty() || name.size() > kMaxCubeNameBytes) {
        return false;
    }
    if (name.find('/') != std::string::npos || name.find('\\') != std::string::npos || name.find("..") != std::string::npos || name.find(':') != std::string::npos) {
        return false;
    }
    for (char c : name) {
        if (static_cast<unsigned char>(c) < 32) {
            return false;
        }
    }
    const std::string ext = upper(name.size() >= 5 ? name.substr(name.size() - 5) : name);
    return ext == ".CUBE" && name.size() > 5;
}

std::vector<std::string> listCubeFiles(const std::filesystem::path& directory) {
    std::vector<std::string> names;
    std::error_code ec;
    if (!std::filesystem::is_directory(directory, ec)) {
        return names;
    }
    for (const auto& entry : std::filesystem::directory_iterator(directory, ec)) {
        std::error_code entryError;
        if (!entry.is_regular_file(entryError)) {
            continue;
        }
        const std::string name = entry.path().filename().string();
        if (safeCubeName(name) && entry.file_size(entryError) <= kMaxCubeFileBytes) {
            names.push_back(name);
        }
        if (names.size() >= 256) {
            break;
        }
    }
    std::sort(names.begin(), names.end());
    return names;
}

}
