#include "app/output_selftest.hpp"

#include "core/dynlib.hpp"
#include "core/frame_lock.hpp"
#include "core/log.hpp"
#include "core/mask_images.hpp"
#include "core/output_map.hpp"
#include "core/spatial_mesh.hpp"
#include "render/output_manager.hpp"
#include "render/texture_history.hpp"

#include <glad/gl.h>

#include <GLFW/glfw3.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <vector>

namespace nvj {

namespace {

struct Rgb {
    int r = 0;
    int g = 0;
    int b = 0;
};

constexpr Rgb kRed{255, 0, 0};
constexpr Rgb kGreen{0, 255, 0};
constexpr Rgb kBlue{0, 0, 255};
constexpr Rgb kYellow{255, 255, 0};
constexpr Rgb kBlack{0, 0, 0};
constexpr Rgb kWhiteColor{255, 255, 255};

std::string describe(const Rgb& c) {
    return "(" + std::to_string(c.r) + "," + std::to_string(c.g) + "," + std::to_string(c.b) + ")";
}

class Runner {
public:
    explicit Runner(GLFWwindow* share) : share_(share) {
        specs_ = defaultOutputs();
        manager_.attach(&specs_);
    }

    ~Runner() {
        manager_.closeAll();
        if (texture_) {
            glDeleteTextures(1, &texture_);
        }
    }

    bool start() {
        specs_[0].enabled = true;
        specs_[0].fullscreen = false;
        if (!manager_.open(0, share_)) {
            logLine("Output self test: could not open the output window");
            return false;
        }
        const int w = 64;
        const int h = 36;
        std::vector<uint8_t> pixels(static_cast<size_t>(w) * static_cast<size_t>(h) * 4u, 255);
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                const bool top = y >= h / 2;
                const bool left = x < w / 2;
                Rgb c = top ? (left ? kRed : kGreen) : (left ? kBlue : kYellow);
                uint8_t* p = &pixels[(static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)) * 4u];
                p[0] = static_cast<uint8_t>(c.r);
                p[1] = static_cast<uint8_t>(c.g);
                p[2] = static_cast<uint8_t>(c.b);
                p[3] = 255;
            }
        }
        glGenTextures(1, &texture_);
        glBindTexture(GL_TEXTURE_2D, texture_);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindTexture(GL_TEXTURE_2D, 0);
        glFinish();
        return true;
    }

    bool draw(const OutputSpec& spec) {
        specs_[0] = spec;
        specs_[0].enabled = true;
        specs_[0].fullscreen = false;
        specs_[0].map.fill = true;
        const bool ok = manager_.capture(0, texture_, 64, 36, frame_);
        if (!ok) {
            ++checks_;
            ++failures_;
            logLine("Output self test: capture failed");
        }
        return ok;
    }

    void setSolid(const Rgb& color) {
        std::vector<uint8_t> pixels(64u * 36u * 4u, 255);
        for (size_t i = 0; i < 64u * 36u; ++i) {
            pixels[i * 4u] = static_cast<uint8_t>(color.r);
            pixels[i * 4u + 1u] = static_cast<uint8_t>(color.g);
            pixels[i * 4u + 2u] = static_cast<uint8_t>(color.b);
        }
        glBindTexture(GL_TEXTURE_2D, texture_);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 64, 36, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        glBindTexture(GL_TEXTURE_2D, 0);
        glFinish();
    }

    void setSpec(const OutputSpec& spec) {
        specs_[0] = spec;
        specs_[0].enabled = true;
        specs_[0].fullscreen = false;
        specs_[0].map.fill = true;
    }

    void render() { manager_.render(texture_, 64, 36); }

    OutputManager& manager() { return manager_; }
    std::array<OutputSpec, kMaxOutputs>& specs() { return specs_; }

    bool openSecond() {
        specs_[1].enabled = true;
        specs_[1].fullscreen = false;
        specs_[1].map.fill = true;
        return manager_.open(1, share_);
    }

    void closeSecond() {
        manager_.close(1);
        specs_[1].enabled = false;
        specs_[1].delayFrames = 0;
    }

    Rgb at(float fx, float fy) const {
        const int x = std::clamp(static_cast<int>(fx * static_cast<float>(frame_.width)), 0, frame_.width - 1);
        const int top = std::clamp(static_cast<int>(fy * static_cast<float>(frame_.height)), 0, frame_.height - 1);
        const int row = frame_.height - 1 - top;
        const uint8_t* p = &frame_.rgb[(static_cast<size_t>(row) * static_cast<size_t>(frame_.width) + static_cast<size_t>(x)) * 3u];
        return {p[0], p[1], p[2]};
    }

    void expect(const char* label, float fx, float fy, const Rgb& want, int tolerance = 4) {
        ++checks_;
        const Rgb got = at(fx, fy);
        if (std::abs(got.r - want.r) > tolerance || std::abs(got.g - want.g) > tolerance || std::abs(got.b - want.b) > tolerance) {
            ++failures_;
            logLine(std::string("Output self test FAILED: ") + label + " at (" + std::to_string(fx) + "," + std::to_string(fy) + ") expected " + describe(want) + " got " + describe(got));
        }
    }

    void expectRange(const char* label, float fx, float fy, int channel, int low, int high) {
        ++checks_;
        const Rgb got = at(fx, fy);
        const int value = channel == 0 ? got.r : (channel == 1 ? got.g : got.b);
        if (value < low || value > high) {
            ++failures_;
            logLine(std::string("Output self test FAILED: ") + label + " channel " + std::to_string(channel) + " value " + std::to_string(value) + " outside " + std::to_string(low) + ".." + std::to_string(high));
        }
    }

    void condition(const char* label, bool ok) {
        ++checks_;
        if (!ok) {
            ++failures_;
            logLine(std::string("Output self test FAILED: ") + label);
        }
    }

    double frameMilliseconds(const OutputSpec& spec, int frames) {
        specs_[0] = spec;
        specs_[0].enabled = true;
        specs_[0].fullscreen = false;
        specs_[0].map.fill = true;
        manager_.render(texture_, 64, 36);
        manager_.render(texture_, 64, 36);
        const auto begin = std::chrono::steady_clock::now();
        for (int i = 0; i < frames; ++i) {
            manager_.render(texture_, 64, 36);
        }
        return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count() / frames;
    }

    int frameWidth() const { return frame_.width; }
    int frameHeight() const { return frame_.height; }
    size_t gpuBytes() const { return manager_.gpuBytes(); }
    int checks() const { return checks_; }
    int failures() const { return failures_; }

private:
    GLFWwindow* share_ = nullptr;
    OutputManager manager_;
    std::array<OutputSpec, kMaxOutputs> specs_;
    GLuint texture_ = 0;
    OutputCapture frame_;
    int checks_ = 0;
    int failures_ = 0;
};

Rgb corrected(const OutputColor& color, const Rgb& source) {
    float rgb[3] = {static_cast<float>(source.r) / 255.0f, static_cast<float>(source.g) / 255.0f, static_cast<float>(source.b) / 255.0f};
    applyColor(color, rgb);
    return {static_cast<int>(std::lround(rgb[0] * 255.0f)), static_cast<int>(std::lround(rgb[1] * 255.0f)), static_cast<int>(std::lround(rgb[2] * 255.0f))};
}

OutputMapping region(float sx, float sy, float sw, float sh, float ox, float oy, float ow, float oh) {
    OutputMapping map;
    map.slice = {sx, sy, sw, sh};
    map.corners = {{{ox, oy}, {ox + ow, oy}, {ox + ow, oy + oh}, {ox, oy + oh}}};
    return map;
}

void quadrants(Runner& run, const char* label, const Rgb& tl, const Rgb& tr, const Rgb& bl, const Rgb& br) {
    run.expect(label, 0.25f, 0.25f, tl);
    run.expect(label, 0.75f, 0.25f, tr);
    run.expect(label, 0.25f, 0.75f, bl);
    run.expect(label, 0.75f, 0.75f, br);
}

void caseDefault(Runner& run) {
    OutputSpec spec = defaultOutputSpec(0);
    run.draw(spec);
    quadrants(run, "default", kRed, kGreen, kBlue, kYellow);
}

void caseSlices(Runner& run) {
    OutputSpec spec = defaultOutputSpec(0);
    spec.map = region(0.5f, 0.0f, 0.5f, 1.0f, 0.0f, 0.0f, 0.5f, 1.0f);
    addSlice(spec);
    spec.slices[0].map = region(0.0f, 0.0f, 0.5f, 1.0f, 0.5f, 0.0f, 0.5f, 1.0f);
    run.draw(spec);
    quadrants(run, "two slices swapped", kGreen, kRed, kYellow, kBlue);

    spec.slices[0].enabled = false;
    run.draw(spec);
    run.expect("disabled slice leaves its area dark", 0.75f, 0.25f, kBlack);
    run.expect("first slice stays", 0.25f, 0.25f, kGreen);

    spec = defaultOutputSpec(0);
    addSlice(spec);
    spec.slices[0].map = region(0.0f, 0.0f, 0.5f, 0.5f, 0.6f, 0.6f, 0.3f, 0.3f);
    run.draw(spec);
    run.expect("later slice covers the earlier one", 0.75f, 0.75f, kRed);
    run.expect("earlier slice stays elsewhere", 0.95f, 0.95f, kYellow);
    run.expect("earlier slice stays at the top left", 0.25f, 0.25f, kRed);

    spec = defaultOutputSpec(0);
    addSlice(spec);
    spec.slices[0].map.flipX = true;
    spec.slices[0].map.mask = maskRectangle(0.5f, 0.0f, 1.0f, 1.0f);
    run.draw(spec);
    quadrants(run, "masked second slice", kRed, kRed, kBlue, kBlue);
}

