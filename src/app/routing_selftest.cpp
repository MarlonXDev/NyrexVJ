#include "app/routing_selftest.hpp"

#include "audio/audio_features.hpp"
#include "core/log.hpp"
#include "core/routing.hpp"
#include "render/clip_library.hpp"
#include "render/shader_source.hpp"

#include <glad/gl.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace nvj {

namespace {

using json = nlohmann::json;
using Clock = std::chrono::steady_clock;
namespace fs = std::filesystem;

struct Pixel {
    int r = 0;
    int g = 0;
    int b = 0;
    int a = 0;
};

void setDataDirectory(const std::string& path) {
#ifdef _WIN32
    _putenv_s("NYREXVJ_DATA_DIR", path.c_str());
#else
    setenv("NYREXVJ_DATA_DIR", path.c_str(), 1);
#endif
}

void clearDataDirectory() {
#ifdef _WIN32
    _putenv_s("NYREXVJ_DATA_DIR", "");
#else
    unsetenv("NYREXVJ_DATA_DIR");
#endif
}

void writeText(const fs::path& path, const std::string& text) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << text;
}

double since(Clock::time_point start) {
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

std::string show(const Pixel& p) {
    return "(" + std::to_string(p.r) + "," + std::to_string(p.g) + "," + std::to_string(p.b) + "," + std::to_string(p.a) + ")";
}

const char* kRed = "void mainImage(out vec4 c, in vec2 p) { c = vec4(1.0, 0.0, 0.0, 1.0); }\n";
const char* kGreen = "void mainImage(out vec4 c, in vec2 p) { c = vec4(0.0, 1.0, 0.0, 1.0); }\n";
const char* kBlue = "void mainImage(out vec4 c, in vec2 p) { c = vec4(0.0, 0.0, 1.0, 1.0); }\n";
const char* kHalf = "void mainImage(out vec4 c, in vec2 p) { c = vec4(0.5, 0.0, 0.0, 0.5); }\n";
const char* kRamp = "void mainImage(out vec4 c, in vec2 p) { vec2 uv = p / iResolution.xy; c = vec4(uv.x, uv.y, 0.25, 1.0); }\n";

class Probe {
public:
    Probe(Engine& engine, json& report) : engine_(engine), report_(report) {}

    void prepare() {
        root_ = fs::temp_directory_path() / "nyrexvj_routing_selftest";
        std::error_code ec;
        fs::remove_all(root_, ec);
        fs::create_directories(root_ / "shaders", ec);
        setDataDirectory(root_.string());
        engine_.comp = Composition{};
        engine_.blackout = false;
        engine_.paused = false;
        engine_.setGpuTiming(false);
        engine_.setResolutionIndex(0);
        for (int l = 0; l < kLayerCount; ++l) {
            engine_.clearLayer(l);
        }
        red_ = addClip("rt_red", kRed);
        green_ = addClip("rt_green", kGreen);
        blue_ = addClip("rt_blue", kBlue);
        half_ = addClip("rt_half", kHalf);
        ramp_ = addClip("rt_ramp", kRamp);
        for (const routing::Source& source : routing::clipSources()) {
            routes_.push_back(clipIndexByName(routing::sourceName(source)));
        }
    }

    void finish() {
        for (int k = 0; k < routing::kSenderCount; ++k) {
            engine_.setSenderSource(static_cast<routing::SenderKind>(k), routing::Source{});
        }
        engine_.setRoutePreviewsWanted(false);
        reset();
        step(4);
        clearDataDirectory();
        std::error_code ec;
        fs::remove_all(root_, ec);
        rescanLibrary();
        engine_.syncLibrary();
    }

    bool ready() const {
        if (red_ < 0 || green_ < 0 || blue_ < 0 || half_ < 0 || ramp_ < 0) {
            return false;
        }
        for (const int clip : routes_) {
            if (clip < 0) {
                return false;
            }
        }
        return !routes_.empty();
    }

    int route(int sourceIndex) const { return routes_[static_cast<size_t>(sourceIndex)]; }

    int routeOf(const routing::Source& source) const {
        const int at = routing::clipSourceIndex(source);
        return at >= 0 ? route(at) : -1;
    }

    int addClip(const std::string& stem, const std::string& text) {
        writeText(root_ / "shaders" / (stem + ".glsl"), text);
        rescanLibrary();
        engine_.syncLibrary();
        return clipIndexByName(shaderClipName(stem));
    }

    void step(int count) {
        for (int i = 0; i < count; ++i) {
            engine_.setAudio(virtualAudio(engine_.beat(), engine_.time()));
            engine_.update(1.0 / 60.0);
            engine_.render();
            glFinish();
            ++frames_;
        }
    }

    void reset() {
        engine_.comp = Composition{};
        engine_.setResolutionIndex(0);
        for (int l = 0; l < kLayerCount; ++l) {
            engine_.clearLayer(l);
        }
    }

    void place(int layer, int clip, int column = 0) {
        engine_.comp.layers[static_cast<size_t>(layer)].setCell(static_cast<size_t>(column), clip);
        engine_.trigger(layer, column);
    }

    void opacity(int layer, float value) { engine_.comp.layers[static_cast<size_t>(layer)].opacity = value; }

    void warm() {
        for (const int clip : {red_, green_, blue_, half_, ramp_}) {
            place(0, clip);
            step(8);
        }
        reset();
        step(2);
    }

    static Pixel pixelAt(const std::vector<unsigned char>& pixels, int width, int x, int y) {
        const int height = static_cast<int>(pixels.size() / 4u) / std::max(1, width);
        x = std::clamp(x, 0, width - 1);
        y = std::clamp(y, 0, std::max(0, height - 1));
        const size_t index = (static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x)) * 4u;
        return Pixel{pixels[index], pixels[index + 1], pixels[index + 2], pixels[index + 3]};
    }

    Pixel outputAt(double fx, double fy) {
        std::vector<unsigned char> pixels;
        int width = 0;
        int height = 0;
        if (engine_.readOutput(pixels, width, height) && width > 0 && height > 0) {
            return pixelAt(pixels, width, static_cast<int>(fx * width), static_cast<int>(fy * height));
        }
        return Pixel{};
    }

    Pixel centre() { return outputAt(0.5, 0.5); }

    Pixel textureAt(GLuint texture, int width, int height, double fx, double fy) {
        if (!texture || width <= 0 || height <= 0) {
            return Pixel{};
        }
        std::vector<unsigned char> pixels(static_cast<size_t>(width) * static_cast<size_t>(height) * 4u, 0);
        GLuint fbo = 0;
        glGenFramebuffers(1, &fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
        Pixel result;
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE) {
            glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
            result = pixelAt(pixels, width, static_cast<int>(fx * width), static_cast<int>(fy * height));
        }
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glDeleteFramebuffers(1, &fbo);
        return result;
    }

    static bool near(const Pixel& a, const Pixel& b, int tolerance) {
        return std::abs(a.r - b.r) <= tolerance && std::abs(a.g - b.g) <= tolerance && std::abs(a.b - b.b) <= tolerance;
    }

    static bool near(const Pixel& p, int r, int g, int b, int tolerance) {
        return near(p, Pixel{r, g, b, 255}, tolerance);
    }

    void record(const std::string& name, bool passed, const std::string& detail) {
        json item;
        item["name"] = name;
        item["passed"] = passed;
        item["detail"] = detail;
        items_.push_back(std::move(item));
        failed_ += passed ? 0 : 1;
        logLine("Routing self test " + name + ": " + (passed ? "passed" : "FAILED") + " (" + detail + ")");
    }

    void expect(const std::string& name, const Pixel& pixel, int r, int g, int b, int tolerance) {
        record(name, near(pixel, r, g, b, tolerance), "got " + show(pixel) + " expected (" + std::to_string(r) + "," + std::to_string(g) + "," + std::to_string(b) + ") +-" + std::to_string(tolerance));
    }

    void expectSame(const std::string& name, const Pixel& a, const Pixel& b, int tolerance) {
        record(name, near(a, b, tolerance), "got " + show(a) + " and " + show(b) + " +-" + std::to_string(tolerance));
    }

    int failed() const { return failed_; }
    json& items() { return items_; }
    long long frames() const { return frames_; }
    Engine& engine() { return engine_; }
    json& summary() { return report_; }

    int redClip() const { return red_; }
    int greenClip() const { return green_; }
    int blueClip() const { return blue_; }
    int halfClip() const { return half_; }
    int rampClip() const { return ramp_; }

private:
    Engine& engine_;
    json& report_;
    fs::path root_;
    json items_ = json::array();
    int failed_ = 0;
    long long frames_ = 0;
    int red_ = -1;
    int green_ = -1;
    int blue_ = -1;
    int half_ = -1;
    int ramp_ = -1;
    std::vector<int> routes_;
};

