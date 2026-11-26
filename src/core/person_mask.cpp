#include "core/person_mask.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace nvj::person {

namespace {

constexpr int kLearnFrames = 12;
constexpr float kInitialSigma = 3.0f;
constexpr float kBackgroundSeconds = 20.0f;
constexpr float kForegroundSeconds = 300.0f;
constexpr int kSceneChangeFrames = 3;
constexpr float kSceneChangeFraction = 0.85f;
constexpr double kCoastSeconds = 1.5;
constexpr double kOccludedSeconds = 4.0;
constexpr int kConfirmHits = 3;

float smooth(float edge0, float edge1, float x) {
    const float t = std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

float follow(double dt, double seconds) {
    if (seconds <= 0.0) {
        return 1.0f;
    }
    return static_cast<float>(1.0 - std::exp(-std::max(dt, 0.0) / seconds));
}

std::uint8_t toByte(float value) {
    return static_cast<std::uint8_t>(std::clamp(value * 255.0f + 0.5f, 0.0f, 255.0f));
}

void morphology(std::vector<std::uint8_t>& values, int width, int height, bool dilate, std::vector<std::uint8_t>& scratch) {
    const std::size_t count = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    scratch.resize(count);
    for (int y = 0; y < height; ++y) {
        const std::uint8_t* row = values.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(width);
        std::uint8_t* out = scratch.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(width);
        for (int x = 0; x < width; ++x) {
            const std::uint8_t a = row[std::max(x - 1, 0)];
            const std::uint8_t b = row[x];
            const std::uint8_t c = row[std::min(x + 1, width - 1)];
            out[x] = dilate ? std::max({a, b, c}) : std::min({a, b, c});
        }
    }
    for (int y = 0; y < height; ++y) {
        const std::uint8_t* up = scratch.data() + static_cast<std::size_t>(std::max(y - 1, 0)) * static_cast<std::size_t>(width);
        const std::uint8_t* mid = scratch.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(width);
        const std::uint8_t* down = scratch.data() + static_cast<std::size_t>(std::min(y + 1, height - 1)) * static_cast<std::size_t>(width);
        std::uint8_t* out = values.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(width);
        for (int x = 0; x < width; ++x) {
            out[x] = dilate ? std::max({up[x], mid[x], down[x]}) : std::min({up[x], mid[x], down[x]});
        }
    }
}

class DifferenceBackend final : public Backend {
public:
    Provenance provenance() const override {
        Provenance info;
        info.id = "difference";
        info.title = "Background difference";
        info.licence = "MIT, part of NyrexVJ";
        info.origin = "computed from the camera picture, no model, nothing is downloaded";
        info.needsModel = false;
        info.usesNetwork = false;
        info.deterministic = true;
        return info;
    }

    bool segment(const Picture& picture, const Params& params, std::vector<std::uint8_t>& score, std::string& problem) override {
        if (!picture.valid() || static_cast<long long>(picture.width) * picture.height > kMaxPicturePixels) {
            problem = "the analysis picture is empty or too large";
            return false;
        }
        const std::size_t count = static_cast<std::size_t>(picture.width) * static_cast<std::size_t>(picture.height);
        if (picture.width != width_ || picture.height != height_ || background_.size() != count * 3u) {
            begin(picture);
        }
        sceneChanged_ = false;
        score.assign(count, 0);
        const float dt = std::clamp(params.dt, 0.001f, 1.0f);
        if (learned_ < kLearnFrames) {
            accumulate(picture);
            return true;
        }
        const float sensitivity = std::clamp(params.sensitivity, 0.0f, 1.0f);
        const float floorThreshold = 40.0f - 26.0f * sensitivity;
        const float sigmaFactor = 5.0f - 2.5f * sensitivity;
        float gain[3] = {1.0f, 1.0f, 1.0f};
        estimateGain(picture, gain);
        const float backgroundRate = follow(dt, kBackgroundSeconds);
        const float foregroundRate = follow(dt, kForegroundSeconds);
        std::size_t strong = 0;
        for (std::size_t i = 0; i < count; ++i) {
            const float r = picture.rgb[i * 3u];
            const float g = picture.rgb[i * 3u + 1u];
            const float b = picture.rgb[i * 3u + 2u];
            float* bg = &background_[i * 3u];
            const float br = bg[0] * gain[0];
            const float bgG = bg[1] * gain[1];
            const float bb = bg[2] * gain[2];
            const float dr = r - br;
            const float dg = g - bgG;
            const float db = b - bb;
            const float distance = std::sqrt(dr * dr + dg * dg + db * db);
            const float sigma = std::sqrt(variance_[i]);
            const float threshold = std::max(floorThreshold, sigmaFactor * sigma);
            float z = distance / threshold;
            if (z > 1.0f) {
                const float lumaNow = 0.299f * r + 0.587f * g + 0.114f * b;
                const float lumaThen = 0.299f * br + 0.587f * bgG + 0.114f * bb;
                if (lumaThen > 20.0f && lumaNow > 8.0f) {
                    const float ratio = lumaNow / lumaThen;
                    if (ratio > 0.45f && ratio < 0.95f) {
                        const float cr = r / lumaNow - br / lumaThen;
                        const float cg = g / lumaNow - bgG / lumaThen;
                        const float cb = b / lumaNow - bb / lumaThen;
                        if (std::sqrt(cr * cr + cg * cg + cb * cb) < 0.08f) {
                            z *= 0.25f;
                        }
                    }
                }
            }
            const float value = smooth(0.8f, 1.8f, z);
            score[i] = toByte(value);
            if (value < 0.25f) {
                bg[0] += backgroundRate * (r / gain[0] - bg[0]);
                bg[1] += backgroundRate * (g / gain[1] - bg[1]);
                bg[2] += backgroundRate * (b / gain[2] - bg[2]);
                variance_[i] += backgroundRate * (std::min(distance * distance, 900.0f) - variance_[i]);
                variance_[i] = std::max(variance_[i], kInitialSigma * kInitialSigma * 0.25f);
            } else if (value > 0.75f) {
                bg[0] += foregroundRate * (r / gain[0] - bg[0]);
                bg[1] += foregroundRate * (g / gain[1] - bg[1]);
                bg[2] += foregroundRate * (b / gain[2] - bg[2]);
                ++strong;
            }
        }
        if (static_cast<float>(strong) > kSceneChangeFraction * static_cast<float>(count)) {
            ++changedFrames_;
        } else {
            changedFrames_ = 0;
        }
        if (changedFrames_ >= kSceneChangeFrames) {
            sceneChanged_ = true;
            begin(picture);
            score.assign(count, 0);
            previous_.assign(count, 0);
            return true;
        }
        morphology(score, picture.width, picture.height, false, scratch_);
        morphology(score, picture.width, picture.height, true, scratch_);
        morphology(score, picture.width, picture.height, true, scratch_);
        morphology(score, picture.width, picture.height, false, scratch_);
        previous_ = score;
        return true;
    }

    void learn() override {
        width_ = 0;
        height_ = 0;
        learned_ = 0;
        background_.clear();
        variance_.clear();
        previous_.clear();
    }

    void reset() override { learn(); }

    bool learning() const override { return learned_ < kLearnFrames; }

    bool sceneChanged() const override { return sceneChanged_; }

private:
    void begin(const Picture& picture) {
        width_ = picture.width;
        height_ = picture.height;
        const std::size_t count = static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_);
        background_.assign(count * 3u, 0.0f);
        variance_.assign(count, kInitialSigma * kInitialSigma);
        previous_.assign(count, 0);
        learned_ = 0;
        changedFrames_ = 0;
        accumulate(picture);
    }

    void accumulate(const Picture& picture) {
        if (learned_ == 0) {
            for (std::size_t i = 0; i < background_.size(); ++i) {
                background_[i] = picture.rgb[i];
            }
            learned_ = 1;
            return;
        }
        ++learned_;
        const float weight = 1.0f / static_cast<float>(learned_);
        const std::size_t count = variance_.size();
        for (std::size_t i = 0; i < count; ++i) {
            float distance2 = 0.0f;
            for (int c = 0; c < 3; ++c) {
                float& bg = background_[i * 3u + static_cast<std::size_t>(c)];
                const float d = static_cast<float>(picture.rgb[i * 3u + static_cast<std::size_t>(c)]) - bg;
                distance2 += d * d;
                bg += d * weight;
            }
            variance_[i] += (std::max(distance2, kInitialSigma * kInitialSigma * 0.25f) - variance_[i]) * weight;
        }
    }

    void estimateGain(const Picture& picture, float (&gain)[3]) const {
        const std::size_t count = static_cast<std::size_t>(picture.width) * static_cast<std::size_t>(picture.height);
        double sumNow[3] = {0.0, 0.0, 0.0};
        double sumThen[3] = {0.0, 0.0, 0.0};
        std::size_t used = 0;
        for (std::size_t i = 0; i < count; ++i) {
            if (i < previous_.size() && previous_[i] >= 64) {
                continue;
            }
            ++used;
            for (std::size_t c = 0; c < 3; ++c) {
                sumNow[c] += picture.rgb[i * 3u + c];
                sumThen[c] += background_[i * 3u + c];
            }
        }
        if (used * 10u < count) {
            return;
        }
        for (std::size_t c = 0; c < 3; ++c) {
            if (sumThen[c] > 16.0) {
                gain[c] = std::clamp(static_cast<float>(sumNow[c] / sumThen[c]), 0.2f, 5.0f);
            }
        }
    }

    int width_ = 0;
    int height_ = 0;
    int learned_ = 0;
    int changedFrames_ = 0;
    bool sceneChanged_ = false;
    std::vector<float> background_;
    std::vector<float> variance_;
    std::vector<std::uint8_t> previous_;
    std::vector<std::uint8_t> scratch_;
};

struct Matrix {
    int redV;
    int greenU;
    int greenV;
    int blueU;
};

constexpr Matrix kBt601{409, 100, 208, 516};
constexpr Matrix kBt709{459, 55, 136, 541};

void yuvToRgb(int y, int u, int v, const Matrix& matrix, int& r, int& g, int& b) {
    const int c = std::max(y - 16, 0) * 298;
    const int d = u - 128;
    const int e = v - 128;
    r = std::clamp((c + matrix.redV * e + 128) >> 8, 0, 255);
    g = std::clamp((c - matrix.greenU * d - matrix.greenV * e + 128) >> 8, 0, 255);
    b = std::clamp((c + matrix.blueU * d + 128) >> 8, 0, 255);
}

}

