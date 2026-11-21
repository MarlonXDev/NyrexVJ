#include "app/output_selftest.hpp"

#include "core/log.hpp"
#include "core/output_map.hpp"
#include "render/output_manager.hpp"

#include <glad/gl.h>

#include <GLFW/glfw3.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
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

    spec = defaultOutputSpec(0);
    run.draw(spec);
    const size_t after = run.gpuBytes();

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
    const double plainFrame = run.frameMilliseconds(plainSpec, kFrames);
    const double maskedFrame = run.frameMilliseconds(maskedSpec, kFrames);
    const double softFrame = run.frameMilliseconds(softSpec, kFrames);
    const double colourFrame = run.frameMilliseconds(colourSpec, kFrames);
    const double fourFrame = run.frameMilliseconds(fourSpec, kFrames);
    const double eightFrame = run.frameMilliseconds(eightSpec, kFrames);
    run.condition("gpu memory returns to the baseline", after == baseline);
    run.condition("a hard mask costs one 1024x576 byte texture", withHardMask == baseline + static_cast<size_t>(kMaskWidth) * static_cast<size_t>(kMaskHeight));
    run.condition("a soft mask costs one 512x288 byte texture", withSoftMask == baseline + static_cast<size_t>(kMaskWidth / 2) * static_cast<size_t>(kMaskHeight / 2));
    logLine("Output self test numbers: default " + std::to_string(baseline) + " B, one hard mask " + std::to_string(withHardMask) + " B, one soft mask " + std::to_string(withSoftMask) + " B, eight masked 17x17 slices " + std::to_string(heavy) + " B");
    char text[400];
    std::snprintf(text, sizeof(text), "Output self test numbers: mask build %.2f ms hard (1024x576), %.2f ms feathered at 1024x576, %.2f ms feathered at 512x288 (used)", plainMs, softMs, halfMs);
    logLine(text);
    std::snprintf(text, sizeof(text), "Output self test numbers: frame (1280x720 window, software OpenGL) default %.2f ms, hard mask %.2f, soft mask %.2f, colour %.2f, 4 masked slices %.2f, 8 soft masked 17x17 slices %.2f", plainFrame, maskedFrame, softFrame, colourFrame, fourFrame, eightFrame);
    logLine(text);
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
    caseWarp(run);
    measure(run);
    logLine("Output self test: " + std::to_string(run.checks()) + " checks, " + std::to_string(run.failures()) + " failed");
    return run.failures() == 0 ? 0 : 1;
}

}