using routing::Source;
using routing::SourceKind;

Source layerSource(int index, bool previous = false) { return Source{SourceKind::Layer, index, previous}; }
Source groupSource(int count, bool previous = false) { return Source{SourceKind::Group, count, previous}; }
Source masterSource(bool previous = false) { return Source{SourceKind::Master, 0, previous}; }

void testSource(Probe& p) {
    p.reset();
    p.place(0, p.redClip());
    p.opacity(0, 0.0f);
    p.place(1, p.routeOf(layerSource(0)));
    p.step(6);
    p.expect("a route clip shows the picture of a layer that is faded out", p.centre(), 255, 0, 0, 3);
    const RouteSlotStatus slot = p.engine().routeSlot(0);
    p.record("the source layer is counted as used", slot.captured && slot.references == 1 && p.engine().routePlan().forceRender[0], "captured " + std::to_string(slot.captured) + " references " + std::to_string(slot.references));
}

void testIdentical(Probe& p) {
    p.reset();
    p.place(0, p.rampClip());
    p.step(6);
    const double points[4][2] = {{0.2, 0.3}, {0.8, 0.7}, {0.5, 0.9}, {0.1, 0.1}};
    Pixel direct[4];
    for (int i = 0; i < 4; ++i) {
        direct[i] = p.outputAt(points[i][0], points[i][1]);
    }
    p.opacity(0, 0.0f);
    p.place(1, p.routeOf(layerSource(0)));
    p.step(6);
    bool same = true;
    std::string detail;
    for (int i = 0; i < 4; ++i) {
        const Pixel routed = p.outputAt(points[i][0], points[i][1]);
        same = same && Probe::near(direct[i], routed, 2);
        detail += show(direct[i]) + "/" + show(routed) + " ";
    }
    p.record("a routed picture is pixel for pixel the picture of the layer", same, detail);

    p.reset();
    p.place(0, p.halfClip());
    p.step(6);
    const Pixel direct2 = p.centre();
    p.opacity(0, 0.0f);
    p.place(1, p.routeOf(layerSource(0)));
    p.step(6);
    const Pixel routed2 = p.centre();
    p.record("transparency of the source is kept", Probe::near(direct2, routed2, 2) && direct2.r > 100 && direct2.r < 156, "direct " + show(direct2) + " routed " + show(routed2));
}