const char* modeName(Mode mode) {
    return mode == Mode::Difference ? "difference" : "off";
}

Mode modeFromName(const std::string& name, Mode fallback) {
    if (name == "off") {
        return Mode::Off;
    }
    if (name == "difference") {
        return Mode::Difference;
    }
    return fallback;
}

const char* tierName(Tier tier) {
    switch (tier) {
    case Tier::Fast:
        return "fast";
    case Tier::Fine:
        return "fine";
    default:
        return "balanced";
    }
}

Tier tierFromName(const std::string& name, Tier fallback) {
    if (name == "fast") {
        return Tier::Fast;
    }
    if (name == "balanced") {
        return Tier::Balanced;
    }
    if (name == "fine") {
        return Tier::Fine;
    }
    return fallback;
}

Tier clampTier(int value) {
    return static_cast<Tier>(std::clamp(value, 0, kTierCount - 1));
}

int clampRate(int value) {
    return std::clamp(value, kMinRate, kMaxRate);
}

int clampPercent(int value) {
    return std::clamp(value, 0, 100);
}

const char* inputName(Input input) {
    switch (input) {
    case Input::Ndi:
        return "ndi";
    case Input::Stream:
        return "stream";
    default:
        return "camera";
    }
}

bool parseInput(const std::string& text, Input& input, int& slot) {
    for (int i = 0; i < kInputCount; ++i) {
        const std::string name = inputName(static_cast<Input>(i));
        if (text.size() == name.size() + 1 && text.compare(0, name.size(), name) == 0) {
            const char digit = text.back();
            if (digit >= '1' && digit <= '4') {
                input = static_cast<Input>(i);
                slot = digit - '1';
                return true;
            }
        }
    }
    return false;
}