void caseMask(Runner& run) {
    OutputSpec spec = defaultOutputSpec(0);
    spec.map.mask = maskRectangle(0.1f, 0.1f, 0.9f, 0.9f);
    run.draw(spec);
    run.expect("mask inside", 0.25f, 0.25f, kRed);
    run.expect("mask inside right", 0.75f, 0.25f, kGreen);
    run.expect("mask outside top left", 0.05f, 0.05f, kBlack);
    run.expect("mask outside bottom right", 0.95f, 0.95f, kBlack);

    spec.map.mask.invert = true;
    run.draw(spec);
    run.expect("inverted mask inside", 0.25f, 0.25f, kBlack);
    run.expect("inverted mask outside top left", 0.05f, 0.05f, kRed);
    run.expect("inverted mask outside bottom right", 0.95f, 0.95f, kYellow);

    spec.map.mask = maskRectangle(0.3f, 0.3f, 0.7f, 0.7f);
    spec.map.mask.feather = 0.1f;
    run.draw(spec);
    run.expect("feather centre", 0.45f, 0.45f, kRed);
    run.expect("feather far outside", 0.1f, 0.1f, kBlack);
    run.expectRange("feather edge is half", 0.3f, 0.4f, 0, 90, 170);
    run.expect("feather edge keeps the hue", 0.3f, 0.4f, Rgb{run.at(0.3f, 0.4f).r, 0, 0});

    spec.map.mask.enabled = false;
    run.draw(spec);
    quadrants(run, "disabled mask", kRed, kGreen, kBlue, kYellow);
}

void caseColor(Runner& run) {
    OutputSpec spec = defaultOutputSpec(0);
    spec.color.saturation = 0.0f;
    run.draw(spec);
    const Rgb grey = corrected(spec.color, kRed);
    run.expect("grey red", 0.25f, 0.25f, grey);
    run.expect("grey equal channels", 0.25f, 0.25f, Rgb{grey.r, grey.r, grey.r});
    run.expect("grey green", 0.75f, 0.25f, corrected(spec.color, kGreen));

    spec.color = OutputColor{};
    spec.color.red = 0.5f;
    run.draw(spec);
    run.expect("red gain on yellow", 0.75f, 0.75f, Rgb{128, 255, 0});
    run.expect("red gain on blue", 0.25f, 0.75f, kBlue);

    spec.color = OutputColor{};
    spec.color.saturation = 0.6f;
    spec.color.contrast = 1.3f;
    spec.color.gamma = 1.4f;
    spec.color.red = 0.9f;
    spec.color.blue = 1.1f;
    spec.color.black = 0.03f;
    run.draw(spec);
    run.expect("combined red", 0.25f, 0.25f, corrected(spec.color, kRed));
    run.expect("combined green", 0.75f, 0.25f, corrected(spec.color, kGreen));
    run.expect("combined blue", 0.25f, 0.75f, corrected(spec.color, kBlue));
    run.expect("combined yellow", 0.75f, 0.75f, corrected(spec.color, kYellow));

    spec.color = OutputColor{};
    spec.color.black = 0.2f;
    spec.map.mask = maskRectangle(0.1f, 0.1f, 0.9f, 0.9f);
    run.draw(spec);
    run.expect("colour does not leak outside the mask", 0.05f, 0.05f, kBlack);
    run.expect("black lift on the image", 0.25f, 0.25f, Rgb{255, 51, 51});
}

void caseBlackLevel(Runner& run) {
    OutputSpec spec = defaultOutputSpec(0);
    spec.map.blend.left = 0.2f;
    spec.map.blend.blackLevel = 0.1f;
    run.draw(spec);
    run.expect("lift outside the overlap", 0.3f, 0.25f, Rgb{255, 26, 26});
    run.expect("lift outside the overlap, green", 0.75f, 0.25f, Rgb{26, 255, 26});
    run.expect("no lift inside the overlap", 0.02f, 0.25f, Rgb{run.at(0.02f, 0.25f).r, 0, 0}, 2);
    run.expectRange("overlap fades", 0.02f, 0.25f, 0, 15, 110);

    spec.map.blend.blackLevel = 0.0f;
    run.draw(spec);
    run.expect("no black level", 0.3f, 0.25f, kRed);

    spec.map.blend = EdgeBlend{};
    spec.map.blend.blackLevel = 0.1f;
    run.draw(spec);
    run.expect("black level without blend does nothing", 0.3f, 0.25f, kRed);
    quadrants(run, "black level without blend", kRed, kGreen, kBlue, kYellow);
}

void caseWarp(Runner& run) {
    OutputSpec spec = defaultOutputSpec(0);
    resizeWarp(spec.map, 17, 17);
    run.draw(spec);
    quadrants(run, "17x17 warp without offsets", kRed, kGreen, kBlue, kYellow);

    run.draw(defaultOutputSpec(0));
    run.expect("unwarped control", 0.51f, 0.49f, kGreen);

    spec.map.warp[8 * 17 + 8] = {0.03f, 0.0f};
    run.draw(spec);
    run.expect("centre node pulls the picture", 0.51f, 0.49f, kRed);
    run.expect("far corners stay", 0.1f, 0.1f, kRed);
    run.expect("far corners stay, bottom right", 0.9f, 0.9f, kYellow);
}

int decodeBars(Runner& run, float fy) {
    int value = 0;
    for (int column = 0; column < kSyncBarBits; ++column) {
        const Rgb c = run.at((static_cast<float>(column) + 0.5f) / static_cast<float>(kSyncBarBits), fy);
        value = (value << 1) | (c.r > 128 ? 1 : 0);
    }
    return value;
}

void checkSyncBars(Runner& run, const char* label, int delay) {
    OutputSpec spec = defaultOutputSpec(0);
    spec.delayFrames = delay;
    run.setSpec(spec);
    for (int i = 0; i < 7; ++i) {
        run.render();
    }
    run.draw(spec);
    const int expected = syncBarValue(run.manager().frameCount(), delay);
    run.condition((std::string(label) + " counter bits").c_str(), decodeBars(run, 0.125f) == expected);
    const int flash = (expected & 1) != 0 ? 255 : 0;
    run.expect((std::string(label) + " flash").c_str(), 0.3f, 0.375f, {flash, flash, flash});
    const int cell = expected % 60;
    run.expect((std::string(label) + " moving bar").c_str(), (static_cast<float>(cell) + 0.5f) / 60.0f, 0.625f, kWhiteColor);
    run.expectRange((std::string(label) + " moving bar background").c_str(), (static_cast<float>((cell + 30) % 60) + 0.5f) / 60.0f, 0.625f, 0, 0, 40);
}

void caseSyncBars(Runner& run) {
    run.manager().setSyncBars(true);
    checkSyncBars(run, "sync bars", 0);
    checkSyncBars(run, "sync bars with delay", 3);
    run.manager().setSyncBars(false);
    OutputSpec spec = defaultOutputSpec(0);
    run.draw(spec);
    quadrants(run, "after sync bars", kRed, kGreen, kBlue, kYellow);
}

void caseDelay(Runner& run) {
    OutputSpec spec = defaultOutputSpec(0);
    run.setSpec(spec);
    run.setSolid(kBlack);
    run.render();
    run.condition("no history without delay", !run.manager().historyActive() && run.manager().historyBytes() == 0);

    spec.delayFrames = 2;
    run.setSpec(spec);
    run.setSolid(kRed);
    run.render();
    run.condition("history allocated", run.manager().historyActive());
    run.condition("history size", run.manager().historyBytes() == 3u * 64u * 36u * 4u);
    run.draw(spec);
    run.expect("delay 2 first frame uses the oldest", 0.5f, 0.5f, kRed);

    run.setSolid(kGreen);
    run.render();
    run.draw(spec);
    run.expect("delay 2 second frame", 0.5f, 0.5f, kRed);

    run.setSolid(kBlue);
    run.render();
    run.draw(spec);
    run.expect("delay 2", 0.5f, 0.5f, kRed);
    spec.delayFrames = 1;
    run.draw(spec);
    run.expect("delay 1", 0.5f, 0.5f, kGreen);
    spec.delayFrames = 0;
    run.draw(spec);
    run.expect("delay 0", 0.5f, 0.5f, kBlue);

    spec.delayFrames = 2;
    run.setSpec(spec);
    run.setSolid(kYellow);
    run.render();
    run.draw(spec);
    run.expect("delay 2 after wrap", 0.5f, 0.5f, kGreen);
    spec.delayFrames = 1;
    run.draw(spec);
    run.expect("delay 1 after wrap", 0.5f, 0.5f, kBlue);

    spec.delayFrames = 0;
    run.setSpec(spec);
    run.render();
    run.condition("history released", !run.manager().historyActive() && run.manager().historyBytes() == 0);
    run.setSolid(kBlack);
    run.setSolid(kBlack);
}