void testSeveral(Probe& p) {
    p.reset();
    p.place(1, p.redClip());
    p.place(2, p.redClip());
    p.opacity(1, 0.5f);
    p.opacity(2, 0.5f);
    p.step(6);
    const Pixel direct = p.centre();
    p.reset();
    p.place(0, p.redClip());
    p.opacity(0, 0.0f);
    p.place(1, p.routeOf(layerSource(0)));
    p.place(2, p.routeOf(layerSource(0)));
    p.opacity(1, 0.5f);
    p.opacity(2, 0.5f);
    p.step(6);
    const Pixel routed = p.centre();
    p.expectSame("two layers showing the same source give the same picture as two direct layers", direct, routed, 2);
    p.record("the source is captured once for two users", p.engine().routeSlot(0).references == 2 && p.engine().routeCapturesAlive() == 1, "references " + std::to_string(p.engine().routeSlot(0).references) + " copies alive " + std::to_string(p.engine().routeCapturesAlive()));
}

void testChain(Probe& p) {
    p.reset();
    p.place(0, p.redClip());
    p.opacity(0, 0.0f);
    p.place(1, p.routeOf(layerSource(0)));
    p.opacity(1, 0.0f);
    p.place(2, p.routeOf(layerSource(1)));
    p.step(6);
    p.expect("a route can feed another route through a faded out layer", p.centre(), 255, 0, 0, 3);
}