std::string inputText(Input input, int slot) {
    return std::string(inputName(input)) + std::to_string(std::clamp(slot, 0, 3) + 1);
}

Settings sanitised(const Settings& settings) {
    Settings out = settings;
    out.mode = static_cast<Mode>(std::clamp(static_cast<int>(settings.mode), 0, kModeCount - 1));
    out.input = static_cast<Input>(std::clamp(static_cast<int>(settings.input), 0, kInputCount - 1));
    out.slot = std::clamp(settings.slot, 0, 3);
    out.tier = clampTier(static_cast<int>(settings.tier));
    out.rate = clampRate(settings.rate);
    out.sensitivity = clampPercent(settings.sensitivity);
    out.softness = clampPercent(settings.softness);
    out.stability = clampPercent(settings.stability);
    return out;
}

int analysisWidth(Tier tier) {
    switch (tier) {
    case Tier::Fast:
        return 96;
    case Tier::Fine:
        return 256;
    default:
        return 160;
    }
}

void analysisSize(int sourceWidth, int sourceHeight, Tier tier, int& width, int& height) {
    width = 0;
    height = 0;
    if (sourceWidth <= 0 || sourceHeight <= 0) {
        return;
    }
    int w = std::min(analysisWidth(tier), sourceWidth);
    int h = static_cast<int>(std::lround(static_cast<double>(sourceHeight) * static_cast<double>(w) / static_cast<double>(sourceWidth)));
    while (h > 0 && static_cast<long long>(w) * h > kMaxPicturePixels && w > 16) {
        w = std::max(16, static_cast<int>(w * 0.9));
        h = static_cast<int>(std::lround(static_cast<double>(sourceHeight) * static_cast<double>(w) / static_cast<double>(sourceWidth)));
    }
    width = std::max(w, 1);
    height = std::max(h, 1);
}

bool reduce(const LiveFrame& frame, Tier tier, Picture& out) {
    out.width = 0;
    out.height = 0;
    out.rgb.clear();
    if (frame.width < 2 || frame.height < 1 || frame.width > 16384 || frame.height > 16384) {
        return false;
    }
    const bool uyvy = frame.format == LiveFormat::Uyvy;
    const bool rgba = frame.format == LiveFormat::Rgba;
    if (!uyvy && !rgba) {
        return false;
    }
    const long long minStride = static_cast<long long>(frame.width) * (uyvy ? 2 : 4);
    if (frame.stride < minStride || static_cast<unsigned long long>(frame.data.size()) < static_cast<unsigned long long>(frame.stride) * static_cast<unsigned long long>(frame.height)) {
        return false;
    }
    int width = 0;
    int height = 0;
    analysisSize(frame.width, frame.height, tier, width, height);
    if (width <= 0 || height <= 0) {
        return false;
    }
    out.width = width;
    out.height = height;
    out.id = frame.id;
    out.time = frame.capturedAt;
    out.rgb.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 3u);
    const Matrix& matrix = frame.height >= 720 ? kBt709 : kBt601;
    const double stepX = static_cast<double>(frame.width) / static_cast<double>(width);
    const double stepY = static_cast<double>(frame.height) / static_cast<double>(height);
    const std::uint8_t* data = frame.data.data();
    const std::size_t stride = static_cast<std::size_t>(frame.stride);
    for (int ty = 0; ty < height; ++ty) {
        for (int tx = 0; tx < width; ++tx) {
            int sum[3] = {0, 0, 0};
            for (int sy = 0; sy < 2; ++sy) {
                const int y = std::clamp(static_cast<int>((ty + 0.25 + 0.5 * sy) * stepY), 0, frame.height - 1);
                for (int sx = 0; sx < 2; ++sx) {
                    const int x = std::clamp(static_cast<int>((tx + 0.25 + 0.5 * sx) * stepX), 0, frame.width - 1);
                    int r = 0;
                    int g = 0;
                    int b = 0;
                    if (uyvy) {
                        const std::uint8_t* pair = data + static_cast<std::size_t>(y) * stride + static_cast<std::size_t>(x & ~1) * 2u;
                        yuvToRgb((x & 1) != 0 ? pair[3] : pair[1], pair[0], pair[2], matrix, r, g, b);
                    } else {
                        const std::uint8_t* px = data + static_cast<std::size_t>(y) * stride + static_cast<std::size_t>(x) * 4u;
                        r = px[0];
                        g = px[1];
                        b = px[2];
                    }
                    sum[0] += r;
                    sum[1] += g;
                    sum[2] += b;
                }
            }
            std::uint8_t* dst = &out.rgb[(static_cast<std::size_t>(ty) * static_cast<std::size_t>(width) + static_cast<std::size_t>(tx)) * 3u];
            dst[0] = static_cast<std::uint8_t>((sum[0] + 2) / 4);
            dst[1] = static_cast<std::uint8_t>((sum[1] + 2) / 4);
            dst[2] = static_cast<std::uint8_t>((sum[2] + 2) / 4);
        }
    }
    return true;
}