void caseFrameLock(Runner& run) {
    run.condition("second output opens", run.openSecond());
    if (!run.manager().isOpen(1)) {
        return;
    }
    run.specs()[1].delayFrames = 3;
    run.render();
    run.condition("history follows the longest delay", run.manager().historyActive() && run.manager().historyBytes() == 4u * 64u * 36u * 4u);
    run.specs()[1].delayFrames = 0;
    run.render();
    run.condition("history released again", !run.manager().historyActive());

    auto average = [&run](bool lock, int frames, int& samples) {
        run.manager().setFrameLock(lock);
        run.manager().resetSkew();
        for (int i = 0; i < frames; ++i) {
            run.render();
        }
        samples = run.manager().skew().count();
        return run.manager().skew().average();
    };
    int lockedSamples = 0;
    int sequentialSamples = 0;
    average(true, 5, lockedSamples);
    const double locked = average(true, 60, lockedSamples);
    average(false, 5, sequentialSamples);
    const double sequential = average(false, 60, sequentialSamples);
    run.manager().setFrameLock(true);
    run.condition("swap skew sampled with two outputs", lockedSamples >= 50 && sequentialSamples >= 50);
    run.condition("locked swaps are closer together than sequential ones", locked < sequential);
    logLine("Output self test swap skew: locked " + std::to_string(locked) + " ms, one by one " + std::to_string(sequential) + " ms");
    run.closeSecond();
    run.manager().resetSkew();
    run.render();
    run.condition("no skew with one output", run.manager().skew().count() == 0);
}

Rgb sourceAt(float fx, float fy) {
    if (fy < 0.5f) {
        return fx < 0.5f ? kRed : kGreen;
    }
    return fx < 0.5f ? kBlue : kYellow;
}

float outlineDistance(const std::vector<Vec2f>& outline, float x, float y) {
    float best = 1.0e9f;
    for (size_t i = 0; i < outline.size(); ++i) {
        const Vec2f& a = outline[i];
        const Vec2f& b = outline[(i + 1) % outline.size()];
        const float dx = b.x - a.x;
        const float dy = b.y - a.y;
        const float lengthSquared = dx * dx + dy * dy;
        float t = lengthSquared > 1.0e-12f ? ((x - a.x) * dx + (y - a.y) * dy) / lengthSquared : 0.0f;
        t = std::clamp(t, 0.0f, 1.0f);
        const float ex = a.x + t * dx - x;
        const float ey = a.y + t * dy - y;
        best = std::min(best, std::sqrt(ex * ex + ey * ey));
    }
    return best;
}

void sweepMask(Runner& run, const std::string& label, const SliceMask& mask) {
    OutputSpec spec = defaultOutputSpec(0);
    spec.map.mask = mask;
    run.draw(spec);
    const std::vector<Vec2f> outline = maskOutline(mask);
    int inside = 0;
    int outside = 0;
    for (int j = 0; j < 9; ++j) {
        for (int i = 0; i < 15; ++i) {
            const float fx = (static_cast<float>(i) + 0.5f) / 15.0f;
            const float fy = (static_cast<float>(j) + 0.5f) / 9.0f;
            if (std::fabs(fx - 0.5f) < 0.03f || std::fabs(fy - 0.5f) < 0.03f || outlineDistance(outline, fx, fy) < 0.035f) {
                continue;
            }
            const bool in = maskContains(mask, fx, fy);
            (in ? inside : outside) += 1;
            const std::string name = label + " probe " + std::to_string(i) + "," + std::to_string(j);
            run.expect(name.c_str(), fx, fy, in ? sourceAt(fx, fy) : kBlack);
        }
    }
    run.condition((label + " has probes inside and outside").c_str(), inside >= 6 && outside >= 6);
}

void caseSliceColor(Runner& run) {
    OutputSpec spec = defaultOutputSpec(0);
    spec.map.sliceColor.red = 0.5f;
    run.draw(spec);
    run.expect("slice gain on yellow", 0.75f, 0.75f, Rgb{128, 255, 0});
    run.expect("slice gain keeps blue", 0.25f, 0.75f, kBlue);
    run.expect("slice gain keeps green", 0.75f, 0.25f, kGreen);

    spec = defaultOutputSpec(0);
    spec.map = region(0.0f, 0.0f, 0.5f, 1.0f, 0.0f, 0.0f, 0.5f, 1.0f);
    addSlice(spec);
    spec.slices[0].map = region(0.5f, 0.0f, 0.5f, 1.0f, 0.5f, 0.0f, 0.5f, 1.0f);
    spec.slices[0].map.sliceColor.saturation = 0.0f;
    run.draw(spec);
    run.expect("first slice keeps its colour", 0.25f, 0.25f, kRed);
    run.expect("first slice keeps its colour below", 0.25f, 0.75f, kBlue);
    const Rgb grey = corrected(spec.slices[0].map.sliceColor, kGreen);
    run.expect("second slice is desaturated", 0.75f, 0.25f, grey);
    run.expect("second slice has equal channels", 0.75f, 0.25f, Rgb{grey.r, grey.r, grey.r});
    run.expect("second slice below is desaturated", 0.75f, 0.75f, corrected(spec.slices[0].map.sliceColor, kYellow));

    spec = defaultOutputSpec(0);
    spec.map.sliceColor.gamma = 1.6f;
    spec.map.sliceColor.red = 0.9f;
    spec.color.contrast = 1.4f;
    spec.color.saturation = 0.7f;
    spec.color.black = 0.05f;
    run.draw(spec);
    const Rgb sliceFirst = corrected(spec.color, corrected(spec.map.sliceColor, kYellow));
    const Rgb outputFirst = corrected(spec.map.sliceColor, corrected(spec.color, kYellow));
    run.condition("the two orders give different colours", std::abs(sliceFirst.g - outputFirst.g) > 6 || std::abs(sliceFirst.r - outputFirst.r) > 6);
    run.expect("slice colour is applied before the output colour", 0.75f, 0.75f, sliceFirst, 6);
    run.expect("slice and output colour on red", 0.25f, 0.25f, corrected(spec.color, corrected(spec.map.sliceColor, kRed)), 6);

    spec = defaultOutputSpec(0);
    spec.map.sliceColor.black = 0.2f;
    spec.map.mask = maskRectangle(0.1f, 0.1f, 0.9f, 0.9f);
    run.draw(spec);
    run.expect("slice colour does not leak outside the mask", 0.05f, 0.05f, kBlack);
    run.expect("slice black lift on the image", 0.25f, 0.25f, Rgb{255, 51, 51});

    spec = defaultOutputSpec(0);
    spec.map.sliceColor = OutputColor{};
    run.draw(spec);
    quadrants(run, "default slice colour changes nothing", kRed, kGreen, kBlue, kYellow);
}

void caseCurves(Runner& run) {
    SliceMask straight = maskRectangle(0.2f, 0.2f, 0.8f, 0.8f);
    OutputSpec spec = defaultOutputSpec(0);
    spec.map.mask = straight;
    run.draw(spec);
    run.expect("straight mask cuts the bulge area", 0.42f, 0.17f, kBlack);

    SliceMask curved = straight;
    maskSmooth(curved, 1.0f);
    run.condition("smoothing creates handles", maskHasCurves(curved) && curved.handles.size() == curved.points.size());
    spec.map.mask = curved;
    run.draw(spec);
    run.expect("curved mask bulges over the straight edge", 0.42f, 0.17f, sourceAt(0.42f, 0.17f));
    run.expect("curved mask keeps the centre", 0.45f, 0.45f, kRed);
    run.expect("curved mask stays closed far outside", 0.05f, 0.05f, kBlack);
    sweepMask(run, "smoothed square", curved);

    SliceMask handmade = maskRectangle(0.25f, 0.25f, 0.75f, 0.75f);
    handmade.handles.assign(4, MaskHandle{});
    handmade.handles[0].out = {0.2f, -0.15f};
    handmade.handles[1].in = {-0.2f, -0.15f};
    handmade.handles[2].out = {-0.1f, 0.2f};
    handmade.handles[3].in = {0.1f, 0.2f};
    sweepMask(run, "hand placed handles", handmade);

    SliceMask feathered = curved;
    feathered.feather = 0.05f;
    spec.map.mask = feathered;
    run.draw(spec);
    run.expect("feathered curve keeps the centre", 0.45f, 0.45f, kRed);
    run.expect("feathered curve cuts far outside", 0.05f, 0.05f, kBlack);

    SliceMask inverted = curved;
    inverted.invert = true;
    spec.map.mask = inverted;
    run.draw(spec);
    run.expect("inverted curve hides the inside", 0.45f, 0.45f, kBlack);
    run.expect("inverted curve shows the outside", 0.05f, 0.05f, kRed);
    run.expect("inverted curve hides the bulge", 0.42f, 0.17f, kBlack);

    maskClearCurves(curved);
    spec.map.mask = curved;
    run.draw(spec);
    run.expect("clearing the curves goes back to straight edges", 0.42f, 0.17f, kBlack);
}