void testGroup(Probe& p) {
    p.reset();
    p.place(0, p.redClip());
    p.place(1, p.blueClip());
    p.opacity(1, 0.5f);
    p.step(6);
    const Pixel mix = p.centre();
    p.place(2, p.routeOf(groupSource(2)));
    p.engine().comp.layers[2].blend = BlendAdd;
    p.step(6);
    const Pixel doubled = p.centre();
    const bool expected = Probe::near(doubled, std::min(255, mix.r * 2), 0, std::min(255, mix.b * 2), 6);
    p.record("a group source is the mix of the layers below, added on top it doubles them", expected && mix.r > 100 && mix.b > 100, "mix " + show(mix) + " with the group added " + show(doubled));
}

void testMaster(Probe& p) {
    p.reset();
    p.place(0, p.redClip());
    p.place(1, p.routeOf(masterSource(true)));
    p.step(8);
    p.expect("the previous master picture can be played back as a source", p.centre(), 255, 0, 0, 3);
    const RouteSlotStatus slot = p.engine().routeSlot(routing::slotOf(masterSource()));
    p.record("the master is captured at the output size", slot.captured && slot.width == p.engine().width() && slot.height == p.engine().height(), std::to_string(slot.width) + "x" + std::to_string(slot.height));
}

void testFeedback(Probe& p) {
    p.reset();
    p.place(0, p.greenClip(), 1);
    p.place(0, p.redClip(), 0);
    p.opacity(0, 0.0f);
    p.place(1, p.routeOf(layerSource(0, true)));
    p.step(1);
    p.expect("the first frame of a feedback source is transparent", p.centre(), 0, 0, 0, 2);
    p.step(1);
    p.expect("the second frame shows the first", p.centre(), 255, 0, 0, 3);
    p.step(4);
    p.engine().trigger(0, 1);
    p.step(1);
    p.expect("a feedback source is one frame late: the frame of the switch still shows red", p.centre(), 255, 0, 0, 3);
    p.step(1);
    p.expect("and the next frame shows green", p.centre(), 0, 255, 0, 3);
    p.engine().trigger(0, 0);
    p.step(1);
    p.expect("switching back is also one frame late", p.centre(), 0, 255, 0, 3);
    p.step(1);
    p.expect("and then red again", p.centre(), 255, 0, 0, 3);
    const routing::Plan& plan = p.engine().routePlan();
    p.record("the plan reports the feedback", plan.feedbacks == 1 && plan.cycles == 0, "feedbacks " + std::to_string(plan.feedbacks));
}