std::unique_ptr<Backend> makeBackend(Mode mode) {
    if (mode == Mode::Difference) {
        return std::make_unique<DifferenceBackend>();
    }
    return nullptr;
}

const char* signalName(int index) {
    static const char* const names[kSignalCount] = {"Person present", "Person X", "Person Y", "Person size", "Person motion", "Person count"};
    return index >= 0 && index < kSignalCount ? names[index] : "";
}

float signalValue(const Signals& signals, int index) {
    switch (index) {
    case 0:
        return std::clamp(signals.presence, 0.0f, 1.0f);
    case 1:
        return std::clamp(signals.x, 0.0f, 1.0f);
    case 2:
        return std::clamp(signals.y, 0.0f, 1.0f);
    case 3:
        return std::clamp(signals.size, 0.0f, 1.0f);
    case 4:
        return std::clamp(signals.motion, 0.0f, 1.0f);
    case 5:
        return std::clamp(signals.countValue, 0.0f, 1.0f);
    default:
        return 0.0f;
    }
}

std::vector<Blob> findBlobs(const MaskBitmap& mask, std::uint8_t threshold, int minArea, int maxBlobs) {
    std::vector<Blob> blobs;
    const int width = mask.width;
    const int height = mask.height;
    if (width <= 0 || height <= 0 || mask.coverage.size() != static_cast<std::size_t>(width) * static_cast<std::size_t>(height) || maxBlobs <= 0) {
        return blobs;
    }
    const std::size_t count = mask.coverage.size();
    std::vector<int> labels(count, 0);
    std::vector<int> parent(1, 0);
    auto find = [&](int a) {
        while (parent[static_cast<std::size_t>(a)] != a) {
            parent[static_cast<std::size_t>(a)] = parent[static_cast<std::size_t>(parent[static_cast<std::size_t>(a)])];
            a = parent[static_cast<std::size_t>(a)];
        }
        return a;
    };
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const std::size_t i = static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x);
            if (mask.coverage[i] < threshold) {
                continue;
            }
            const int left = x > 0 ? labels[i - 1] : 0;
            const int up = y > 0 ? labels[i - static_cast<std::size_t>(width)] : 0;
            if (left == 0 && up == 0) {
                labels[i] = static_cast<int>(parent.size());
                parent.push_back(labels[i]);
            } else if (left != 0 && up != 0) {
                const int a = find(left);
                const int b = find(up);
                labels[i] = std::min(a, b);
                parent[static_cast<std::size_t>(std::max(a, b))] = std::min(a, b);
            } else {
                labels[i] = left != 0 ? left : up;
            }
        }
    }
    std::vector<int> slot(parent.size(), -1);
    std::vector<double> sumX;
    std::vector<double> sumY;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const int label = labels[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)];
            if (label == 0) {
                continue;
            }
            const int root = find(label);
            int& index = slot[static_cast<std::size_t>(root)];
            if (index < 0) {
                index = static_cast<int>(blobs.size());
                Blob blob;
                blob.minX = x;
                blob.maxX = x;
                blob.minY = y;
                blob.maxY = y;
                blobs.push_back(blob);
                sumX.push_back(0.0);
                sumY.push_back(0.0);
            }
            Blob& blob = blobs[static_cast<std::size_t>(index)];
            blob.minX = std::min(blob.minX, x);
            blob.maxX = std::max(blob.maxX, x);
            blob.minY = std::min(blob.minY, y);
            blob.maxY = std::max(blob.maxY, y);
            ++blob.area;
            sumX[static_cast<std::size_t>(index)] += x + 0.5;
            sumY[static_cast<std::size_t>(index)] += y + 0.5;
        }
    }
    std::vector<Blob> kept;
    for (std::size_t i = 0; i < blobs.size(); ++i) {
        if (blobs[i].area < minArea) {
            continue;
        }
        blobs[i].cx = static_cast<float>(sumX[i] / blobs[i].area);
        blobs[i].cy = static_cast<float>(sumY[i] / blobs[i].area);
        kept.push_back(blobs[i]);
    }
    std::stable_sort(kept.begin(), kept.end(), [](const Blob& a, const Blob& b) { return a.area > b.area; });
    if (static_cast<int>(kept.size()) > maxBlobs) {
        kept.resize(static_cast<std::size_t>(maxBlobs));
    }
    return kept;
}