void caseFreehand(Runner& run) {
    std::vector<Vec2f> stroke;
    for (int i = 0; i <= 240; ++i) {
        const float a = static_cast<float>(i) / 240.0f * 6.2831853f;
        const float wobble = 0.004f * std::sin(a * 23.0f);
        stroke.push_back({0.5f + (0.32f + wobble) * std::cos(a) * 0.5625f, 0.5f + (0.32f + wobble) * std::sin(a)});
    }
    SliceMask mask;
    run.condition("a freehand stroke becomes a mask", maskFromStroke(mask, stroke));
    run.condition("the stroke is reduced to at most 32 points", mask.points.size() >= 3 && mask.points.size() <= static_cast<size_t>(kMaxMaskPoints));
    run.condition("a freehand mask is enabled and has no curves", mask.enabled && !maskHasCurves(mask));
    sweepMask(run, "freehand circle", mask);

    OutputSpec spec = defaultOutputSpec(0);
    spec.map.mask = mask;
    run.draw(spec);
    run.expect("freehand mask shows the inside", 0.4f, 0.4f, kRed);
    run.expect("freehand mask hides the corner", 0.03f, 0.03f, kBlack);

    SliceMask tooShort;
    run.condition("a tiny stroke is refused", !maskFromStroke(tooShort, {{0.5f, 0.5f}, {0.5001f, 0.5001f}}));
}

std::vector<uint8_t> makeTga(int width, int height, bool alpha, const std::function<void(int, int, uint8_t*)>& pixel) {
    std::vector<uint8_t> bytes(18, 0);
    bytes[2] = 2;
    bytes[12] = static_cast<uint8_t>(width & 0xff);
    bytes[13] = static_cast<uint8_t>(width >> 8);
    bytes[14] = static_cast<uint8_t>(height & 0xff);
    bytes[15] = static_cast<uint8_t>(height >> 8);
    bytes[16] = alpha ? 32 : 24;
    bytes[17] = static_cast<uint8_t>(0x20 | (alpha ? 8 : 0));
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            uint8_t rgba[4] = {0, 0, 0, 255};
            pixel(x, y, rgba);
            bytes.push_back(rgba[2]);
            bytes.push_back(rgba[1]);
            bytes.push_back(rgba[0]);
            if (alpha) {
                bytes.push_back(rgba[3]);
            }
        }
    }
    return bytes;
}

bool writeTemp(const std::filesystem::path& path, const std::vector<uint8_t>& bytes) {
    std::ofstream stream(path, std::ios::binary);
    if (!stream) {
        return false;
    }
    stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return static_cast<bool>(stream);
}

void caseImageMask(Runner& run) {
    namespace fs = std::filesystem;
    MaskImages& images = MaskImages::instance();
    const fs::path temp = fs::temp_directory_path();
    const fs::path lumaFile = temp / "nyrexvj-selftest-mask-luma.tga";
    const fs::path alphaFile = temp / "nyrexvj-selftest-mask-alpha.tga";
    const bool wrote = writeTemp(lumaFile, makeTga(64, 36, false, [](int x, int, uint8_t* p) { p[0] = p[1] = p[2] = x < 32 ? 255 : 0; })) &&
                       writeTemp(alphaFile, makeTga(64, 36, true, [](int, int y, uint8_t* p) { p[0] = p[1] = p[2] = 255; p[3] = y < 18 ? 255 : 0; }));
    run.condition("test mask pictures were written", wrote);
    if (!wrote) {
        return;
    }
    std::string lumaName;
    std::string alphaName;
    std::string error;
    run.condition("a mask picture can be imported", images.importFile(lumaFile.string(), lumaName, error) && images.importFile(alphaFile.string(), alphaName, error));
    if (lumaName.empty() || alphaName.empty()) {
        logLine("Output self test: import error: " + error);
        return;
    }
    images.clear();

    OutputSpec spec = defaultOutputSpec(0);
    spec.map.mask.enabled = true;
    spec.map.mask.image = lumaName;
    run.draw(spec);
    run.expect("an image mask stays hidden until it is loaded", 0.25f, 0.25f, kBlack);
    run.expect("an unloaded image mask hides the rest too", 0.75f, 0.75f, kBlack);
    run.condition("the picture loads in the background", images.waitFor(lumaName, 10000));
    run.draw(spec);
    run.expect("bright side of the image shows the slice", 0.25f, 0.25f, kRed);
    run.expect("bright side below", 0.25f, 0.75f, kBlue);
    run.expect("dark side of the image hides the slice", 0.75f, 0.25f, kBlack);
    run.expect("dark side below", 0.75f, 0.75f, kBlack);

    spec.map.mask.invert = true;
    run.draw(spec);
    run.expect("inverted image mask hides the bright side", 0.25f, 0.25f, kBlack);
    run.expect("inverted image mask shows the dark side", 0.75f, 0.25f, kGreen);

    spec.map.mask.invert = false;
    spec.map.mask.feather = 0.05f;
    run.draw(spec);
    run.expect("softened image mask keeps the bright side", 0.2f, 0.25f, kRed);
    run.expect("softened image mask keeps the dark side hidden", 0.8f, 0.25f, kBlack);

    spec.map.mask.feather = 0.0f;
    spec.map.mask.image = alphaName;
    run.draw(spec);
    run.condition("the transparent picture loads", images.waitFor(alphaName, 10000));
    run.draw(spec);
    run.expect("opaque top half shows the slice", 0.25f, 0.25f, kRed);
    run.expect("opaque top half shows the slice right", 0.75f, 0.25f, kGreen);
    run.expect("transparent bottom half hides the slice", 0.25f, 0.75f, kBlack);
    run.expect("transparent bottom half hides the slice right", 0.75f, 0.75f, kBlack);

    spec.map.mask.image = "selftest-missing-mask.png";
    run.draw(spec);
    images.waitFor(spec.map.mask.image, 10000);
    run.draw(spec);
    run.expect("a missing mask picture hides the slice", 0.25f, 0.25f, kBlack);
    run.condition("a missing mask picture reports an error", !images.error(spec.map.mask.image).empty());

    spec.map.mask.image.clear();
    spec.map.mask.points = {{0.1f, 0.1f}, {0.9f, 0.1f}, {0.9f, 0.9f}, {0.1f, 0.9f}};
    run.draw(spec);
    run.expect("without the picture the polygon is used again", 0.25f, 0.25f, kRed);
    run.expect("without the picture the polygon cuts", 0.03f, 0.03f, kBlack);

    std::error_code ec;
    fs::remove(images.folder() / lumaName, ec);
    fs::remove(images.folder() / alphaName, ec);
    fs::remove(lumaFile, ec);
    fs::remove(alphaFile, ec);
    images.clear();
}

void setEnvironment(const char* name, const std::string& value, bool present) {
#ifdef _WIN32
    _putenv_s(name, present ? value.c_str() : "");
#else
    if (present) {
        setenv(name, value.c_str(), 1);
    } else {
        unsetenv(name);
    }
#endif
}

struct FakeNdi {
    DynamicLibrary library;
    int (*frames)() = nullptr;
    int (*created)() = nullptr;
    int (*destroyed)() = nullptr;
    int (*width)() = nullptr;
    int (*height)() = nullptr;
    int (*stride)() = nullptr;
    const char* (*name)() = nullptr;
    const uint8_t* (*last)() = nullptr;
    int (*lastSize)() = nullptr;
    void (*setConnections)(int) = nullptr;

    template <typename Fn>
    bool bind(Fn& target, const char* symbolName) {
        target = reinterpret_cast<Fn>(library.symbol(symbolName));
        return target != nullptr;
    }

    bool open(const std::string& path) {
        std::string error;
        if (!library.open(path, error)) {
            return false;
        }
        return bind(frames, "fake_ndi_frames") && bind(created, "fake_ndi_created") && bind(destroyed, "fake_ndi_destroyed") && bind(width, "fake_ndi_width") && bind(height, "fake_ndi_height") && bind(stride, "fake_ndi_stride") && bind(name, "fake_ndi_name") && bind(last, "fake_ndi_last") && bind(lastSize, "fake_ndi_last_size") && bind(setConnections, "fake_ndi_set_connections");
    }

    Rgb at(float fx, float fy) const {
        const int w = width();
        const int h = height();
        const int x = std::clamp(static_cast<int>(fx * static_cast<float>(w)), 0, w - 1);
        const int y = std::clamp(static_cast<int>(fy * static_cast<float>(h)), 0, h - 1);
        const uint8_t* p = last() + (static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)) * 4u;
        return {p[0], p[1], p[2]};
    }
};

void caseNdiFallback(Runner& run) {
    const char* original = std::getenv("NYREXVJ_NDI_LIB");
    const bool had = original != nullptr;
    const std::string saved = had ? original : "";
    setEnvironment("NYREXVJ_NDI_LIB", "/nonexistent/nyrexvj-no-ndi-runtime", true);
    OutputSpec spec = defaultOutputSpec(0);
    spec.ndi = true;
    run.draw(spec);
    run.draw(spec);
    const OutputNdiStatus& status = run.manager().ndiStatus(0);
    run.condition("NDI without a runtime says so", status.wanted && status.error == "The NDI runtime is not installed");
    run.condition("NDI without a runtime is not active", !status.active && status.connections == 0);
    quadrants(run, "the picture still renders without the NDI runtime", kRed, kGreen, kBlue, kYellow);
    const size_t withoutNdi = run.gpuBytes();
    spec.ndi = false;
    run.draw(spec);
    run.condition("turning NDI off clears the message", !run.manager().ndiStatus(0).wanted && run.manager().ndiStatus(0).error.empty());
    run.condition("a failed NDI start uses no extra memory", run.gpuBytes() == withoutNdi);
    setEnvironment("NYREXVJ_NDI_LIB", saved, had);
}