void testCycles(Probe& p) {
    p.reset();
    p.place(0, p.routeOf(layerSource(0)));
    p.place(2, p.routeOf(layerSource(2)));
    p.place(1, p.routeOf(layerSource(2)));
    p.step(6);
    const routing::Plan& plan = p.engine().routePlan();
    p.record("loops are found and refused", plan.cycles == 3, "cycles " + std::to_string(plan.cycles));
    p.expect("a refused route draws nothing", p.centre(), 0, 0, 0, 1);
    bool blocked = !plan.consumers.empty();
    for (const routing::Resolved& resolved : plan.consumers) {
        blocked = blocked && resolved.status == routing::Status::Cycle;
    }
    p.record("every consumer is reported as blocked", blocked, "consumers " + std::to_string(plan.consumers.size()));
    p.step(60);
    p.record("the program keeps rendering with a refused route", glGetError() == GL_NO_ERROR, "no graphics error");
}

void testMissing(Probe& p) {
    p.reset();
    p.place(1, p.routeOf(layerSource(0)));
    p.step(6);
    p.expect("a route whose source layer is empty draws nothing", p.centre(), 0, 0, 0, 1);
    p.record("it is reported as not playing", p.engine().routePlan().inactive == 1, "inactive " + std::to_string(p.engine().routePlan().inactive));
    p.place(0, p.redClip());
    p.engine().comp.layers[0].bypass = true;
    p.step(4);
    p.expect("a bypassed source layer counts as not playing", p.centre(), 0, 0, 0, 1);
    p.engine().comp.layers[0].bypass = false;
    p.opacity(0, 0.0f);
    p.step(3);
    p.expect("the picture is back when the source plays again", p.centre(), 255, 0, 0, 3);
}

void testSwitching(Probe& p) {
    p.reset();
    p.place(0, p.redClip());
    p.opacity(0, 0.0f);
    p.place(1, p.routeOf(layerSource(0)), 0);
    p.place(1, p.routeOf(layerSource(0, true)), 1);
    p.place(1, p.blueClip(), 2);
    p.step(4);
    int wrong = 0;
    std::string firstWrong;
    for (int frame = 0; frame < 120; ++frame) {
        const int column = frame % 3;
        p.engine().trigger(1, column);
        p.step(1);
        const Pixel pixel = p.centre();
        const bool good = column == 2 ? Probe::near(pixel, 0, 0, 255, 3) : Probe::near(pixel, 255, 0, 0, 3);
        if (!good) {
            ++wrong;
            if (firstWrong.empty()) {
                firstWrong = "frame " + std::to_string(frame) + " column " + std::to_string(column) + " got " + show(pixel);
            }
        }
    }
    p.record("switching between a route, a feedback route and a plain clip every frame never shows a wrong picture", wrong == 0, wrong == 0 ? "120 frames" : std::to_string(wrong) + " wrong, first " + firstWrong);

    wrong = 0;
    p.engine().trigger(1, 0);
    for (int frame = 0; frame < 90; ++frame) {
        p.engine().comp.layers[0].activeColumn = frame % 2 == 0 ? 0 : -1;
        p.step(1);
        const Pixel pixel = p.centre();
        const bool wanted = frame % 2 == 0;
        const bool good = wanted ? Probe::near(pixel, 255, 0, 0, 3) : Probe::near(pixel, 0, 0, 0, 1);
        wrong += good ? 0 : 1;
    }
    p.record("starting and stopping the source layer every frame follows exactly", wrong == 0, std::to_string(wrong) + " wrong frames of 90");
}

void testResize(Probe& p) {
    p.reset();
    p.place(0, p.redClip());
    p.opacity(0, 0.0f);
    p.place(1, p.routeOf(layerSource(0)));
    p.step(4);
    const int before = p.engine().width();
    p.engine().setResolutionIndex(1);
    p.step(1);
    p.expect("the picture is there in the very first frame after a resolution change", p.centre(), 255, 0, 0, 3);
    const RouteSlotStatus slot = p.engine().routeSlot(0);
    p.record("the copy follows the new size", p.engine().width() != before && slot.width == p.engine().width() && slot.height == p.engine().height(), std::to_string(before) + " to " + std::to_string(p.engine().width()) + ", copy " + std::to_string(slot.width) + "x" + std::to_string(slot.height));
    p.engine().setResolutionIndex(0);
    p.step(2);
    p.expect("and back again", p.centre(), 255, 0, 0, 3);
    p.reset();
    p.place(0, p.redClip());
    p.opacity(0, 0.0f);
    p.place(1, p.routeOf(layerSource(0, true)));
    p.step(4);
    p.engine().setResolutionIndex(1);
    p.step(1);
    const Pixel gap = p.centre();
    p.step(1);
    const Pixel after = p.centre();
    p.record("a feedback source restarts cleanly after a resolution change", Probe::near(gap, 0, 0, 0, 2) && Probe::near(after, 255, 0, 0, 3), "first " + show(gap) + " then " + show(after));
    p.engine().setResolutionIndex(0);
    p.step(2);
}

