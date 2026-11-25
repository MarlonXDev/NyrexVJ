#pragma once

#include "core/particles.hpp"
#include "core/text_doc.hpp"
#include "core/text_font.hpp"
#include "core/text_layout.hpp"
#include "render/gl_util.hpp"
#include "render/gpu_particles.hpp"

#include <array>
#include <chrono>
#include <future>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace nvj {

struct GeneratorFrame {
    double clock = 0.0;
    double beat = 0.0;
    float level = 0.0f;
    std::array<float, 3> colorA{1.0f, 1.0f, 1.0f};
    std::array<float, 3> colorB{1.0f, 1.0f, 1.0f};
    bool rainbow = false;
    float scale = 1.0f;
    int width = 1920;
    int height = 1080;
};

enum class GeneratorState { Idle, Loading, Ready, Failed };

const char* particleFragmentSource();

class GeneratorGl {
public:
    ~GeneratorGl();
    bool ensure(std::string& error);
    void release();
    bool ready() const { return textProgram != 0 && particleProgram != 0; }
    bool failed() const { return tried_ && !ready(); }
    const std::string& failure() const { return failure_; }
    void simulateCompileFailure();
    void allowRetry();

    GLuint vao = 0;
    GLuint textProgram = 0;
    GLint textSize = -1;
    GLint textAtlas = -1;
    GLuint particleProgram = 0;
    GLint particleAspect = -1;
    GLint particleColorA = -1;
    GLint particleColorB = -1;
    GLint particleMode = -1;
    GLint particleSoft = -1;
    GLint particleScale = -1;
    GpuParticleGl gpuParticles;

private:
    bool tried_ = false;
    std::string failure_;
};

class GeneratorPlayer {
public:
    virtual ~GeneratorPlayer() = default;
    virtual GeneratorState poll(int outputHeight) = 0;
    virtual bool draw(GeneratorGl& gl, const GeneratorFrame& frame, int instance) = 0;
    virtual void reload() = 0;
    virtual void release() = 0;
    virtual unsigned long long gpuBytes() const = 0;

    GeneratorState state() const { return state_; }
    const std::string& error() const { return error_; }
    const std::string& note() const { return note_; }
    const std::string& label() const { return label_; }
    void touch() { lastUse_ = std::chrono::steady_clock::now(); }
    double idleSeconds() const { return std::chrono::duration<double>(std::chrono::steady_clock::now() - lastUse_).count(); }

    bool thumbnailDone = false;

protected:
    GeneratorState state_ = GeneratorState::Idle;
    std::string error_;
    std::string note_;
    std::string label_;
    std::chrono::steady_clock::time_point lastUse_ = std::chrono::steady_clock::now();
};

struct TextBakeInput {
    std::u32string text;
    std::string font;
    int pixelHeight = 64;
    bool kerning = true;
};

struct TextBakeOutput {
    text::Atlas atlas;
    std::u32string text;
    std::string error;
    std::string note;
    int requestedHeight = 0;
};

TextBakeOutput bakeText(const TextBakeInput& input);
int textBakeHeight(float neededPixels);

class TextPlayer final : public GeneratorPlayer {
public:
    TextPlayer(std::string label, std::string name);
    ~TextPlayer() override;

    GeneratorState poll(int outputHeight) override;
    bool draw(GeneratorGl& gl, const GeneratorFrame& frame, int instance) override;
    void reload() override;
    void release() override;
    unsigned long long gpuBytes() const override;

    void setDocument(const text::TextDoc& doc);
    const text::TextDoc& document() const { return doc_; }
    int glyphCount() const { return layout_.visibleCount; }
    int bakedHeight() const { return static_cast<int>(atlas_.pixelHeight); }
    int atlasSide() const { return atlas_.width; }
    int bakeCount() const { return bakeCount_; }
    bool baking() const { return job_.valid(); }

private:
    void startBake(int pixelHeight);
    void consume(TextBakeOutput output);
    void freeTexture();

    std::string name_;
    bool fromFile_ = true;
    bool docLoaded_ = false;
    text::TextDoc doc_;
    std::u32string decoded_;
    std::u32string bakedText_;
    int docVersion_ = 0;
    int bakedVersion_ = -1;
    int bakedRequest_ = 0;
    int bakeCount_ = 0;
    int wantedHeight_ = 64;
    std::chrono::steady_clock::time_point lastBake_{};
    std::future<TextBakeOutput> job_;
    int jobVersion_ = 0;
    text::Atlas atlas_;
    text::Layout layout_;
    long long layoutKey_ = -1;
    GLuint texture_ = 0;
    GLuint buffer_ = 0;
    std::vector<float> vertices_;
};

class ParticlePlayer final : public GeneratorPlayer {
public:
    ParticlePlayer(std::string label, std::string name, bool builtin, int limit);
    ~ParticlePlayer() override;

    GeneratorState poll(int outputHeight) override;
    bool draw(GeneratorGl& gl, const GeneratorFrame& frame, int instance) override;
    bool drawThumbnail(GeneratorGl& gl, const GeneratorFrame& frame);
    void reload() override;
    void release() override;
    unsigned long long gpuBytes() const override;

    struct BackendInfo {
        bool gpu = false;
        int capacity = 0;
        int used = 0;
        std::uint64_t recycled = 0;
        std::string note;
    };

    void setParams(const particles::Params& params);
    void setLimit(int limit);
    void setGpu(bool enabled);
    bool gpuEnabled() const { return gpuEnabled_; }
    const particles::Params& params() const { return params_; }
    int alive(int instance) const;
    long long dropped() const;
    int limit() const { return limit_; }
    const BackendInfo& backend() const { return backend_; }
    GpuParticleSystem* gpuSystem(int instance);

private:
    struct Slot {
        std::unique_ptr<particles::System> system;
        std::unique_ptr<GpuParticleSystem> gpu;
        int version = -1;
        int epoch = -1;
        std::string note;
    };
    void rebuild(GeneratorGl& gl, Slot& slot);
    bool drawSystem(GeneratorGl& gl, const GeneratorFrame& frame, particles::System& system, const particles::Drive& drive);

    std::string name_;
    bool builtin_ = false;
    bool fromFile_ = false;
    bool loaded_ = false;
    particles::Params params_;
    int version_ = 0;
    int limit_ = particles::kDefaultLimit;
    std::map<int, Slot> slots_;
    bool gpuEnabled_ = false;
    int gpuEpoch_ = 0;
    BackendInfo backend_;
    GLuint buffer_ = 0;
    size_t bufferCapacity_ = 0;
    std::vector<particles::Vertex> vertices_;
};

}