void caseNdiOutput(Runner& run) {
    const char* path = std::getenv("NYREXVJ_NDI_LIB");
    FakeNdi fake;
    if (!path || !*path || !fake.open(path)) {
        logLine("Output self test: mapped NDI cases skipped (set NYREXVJ_NDI_LIB to the fake NDI library)");
        return;
    }
    OutputSpec spec = defaultOutputSpec(0);
    run.draw(spec);
    const size_t baseline = run.gpuBytes();

    spec.ndi = true;
    spec.ndiName = "Selftest A";
    fake.setConnections(0);
    for (int i = 0; i < 4; ++i) {
        run.draw(spec);
    }
    const OutputNdiStatus& status = run.manager().ndiStatus(0);
    run.condition("NDI output starts a sender", status.active && fake.created() == 1);
    run.condition("NDI output uses the chosen name", std::string(fake.name()) == "Selftest A");
    run.condition("nothing is read back without a receiver", fake.frames() == 0 && status.connections == 0);
    run.condition("no readback memory without a receiver", run.gpuBytes() == baseline);

    fake.setConnections(2);
    for (int i = 0; i < 5; ++i) {
        run.draw(spec);
    }
    run.condition("frames are sent once a receiver connects", fake.frames() >= 3);
    run.condition("receivers are counted", run.manager().ndiStatus(0).connections == 2);
    const int width = run.frameWidth();
    const int height = run.frameHeight();
    run.condition("NDI frame has the window size", fake.width() == width && fake.height() == height && fake.stride() == width * 4);
    run.condition("NDI frame has all its bytes", fake.lastSize() == width * height * 4);
    run.condition("readback memory matches the estimate", run.gpuBytes() - baseline == ndiReadbackBytes(width, height));
    const auto sameAsWindow = [&](const char* label) {
        for (int j = 0; j < 5; ++j) {
            for (int i = 0; i < 9; ++i) {
                const float fx = (static_cast<float>(i) + 0.5f) / 9.0f;
                const float fy = (static_cast<float>(j) + 0.5f) / 5.0f;
                const Rgb window = run.at(fx, fy);
                const Rgb sent = fake.at(fx, fy);
                const bool equal = std::abs(window.r - sent.r) <= 4 && std::abs(window.g - sent.g) <= 4 && std::abs(window.b - sent.b) <= 4;
                if (!equal) {
                    run.condition((std::string(label) + " at " + std::to_string(i) + "," + std::to_string(j)).c_str(), false);
                    return;
                }
            }
        }
        run.condition(label, true);
    };
    sameAsWindow("NDI frame equals the window picture");
    run.condition("NDI frame is upright", fake.at(0.25f, 0.25f).r > 200 && fake.at(0.25f, 0.25f).g < 60 && fake.at(0.25f, 0.75f).b > 200);

    spec.map.mask = maskRectangle(0.1f, 0.1f, 0.9f, 0.9f);
    spec.map.sliceColor.red = 0.5f;
    spec.color.blue = 0.5f;
    for (int i = 0; i < 5; ++i) {
        run.draw(spec);
    }
    sameAsWindow("NDI frame follows mask and colour");
    const Rgb masked = fake.at(0.05f, 0.05f);
    run.condition("NDI frame carries the mask", masked.r < 8 && masked.g < 8 && masked.b < 8);
    const Rgb tinted = fake.at(0.75f, 0.75f);
    const Rgb wantTint = corrected(spec.color, corrected(spec.map.sliceColor, kYellow));
    run.condition("NDI frame carries both colour corrections", std::abs(tinted.r - wantTint.r) <= 6 && std::abs(tinted.g - wantTint.g) <= 6 && std::abs(tinted.b - wantTint.b) <= 6);

    spec.ndiName = "Selftest B";
    for (int i = 0; i < 4; ++i) {
        run.draw(spec);
    }
    run.condition("a new name restarts the sender", fake.created() == 2 && fake.destroyed() == 1 && std::string(fake.name()) == "Selftest B");

    const int sentBefore = fake.frames();
    const size_t withReadback = run.gpuBytes();
    run.manager().setBudgetBytes(1024u * 1024u);
    for (int i = 0; i < 4; ++i) {
        run.draw(spec);
    }
    run.condition("NDI readback is refused over the budget", run.manager().ndiStatus(0).overBudget);
    run.condition("no frames are sent over the budget", fake.frames() == sentBefore);
    run.condition("the readback memory is released over the budget", withReadback - run.gpuBytes() == ndiReadbackBytes(width, height));
    run.expect("the window still renders over the NDI budget", 0.25f, 0.25f, corrected(spec.color, corrected(spec.map.sliceColor, kRed)), 6);
    run.expect("the window still masks over the NDI budget", 0.05f, 0.05f, kBlack);
    run.manager().setBudgetBytes(kOutputGpuBudgetBytes);
    for (int i = 0; i < 5; ++i) {
        run.draw(spec);
    }
    run.condition("NDI readback resumes within the budget", !run.manager().ndiStatus(0).overBudget && fake.frames() > sentBefore);

    const size_t beforeOff = run.gpuBytes();
    spec.ndi = false;
    run.draw(spec);
    run.draw(spec);
    run.condition("turning NDI off stops the sender", fake.destroyed() == 2 && !run.manager().ndiStatus(0).wanted);
    run.condition("turning NDI off frees the readback memory", beforeOff - run.gpuBytes() == ndiReadbackBytes(width, height));
    const int sentAfter = fake.frames();
    run.draw(spec);
    run.draw(spec);
    run.condition("no frames are sent after turning NDI off", fake.frames() == sentAfter);

    spec.map = OutputMapping{};
    spec.color = OutputColor{};
    spec.ndi = false;
    const double without = run.frameMilliseconds(spec, 60);
    spec.ndi = true;
    fake.setConnections(1);
    const double withReceiver = run.frameMilliseconds(spec, 60);
    fake.setConnections(0);
    const double withoutReceiver = run.frameMilliseconds(spec, 60);
    char text[300];
    std::snprintf(text, sizeof(text), "Output self test numbers: frame (1280x720 window, software OpenGL) without NDI %.2f ms, NDI on without receiver %.2f ms, NDI on with one receiver %.2f ms (readback and send)", without, withoutReceiver, withReceiver);
    logLine(text);
}

constexpr float kSpatialHeight = 2.0f;
constexpr float kSpatialWidth = kSpatialHeight * 16.0f / 9.0f;
constexpr float kSpatialDistance = 4.0f;

SpatialMapping fillingWall() {
    SpatialMapping spatial;
    spatial.enabled = true;
    spatial.projector.pose = SpatialPose{Vec3f{0.0f, 0.0f, kSpatialDistance}, 0.0f, 0.0f, 0.0f};
    spatial.projector.lens = SpatialLens{static_cast<float>(2.0 * std::atan(0.5 * kSpatialHeight / kSpatialDistance) * 180.0 / 3.14159265358979), 16.0f / 9.0f, 0.0f, 0.0f, 0.1f, 100.0f};
    SpatialSurface wall;
    wall.name = "Wall";
    wall.corners = {{{-kSpatialWidth * 0.5f, 1.0f, 0.0f}, {kSpatialWidth * 0.5f, 1.0f, 0.0f}, {kSpatialWidth * 0.5f, -1.0f, 0.0f}, {-kSpatialWidth * 0.5f, -1.0f, 0.0f}}};
    spatial.surfaces.push_back(wall);
    return spatial;
}

OutputSpec spatialSpec(const SpatialMapping& spatial) {
    OutputSpec spec = defaultOutputSpec(0);
    spec.map.spatial = spatial;
    return spec;
}

Rgb pictureColor(float u, float v) {
    const bool top = v > 0.5f;
    const bool left = u < 0.5f;
    return top ? (left ? kRed : kGreen) : (left ? kBlue : kYellow);
}

struct Agreement {
    int tested = 0;
    int matched = 0;
    int lit = 0;
};