void testSenders(Probe& p) {
    p.reset();
    p.place(0, p.redClip());
    p.opacity(0, 0.0f);
    p.place(1, p.blueClip());
    p.engine().setSenderSource(routing::SenderKind::Spout, layerSource(0));
    p.engine().setSenderSource(routing::SenderKind::Ndi, groupSource(2));
    p.step(6);
    Engine& e = p.engine();
    p.expect("the master picture stays the master picture", p.centre(), 0, 0, 255, 3);
    const GLuint spout = e.routedTexture(routing::SenderKind::Spout);
    const Pixel layerCopy = p.textureAt(spout, e.width(), e.height(), 0.5, 0.5);
    p.record("Spout can send a single layer", spout != 0 && spout != e.outputTexture() && Probe::near(layerCopy, 255, 0, 0, 3) && layerCopy.a == 255 && e.senderSourceLive(routing::SenderKind::Spout), "got " + show(layerCopy));
    const Pixel groupCopy = p.textureAt(e.routedTexture(routing::SenderKind::Ndi), e.width(), e.height(), 0.5, 0.5);
    p.record("NDI can send a group of layers", Probe::near(groupCopy, 0, 0, 255, 3) && groupCopy.a == 255, "got " + show(groupCopy));
    p.record("the recording keeps the master", e.routedTexture(routing::SenderKind::Recording) == e.outputTexture(), "texture " + std::to_string(e.routedTexture(routing::SenderKind::Recording)));
    p.record("a sender source keeps the layer rendering while it is faded out", e.routePlan().forceRender[0], "forced " + std::to_string(e.routePlan().forceRender[0]));
    e.setSenderSource(routing::SenderKind::Spout, layerSource(2));
    p.step(4);
    const GLuint empty = e.routedTexture(routing::SenderKind::Spout);
    const Pixel black = p.textureAt(empty, e.width(), e.height(), 0.5, 0.5);
    p.record("a sender whose source is not playing sends black and says so", empty != 0 && Probe::near(black, 0, 0, 0, 1) && !e.senderSourceLive(routing::SenderKind::Spout) && e.routePlan().inactive >= 1, "got " + show(black));
    e.setSenderSource(routing::SenderKind::Spout, masterSource());
    e.setSenderSource(routing::SenderKind::Ndi, masterSource());
    p.step(4);
    p.record("choosing the master again sends the master and frees the copies", e.routedTexture(routing::SenderKind::Spout) == e.outputTexture() && e.routedTexture(routing::SenderKind::Ndi) == e.outputTexture(), "texture check");
    routing::Source parsed;
    p.record("a sender can only be set to a same frame source", routing::parseSenderKey("layer2", parsed) && parsed == layerSource(1) && !routing::parseSenderKey("feedback", parsed), "key check");
    e.setSenderSource(routing::SenderKind::Spout, layerSource(0, true));
    p.record("a feedback source is not accepted for a sender", e.senderSource(routing::SenderKind::Spout) == masterSource(), "sender source is the master");
}