const char* trackStateName(TrackState state) {
    switch (state) {
    case TrackState::Tracked:
        return "tracked";
    case TrackState::Coasting:
        return "coasting";
    default:
        return "tentative";
    }
}

namespace {

struct Box {
    float left;
    float top;
    float right;
    float bottom;
};

Box boxOf(float x, float y, float width, float height) {
    return {x - width * 0.5f, y - height * 0.5f, x + width * 0.5f, y + height * 0.5f};
}

float overlap(const Box& a, const Box& b) {
    const float w = std::min(a.right, b.right) - std::max(a.left, b.left);
    const float h = std::min(a.bottom, b.bottom) - std::max(a.top, b.top);
    if (w <= 0.0f || h <= 0.0f) {
        return 0.0f;
    }
    const float inter = w * h;
    const float uni = (a.right - a.left) * (a.bottom - a.top) + (b.right - b.left) * (b.bottom - b.top) - inter;
    return uni > 0.0f ? inter / uni : 0.0f;
}

bool inside(const Box& box, float x, float y) {
    return x >= box.left && x <= box.right && y >= box.top && y <= box.bottom;
}

struct Candidate {
    float cost;
    std::size_t track;
    std::size_t blob;
};

}

void Tracker::update(const std::vector<Blob>& blobs, int width, int height, double dt) {
    const float step = static_cast<float>(std::clamp(dt, 0.001, 1.0));
    const float aspect = height > 0 ? static_cast<float>(width) / static_cast<float>(height) : 1.0f;
    struct Seen {
        float x;
        float y;
        float w;
        float h;
        float area;
    };
    std::vector<Seen> seen;
    if (width > 0 && height > 0) {
        for (const Blob& blob : blobs) {
            Seen item;
            item.x = blob.cx / static_cast<float>(width);
            item.y = blob.cy / static_cast<float>(height);
            item.w = static_cast<float>(blob.maxX - blob.minX + 1) / static_cast<float>(width);
            item.h = static_cast<float>(blob.maxY - blob.minY + 1) / static_cast<float>(height);
            item.area = static_cast<float>(blob.area) / (static_cast<float>(width) * static_cast<float>(height));
            seen.push_back(item);
        }
    }
    std::vector<float> px(tracks_.size());
    std::vector<float> py(tracks_.size());
    for (std::size_t t = 0; t < tracks_.size(); ++t) {
        px[t] = tracks_[t].x + tracks_[t].vx * step;
        py[t] = tracks_[t].y + tracks_[t].vy * step;
    }
    std::vector<Candidate> candidates;
    for (std::size_t t = 0; t < tracks_.size(); ++t) {
        const Track& track = tracks_[t];
        for (std::size_t b = 0; b < seen.size(); ++b) {
            const float dx = (px[t] - seen[b].x) * aspect;
            const float dy = py[t] - seen[b].y;
            const float distance = std::sqrt(dx * dx + dy * dy);
            const float reference = std::max({track.width * aspect, track.height, seen[b].w * aspect, seen[b].h, 0.02f});
            const float ratio = distance / reference;
            const float iou = overlap(boxOf(px[t], py[t], track.width, track.height), boxOf(seen[b].x, seen[b].y, seen[b].w, seen[b].h));
            if (ratio < 0.75f || iou > 0.1f) {
                candidates.push_back({ratio - 0.5f * iou, t, b});
            }
        }
    }
    std::stable_sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) { return a.cost < b.cost; });
    std::vector<int> blobOf(tracks_.size(), -1);
    std::vector<bool> blobUsed(seen.size(), false);
    for (const Candidate& candidate : candidates) {
        if (blobOf[candidate.track] >= 0 || blobUsed[candidate.blob]) {
            continue;
        }
        blobOf[candidate.track] = static_cast<int>(candidate.blob);
        blobUsed[candidate.blob] = true;
    }
    std::vector<Box> blobBoxes;
    for (const Seen& item : seen) {
        blobBoxes.push_back(boxOf(item.x, item.y, item.w, item.h));
    }
    for (std::size_t t = 0; t < tracks_.size(); ++t) {
        Track& track = tracks_[t];
        track.age += step;
        if (blobOf[t] >= 0) {
            const Seen& item = seen[static_cast<std::size_t>(blobOf[t])];
            const float rx = item.x - px[t];
            const float ry = item.y - py[t];
            track.x = px[t] + 0.7f * rx;
            track.y = py[t] + 0.7f * ry;
            track.vx += 0.25f * rx / step;
            track.vy += 0.25f * ry / step;
            track.vx = std::clamp(track.vx, -3.0f, 3.0f);
            track.vy = std::clamp(track.vy, -3.0f, 3.0f);
            track.width = track.hits > 0 ? 0.6f * track.width + 0.4f * item.w : item.w;
            track.height = track.hits > 0 ? 0.6f * track.height + 0.4f * item.h : item.h;
            const bool grew = track.area > 0.0f && item.area > 1.7f * track.area;
            track.area = track.hits > 0 ? 0.6f * track.area + 0.4f * item.area : item.area;
            track.merged = grew;
            track.missed = 0;
            track.lostFor = 0.0;
            ++track.hits;
            track.state = track.hits >= kConfirmHits ? TrackState::Tracked : TrackState::Tentative;
        } else {
            ++track.missed;
            track.lostFor += step;
            track.x = std::clamp(px[t], -0.25f, 1.25f);
            track.y = std::clamp(py[t], -0.25f, 1.25f);
            track.vx *= 0.85f;
            track.vy *= 0.85f;
            bool covered = false;
            for (const Box& box : blobBoxes) {
                if (inside(box, track.x, track.y)) {
                    covered = true;
                    break;
                }
            }
            track.merged = covered;
            if (track.state != TrackState::Tentative) {
                track.state = TrackState::Coasting;
            }
        }
    }
    tracks_.erase(std::remove_if(tracks_.begin(), tracks_.end(), [](const Track& track) {
        if (track.state == TrackState::Tentative) {
            return track.missed >= 2;
        }
        return track.lostFor > (track.merged ? kOccludedSeconds : kCoastSeconds);
    }), tracks_.end());
    for (std::size_t b = 0; b < seen.size(); ++b) {
        if (blobUsed[b] || static_cast<int>(tracks_.size()) >= kMaxTracks) {
            continue;
        }
        bool fragment = false;
        for (const Track& track : tracks_) {
            if (track.state == TrackState::Tentative) {
                continue;
            }
            if (inside(boxOf(track.x, track.y, track.width * 1.1f, track.height * 1.1f), seen[b].x, seen[b].y) && seen[b].w * seen[b].h < 0.6f * track.width * track.height) {
                fragment = true;
                break;
            }
        }
        if (fragment) {
            continue;
        }
        Track track;
        track.id = nextId_++;
        track.state = TrackState::Tentative;
        track.x = seen[b].x;
        track.y = seen[b].y;
        track.width = seen[b].w;
        track.height = seen[b].h;
        track.area = seen[b].area;
        track.hits = 1;
        tracks_.push_back(track);
    }
}