Agreement compareWithCpu(Runner& run, const OutputMapping& map) {
    Agreement result;
    const int columns = 48;
    const int rows = 27;
    for (int j = 0; j < rows; ++j) {
        for (int i = 0; i < columns; ++i) {
            const float x = (static_cast<float>(i) + 0.5f) / static_cast<float>(columns);
            const float y = (static_cast<float>(j) + 0.5f) / static_cast<float>(rows);
            const SpatialHit centre = castProjectorRay(map, x, y);
            bool stable = true;
            const float offsets[4][2] = {{0.006f, 0.0f}, {-0.006f, 0.0f}, {0.0f, 0.01f}, {0.0f, -0.01f}};
            for (const auto& o : offsets) {
                const SpatialHit n = castProjectorRay(map, x + o[0], y + o[1]);
                if (n.hit != centre.hit || n.surface != centre.surface || n.contentValid != centre.contentValid) {
                    stable = false;
                }
            }
            if (!stable) {
                continue;
            }
            Rgb want = kBlack;
            if (centre.hit && centre.contentValid) {
                if (std::fabs(centre.u - 0.5f) < 0.025f || std::fabs(centre.v - 0.5f) < 0.04f || centre.u < 0.02f || centre.u > 0.98f || centre.v < 0.02f || centre.v > 0.98f) {
                    continue;
                }
                want = pictureColor(centre.u, centre.v);
                const float f = edgeFactor(map.blend, x, y) * map.brightness;
                want = {static_cast<int>(std::lround(want.r * f)), static_cast<int>(std::lround(want.g * f)), static_cast<int>(std::lround(want.b * f))};
                ++result.lit;
            }
            ++result.tested;
            const Rgb got = run.at(x, y);
            if (std::abs(got.r - want.r) <= 6 && std::abs(got.g - want.g) <= 6 && std::abs(got.b - want.b) <= 6) {
                ++result.matched;
            }
        }
    }
    return result;
}

void expectAgreement(Runner& run, const std::string& label, const OutputMapping& map, int minimumLit) {
    const Agreement a = compareWithCpu(run, map);
    if (a.matched != a.tested || a.lit < minimumLit) {
        logLine("Output self test: " + label + " matched " + std::to_string(a.matched) + " of " + std::to_string(a.tested) + " probes, " + std::to_string(a.lit) + " lit");
    }
    run.condition((label + " agrees with the CPU model on every probe").c_str(), a.matched == a.tested && a.tested > 0);
    run.condition((label + " lights enough probes").c_str(), a.lit >= minimumLit);
}

void caseSpatialBasics(Runner& run) {
    SpatialMapping spatial = fillingWall();
    run.draw(spatialSpec(spatial));
    quadrants(run, "spatial wall that fills the picture", kRed, kGreen, kBlue, kYellow);
    run.condition("the output window has a depth buffer", run.manager().depthAvailable(0));

    SpatialMapping half = spatial;
    half.projector.pose.position.z = 2.0f * kSpatialDistance;
    half.projector.lens.fovY = spatial.projector.lens.fovY;
    run.draw(spatialSpec(half));
    run.expect("far projector leaves the border dark", 0.05f, 0.05f, kBlack);
    run.expect("far projector leaves the border dark on the right", 0.95f, 0.5f, kBlack);
    run.expect("far projector keeps the top left of the picture", 0.4f, 0.4f, kRed);
    run.expect("far projector keeps the top right of the picture", 0.6f, 0.4f, kGreen);
    run.expect("far projector keeps the bottom left of the picture", 0.4f, 0.6f, kBlue);
    run.expect("far projector keeps the bottom right of the picture", 0.6f, 0.6f, kYellow);

    OutputSpec flipped = spatialSpec(spatial);
    flipped.map.flipX = true;
    run.draw(flipped);
    quadrants(run, "spatial wall with flip X", kGreen, kRed, kYellow, kBlue);
    flipped.map.flipX = false;
    flipped.map.flipY = true;
    run.draw(flipped);
    quadrants(run, "spatial wall with flip Y", kBlue, kYellow, kRed, kGreen);

    OutputSpec area = spatialSpec(spatial);
    area.map.slice = {0.5f, 0.0f, 0.5f, 1.0f};
    run.draw(area);
    run.expect("picture area right half fills the wall at the top", 0.25f, 0.25f, kGreen);
    run.expect("picture area right half fills the wall at the bottom", 0.75f, 0.75f, kYellow);

    SpatialMapping behind = spatial;
    behind.projector.pose.position.z = -kSpatialDistance;
    run.draw(spatialSpec(behind));
    run.expect("a wall behind the projector stays dark", 0.5f, 0.5f, kBlack);
    run.expect("a wall behind the projector stays dark at a corner", 0.25f, 0.25f, kBlack);

    SpatialMapping empty = spatial;
    empty.surfaces.clear();
    run.draw(spatialSpec(empty));
    run.expect("spatial mapping without a surface draws nothing", 0.25f, 0.25f, kBlack);

    SpatialMapping off = spatial;
    off.enabled = false;
    run.draw(spatialSpec(off));
    quadrants(run, "spatial mapping switched off gives the flat picture", kRed, kGreen, kBlue, kYellow);
}

void caseSpatialPerspective(Runner& run) {
    SpatialMapping spatial = fillingWall();
    spatial.surfaces[0] = wallSurface("Oblique", Vec3f{0.0f, 0.0f, 0.0f}, 3.4f, 2.0f, 35.0f, 0.0f);
    spatial.surfaces[0].divisions = 6;
    spatial.projector.pose = poseLookAt(Vec3f{-1.0f, 0.3f, 4.5f}, Vec3f{0.0f, 0.0f, 0.0f}, 0.0f);
    spatial.projector.lens.fovY = 40.0f;
    OutputSpec spec = spatialSpec(spatial);
    run.draw(spec);
    expectAgreement(run, "oblique wall", spec.map, 200);
    run.expect("outside the oblique wall", 0.02f, 0.5f, kBlack);

    spec.map.flipX = true;
    run.draw(spec);
    expectAgreement(run, "oblique wall with flip X", spec.map, 200);
    spec.map.flipX = false;
    spec.map.slice = {0.25f, 0.25f, 0.5f, 0.5f};
    run.draw(spec);
    expectAgreement(run, "oblique wall with a picture area", spec.map, 150);

    spec.map.slice = {0.0f, 0.0f, 1.0f, 1.0f};
    spec.map.blend.left = 0.3f;
    spec.map.blend.top = 0.2f;
    run.draw(spec);
    expectAgreement(run, "oblique wall with an edge blend measured on the output", spec.map, 200);

    SpatialMapping tilted = spatial;
    tilted.surfaces[0] = wallSurface("Tilted", Vec3f{0.0f, 0.0f, 0.0f}, 3.0f, 2.0f, -20.0f, 25.0f);
    tilted.surfaces[0].divisions = 8;
    OutputSpec tiltedSpec = spatialSpec(tilted);
    run.draw(tiltedSpec);
    expectAgreement(run, "wall tilted both ways", tiltedSpec.map, 200);
}

void caseSpatialOcclusion(Runner& run) {
    SpatialMapping spatial = fillingWall();
    SpatialSurface pillar = wallSurface("Pillar", Vec3f{-0.6f, -0.4f, 2.0f}, 0.6f, 1.0f, 0.0f, 0.0f);
    pillar.uv = {0.0f, 0.0f, 0.25f, 0.25f};
    spatial.surfaces.insert(spatial.surfaces.begin(), pillar);
    OutputSpec spec = spatialSpec(spatial);
    run.draw(spec);
    const ProjectedPoint centre = projectPoint(spatial.projector, Vec3f{-0.6f, -0.4f, 2.0f});
    const Vec3f behindPillar{-1.2f, -0.8f, 0.0f};
    run.condition("the wall point behind the pillar is hidden for the projector", !pointLitByProjector(spec.map, behindPillar));
    run.condition("a wall point beside the pillar is lit", pointLitByProjector(spec.map, Vec3f{1.0f, 0.5f, 0.0f}));
    run.expect("the pillar hides the wall behind it", centre.x, centre.y, kRed);
    run.expect("the wall shows beside the pillar", 0.8f, 0.8f, kYellow);
    expectAgreement(run, "wall with a pillar in front", spec.map, 200);

    spatial.surfaces = {fillingWall().surfaces[0], pillar};
    OutputSpec reversed = spatialSpec(spatial);
    run.draw(reversed);
    run.expect("drawing order does not matter, the pillar still hides the wall", centre.x, centre.y, kRed);

    SpatialMapping facing = fillingWall();
    SpatialSurface away = wallSurface("Away", Vec3f{0.0f, 0.0f, 0.0f}, kSpatialWidth, kSpatialHeight, 180.0f, 0.0f);
    facing.surfaces = {away};
    OutputSpec culled = spatialSpec(facing);
    run.draw(culled);
    run.expect("a surface that faces away is not drawn", 0.25f, 0.25f, kBlack);
    run.expect("a surface that faces away is not drawn at the centre", 0.6f, 0.6f, kBlack);
    facing.surfaces[0].doubleSided = true;
    OutputSpec both = spatialSpec(facing);
    run.draw(both);
    expectAgreement(run, "double sided surface seen from behind", both.map, 150);

    SpatialMapping floorScene = fillingWall();
    SpatialSurface ground = wallSurface("Floor", Vec3f{0.0f, 0.0f, 0.0f}, 12.0f, 24.0f, 0.0f, -90.0f);
    ground.divisions = 8;
    floorScene.surfaces = {ground};
    floorScene.projector.pose = SpatialPose{Vec3f{0.0f, 2.0f, 2.0f}, 0.0f, -30.0f, 0.0f};
    floorScene.projector.lens.fovY = 50.0f;
    OutputSpec floorSpec = spatialSpec(floorScene);
    run.draw(floorSpec);
    const SpatialMesh mesh = buildSpatialMesh(floorSpec.map);
    run.condition("the floor crosses the near plane", mesh.stats.clipped > 0);
    expectAgreement(run, "floor that reaches behind the projector", floorSpec.map, 300);
}