void testPreview(Probe& p) {
    p.reset();
    p.place(0, p.redClip());
    p.opacity(0, 0.0f);
    p.place(1, p.routeOf(layerSource(0)));
    p.engine().setRoutePreviewsWanted(true);
    p.step(6);
    const RouteSlotStatus slot = p.engine().routeSlot(0);
    const Pixel small = p.textureAt(slot.preview, 160, 90, 0.5, 0.5);
    p.record("a small live preview of a source exists while the window is open", slot.preview != 0 && slot.fresh && Probe::near(small, 255, 0, 0, 16), "got " + show(small));
    p.engine().setRoutePreviewsWanted(false);
    p.step(2);
    const RouteSlotStatus closed = p.engine().routeSlot(0);
    p.record("the preview is freed when the window is closed", closed.preview == 0, "preview " + std::to_string(closed.preview));
}

void drain(Probe& p) {
    p.reset();
    p.step(static_cast<int>(routing::kHoldFrames) + 10);
}

void testMemory(Probe& p, json& summary) {
    drain(p);
    p.place(0, p.redClip());
    p.opacity(0, 0.0f);
    p.place(1, p.routeOf(layerSource(0)));
    p.step(6);
    Engine& e = p.engine();
    const size_t used = e.routeBytes();
    const size_t expected = static_cast<size_t>(e.width()) * static_cast<size_t>(e.height()) * 4u;
    p.record("one routed source costs one picture of memory", used == expected && e.gpuMemory().routing == used, "used " + std::to_string(used) + " bytes, one picture is " + std::to_string(expected));
    summary["bytesPerSource"] = used;
    p.reset();
    p.step(static_cast<int>(routing::kHoldFrames) - 20);
    const bool stillThere = e.routeCapturesAlive() > 0;
    p.step(40);
    p.record("a copy is kept for a short time and then released", stillThere && e.routeCapturesAlive() == 0 && e.routeBytes() == 0, "alive before " + std::to_string(stillThere) + " after " + std::to_string(e.routeCapturesAlive()));
}

void testTransition(Probe& p) {
    p.reset();
    p.place(0, p.redClip());
    p.opacity(0, 0.0f);
    p.place(1, p.routeOf(layerSource(0)));
    p.engine().comp.deckSettings.seconds = 1.5f;
    p.step(4);
    Engine& e = p.engine();
    const bool started = e.switchDeck(1);
    p.step(3);
    const Pixel start = p.centre();
    p.record("the outgoing deck keeps its routes during a transition", started && e.transitioning() && start.r > 200 && start.g < 10 && start.b < 10, "got " + show(start));
    p.step(120);
    p.record("the transition ends and the new deck is empty", !e.transitioning() && Probe::near(p.centre(), 0, 0, 0, 2), "got " + show(p.centre()));
    e.switchDeck(0);
    p.step(120);
    p.expect("switching back plays the routes of the first deck again", p.centre(), 255, 0, 0, 3);
    p.record("no graphics error after the transitions", glGetError() == GL_NO_ERROR, "no graphics error");
}

void testUnused(Probe& p, json& summary) {
    drain(p);
    p.place(0, p.redClip());
    p.step(10);
    Engine& e = p.engine();
    const unsigned long long before = e.routeCaptureCount();
    p.step(30);
    p.record("without any routing nothing is copied and nothing is allocated", e.routeCaptureCount() == before && e.routeBytes() == 0 && e.routeCapturesAlive() == 0 && !e.routePlan().any(), "captures " + std::to_string(e.routeCaptureCount() - before) + " bytes " + std::to_string(e.routeBytes()));
    p.expect("a layer without routing looks as before", p.centre(), 255, 0, 0, 2);
    summary["captureCountUnused"] = e.routeCaptureCount() - before;
}

double frameMs(Probe& p, int frames) {
    p.step(8);
    const auto begin = Clock::now();
    p.step(frames);
    return since(begin) / static_cast<double>(std::max(1, frames));
}