void Tracker::clear() {
    tracks_.clear();
}

void SignalFilter::update(const std::vector<Track>& tracks, double dt) {
    std::vector<const Track*> confirmed;
    for (const Track& track : tracks) {
        if (track.state != TrackState::Tentative) {
            confirmed.push_back(&track);
        }
    }
    signals_.count = static_cast<int>(confirmed.size());
    signals_.present = !confirmed.empty();
    signals_.countValue = static_cast<float>(std::min(signals_.count, kMaxTracks)) / static_cast<float>(kMaxTracks);
    const Track* primary = nullptr;
    float largest = 0.0f;
    for (const Track* track : confirmed) {
        largest = std::max(largest, track->area);
    }
    for (const Track* track : confirmed) {
        if (track->id == signals_.primary && track->area >= 0.5f * largest) {
            primary = track;
        }
    }
    if (primary == nullptr) {
        for (const Track* track : confirmed) {
            if (primary == nullptr || track->area > primary->area) {
                primary = track;
            }
        }
    }
    const float presenceTarget = signals_.present ? 1.0f : 0.0f;
    signals_.presence += (presenceTarget - signals_.presence) * follow(dt, presenceTarget > signals_.presence ? 0.05 : 0.4);
    if (primary != nullptr && primary->state == TrackState::Coasting) {
        signals_.primary = primary->id;
        signals_.motion += (0.0f - signals_.motion) * follow(dt, 0.2);
    } else if (primary != nullptr) {
        const float k = follow(dt, 0.08);
        if (signals_.primary != primary->id) {
            signals_.x = primary->x;
            signals_.y = 1.0f - primary->y;
            signals_.size = primary->height;
        }
        signals_.primary = primary->id;
        signals_.x += (std::clamp(primary->x, 0.0f, 1.0f) - signals_.x) * k;
        signals_.y += (std::clamp(1.0f - primary->y, 0.0f, 1.0f) - signals_.y) * k;
        signals_.size += (std::clamp(primary->height, 0.0f, 1.0f) - signals_.size) * k;
        const float speed = std::sqrt(primary->vx * primary->vx + primary->vy * primary->vy);
        signals_.motion += (std::clamp(speed / 0.6f, 0.0f, 1.0f) - signals_.motion) * follow(dt, 0.2);
    } else {
        signals_.primary = 0;
        signals_.motion += (0.0f - signals_.motion) * follow(dt, 0.2);
    }
}

void SignalFilter::clear() {
    signals_ = Signals();
}

void Stabiliser::configure(int softness, int stability) {
    softness_ = clampPercent(softness);
    stability_ = clampPercent(stability);
}