void caseSpatialCamera(Runner& run) {
    SpatialMapping spatial = fillingWall();
    spatial.surfaces[0] = wallSurface("Oblique", Vec3f{0.0f, 0.0f, 0.0f}, 3.4f, 2.0f, 25.0f, 0.0f);
    spatial.surfaces[0].divisions = 6;
    spatial.projector.pose = poseLookAt(Vec3f{-1.2f, 0.3f, 4.5f}, Vec3f{0.0f, 0.0f, 0.0f}, 0.0f);
    spatial.projector.lens.fovY = 45.0f;
    spatial.source = SpatialSource::ViewerCamera;
    spatial.camera.pose = poseLookAt(Vec3f{0.0f, 0.0f, 7.0f}, Vec3f{0.0f, 0.0f, 0.0f}, 0.0f);
    spatial.camera.lens.fovY = 28.0f;
    OutputSpec spec = spatialSpec(spatial);
    run.draw(spec);
    expectAgreement(run, "viewer camera content", spec.map, 150);

    SpatialMapping narrow = spatial;
    narrow.camera.lens.fovY = 11.0f;
    OutputSpec narrowSpec = spatialSpec(narrow);
    run.draw(narrowSpec);
    expectAgreement(run, "viewer camera that sees only part of the wall", narrowSpec.map, 80);
    int dark = 0;
    int onWall = 0;
    for (int j = 0; j < 27; ++j) {
        for (int i = 0; i < 48; ++i) {
            const float x = (static_cast<float>(i) + 0.5f) / 48.0f;
            const float y = (static_cast<float>(j) + 0.5f) / 27.0f;
            const SpatialHit hit = castProjectorRay(narrowSpec.map, x, y);
            if (hit.hit) {
                ++onWall;
                dark += hit.contentValid ? 0 : 1;
            }
        }
    }
    run.condition("a narrow camera leaves part of the wall without picture", dark > 40 && onWall > dark);

    OutputSpec areaSpec = spec;
    areaSpec.map.slice = {0.25f, 0.25f, 0.5f, 0.5f};
    run.draw(areaSpec);
    expectAgreement(run, "viewer camera with a picture area", areaSpec.map, 100);
    OutputSpec flipSpec = spec;
    flipSpec.map.flipX = true;
    flipSpec.map.flipY = true;
    run.draw(flipSpec);
    expectAgreement(run, "viewer camera with both flips", flipSpec.map, 150);
}

void caseSpatialBlendAndSlices(Runner& run) {
    SpatialMapping spatial = fillingWall();
    OutputSpec spec = spatialSpec(spatial);
    spec.map.blend.left = 0.25f;
    spec.map.blend.right = 0.25f;
    run.draw(spec);
    for (float x : {0.04f, 0.1f, 0.2f, 0.8f, 0.95f}) {
        const float f = edgeFactor(spec.map.blend, x, 0.25f);
        const int level = static_cast<int>(std::lround(255.0f * f));
        const Rgb want = x < 0.5f ? Rgb{level, 0, 0} : Rgb{0, level, 0};
        run.expect("edge blend ramp on a spatial output", x, 0.25f, want, 7);
    }
    run.expect("inside the blend zone the picture is untouched", 0.4f, 0.25f, kRed);

    OutputSpec brightnessSpec = spatialSpec(spatial);
    brightnessSpec.map.brightness = 0.5f;
    run.draw(brightnessSpec);
    run.expect("brightness applies to a spatial output", 0.25f, 0.25f, {128, 0, 0}, 3);

    OutputSpec withSlice = spatialSpec(spatial);
    addSlice(withSlice);
    withSlice.slices[0].map = region(0.0f, 0.0f, 0.5f, 0.5f, 0.6f, 0.6f, 0.3f, 0.3f);
    run.draw(withSlice);
    run.expect("a flat slice can sit on top of a spatial slice", 0.75f, 0.75f, kRed);
    run.expect("the spatial slice stays elsewhere", 0.25f, 0.75f, kBlue);

    OutputSpec spatialSecond = defaultOutputSpec(0);
    spatialSecond.map = region(0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.5f, 1.0f);
    addSlice(spatialSecond);
    spatialSecond.slices[0].map.spatial = spatial;
    spatialSecond.slices[0].map.spatial.projector.pose.position.z = 3.0f * kSpatialDistance;
    run.draw(spatialSecond);
    run.expect("a spatial second slice is drawn over the flat one", 0.4f, 0.4f, kRed);
    run.expect("a spatial second slice shows where the flat one has nothing", 0.6f, 0.4f, kGreen);
    run.expect("the flat slice stays outside the spatial wall", 0.1f, 0.9f, kBlue);
}

void caseSpatialSwitching(Runner& run) {
    OutputSpec flat = defaultOutputSpec(0);
    OutputSpec spatial = spatialSpec(fillingWall());
    run.draw(flat);
    const size_t baseline = run.gpuBytes();
    for (int round = 0; round < 3; ++round) {
        run.draw(flat);
        quadrants(run, "flat after switching", kRed, kGreen, kBlue, kYellow);
        run.draw(spatial);
        quadrants(run, "spatial after switching", kRed, kGreen, kBlue, kYellow);
    }
    run.draw(flat);
    run.condition("switching between flat and spatial returns the video memory to its start value", run.gpuBytes() == baseline);

    std::array<OutputSpec, kMaxOutputs> planned = defaultOutputs();
    planned[0] = spatial;
    const OutputBudget estimate = estimateOutputBudget(planned, 64, 36, 1280, 720);
    run.draw(spatial);
    SpatialMapping wall = fillingWall();
    const size_t expected = (5u * 5u) * sizeof(SpatialVertex) + 4u * 4u * 6u * sizeof(uint32_t);
    run.condition("the budget estimate of a spatial wall matches its mesh", estimate.meshBytes == expected && spatialMeshBytesEstimate(wall) == expected);
    run.condition("the budget estimate of a spatial wall matches the measured memory", run.gpuBytes() == expected);
}

void caseSpatialMarkers(Runner& run) {
    SpatialMapping spatial = fillingWall();
    CalibrationPoint point;
    point.name = "P1";
    point.world = Vec3f{0.0f, 0.0f, 0.0f};
    point.outputX = 0.3f;
    point.outputY = 0.3f;
    spatial.points.push_back(point);
    OutputSpec spec = spatialSpec(spatial);
    run.draw(spec);
    run.expect("no marker while markers are off", 0.3f, 0.3f, kRed, 4);
    run.manager().setMarkers(0, true, -1);
    run.draw(spec);
    run.expect("target marker where the point should land", 0.3f, 0.3f, {26, 255, 102}, 6);
    run.expect("predicted marker where the pose puts the point", 0.5f, 0.5f, {255, 140, 26}, 6);
    run.manager().setMarkers(0, true, 0);
    run.draw(spec);
    run.expect("selected marker is white", 0.3f, 0.3f, kWhiteColor, 4);
    spec.map.spatial.points[0].enabled = false;
    run.draw(spec);
    run.expect("switched off marker is grey", 0.3f, 0.3f, {115, 115, 115}, 6);
    run.manager().setMarkers(0, false, -1);
    run.draw(spec);
    run.expect("markers disappear again", 0.3f, 0.3f, kRed, 4);

    const char* path = std::getenv("NYREXVJ_NDI_LIB");
    FakeNdi fake;
    if (path && *path && fake.open(path)) {
        spec.map.spatial.points[0].enabled = true;
        spec.ndi = true;
        spec.ndiName = "Selftest markers";
        fake.setConnections(2);
        run.manager().setMarkers(0, true, -1);
        for (int i = 0; i < 6; ++i) {
            run.draw(spec);
        }
        run.expect("markers are drawn on the window while NDI runs", 0.3f, 0.3f, {26, 255, 102}, 6);
        run.condition("an NDI frame was sent with the markers on", fake.frames() >= 3);
        const Rgb sentTarget = fake.at(0.3f, 0.3f);
        const Rgb sentPredicted = fake.at(0.5f, 0.5f);
        run.condition("the target marker is not in the NDI frame", sentTarget.r > 200 && sentTarget.g < 60 && sentTarget.b < 60);
        run.condition("the predicted marker is not in the NDI frame", std::abs(sentPredicted.r - 255) > 40 || std::abs(sentPredicted.g - 140) > 40 || std::abs(sentPredicted.b - 26) > 40);
        run.manager().setMarkers(0, false, -1);
        spec.ndi = false;
        run.draw(spec);
        fake.setConnections(0);
    } else {
        logLine("Output self test: the NDI check of the markers was skipped (set NYREXVJ_NDI_LIB to the fake NDI library)");
    }
}