json benchmark(Probe& p, int frames) {
    json result = json::object();
    Engine& e = p.engine();
    p.reset();
    for (int l = 0; l < kLayerCount; ++l) {
        p.place(l, p.rampClip());
    }
    result["plainFourLayersMs"] = frameMs(p, frames);
    p.reset();
    for (int l = 0; l < 3; ++l) {
        p.place(l, p.rampClip());
    }
    p.place(3, p.routeOf(groupSource(3)));
    result["threeLayersAndOneRouteMs"] = frameMs(p, frames);
    p.reset();
    for (int l = 0; l < 3; ++l) {
        p.place(l, p.rampClip());
    }
    p.place(3, p.rampClip());
    for (int l = 0; l < kLayerCount; ++l) {
        p.opacity(l, 0.9f);
    }
    const double scene = frameMs(p, frames);
    result["sendersSceneMs"] = scene;
    e.setSenderSource(routing::SenderKind::Spout, layerSource(0));
    const double one = frameMs(p, frames);
    result["oneLayerSenderMs"] = one;
    result["oneLayerSenderCostMs"] = one - scene;
    e.setSenderSource(routing::SenderKind::Spout, masterSource());
    e.setSenderSource(routing::SenderKind::Ndi, groupSource(3));
    e.setSenderSource(routing::SenderKind::Recording, groupSource(2));
    const double two = frameMs(p, frames);
    result["twoGroupSendersMs"] = two;
    result["twoGroupSendersCostMs"] = two - scene;
    for (int k = 0; k < routing::kSenderCount; ++k) {
        e.setSenderSource(static_cast<routing::SenderKind>(k), masterSource());
    }
    drain(p);
    p.place(0, p.rampClip());
    p.opacity(0, 0.0f);
    p.place(1, p.routeOf(layerSource(0)));
    p.step(4);
    result["routedPictureBytes"] = e.routeBytes();
    result["outputWidth"] = e.width();
    result["outputHeight"] = e.height();
    result["frames"] = frames;
    const double plain = result["plainFourLayersMs"].get<double>();
    const double routed = result["threeLayersAndOneRouteMs"].get<double>();
    result["routeCostMs"] = routed - plain;
    return result;
}

}

int runRoutingSelfTest(Engine& engine, int benchFrames, const std::string& outputPath) {
    json report = json::object();
    report["renderer"] = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
    report["version"] = reinterpret_cast<const char*>(glGetString(GL_VERSION));
    json summary = json::object();
    const Composition savedComposition = engine.comp;
    Probe probe(engine, summary);
    probe.prepare();
    if (!probe.ready()) {
        probe.record("the test clips and the route clips are in the library", false, "a clip is missing");
    } else {
        probe.warm();
        testSource(probe);
        testIdentical(probe);
        testSeveral(probe);
        testChain(probe);
        testGroup(probe);
        testMaster(probe);
        testFeedback(probe);
        testCycles(probe);
        testMissing(probe);
        testSwitching(probe);
        testResize(probe);
        testSenders(probe);
        testPreview(probe);
        testMemory(probe, summary);
        testTransition(probe);
        testUnused(probe, summary);
        if (benchFrames > 0) {
            report["benchmark"] = benchmark(probe, benchFrames);
        }
        GLint program = 0;
        glGetIntegerv(GL_CURRENT_PROGRAM, &program);
        probe.record("the graphics state is left clean", glGetError() == GL_NO_ERROR && program == 0, "no error and no program left bound");
    }
    probe.finish();
    engine.applyComposition(savedComposition);
    report["summary"] = summary;
    report["frames"] = probe.frames();
    report["checks"] = probe.items();
    report["failed"] = probe.failed();
    report["passed"] = probe.failed() == 0;
    const std::string text = report.dump(2);
    logLine("Routing self test result:\n" + text);
    if (!outputPath.empty()) {
        std::ofstream out(outputPath);
        out << text << '\n';
    }
    return probe.failed() == 0 ? 0 : 1;
}

}