void Stabiliser::process(const std::vector<std::uint8_t>& score, int width, int height, double dt, MaskBitmap& out) {
    const std::size_t count = static_cast<std::size_t>(std::max(width, 0)) * static_cast<std::size_t>(std::max(height, 0));
    if (count == 0 || score.size() != count) {
        out = MaskBitmap();
        previous_.clear();
        return;
    }
    scratch_.assign(count, 0.0f);
    std::vector<float> current(count);
    for (std::size_t i = 0; i < count; ++i) {
        current[i] = static_cast<float>(score[i]) / 255.0f;
    }
    const int radius = static_cast<int>(std::lround(static_cast<double>(softness_) / 100.0 * 0.03 * width));
    if (radius > 0) {
        for (int pass = 0; pass < 2; ++pass) {
            for (int y = 0; y < height; ++y) {
                const float* row = current.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(width);
                float* dst = scratch_.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(width);
                for (int x = 0; x < width; ++x) {
                    float sum = 0.0f;
                    for (int k = -radius; k <= radius; ++k) {
                        sum += row[std::clamp(x + k, 0, width - 1)];
                    }
                    dst[x] = sum / static_cast<float>(radius * 2 + 1);
                }
            }
            for (int y = 0; y < height; ++y) {
                float* dst = current.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(width);
                for (int x = 0; x < width; ++x) {
                    float sum = 0.0f;
                    for (int k = -radius; k <= radius; ++k) {
                        sum += scratch_[static_cast<std::size_t>(std::clamp(y + k, 0, height - 1)) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)];
                    }
                    dst[x] = sum / static_cast<float>(radius * 2 + 1);
                }
            }
        }
    }
    if (previous_.size() != count || width_ != width || height_ != height) {
        previous_ = current;
        width_ = width;
        height_ = height;
    } else {
        const double s = static_cast<double>(stability_) / 100.0;
        const float up = follow(dt, 0.12 * s);
        const float down = follow(dt, 0.03 + 0.47 * s);
        for (std::size_t i = 0; i < count; ++i) {
            const float rate = current[i] > previous_[i] ? up : down;
            previous_[i] += (current[i] - previous_[i]) * rate;
        }
    }
    out.width = width;
    out.height = height;
    out.coverage.resize(count);
    for (std::size_t i = 0; i < count; ++i) {
        out.coverage[i] = toByte(smooth(0.1f, 0.9f, previous_[i]));
    }
}

void Stabiliser::decay(double dt, MaskBitmap& out) {
    if (previous_.empty()) {
        return;
    }
    const float keep = 1.0f - follow(dt, 0.25);
    for (float& value : previous_) {
        value *= keep;
    }
    out.width = width_;
    out.height = height_;
    out.coverage.resize(previous_.size());
    for (std::size_t i = 0; i < previous_.size(); ++i) {
        out.coverage[i] = toByte(smooth(0.1f, 0.9f, previous_[i]));
    }
}

void Stabiliser::clear() {
    previous_.clear();
    width_ = 0;
    height_ = 0;
}

const char* stateName(State state) {
    switch (state) {
    case State::Learning:
        return "learning";
    case State::Running:
        return "running";
    case State::SourceLost:
        return "source lost";
    case State::Failed:
        return "failed";
    default:
        return "off";
    }
}

Analyzer::Analyzer(std::unique_ptr<Backend> backend)
    : backend_(std::move(backend)) {
    if (backend_) {
        provenance_ = backend_->provenance();
    }
}

void Analyzer::configure(const Settings& settings) {
    settings_ = sanitised(settings);
    stabiliser_.configure(settings_.softness, settings_.stability);
}

bool Analyzer::process(const Picture& picture, Result& out) {
    out.note.clear();
    if (!backend_) {
        state_ = State::Failed;
        out.state = state_;
        out.note = "no segmentation backend";
        return false;
    }
    if (!picture.valid()) {
        state_ = State::Failed;
        out.state = state_;
        out.note = "the analysis picture is empty";
        return false;
    }
    if (picture.width != width_ || picture.height != height_) {
        tracker_.clear();
        stabiliser_.clear();
        width_ = picture.width;
        height_ = picture.height;
    }
    double dt = 1.0 / static_cast<double>(std::max(settings_.rate, 1));
    if (lastTime_ >= 0.0 && std::isfinite(picture.time) && picture.time > lastTime_) {
        dt = std::clamp(picture.time - lastTime_, 0.001, 1.0);
    }
    lastTime_ = std::isfinite(picture.time) ? picture.time : lastTime_;
    Params params;
    params.sensitivity = static_cast<float>(settings_.sensitivity) / 100.0f;
    params.dt = static_cast<float>(dt);
    std::string problem;
    if (!backend_->segment(picture, params, score_, problem)) {
        state_ = State::Failed;
        out.state = state_;
        out.note = problem;
        return false;
    }
    if (backend_->sceneChanged()) {
        tracker_.clear();
        filter_.clear();
        stabiliser_.clear();
        out.note = "the picture changed completely, learning the background again";
    }
    lostFor_ = 0.0;
    if (backend_->learning()) {
        state_ = State::Learning;
        tracker_.clear();
        stabiliser_.clear();
        out.mask.width = picture.width;
        out.mask.height = picture.height;
        out.mask.coverage.assign(static_cast<std::size_t>(picture.width) * static_cast<std::size_t>(picture.height), 0);
        tracker_.update({}, picture.width, picture.height, dt);
        filter_.update(tracker_.tracks(), dt);
    } else {
        state_ = State::Running;
        stabiliser_.process(score_, picture.width, picture.height, dt, out.mask);
        const int minArea = std::max(8, static_cast<int>(static_cast<long long>(picture.width) * picture.height / 250));
        const std::vector<Blob> blobs = findBlobs(out.mask, 128, minArea, kMaxTracks * 2);
        tracker_.update(blobs, picture.width, picture.height, dt);
        filter_.update(tracker_.tracks(), dt);
    }
    std::size_t strong = 0;
    for (std::uint8_t v : out.mask.coverage) {
        strong += v >= 128 ? 1u : 0u;
    }
    out.foregroundPercent = out.mask.coverage.empty() ? 0 : static_cast<int>(strong * 100u / out.mask.coverage.size());
    out.state = state_;
    out.tracks = tracker_.tracks();
    out.signals = filter_.signals();
    out.pictureId = picture.id;
    out.generation = ++generation_;
    return true;
}