void measureHistory() {
    const int width = 1920;
    const int height = 1080;
    GLuint source = 0;
    glGenTextures(1, &source);
    glBindTexture(GL_TEXTURE_2D, source);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glBindTexture(GL_TEXTURE_2D, 0);
    TextureHistory history;
    for (int i = 0; i < 4; ++i) {
        history.update(source, width, height, 2);
    }
    glFinish();
    const int runs = 30;
    const auto begin = std::chrono::steady_clock::now();
    for (int i = 0; i < runs; ++i) {
        history.update(source, width, height, 2);
        glFinish();
    }
    const double copyMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count() / runs;
    char text[256];
    std::snprintf(text, sizeof(text), "Output self test numbers: delay history at 1920x1080 with 2 frames of delay, %.2f ms per frame for the copy (software OpenGL), %.1f MiB of video memory", copyMs, static_cast<double>(history.gpuBytes()) / (1024.0 * 1024.0));
    logLine(text);
    history.release();
    glDeleteTextures(1, &source);
}

void measure(Runner& run) {
    OutputSpec spec = defaultOutputSpec(0);
    run.draw(spec);
    const size_t baseline = run.gpuBytes();

    SliceMask plain = maskRectangle(0.2f, 0.2f, 0.8f, 0.8f);
    SliceMask soft = plain;
    soft.feather = 0.1f;
    auto timeBuild = [](const SliceMask& mask) {
        const auto begin = std::chrono::steady_clock::now();
        const int runs = 5;
        for (int i = 0; i < runs; ++i) {
            buildMaskImage(mask);
        }
        return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count() / runs;
    };
    const double plainMs = timeBuild(plain);
    const double softMs = timeBuild(soft);
    auto timeHalf = [](const SliceMask& mask) {
        const auto begin = std::chrono::steady_clock::now();
        const int runs = 5;
        for (int i = 0; i < runs; ++i) {
            buildMaskImage(mask, kMaskWidth / 2, kMaskHeight / 2);
        }
        return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count() / runs;
    };
    const double halfMs = timeHalf(soft);

    spec.map.mask = plain;
    run.draw(spec);
    const size_t withHardMask = run.gpuBytes();
    spec.map.mask = soft;
    run.draw(spec);
    const size_t withSoftMask = run.gpuBytes();

    spec = defaultOutputSpec(0);
    resizeWarp(spec.map, 17, 17);
    while (addSlice(spec)) {
    }
    for (OutputSlice& slice : spec.slices) {
        resizeWarp(slice.map, 17, 17);
        slice.map.mask = soft;
    }
    spec.map.mask = soft;
    run.draw(spec);
    const size_t heavy = run.gpuBytes();
    std::array<OutputSpec, kMaxOutputs> planned = defaultOutputs();
    planned[0] = spec;
    const OutputBudget heavyEstimate = estimateOutputBudget(planned, 64, 36, 1280, 720);

    spec = defaultOutputSpec(0);
    run.draw(spec);
    const size_t after = run.gpuBytes();
    planned[0] = spec;
    const OutputBudget plainEstimate = estimateOutputBudget(planned, 64, 36, 1280, 720);

    SliceMask curve = maskEllipse(0.5f, 0.5f, 0.4f, 0.4f, 32);
    maskSmooth(curve, 1.0f);
    MaskBitmap bitmap;
    bitmap.width = kMaskWidth;
    bitmap.height = kMaskHeight;
    bitmap.coverage.assign(static_cast<size_t>(kMaskWidth) * static_cast<size_t>(kMaskHeight), 200);
    SliceMask imageMask;
    imageMask.enabled = true;
    imageMask.image = "bench.png";
    auto timeImage = [&](const SliceMask& mask, int width, int height) {
        const auto begin = std::chrono::steady_clock::now();
        const int runs = 5;
        for (int i = 0; i < runs; ++i) {
            buildMaskImage(mask, width, height, &bitmap);
        }
        return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count() / runs;
    };
    const double curveMs = timeBuild(curve);
    const double imageMs = timeImage(imageMask, kMaskWidth, kMaskHeight);
    SliceMask softImage = imageMask;
    softImage.feather = 0.1f;
    const double softImageMs = timeImage(softImage, kMaskWidth / 2, kMaskHeight / 2);

    constexpr int kFrames = 60;
    OutputSpec plainSpec = defaultOutputSpec(0);
    OutputSpec maskedSpec = defaultOutputSpec(0);
    maskedSpec.map.mask = plain;
    OutputSpec softSpec = defaultOutputSpec(0);
    softSpec.map.mask = soft;
    OutputSpec colourSpec = defaultOutputSpec(0);
    colourSpec.color.saturation = 0.8f;
    colourSpec.color.gamma = 1.2f;
    OutputSpec fourSpec = defaultOutputSpec(0);
    for (int i = 0; i < 3; ++i) {
        addSlice(fourSpec);
        fourSpec.slices[static_cast<size_t>(i)].map.mask = plain;
    }
    OutputSpec eightSpec = defaultOutputSpec(0);
    resizeWarp(eightSpec.map, 17, 17);
    eightSpec.map.mask = soft;
    while (addSlice(eightSpec)) {
    }
    for (OutputSlice& slice : eightSpec.slices) {
        resizeWarp(slice.map, 17, 17);
        slice.map.mask = soft;
    }
    OutputSpec sliceColourSpec = defaultOutputSpec(0);
    sliceColourSpec.map.sliceColor.saturation = 0.8f;
    sliceColourSpec.map.sliceColor.gamma = 1.2f;
    sliceColourSpec.color = colourSpec.color;
    OutputSpec curveSpec = defaultOutputSpec(0);
    curveSpec.map.mask = curve;
    const double plainFrame = run.frameMilliseconds(plainSpec, kFrames);
    const double maskedFrame = run.frameMilliseconds(maskedSpec, kFrames);
    const double softFrame = run.frameMilliseconds(softSpec, kFrames);
    const double colourFrame = run.frameMilliseconds(colourSpec, kFrames);
    const double fourFrame = run.frameMilliseconds(fourSpec, kFrames);
    const double eightFrame = run.frameMilliseconds(eightSpec, kFrames);
    const double sliceColourFrame = run.frameMilliseconds(sliceColourSpec, kFrames);
    const double curveFrame = run.frameMilliseconds(curveSpec, kFrames);
    run.condition("gpu memory returns to the baseline", after == baseline);
    run.condition("a hard mask costs one 1024x576 byte texture", withHardMask == baseline + static_cast<size_t>(kMaskWidth) * static_cast<size_t>(kMaskHeight));
    run.condition("a soft mask costs one 512x288 byte texture", withSoftMask == baseline + static_cast<size_t>(kMaskWidth / 2) * static_cast<size_t>(kMaskHeight / 2));
    run.condition("the budget estimate matches the measured memory of eight slices", heavyEstimate.meshBytes + heavyEstimate.maskBytes == heavy);
    run.condition("the budget estimate matches the measured memory of one plain slice", plainEstimate.total() == baseline);
    logLine("Output self test numbers: default " + std::to_string(baseline) + " B, one hard mask " + std::to_string(withHardMask) + " B, one soft mask " + std::to_string(withSoftMask) + " B, eight masked 17x17 slices " + std::to_string(heavy) + " B");
    char text[400];
    std::snprintf(text, sizeof(text), "Output self test numbers: mask build %.2f ms hard (1024x576), %.2f ms feathered at 1024x576, %.2f ms feathered at 512x288 (used)", plainMs, softMs, halfMs);
    logLine(text);
    std::snprintf(text, sizeof(text), "Output self test numbers: frame (1280x720 window, software OpenGL) default %.2f ms, hard mask %.2f, soft mask %.2f, colour %.2f, 4 masked slices %.2f, 8 soft masked 17x17 slices %.2f", plainFrame, maskedFrame, softFrame, colourFrame, fourFrame, eightFrame);
    logLine(text);
    std::snprintf(text, sizeof(text), "Output self test numbers: curved 32 point mask build %.2f ms hard (1024x576), image mask build %.2f ms hard (1024x576) and %.2f ms feathered at 512x288", curveMs, imageMs, softImageMs);
    logLine(text);
    std::snprintf(text, sizeof(text), "Output self test numbers: frame (1280x720 window, software OpenGL) slice and output colour %.2f ms, curved mask %.2f ms", sliceColourFrame, curveFrame);
    logLine(text);
    logLine("Output self test numbers: budget estimate for eight masked 17x17 slices " + std::to_string(heavyEstimate.meshBytes + heavyEstimate.maskBytes) + " B against " + std::to_string(heavy) + " B measured");
}

}

int runOutputSelfTest(GLFWwindow* share) {
    Runner run(share);
    if (!run.start()) {
        return 1;
    }
    caseDefault(run);
    caseSlices(run);
    caseMask(run);
    caseColor(run);
    caseBlackLevel(run);
    caseSliceColor(run);
    caseCurves(run);
    caseFreehand(run);
    caseImageMask(run);
    caseNdiFallback(run);
    caseNdiOutput(run);
    caseWarp(run);
    caseSpatialBasics(run);
    caseSpatialPerspective(run);
    caseSpatialOcclusion(run);
    caseSpatialCamera(run);
    caseSpatialBlendAndSlices(run);
    caseSpatialSwitching(run);
    caseSpatialMarkers(run);
    caseSyncBars(run);
    caseDelay(run);
    caseFrameLock(run);
    measureHistory();
    measure(run);
    logLine("Output self test: " + std::to_string(run.checks()) + " checks, " + std::to_string(run.failures()) + " failed");
    return run.failures() == 0 ? 0 : 1;
}

}