void Analyzer::sourceLost(double dt, Result& out) {
    lostFor_ += std::max(dt, 0.0);
    out.note.clear();
    stabiliser_.decay(dt, out.mask);
    tracker_.update({}, std::max(width_, 1), std::max(height_, 1), dt);
    filter_.update(tracker_.tracks(), dt);
    state_ = State::SourceLost;
    out.state = state_;
    out.tracks = tracker_.tracks();
    out.signals = filter_.signals();
    out.generation = ++generation_;
    out.foregroundPercent = 0;
    for (std::uint8_t v : out.mask.coverage) {
        out.foregroundPercent += v >= 128 ? 1 : 0;
    }
    out.foregroundPercent = out.mask.coverage.empty() ? 0 : static_cast<int>(static_cast<std::size_t>(out.foregroundPercent) * 100u / out.mask.coverage.size());
}

void Analyzer::learn() {
    if (backend_) {
        backend_->learn();
    }
    tracker_.clear();
    filter_.clear();
    stabiliser_.clear();
    state_ = State::Learning;
}

void Analyzer::reset() {
    learn();
    lastTime_ = -1.0;
    lostFor_ = 0.0;
    width_ = 0;
    height_ = 0;
}

Governor::Governor(Tier tier, int rate)
    : requestedTier_(tier), requestedRate_(clampRate(rate)), tier_(tier), rate_(clampRate(rate)) {
}

void Governor::request(Tier tier, int rate) {
    const int clamped = clampRate(rate);
    if (tier == requestedTier_ && clamped == requestedRate_) {
        return;
    }
    requestedTier_ = tier;
    requestedRate_ = clamped;
    tier_ = tier;
    rate_ = clamped;
    disabled_ = false;
    recent_.clear();
    calm_ = 0;
}

double Governor::budgetMs() const {
    return kCpuShare * 1000.0 / static_cast<double>(std::max(rate_, 1));
}

double Governor::average() const {
    if (history_.empty()) {
        return 0.0;
    }
    return std::accumulate(history_.begin(), history_.end(), 0.0) / static_cast<double>(history_.size());
}

double Governor::p95() const {
    if (history_.empty()) {
        return 0.0;
    }
    std::vector<double> sorted = history_;
    std::sort(sorted.begin(), sorted.end());
    return sorted[std::min(sorted.size() - 1, static_cast<std::size_t>(static_cast<double>(sorted.size()) * 0.95))];
}

Governor::Decision Governor::record(double milliseconds) {
    if (!std::isfinite(milliseconds) || milliseconds < 0.0) {
        milliseconds = 0.0;
    }
    history_.push_back(milliseconds);
    if (history_.size() > 512) {
        history_.erase(history_.begin(), history_.begin() + 128);
    }
    if (disabled_) {
        return Decision::Keep;
    }
    recent_.push_back(milliseconds);
    if (recent_.size() > 6) {
        recent_.erase(recent_.begin());
    }
    if (recent_.size() < 6) {
        return Decision::Keep;
    }
    const double mean = std::accumulate(recent_.begin(), recent_.end(), 0.0) / static_cast<double>(recent_.size());
    if (mean > budgetMs()) {
        recent_.clear();
        calm_ = 0;
        ++lowered_;
        if (tier_ != Tier::Fast) {
            tier_ = static_cast<Tier>(static_cast<int>(tier_) - 1);
            return Decision::LowerTier;
        }
        if (rate_ > kMinRate) {
            rate_ = std::max(kMinRate, rate_ / 2);
            return Decision::LowerRate;
        }
        disabled_ = true;
        return Decision::Disable;
    }
    const bool belowRequested = tier_ != requestedTier_ || rate_ != requestedRate_;
    if (belowRequested && mean < 0.35 * budgetMs()) {
        ++calm_;
    } else {
        calm_ = 0;
    }
    if (calm_ >= 60) {
        if (rate_ < requestedRate_) {
            const int next = std::min(requestedRate_, rate_ * 2);
            if (mean < 0.35 * kCpuShare * 1000.0 / static_cast<double>(next)) {
                rate_ = next;
                calm_ = 0;
                recent_.clear();
                return Decision::RaiseRate;
            }
        } else if (tier_ != requestedTier_) {
            const double widthNow = analysisWidth(tier_);
            const double widthNext = analysisWidth(static_cast<Tier>(static_cast<int>(tier_) + 1));
            if (mean * (widthNext * widthNext) / (widthNow * widthNow) < 0.5 * budgetMs()) {
                tier_ = static_cast<Tier>(static_cast<int>(tier_) + 1);
                calm_ = 0;
                recent_.clear();
                return Decision::RaiseTier;
            }
        }
        calm_ = 0;
    }
    return Decision::Keep;
}

void Governor::reset() {
    tier_ = requestedTier_;
    rate_ = requestedRate_;
    disabled_ = false;
    recent_.clear();
    history_.clear();
    calm_ = 0;
    lowered_ = 0;
}

}
