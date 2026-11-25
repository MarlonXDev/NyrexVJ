#include "render/engine.hpp"

#include "render/clip_library.hpp"
#include "render/effect_library.hpp"
#include "core/log.hpp"
#include "core/modulation.hpp"
#include "core/node_compile.hpp"
#include "core/node_graph.hpp"
#include "core/plugin_host.hpp"
#include "scene3d/procedural.hpp"
#include "scene3d/scene_pack.hpp"

#include <algorithm>
#include <chrono>
#include <thread>
#include <cmath>
#include <filesystem>

namespace nvj {

namespace {

constexpr int kThumbWidth = 160;
constexpr int kThumbHeight = 90;
constexpr int kPreviewWidth = 320;
constexpr int kPreviewHeight = 180;
constexpr int kPreviewBusWidth = 480;
constexpr int kPreviewBusHeight = 270;

const char* kPassHeader = R"glsl(#version 330 core
in vec2 vUv;
out vec4 fragColor;
)glsl";

const char* kCompositeBody = R"glsl(
uniform sampler2D uTex;
uniform float uOpacity;
uniform int uMode;
uniform vec4 uGrade;
void main() {
    vec4 t = texture(uTex, vUv);
    vec3 c = applyGrade(t.a > 0.001 ? t.rgb / t.a : t.rgb, uGrade) * t.a;
    if (uMode == 2) {
        fragColor = vec4(c * uOpacity, 1.0);
    } else if (uMode == 3) {
        fragColor = vec4(vec3(1.0 - t.a * uOpacity) + c * uOpacity, 1.0);
    } else if (uMode == 1) {
        fragColor = vec4(c, uOpacity);
    } else {
        fragColor = vec4(c * uOpacity, t.a * uOpacity);
    }
}
)glsl";

const char* kBlendBody = R"glsl(
uniform sampler2D uTex;
uniform sampler2D uBack;
uniform float uOpacity;
uniform int uMode;
uniform vec4 uGrade;

vec3 blendColors(vec3 b, vec3 s, int mode) {
    if (mode == 4) {
        return max(b - s, 0.0);
    }
    if (mode == 5) {
        return abs(b - s);
    }
    if (mode == 6) {
        return b + s - 2.0 * b * s;
    }
    if (mode == 7) {
        return mix(2.0 * b * s, 1.0 - 2.0 * (1.0 - b) * (1.0 - s), step(0.5, b));
    }
    if (mode == 8) {
        vec3 d = mix(((16.0 * b - 12.0) * b + 4.0) * b, sqrt(b), step(0.25, b));
        return mix(b - (1.0 - 2.0 * s) * b * (1.0 - b), b + (2.0 * s - 1.0) * (d - b), step(0.5, s));
    }
    if (mode == 9) {
        return mix(2.0 * b * s, 1.0 - 2.0 * (1.0 - b) * (1.0 - s), step(0.5, s));
    }
    if (mode == 10) {
        return max(b, s);
    }
    if (mode == 11) {
        return min(b, s);
    }
    if (mode == 12) {
        return b / max(1.0 - s, vec3(0.001));
    }
    if (mode == 13) {
        return max(b + s - 1.0, 0.0);
    }
    return s;
}

void main() {
    vec4 t = texture(uTex, vUv);
    vec3 s = clamp(applyGrade(t.a > 0.001 ? t.rgb / t.a : t.rgb, uGrade), 0.0, 1.0);
    vec3 b = texture(uBack, vUv).rgb;
    vec3 r = clamp(blendColors(b, s, uMode), 0.0, 1.0);
    fragColor = vec4(mix(b, r, uOpacity * t.a), 1.0);
}
)glsl";

const char* kFinalBody = R"glsl(
uniform sampler2D uTex;
uniform float uLevel;
uniform vec4 uGrade;
void main() {
    vec3 c = applyGrade(texture(uTex, vUv).rgb, uGrade);
    fragColor = vec4(clamp(c, 0.0, 1.0) * uLevel, 1.0);
}
)glsl";

const char* kTransitionBody = R"glsl(
uniform sampler2D uA;
uniform sampler2D uB;
uniform float uP;
uniform int uType;
uniform float uAspect;
uniform float uTime;
const float PI = 3.14159265;

float h21(vec2 p) {
    p = fract(p * vec2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return fract(p.x * p.y);
}

float vn(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(h21(i), h21(i + vec2(1.0, 0.0)), f.x), mix(h21(i + vec2(0.0, 1.0)), h21(i + vec2(1.0, 1.0)), f.x), f.y);
}

float fbm(vec2 p) {
    float s = 0.0;
    float a = 0.5;
    for (int i = 0; i < 4; ++i) {
        s += vn(p) * a;
        p = p * 2.03 + 11.7;
        a *= 0.5;
    }
    return s / 0.9375;
}

mat2 rot(float a) {
    float c = cos(a);
    float s = sin(a);
    return mat2(c, -s, s, c);
}

vec2 mrep(vec2 q) {
    return 1.0 - abs(mod(q, 2.0) - 1.0);
}

float reveal(float x, float p, float w) {
    float thr = p * (1.0 + 2.0 * w) - w;
    return smoothstep(x - w, x + w, thr);
}

vec3 colA(vec2 uv) {
    return texture(uA, clamp(uv, 0.0, 1.0)).rgb;
}

vec3 colB(vec2 uv) {
    return texture(uB, clamp(uv, 0.0, 1.0)).rgb;
}

vec3 colAm(vec2 uv) {
    return texture(uA, mrep(uv)).rgb;
}

vec3 colBm(vec2 uv) {
    return texture(uB, mrep(uv)).rgb;
}

void main() {
    vec2 uv = vUv;
    float t = clamp(uP, 0.0, 1.0);
    float p = t * t * t * (t * (t * 6.0 - 15.0) + 10.0);
    vec2 asp = vec2(uAspect, 1.0);
    vec3 c;
    if (uType == 1) {
        float n = fbm(uv * asp * 5.0 + vec2(0.0, uTime * 0.2));
        float m = reveal(n, p, 0.22);
        float edge = 4.0 * m * (1.0 - m);
        c = mix(colA(uv), colB(uv), m);
        c += vec3(1.0, 0.55, 0.18) * edge * edge * 0.55;
    } else if (uType == 2) {
        float za = 1.0 + p * 1.6;
        float zb = mix(0.45, 1.0, p);
        vec2 q = uv - 0.5;
        vec3 a = colAm(q / za + 0.5);
        vec3 b = colBm(q * zb + 0.5);
        float m = smoothstep(0.15, 0.85, p);
        c = mix(a, b, m);
    } else if (uType == 3) {
        float x = dot(uv - 0.5, normalize(vec2(1.0, 0.45))) * 0.78 + 0.5;
        float m = reveal(x, p, 0.14);
        c = mix(colA(uv), colB(uv), m);
    } else if (uType == 4) {
        float r = length((uv - 0.5) * asp) / (0.5 * length(asp));
        float m = reveal(r, p, 0.16);
        float ring = 4.0 * m * (1.0 - m);
        c = mix(colA(uv), colB(uv), m);
        c += vec3(0.35, 0.55, 1.0) * ring * ring * 0.4;
    } else if (uType == 5) {
        vec2 q = (uv - 0.5) * asp;
        float ang = 1.4;
        vec2 qa = rot(-p * ang) * q / (1.0 + p * 1.1);
        vec2 qb = rot((1.0 - p) * ang) * q * mix(0.55, 1.0, p);
        vec3 a = colAm(qa / asp + 0.5);
        vec3 b = colBm(qb / asp + 0.5);
        float m = smoothstep(0.2, 0.8, p);
        c = mix(a, b, m);
    } else if (uType == 6) {
        float edge = 1.0 - p;
        float soft = 0.02;
        float m = smoothstep(edge - soft, edge + soft, uv.x);
        vec3 a = colA(vec2(uv.x + p, uv.y));
        vec3 b = colB(vec2(uv.x - edge, uv.y));
        c = mix(a, b, m);
        c *= 1.0 - 0.35 * exp(-abs(uv.x - edge) * 14.0);
    } else {
        c = mix(colA(uv), colB(uv), p);
    }
    fragColor = vec4(c, 1.0);
}
)glsl";

const char* kSpoutBody = R"glsl(
uniform sampler2D uTex;
uniform vec2 uFit;
uniform float uFlip;
void main() {
    vec2 p = (vUv - 0.5) * uFit + 0.5;
    vec2 inside = step(vec2(0.0), p) * step(p, vec2(1.0));
    vec2 s = vec2(p.x, mix(p.y, 1.0 - p.y, uFlip));
    fragColor = vec4(texture(uTex, s).rgb * inside.x * inside.y, 1.0);
}
)glsl";

const char* kMediaBody = R"glsl(
uniform sampler2D uTex;
uniform vec2 uFit;
uniform float uPremult;
uniform float uBackdrop;
uniform vec2 uTexScale;
uniform float uYcocg;
void main() {
    vec2 p = (vUv - 0.5) * uFit + 0.5;
    vec2 inside = step(vec2(0.0), p) * step(p, vec2(1.0));
    float mask = inside.x * inside.y;
    vec4 t = texture(uTex, vec2(p.x, 1.0 - p.y) * uTexScale);
    if (uYcocg > 0.5) {
        float k = t.z * (255.0 / 8.0) + 1.0;
        float co = (t.x - 0.50196) / k;
        float cg = (t.y - 0.50196) / k;
        t = vec4(t.w + co - cg, t.w + cg, t.w - co - cg, 1.0);
    }
    float a = t.a * mask;
    vec3 c = (uPremult > 0.5 ? t.rgb : t.rgb * t.a) * mask;
    if (uBackdrop > 0.5) {
        vec2 g = floor(vUv * vec2(16.0, 9.0));
        vec3 back = mix(vec3(0.10), vec3(0.16), mod(g.x + g.y, 2.0));
        fragColor = vec4(c + back * (1.0 - a), 1.0);
    } else {
        fragColor = vec4(c, a);
    }
}
)glsl";

const char* kThumbBody = R"glsl(
uniform sampler2D uTex;
uniform vec2 uTexel;
void main() {
    vec3 acc = vec3(0.0);
    for (int j = 0; j < 4; ++j) {
        for (int i = 0; i < 4; ++i) {
            vec2 o = (vec2(float(i), float(j)) - 1.5) * 3.0 * uTexel;
            acc += texture(uTex, vUv + o).rgb;
        }
    }
    fragColor = vec4(acc / 16.0, 1.0);
}
)glsl";

void applyBlend(int mode) {
    switch (mode) {
    case BlendAdd:
        glBlendFunc(GL_SRC_ALPHA, GL_ONE);
        break;
    case BlendScreen:
        glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_COLOR);
        break;
    case BlendMultiply:
        glBlendFunc(GL_DST_COLOR, GL_ZERO);
        break;
    default:
        glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        break;
    }
}

constexpr unsigned long long kHiddenPreviewInterval = 8;

bool chainHasFeedback(const std::array<EffectSlot, kEffectSlots>& slots) {
    const std::vector<EffectDef>& library = effectLibrary();
    for (const EffectSlot& slot : slots) {
        if (slot.enabled && slot.effect >= 0 && slot.effect < static_cast<int>(library.size()) && library[static_cast<size_t>(slot.effect)].usesPrevious) {
            return true;
        }
    }
    return false;
}

void clearTarget(const RenderTarget& target) {
    target.bind();
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
}

}

bool Engine::buildPrograms() {
    std::string error;

    const std::string grade = gradeGlsl();
    composite_.id = buildProgram(clipVertexSource(), std::string(kPassHeader) + grade + kCompositeBody, &error);
    if (!composite_.id) {
        logLine("Composite shader failed: " + error);
        return false;
    }
    composite_.tex = glGetUniformLocation(composite_.id, "uTex");
    composite_.opacity = glGetUniformLocation(composite_.id, "uOpacity");
    composite_.mode = glGetUniformLocation(composite_.id, "uMode");
    composite_.grade = glGetUniformLocation(composite_.id, "uGrade");

    error.clear();
    blend_.id = buildProgram(clipVertexSource(), std::string(kPassHeader) + grade + kBlendBody, &error);
    if (!blend_.id) {
        logLine("Blend shader failed: " + error);
        return false;
    }
    blend_.tex = glGetUniformLocation(blend_.id, "uTex");
    blend_.back = glGetUniformLocation(blend_.id, "uBack");
    blend_.opacity = glGetUniformLocation(blend_.id, "uOpacity");
    blend_.mode = glGetUniformLocation(blend_.id, "uMode");
    blend_.grade = glGetUniformLocation(blend_.id, "uGrade");

    error.clear();
    final_.id = buildProgram(clipVertexSource(), std::string(kPassHeader) + grade + kFinalBody, &error);
    if (!final_.id) {
        logLine("Final shader failed: " + error);
        return false;
    }
    final_.tex = glGetUniformLocation(final_.id, "uTex");
    final_.level = glGetUniformLocation(final_.id, "uLevel");
    final_.grade = glGetUniformLocation(final_.id, "uGrade");

    error.clear();
    transition_program_.id = buildProgram(clipVertexSource(), std::string(kPassHeader) + kTransitionBody, &error);
    if (!transition_program_.id) {
        logLine("Transition shader failed: " + error);
        return false;
    }
    transition_program_.a = glGetUniformLocation(transition_program_.id, "uA");
    transition_program_.b = glGetUniformLocation(transition_program_.id, "uB");
    transition_program_.progress = glGetUniformLocation(transition_program_.id, "uP");
    transition_program_.type = glGetUniformLocation(transition_program_.id, "uType");
    transition_program_.aspect = glGetUniformLocation(transition_program_.id, "uAspect");
    transition_program_.time = glGetUniformLocation(transition_program_.id, "uTime");

    error.clear();
    thumb_.id = buildProgram(clipVertexSource(), std::string(kPassHeader) + kThumbBody, &error);
    if (!thumb_.id) {
        logLine("Thumbnail shader failed: " + error);
        return false;
    }
    thumb_.tex = glGetUniformLocation(thumb_.id, "uTex");
    thumb_.texel = glGetUniformLocation(thumb_.id, "uTexel");

    error.clear();
    spoutProgram_.id = buildProgram(clipVertexSource(), std::string(kPassHeader) + kSpoutBody, &error);
    if (!spoutProgram_.id) {
        logLine("Spout shader failed: " + error);
        return false;
    }
    spoutProgram_.tex = glGetUniformLocation(spoutProgram_.id, "uTex");
    spoutProgram_.fit = glGetUniformLocation(spoutProgram_.id, "uFit");
    spoutProgram_.flip = glGetUniformLocation(spoutProgram_.id, "uFlip");
    spoutLastUse_.fill(-1.0e9);

    error.clear();
    mediaProgram_.id = buildProgram(clipVertexSource(), std::string(kPassHeader) + kMediaBody, &error);
    if (!mediaProgram_.id) {
        logLine("Media shader failed: " + error);
        return false;
    }
    mediaProgram_.tex = glGetUniformLocation(mediaProgram_.id, "uTex");
    mediaProgram_.fit = glGetUniformLocation(mediaProgram_.id, "uFit");
    mediaProgram_.premult = glGetUniformLocation(mediaProgram_.id, "uPremult");
    mediaProgram_.backdrop = glGetUniformLocation(mediaProgram_.id, "uBackdrop");
    mediaProgram_.texScale = glGetUniformLocation(mediaProgram_.id, "uTexScale");
    mediaProgram_.ycocg = glGetUniformLocation(mediaProgram_.id, "uYcocg");

    clips_.assign(clipLibrary().size(), ClipProgram{});
    effects_.assign(effectLibrary().size(), EffectProgram{});

    scenes_.clear();
    scenes_.resize(clipLibrary().size());
    for (size_t i = 0; i < scenes_.size(); ++i) {
        scenes_[i] = makeScene(clipLibrary()[i]);
    }
    if (!sceneRenderer_.init()) {
        logLine("3D clips are unavailable");
    }
    media_.clear();
    media_.resize(clipLibrary().size());
    for (size_t i = 0; i < media_.size(); ++i) {
        media_[i] = makeMedia(clipLibrary()[i]);
    }
    generators_.clear();
    generators_.resize(clipLibrary().size());
    for (size_t i = 0; i < generators_.size(); ++i) {
        generators_[i] = makeGenerator(clipLibrary()[i]);
    }
    return true;
}

VideoOptions Engine::videoOptions() const {
    VideoOptions options;
    options.mode = videoMode_;
    options.gpuConversion = gpuConversion_ && videoGpu_.caps().conversion;
    options.compressedTextures = videoGpu_.caps().compressedTextures;
    options.hapDirect = hapDirect_ && videoGpu_.caps().compressedTextures;
    return options;
}

std::vector<const MediaPlayer*> Engine::activeVideos() const {
    std::vector<const MediaPlayer*> result;
    for (const std::unique_ptr<MediaPlayer>& player : media_) {
        if (player && player->isVideo() && player->state() == MediaState::Ready) {
            result.push_back(player.get());
        }
    }
    return result;
}

void Engine::setVideoSettings(VideoDecodeMode mode, bool hapDirect, bool gpuConversion) {
    if (mode == videoMode_ && hapDirect == hapDirect_ && gpuConversion == gpuConversion_) {
        return;
    }
    videoMode_ = mode;
    hapDirect_ = hapDirect;
    gpuConversion_ = gpuConversion;
    const VideoOptions options = videoOptions();
    for (std::unique_ptr<MediaPlayer>& player : media_) {
        if (!player || !player->isVideo()) {
            continue;
        }
        player->setVideoOptions(options);
        if (player->state() != MediaState::Idle) {
            player->release();
        }
    }
}

std::unique_ptr<MediaPlayer> Engine::makeMedia(const ClipDef& def) {
    if (def.kind == ClipKind::ImageFile || def.kind == ClipKind::VideoFile) {
        const bool video = def.kind == ClipKind::VideoFile;
        return std::make_unique<MediaPlayer>(def.name, def.path, video, video ? &videoGpu_ : nullptr, videoOptions());
    }
    return nullptr;
}

std::unique_ptr<GeneratorPlayer> Engine::makeGenerator(const ClipDef& def) const {
    if (def.kind == ClipKind::TextFile) {
        return std::make_unique<TextPlayer>(def.name, generatorStem(def));
    }
    if (def.kind == ClipKind::ParticleClip) {
        auto player = std::make_unique<ParticlePlayer>(def.name, generatorStem(def), def.path.empty(), particleLimit_);
        player->setGpu(gpuParticles_);
        return player;
    }
    return nullptr;
}

std::unique_ptr<ScenePlayer> Engine::makeScene(const ClipDef& def) const {
    if (def.kind == ClipKind::BuiltinScene) {
        const size_t sceneIndex = static_cast<size_t>(def.sceneIndex);
        return std::make_unique<ScenePlayer>(def.name, [sceneIndex](const GltfLimits&) {
            GltfResult result;
            result.scene = builtinScenes()[sceneIndex].build();
            result.ok = true;
            return result;
        });
    }
    if (def.kind == ClipKind::ScenePack) {
        const std::string path = def.path;
        return std::make_unique<ScenePlayer>(def.name, [path](const GltfLimits& limits) {
            GltfResult result;
            ScenePack pack;
            if (!readScenePackFile(pathFromUtf8(path), pack, result.error)) {
                return result;
            }
            AssetSource source({pathFromUtf8(modelDirectory()), pathFromUtf8(texturesDirectory())}, std::make_shared<ScenePack>(pack));
            return buildSceneFromDoc(pack.doc, source.assets(), limits);
        });
    }
    if (def.kind == ClipKind::ModelFile) {
        const std::string path = def.path;
        return std::make_unique<ScenePlayer>(def.name, [path](const GltfLimits& limits) { return loadGltfFile(std::filesystem::path(std::u8string(path.begin(), path.end())), limits); });
    }
    return nullptr;
}

void Engine::reloadShaderClips(const std::vector<size_t>& indices) {
    for (size_t index : indices) {
        if (index >= clips_.size()) {
            continue;
        }
        releaseClipProgram(clips_[index]);
        clips_[index] = ClipProgram{};
        clearShaderStates(static_cast<int>(index));
        if (std::find(reloadThumbs_.begin(), reloadThumbs_.end(), index) == reloadThumbs_.end()) {
            reloadThumbs_.push_back(index);
        }
    }
}

void Engine::syncEffects() {
    const size_t size = effectLibrary().size();
    if (effects_.size() < size) {
        effects_.resize(size);
    }
}

void Engine::reloadNodeEffects(const std::vector<size_t>& indices) {
    const std::vector<EffectDef>& library = effectLibrary();
    for (size_t index : indices) {
        if (index >= effects_.size() || index >= library.size()) {
            continue;
        }
        if (!library[index].error.empty() && (effects_[index].id || !effects_[index].passes.empty())) {
            continue;
        }
        if (effects_[index].id) {
            glDeleteProgram(effects_[index].id);
        }
        for (const PassProgram& pass : effects_[index].passes) {
            glDeleteProgram(pass.id);
        }
        effects_[index] = EffectProgram{};
        clearShaderStates(kEffectStateBase + static_cast<int>(index));
    }
}

std::string Engine::effectError(int effect) const {
    const std::vector<EffectDef>& library = effectLibrary();
    if (effect < 0 || static_cast<size_t>(effect) >= library.size()) {
        return std::string();
    }
    if (!library[static_cast<size_t>(effect)].error.empty()) {
        return library[static_cast<size_t>(effect)].error;
    }
    if (static_cast<size_t>(effect) < effects_.size()) {
        return effects_[static_cast<size_t>(effect)].error;
    }
    return std::string();
}

int Engine::checkNodeCatalogue(std::string& report) {
    int failed = 0;
    const auto test = [&](const GraphDocument& graph, const std::string& label) {
        const CompiledGraph compiled = compileGraph(graph);
        if (!compiled.ok) {
            ++failed;
            report += "node graph " + label + ": " + compiled.firstError() + "\n";
            return;
        }
        std::string error;
        const GLuint program = buildProgram(clipVertexSource(), assembleEffectSource(compiled.glsl), &error);
        if (!program) {
            ++failed;
            report += "node graph " + label + ": " + error + "\n";
            return;
        }
        glDeleteProgram(program);
    };
    int graphs = 0;
    for (const NodeSpec& spec : nodeCatalogue()) {
        for (const GraphDocument& graph : catalogueGraphs(spec)) {
            test(graph, graph.name);
            ++graphs;
        }
    }
    for (const ExampleGraph& example : exampleGraphs()) {
        test(example.build(), example.name);
        ++graphs;
    }
    logLine("Node catalogue: " + std::to_string(graphs) + " graphs compiled, " + std::to_string(failed) + " failed");
    return failed;
}

std::string Engine::clipError(int clip) const {
    if (clip < 0 || static_cast<size_t>(clip) >= clips_.size()) {
        return std::string();
    }
    const ClipDef& def = clipLibrary()[static_cast<size_t>(clip)];
    if (!def.error.empty()) {
        return def.error;
    }
    if (static_cast<size_t>(clip) < generators_.size() && generators_[static_cast<size_t>(clip)]) {
        const std::string& own = generators_[static_cast<size_t>(clip)]->error();
        if (own.empty() && generatorGl_.failed()) {
            return generatorGl_.failure();
        }
        return own;
    }
    return clips_[static_cast<size_t>(clip)].error;
}

std::string Engine::shaderRuntimeError(int clip) const {
    if (clip < 0 || static_cast<size_t>(clip) >= clips_.size()) {
        return std::string();
    }
    return clips_[static_cast<size_t>(clip)].runtimeError;
}

std::string Engine::shaderNote(int clip) const {
    if (clip < 0 || static_cast<size_t>(clip) >= clips_.size()) {
        return std::string();
    }
    return clips_[static_cast<size_t>(clip)].note;
}

void Engine::setShaderBufferBudget(unsigned long long bytes) {
    shaderBudget_ = bytes;
    for (auto& entry : shaderStates_) {
        entry.second.destroy();
    }
    shaderStates_.clear();
}

size_t Engine::shaderBufferBytes() const {
    size_t total = 0;
    for (const auto& entry : shaderStates_) {
        total += entry.second.bytes();
    }
    return total;
}

void Engine::syncLibrary() {
    const std::vector<ClipDef>& library = clipLibrary();
    const size_t before = clips_.size();
    if (library.size() <= before) {
        return;
    }
    clips_.resize(library.size());
    scenes_.resize(library.size());
    media_.resize(library.size());
    generators_.resize(library.size());
    clipThumbs_.resize(library.size());
    for (size_t i = before; i < library.size(); ++i) {
        clipThumbs_[i].create(kThumbWidth, kThumbHeight);
        scenes_[i] = makeScene(library[i]);
        media_[i] = makeMedia(library[i]);
        generators_[i] = makeGenerator(library[i]);
        if (media_[i]) {
            deferredMediaThumbs_.push_back(i);
        }
        if (library[i].kind == ClipKind::ScenePack) {
            deferredThumbs_.push_back(i);
        }
    }
}

bool Engine::isMediaClip(int clip) const {
    return clip >= 0 && static_cast<size_t>(clip) < media_.size() && media_[static_cast<size_t>(clip)];
}

MediaPlayer* Engine::mediaPlayer(int clip) {
    return isMediaClip(clip) ? media_[static_cast<size_t>(clip)].get() : nullptr;
}

int Engine::loadedMediaCount() const {
    int count = 0;
    for (const std::unique_ptr<MediaPlayer>& player : media_) {
        if (player && player->state() == MediaState::Ready) {
            ++count;
        }
    }
    return count;
}

bool Engine::ensureMedia(size_t index) {
    if (index >= media_.size() || !media_[index]) {
        return false;
    }
    MediaPlayer& player = *media_[index];
    player.touch();
    if (player.poll() != MediaState::Ready) {
        return false;
    }
    if (!player.update(player.lastClock())) {
        return false;
    }
    if (!player.thumbnailDone) {
        renderMediaThumbnail(index);
    }
    return true;
}

void Engine::renderMediaThumbnail(size_t index) {
    MediaPlayer& player = *media_[index];
    player.thumbnailDone = true;
    if (index >= clipThumbs_.size() || !clipThumbs_[index].fbo) {
        return;
    }
    clipThumbs_[index].bind();
    glDisable(GL_BLEND);
    drawMedia(player, 1.0f, true, player.lastClock());
}

bool Engine::drawMedia(MediaPlayer& player, float scale, bool backdrop, double clock, const ClipPlayback* playback) {
    glClearColor(0.0f, 0.0f, 0.0f, backdrop ? 1.0f : 0.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    if (playback != nullptr) {
        player.setPlayback(*playback);
    }
    if (!player.update(clock, beat_) || player.texture() == 0 || player.width() <= 0 || player.height() <= 0) {
        return false;
    }
    const float source = static_cast<float>(player.width()) / static_cast<float>(player.height());
    const float target = static_cast<float>(out_.width) / static_cast<float>(out_.height);
    const float zoom = std::max(scale, 0.05f);
    glUseProgram(mediaProgram_.id);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, player.texture());
    glUniform1i(mediaProgram_.tex, 0);
    glUniform2f(mediaProgram_.fit, std::max(target / source, 1.0f) / zoom, std::max(source / target, 1.0f) / zoom);
    glUniform1f(mediaProgram_.premult, player.premultiplied() ? 1.0f : 0.0f);
    glUniform1f(mediaProgram_.backdrop, backdrop ? 1.0f : 0.0f);
    glUniform2f(mediaProgram_.texScale, player.scaleX(), player.scaleY());
    glUniform1f(mediaProgram_.ycocg, player.ycocg() ? 1.0f : 0.0f);
    triangle_.draw();
    glBindTexture(GL_TEXTURE_2D, 0);
    return true;
}

bool Engine::isGeneratorClip(int clip) const {
    return clip >= 0 && static_cast<size_t>(clip) < generators_.size() && generators_[static_cast<size_t>(clip)];
}

GeneratorPlayer* Engine::generatorPlayer(int clip) {
    return isGeneratorClip(clip) ? generators_[static_cast<size_t>(clip)].get() : nullptr;
}

void Engine::setParticleLimit(int limit) {
    particleLimit_ = particles::clampLimit(limit);
    for (std::unique_ptr<GeneratorPlayer>& player : generators_) {
        if (auto* particle = dynamic_cast<ParticlePlayer*>(player.get())) {
            particle->setLimit(particleLimit_);
        }
    }
    if (particleEditor_) {
        particleEditor_->setLimit(particleLimit_);
    }
}

void Engine::setGpuParticles(bool enabled, int budgetMb) {
    gpuParticles_ = enabled;
    gpuParticleBudgetMb_ = gpuparticles::clampBudgetMb(budgetMb);
    generatorGl_.gpuParticles.budget->setLimit(static_cast<std::uint64_t>(gpuParticleBudgetMb_) * 1024ull * 1024ull);
    for (std::unique_ptr<GeneratorPlayer>& player : generators_) {
        if (auto* particle = dynamic_cast<ParticlePlayer*>(player.get())) {
            particle->setGpu(gpuParticles_);
        }
    }
    if (particleEditor_) {
        particleEditor_->setGpu(gpuParticles_);
    }
}

void Engine::reloadGeneratorClips(const std::vector<size_t>& indices) {
    for (size_t index : indices) {
        if (index >= generators_.size() || !generators_[index]) {
            continue;
        }
        generators_[index]->reload();
        generators_[index]->thumbnailDone = false;
        if (std::find(deferredGeneratorThumbs_.begin(), deferredGeneratorThumbs_.end(), index) == deferredGeneratorThumbs_.end()) {
            deferredGeneratorThumbs_.push_back(index);
        }
    }
}

GeneratorFrame Engine::generatorFrame(const Layer& layer, float scale, double clock, int width, int height) const {
    GeneratorFrame frame;
    frame.clock = clock;
    frame.beat = beat_;
    frame.level = audio_.level;
    frame.colorA = {layer.colorA[0], layer.colorA[1], layer.colorA[2]};
    frame.colorB = {layer.colorB[0], layer.colorB[1], layer.colorB[2]};
    frame.rainbow = layer.rainbow;
    frame.scale = scale;
    frame.width = width;
    frame.height = height;
    return frame;
}

bool Engine::ensureGenerator(size_t index) {
    if (index >= generators_.size() || !generators_[index]) {
        return false;
    }
    GeneratorPlayer& player = *generators_[index];
    player.touch();
    std::string error;
    if (!generatorGl_.ensure(error)) {
        return false;
    }
    if (player.poll(out_.height) != GeneratorState::Ready) {
        return false;
    }
    if (!player.thumbnailDone) {
        renderGeneratorThumbnail(index);
    }
    return true;
}

void Engine::renderGeneratorThumbnail(size_t index) {
    GeneratorPlayer& player = *generators_[index];
    player.thumbnailDone = true;
    if (index >= clipThumbs_.size() || !clipThumbs_[index].fbo) {
        return;
    }
    clipThumbs_[index].bind();
    glDisable(GL_BLEND);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    const Layer defaults;
    GeneratorFrame frame = generatorFrame(defaults, 1.0f, 1.2, kThumbWidth, kThumbHeight);
    frame.beat = 2.4;
    frame.level = 0.5f;
    if (auto* particle = dynamic_cast<ParticlePlayer*>(&player)) {
        particle->drawThumbnail(generatorGl_, frame);
    } else {
        player.draw(generatorGl_, frame, -1);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

bool Engine::drawGenerator(size_t index, int instance, const Layer& layer, float scale, double clock, const RenderTarget& target) {
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    GeneratorPlayer& player = *generators_[index];
    player.touch();
    return player.draw(generatorGl_, generatorFrame(layer, scale, clock, target.width, target.height), instance);
}

void Engine::serviceGenerators() {
    constexpr double kIdleSeconds = 120.0;
    for (const Layer& layer : comp.layers) {
        for (const int cell : layer.cells) {
            if (cell >= 0 && static_cast<size_t>(cell) < generators_.size() && generators_[static_cast<size_t>(cell)] && layer.activeColumn >= 0 && layer.cells[static_cast<size_t>(layer.activeColumn)] == cell) {
                generators_[static_cast<size_t>(cell)]->touch();
            }
        }
    }
    if (!deferredGeneratorThumbs_.empty()) {
        const size_t i = deferredGeneratorThumbs_.front();
        if (i >= generators_.size() || !generators_[i] || generators_[i]->state() == GeneratorState::Failed) {
            deferredGeneratorThumbs_.erase(deferredGeneratorThumbs_.begin());
        } else {
            ensureGenerator(i);
            if (generators_[i]->thumbnailDone || generators_[i]->state() == GeneratorState::Failed) {
                deferredGeneratorThumbs_.erase(deferredGeneratorThumbs_.begin());
            }
        }
    }
    for (std::unique_ptr<GeneratorPlayer>& player : generators_) {
        if (player && player->state() == GeneratorState::Ready && player->idleSeconds() > kIdleSeconds) {
            if (dynamic_cast<TextPlayer*>(player.get()) != nullptr) {
                player->release();
                logLine("Text '" + player->label() + "' was unloaded because it was not used for two minutes");
            }
        }
    }
}

bool Engine::drawTextPreview(const text::TextDoc& doc, RenderTarget& target, double clock) {
    std::string error;
    if (!generatorGl_.ensure(error) || !target.fbo) {
        return false;
    }
    if (!textEditor_) {
        textEditor_ = std::make_unique<TextPlayer>("editor", std::string());
    }
    const std::string key = text::textDocToJson(doc);
    if (key != textEditorKey_) {
        textEditorKey_ = key;
        textEditor_->setDocument(doc);
    }
    textEditor_->touch();
    target.bind();
    glDisable(GL_BLEND);
    glClearColor(0.05f, 0.05f, 0.06f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    if (textEditor_->poll(out_.height) != GeneratorState::Ready) {
        return false;
    }
    const Layer defaults;
    GeneratorFrame frame = generatorFrame(defaults, 1.0f, clock, target.width, target.height);
    const bool drawn = textEditor_->draw(generatorGl_, frame, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return drawn;
}

bool Engine::drawParticlePreview(const particles::Params& params, RenderTarget& target, double clock) {
    std::string error;
    if (!generatorGl_.ensure(error) || !target.fbo) {
        return false;
    }
    if (!particleEditor_) {
        particleEditor_ = std::make_unique<ParticlePlayer>("editor", std::string(), false, particleLimit_);
        particleEditor_->setGpu(gpuParticles_);
    }
    const std::string key = particles::paramsToJson(params);
    if (key != particleEditorKey_) {
        particleEditorKey_ = key;
        particleEditor_->setParams(params);
    }
    particleEditor_->setLimit(particleLimit_);
    particleEditor_->touch();
    target.bind();
    glDisable(GL_BLEND);
    glClearColor(0.05f, 0.05f, 0.06f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    const Layer defaults;
    GeneratorFrame frame = generatorFrame(defaults, 1.0f, clock, target.width, target.height);
    const bool drawn = particleEditor_->draw(generatorGl_, frame, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return drawn;
}

void Engine::requestTextPreview(const text::TextDoc& doc, double clock) {
    generatorPreviewKind_ = EditorPreviewKind::Text;
    generatorPreviewText_ = doc;
    generatorPreviewClock_ = clock;
}

void Engine::requestParticlePreview(const particles::Params& params, double clock) {
    generatorPreviewKind_ = EditorPreviewKind::Particles;
    generatorPreviewParticles_ = params;
    generatorPreviewClock_ = clock;
}

void Engine::requestChainPreview(const std::array<EffectSlot, kEffectSlots>& slots, double clock) {
    chainPreviewRequested_ = true;
    chainPreviewSlots_ = slots;
    chainPreviewClock_ = clock;
}

void Engine::renderEditorPreviews() {
    constexpr int kEditorWidth = 480;
    constexpr int kEditorHeight = 270;
    generatorPreviewReady_ = false;
    if (generatorPreviewKind_ == EditorPreviewKind::None) {
        if (generatorPreview_.fbo) {
            generatorPreview_.destroy();
        }
        textEditor_.reset();
        particleEditor_.reset();
        textEditorKey_.clear();
        particleEditorKey_.clear();
    } else {
        if (!generatorPreview_.fbo && !generatorPreview_.create(kEditorWidth, kEditorHeight)) {
            generatorPreview_.destroy();
        } else if (generatorPreviewKind_ == EditorPreviewKind::Text) {
            particleEditor_.reset();
            particleEditorKey_.clear();
            generatorPreviewReady_ = drawTextPreview(generatorPreviewText_, generatorPreview_, generatorPreviewClock_) || generatorPreview_.fbo != 0;
        } else {
            textEditor_.reset();
            textEditorKey_.clear();
            drawParticlePreview(generatorPreviewParticles_, generatorPreview_, generatorPreviewClock_);
            generatorPreviewReady_ = generatorPreview_.fbo != 0;
        }
        generatorPreviewKind_ = EditorPreviewKind::None;
    }

    chainPreviewResult_ = nullptr;
    if (!chainPreviewRequested_) {
        if (chainPreviewA_.fbo) {
            chainPreviewA_.destroy();
            chainPreviewB_.destroy();
            for (RenderTarget& target : chainPreviewFeedback_) {
                target.destroy();
            }
        }
        return;
    }
    chainPreviewRequested_ = false;
    if (chainPreviewClip_ == -2) {
        chainPreviewClip_ = -1;
        const std::vector<ClipDef>& library = clipLibrary();
        for (size_t i = 0; i < library.size(); ++i) {
            if (library[i].name == "Plasma" && library[i].error.empty() && !library[i].isScene()) {
                chainPreviewClip_ = static_cast<int>(i);
                break;
            }
        }
    }
    if (chainPreviewClip_ < 0 || !ensureClip(static_cast<size_t>(chainPreviewClip_))) {
        return;
    }
    if (!chainPreviewA_.fbo && !(chainPreviewA_.create(kEditorWidth, kEditorHeight) && chainPreviewB_.create(kEditorWidth, kEditorHeight))) {
        chainPreviewA_.destroy();
        chainPreviewB_.destroy();
        return;
    }
    chainPreviewFeedbackUsed_.fill(false);
    const Layer defaults;
    drawLayerSource(chainPreviewClip_, kLayerCount * 2, defaults, ClipPlayback{}, 1.0f, chainPreviewClock_, chainPreviewA_);
    chainPreviewResult_ = runChain(chainPreviewSlots_, chainPreviewFeedback_.data(), chainPreviewFeedbackUsed_.data(), &chainPreviewA_, &chainPreviewB_, 0, -1);
    for (size_t k = 0; k < chainPreviewFeedback_.size(); ++k) {
        if (chainPreviewFeedback_[k].fbo && !chainPreviewFeedbackUsed_[k]) {
            chainPreviewFeedback_[k].destroy();
        }
    }
}

void Engine::serviceMedia() {
    constexpr double kImageIdleSeconds = 120.0;
    constexpr double kVideoIdleSeconds = 6.0;
    const bool trimming = memoryTrimming();
    const std::vector<char> wanted = trimming ? activeAndNextCells(media_.size(), true) : std::vector<char>();
    for (const Layer& layer : comp.layers) {
        for (const int cell : layer.cells) {
            if (cell >= 0 && static_cast<size_t>(cell) < media_.size() && media_[static_cast<size_t>(cell)] && !media_[static_cast<size_t>(cell)]->isVideo()) {
                if (!trimming || wanted[static_cast<size_t>(cell)] || !media_[static_cast<size_t>(cell)]->thumbnailDone) {
                    ensureMedia(static_cast<size_t>(cell));
                }
            }
        }
    }
    if (!deferredMediaThumbs_.empty()) {
        const size_t i = deferredMediaThumbs_.front();
        if (i >= media_.size() || !media_[i] || media_[i]->state() == MediaState::Failed) {
            deferredMediaThumbs_.erase(deferredMediaThumbs_.begin());
        } else {
            ensureMedia(i);
            if (media_[i]->thumbnailDone || media_[i]->state() == MediaState::Failed) {
                deferredMediaThumbs_.erase(deferredMediaThumbs_.begin());
            }
        }
    }
    for (std::unique_ptr<MediaPlayer>& player : media_) {
        if (!player || player->state() != MediaState::Ready) {
            continue;
        }
        if (player->idleSeconds() > (player->isVideo() ? kVideoIdleSeconds : kImageIdleSeconds)) {
            player->release();
            if (!player->isVideo()) {
                logLine("Image '" + player->label() + "' was unloaded because it was not used for two minutes");
            }
        }
    }
}

void Engine::prewarmMedia() {
    if (!prewarmNext || (memoryTrimming() && !gpudiag::preparingAllowed(pressure_.level()))) {
        return;
    }
    std::array<int, kLayerCount> prepared{};
    prepared.fill(-1);
    for (int l = 0; l < kLayerCount; ++l) {
        const Layer& layer = comp.layers[static_cast<size_t>(l)];
        if (layer.activeColumn < 0) {
            continue;
        }
        for (int step = 1; step < kColumnCount; ++step) {
            const int column = (layer.activeColumn + step) % kColumnCount;
            const int clip = layer.cells[static_cast<size_t>(column)];
            if (clip < 0) {
                continue;
            }
            bool playing = false;
            for (const Layer& other : comp.layers) {
                playing = playing || (other.activeColumn >= 0 && other.cells[static_cast<size_t>(other.activeColumn)] == clip);
            }
            const bool taken = std::find(prepared.begin(), prepared.end(), clip) != prepared.end();
            if (!playing && !taken && isMediaClip(clip) && media_[static_cast<size_t>(clip)]->isVideo()) {
                prepared[static_cast<size_t>(l)] = clip;
                MediaPlayer& player = *media_[static_cast<size_t>(clip)];
                player.touch();
                if (player.poll() == MediaState::Ready) {
                    player.prepare(layer.playback[static_cast<size_t>(column)]);
                }
            }
            break;
        }
    }
}

void Engine::reloadScene(int clip) {
    ScenePlayer* player = scenePlayer(clip);
    if (player == nullptr) {
        return;
    }
    player->release();
    player->thumbnailDone = false;
    const size_t index = static_cast<size_t>(clip);
    if (std::find(deferredThumbs_.begin(), deferredThumbs_.end(), index) == deferredThumbs_.end()) {
        deferredThumbs_.push_back(index);
    }
}

int Engine::loadedSceneCount() const {
    int count = 0;
    for (const std::unique_ptr<ScenePlayer>& player : scenes_) {
        if (player && player->state() == SceneState::Ready) {
            ++count;
        }
    }
    return count;
}

bool Engine::init() {
    triangle_.create();
    videoGpu_.create();
    strip_.init();
    glGenQueries(kGpuQueries, gpuQueries_.data());
    glGenTextures(1, &audioTexture_);
    glBindTexture(GL_TEXTURE_2D, audioTexture_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F, kSpectrumBins, 2, 0, GL_RED, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
    glGenSamplers(static_cast<GLsizei>(channelSamplers_.size()), channelSamplers_.data());
    for (size_t k = 0; k < channelSamplers_.size(); ++k) {
        const bool nearest = (k & 1u) != 0;
        const bool repeat = (k & 2u) != 0;
        glSamplerParameteri(channelSamplers_[k], GL_TEXTURE_MIN_FILTER, nearest ? GL_NEAREST : GL_LINEAR);
        glSamplerParameteri(channelSamplers_[k], GL_TEXTURE_MAG_FILTER, nearest ? GL_NEAREST : GL_LINEAR);
        glSamplerParameteri(channelSamplers_[k], GL_TEXTURE_WRAP_S, repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE);
        glSamplerParameteri(channelSamplers_[k], GL_TEXTURE_WRAP_T, repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE);
    }
    glGenTextures(1, &blackTexture_);
    glBindTexture(GL_TEXTURE_2D, blackTexture_);
    const unsigned char black[4] = {0, 0, 0, 0};
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, black);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glBindTexture(GL_TEXTURE_2D, 0);
    audio_ = virtualAudio(0.0, 0.0);
    if (!buildPrograms()) {
        return false;
    }

    comp = Composition{};
    const Resolution& initial = kResolutions[kDefaultResolution];
    if (!createTargets(initial.width, initial.height)) {
        return false;
    }
    currentResolution_ = kDefaultResolution;
    comp.resolution = kDefaultResolution;

    clipThumbs_.assign(clips_.size(), RenderTarget{});
    for (RenderTarget& thumb : clipThumbs_) {
        thumb.create(kThumbWidth, kThumbHeight);
    }
    for (RenderTarget& preview : previews_) {
        preview.create(kPreviewWidth, kPreviewHeight);
        clearTarget(preview);
    }
    nextThumb_ = 0;
    renderClipThumbnails(4);
    return true;
}

bool Engine::ensureScene(size_t index) {
    if (index >= scenes_.size() || !scenes_[index] || !sceneRenderer_.ready()) {
        return false;
    }
    ScenePlayer& player = *scenes_[index];
    if (player.poll() != SceneState::Ready) {
        return false;
    }
    if (!player.thumbnailDone) {
        renderSceneThumbnail(index);
    }
    return true;
}

void Engine::renderSceneThumbnail(size_t index) {
    ScenePlayer& player = *scenes_[index];
    player.thumbnailDone = true;
    if (index >= clipThumbs_.size() || !clipThumbs_[index].fbo) {
        return;
    }
    const AudioFeatures sample = virtualAudio(2.3, 2.6);
    const Layer defaults;
    SceneFrame frame;
    frame.time = 2.6f;
    frame.bass = sample.bass;
    frame.mid = sample.mid;
    frame.high = sample.high;
    frame.level = sample.level;
    frame.kick = sample.kick;
    frame.colorA = defaults.colorA;
    frame.colorB = defaults.colorB;
    sceneRenderer_.draw(player, frame, clipThumbs_[index]);
}

void Engine::serviceScenes() {
    constexpr double kUnloadAfterSeconds = 120.0;
    std::vector<char> referenced(scenes_.size(), 0);
    const bool trimming = memoryTrimming();
    const std::vector<char> wanted = trimming ? activeAndNextCells(scenes_.size(), true) : std::vector<char>();
    for (const Layer& layer : comp.layers) {
        for (const int cell : layer.cells) {
            if (cell >= 0 && static_cast<size_t>(cell) < scenes_.size() && scenes_[static_cast<size_t>(cell)]) {
                if (trimming && !wanted[static_cast<size_t>(cell)]) {
                    continue;
                }
                referenced[static_cast<size_t>(cell)] = 1;
                scenes_[static_cast<size_t>(cell)]->touch();
                ensureScene(static_cast<size_t>(cell));
            }
        }
    }
    for (size_t k = 0; k < deferredThumbs_.size();) {
        const size_t i = deferredThumbs_[k];
        const bool done = ensureScene(i) || scenes_[i]->state() == SceneState::Failed;
        if (done) {
            deferredThumbs_.erase(deferredThumbs_.begin() + static_cast<long>(k));
        } else {
            referenced[i] = 1;
            ++k;
        }
    }
    for (size_t i = 0; i < scenes_.size(); ++i) {
        if (scenes_[i] && !referenced[i] && scenes_[i]->state() == SceneState::Ready && scenes_[i]->idleSeconds() > kUnloadAfterSeconds) {
            scenes_[i]->release();
            logLine("3D scene '" + scenes_[i]->label() + "' was unloaded because it was not used for two minutes");
        }
    }
}

void Engine::setMemoryPolicy(const MemoryPolicy& policy) {
    MemoryPolicy next = policy;
    next.budgetBytes = gpudiag::clampBudgetBytes(policy.budgetBytes);
    const bool changed = next.budgetBytes != memoryPolicy_.budgetBytes || next.trim != memoryPolicy_.trim;
    memoryPolicy_ = next;
    if (changed) {
        pressure_.reset();
        appliedPressure_ = 0;
        memoryCheckAt_ = -1.0e9;
    }
}

std::vector<char> Engine::activeAndNextCells(size_t count, bool includeNext) const {
    std::vector<char> wanted(count, 0);
    const auto mark = [&](const std::array<Layer, kLayerCount>& layers) {
        for (const Layer& layer : layers) {
            if (layer.activeColumn < 0 || layer.activeColumn >= kColumnCount) {
                continue;
            }
            const int active = layer.cells[static_cast<size_t>(layer.activeColumn)];
            if (active >= 0 && static_cast<size_t>(active) < count) {
                wanted[static_cast<size_t>(active)] = 1;
            }
            if (!includeNext) {
                continue;
            }
            for (int step = 1; step < kColumnCount; ++step) {
                const int clip = layer.cells[static_cast<size_t>((layer.activeColumn + step) % kColumnCount)];
                if (clip >= 0) {
                    if (static_cast<size_t>(clip) < count) {
                        wanted[static_cast<size_t>(clip)] = 1;
                    }
                    break;
                }
            }
        }
    };
    mark(comp.layers);
    if (transition_.active) {
        mark(outLayers_);
    }
    return wanted;
}

void Engine::sampleDriverMemory() {
    if (driverMemoryDisabled_) {
        return;
    }
    gpudiag::DriverMemory reading;
    if (GLAD_GL_NVX_gpu_memory_info) {
        glGetError();
        const auto query = [](GLenum name) {
            GLint value = -1;
            glGetIntegerv(name, &value);
            return static_cast<long long>(value);
        };
        gpudiag::NvxRaw raw;
        raw.dedicatedKb = query(GL_GPU_MEMORY_INFO_DEDICATED_VIDMEM_NVX);
        raw.availableKb = query(GL_GPU_MEMORY_INFO_CURRENT_AVAILABLE_VIDMEM_NVX);
        raw.evictedKb = query(GL_GPU_MEMORY_INFO_EVICTED_MEMORY_NVX);
        raw.evictionCount = query(GL_GPU_MEMORY_INFO_EVICTION_COUNT_NVX);
        reading = gpudiag::fromNvx(raw);
    } else if (GLAD_GL_ATI_meminfo) {
        glGetError();
        GLint values[4] = {-1, -1, -1, -1};
        glGetIntegerv(GL_TEXTURE_FREE_MEMORY_ATI, values);
        reading = gpudiag::fromAti({values[0], values[1], values[2], values[3]});
    } else {
        driverMemoryDisabled_ = true;
        return;
    }
    if (glGetError() != GL_NO_ERROR) {
        driverMemoryDisabled_ = true;
        driverMemory_ = gpudiag::DriverMemory{};
        logLine("The graphics driver refused the video memory query; driver memory numbers are switched off");
        return;
    }
    driverMemory_ = reading;
}

void Engine::refreshDiagnostics() {
    const GpuMemory memory = gpuMemory();
    const std::vector<char> activeMedia = activeAndNextCells(media_.size(), false);
    const std::vector<char> activeScenes = activeAndNextCells(scenes_.size(), false);
    std::uint64_t evictable = 0;
    for (size_t i = 0; i < media_.size(); ++i) {
        if (media_[i] && media_[i]->state() == MediaState::Ready && !activeMedia[i]) {
            evictable += media_[i]->gpuBytes();
        }
    }
    for (size_t i = 0; i < scenes_.size(); ++i) {
        if (scenes_[i] && scenes_[i]->state() == SceneState::Ready && !activeScenes[i]) {
            evictable += scenes_[i]->stats().gpuBytes;
        }
    }
    Diagnostics& d = diagnostics_;
    d.driver = driverMemory_;
    d.estimateBytes = memory.total();
    d.budgetBytes = memoryPolicy_.budgetBytes;
    d.evictableBytes = std::min<std::uint64_t>(evictable, d.estimateBytes);
    d.residentBytes = d.estimateBytes - d.evictableBytes;
    d.releasedBytes = releasedBytes_;
    d.releasedCount = releasedCount_;
    d.pressureLevel = pressure_.level();
    d.trimming = memoryTrimming();
    d.uploadBytesPerSecond = traffic_.bytesPerSecond();
    d.uploadPeakBytesPerSecond = traffic_.peakBytesPerSecond();
    d.uploadTotalBytes = traffic_.total();
}

void Engine::trimMemory(std::uint64_t estimate, std::uint64_t target) {
    struct Ref {
        bool scene = false;
        size_t index = 0;
    };
    const std::vector<char> activeMedia = activeAndNextCells(media_.size(), false);
    const std::vector<char> activeScenes = activeAndNextCells(scenes_.size(), false);
    std::vector<gpudiag::Candidate> candidates;
    std::vector<Ref> refs;
    for (size_t i = 0; i < media_.size(); ++i) {
        const std::unique_ptr<MediaPlayer>& player = media_[i];
        if (!player || player->state() != MediaState::Ready) {
            continue;
        }
        gpudiag::Candidate candidate;
        candidate.kind = player->isVideo() ? gpudiag::ResourceClass::PreparedVideo : gpudiag::ResourceClass::IdleImage;
        candidate.bytes = player->gpuBytes();
        candidate.idleSeconds = player->idleSeconds();
        candidate.inUse = activeMedia[i] != 0;
        candidates.push_back(candidate);
        refs.push_back({false, i});
    }
    for (size_t i = 0; i < scenes_.size(); ++i) {
        const std::unique_ptr<ScenePlayer>& player = scenes_[i];
        if (!player || player->state() != SceneState::Ready) {
            continue;
        }
        gpudiag::Candidate candidate;
        candidate.kind = gpudiag::ResourceClass::Scene3d;
        candidate.bytes = player->stats().gpuBytes;
        candidate.idleSeconds = player->idleSeconds();
        candidate.inUse = activeScenes[i] != 0;
        candidates.push_back(candidate);
        refs.push_back({true, i});
    }
    gpudiag::EvictionRequest request;
    request.currentBytes = estimate;
    request.targetBytes = target;
    request.minIdleSeconds = gpudiag::minIdleSecondsFor(pressure_.level());
    request.maxCount = 8;
    for (const size_t pick : gpudiag::planEviction(candidates, request)) {
        const Ref& ref = refs[pick];
        const std::uint64_t bytes = candidates[pick].bytes;
        std::string label;
        if (ref.scene) {
            label = scenes_[ref.index]->label();
            scenes_[ref.index]->release();
        } else {
            label = media_[ref.index]->label();
            media_[ref.index]->release();
        }
        releasedBytes_ += bytes;
        ++releasedCount_;
        logLine("Released " + std::string(gpudiag::resourceClassName(candidates[pick].kind)) + " '" + label + "' (" + std::to_string(bytes / gpudiag::kMiB) + " MB) because the graphics memory is over the budget");
    }
}

void Engine::serviceMemory() {
    const unsigned long long streamed = streamedUploadBytes();
    if (streamed >= lastStreamedBytes_) {
        traffic_.add(streamed - lastStreamedBytes_);
    }
    lastStreamedBytes_ = streamed;
    traffic_.advance(clock_);
    if (clock_ - driverSampledAt_ >= 1.0) {
        driverSampledAt_ = clock_;
        sampleDriverMemory();
    }
    if (clock_ - memoryCheckAt_ < 0.25) {
        return;
    }
    const double elapsed = std::min(clock_ - memoryCheckAt_, 1.0);
    memoryCheckAt_ = clock_;
    refreshDiagnostics();
    const int before = appliedPressure_;
    const int level = pressure_.update(elapsed, diagnostics_.estimateBytes, memoryPolicy_.budgetBytes);
    if (level != before) {
        appliedPressure_ = level;
        diagnostics_.pressureLevel = level;
        logLine("Graphics memory pressure step " + std::to_string(level) + " of " + std::to_string(gpudiag::kMaxPressureLevel));
    }
    if (memoryTrimming() && !transition_.active) {
        const std::uint64_t estimate = diagnostics_.estimateBytes;
        if (estimate > memoryPolicy_.budgetBytes) {
            trimMemory(estimate, gpudiag::lowWater(memoryPolicy_.budgetBytes));
        } else if (level > 0 && estimate > gpudiag::recoveryWater(memoryPolicy_.budgetBytes)) {
            trimMemory(estimate, gpudiag::recoveryWater(memoryPolicy_.budgetBytes));
        }
    }
}

GpuMemory Engine::gpuMemory() const {
    const auto bytes = [](const RenderTarget& target) {
        return target.fbo ? static_cast<size_t>(target.width) * static_cast<size_t>(target.height) * 4 : static_cast<size_t>(0);
    };
    GpuMemory memory;
    for (const RenderTarget* target : {&layerA_, &layerB_, &compA_, &compB_, &out_, &deckTex_[0], &deckTex_[1], &layerLoA_, &layerLoB_}) {
        memory.targets += bytes(*target);
    }
    for (const RenderTarget& target : feedback_) {
        memory.targets += bytes(target);
    }
    for (const RenderTarget& target : clipThumbs_) {
        memory.thumbnails += bytes(target);
    }
    for (const RenderTarget& target : previews_) {
        memory.thumbnails += bytes(target);
    }
    memory.sceneDepth = sceneRenderer_.depthBytes();
    memory.shaderBuffers = shaderBufferBytes();
    for (const std::unique_ptr<LiveInput>& input : liveInputs_) {
        if (input) {
            memory.live += static_cast<size_t>(input->info().memory.total());
        }
    }
    for (const std::unique_ptr<MediaPlayer>& player : media_) {
        if (player) {
            memory.media += static_cast<size_t>(player->gpuBytes());
            if (player->isVideo()) {
                const VideoMemory video = player->videoMemory();
                memory.video.output += video.output;
                memory.video.planes += video.planes;
                memory.video.compressed += video.compressed;
                memory.video.staging += video.staging;
            }
        }
    }
    for (const std::unique_ptr<ScenePlayer>& player : scenes_) {
        if (player && player->state() == SceneState::Ready) {
            memory.sceneGeometry += player->stats().gpuBytes;
        }
    }
    for (const std::unique_ptr<GeneratorPlayer>& player : generators_) {
        if (player) {
            memory.generators += static_cast<size_t>(player->gpuBytes());
        }
    }
    if (textEditor_) {
        memory.generators += static_cast<size_t>(textEditor_->gpuBytes());
    }
    if (particleEditor_) {
        memory.generators += static_cast<size_t>(particleEditor_->gpuBytes());
    }
    return memory;
}

bool Engine::isSceneClip(int clip) const {
    return clip >= 0 && static_cast<size_t>(clip) < scenes_.size() && scenes_[static_cast<size_t>(clip)];
}

ScenePlayer* Engine::scenePlayer(int clip) {
    return isSceneClip(clip) ? scenes_[static_cast<size_t>(clip)].get() : nullptr;
}

bool Engine::ensureClip(size_t index) {
    if (index >= clips_.size()) {
        return false;
    }
    if (index < scenes_.size() && scenes_[index]) {
        return ensureScene(index);
    }
    if (index < media_.size() && media_[index]) {
        return ensureMedia(index);
    }
    if (index < generators_.size() && generators_[index]) {
        return ensureGenerator(index);
    }
    ClipProgram& clip = clips_[index];
    if (clip.tried) {
        return clip.ready();
    }
    clip.tried = true;
    clip.error.clear();
    const ClipDef& def = clipLibrary()[index];
    if (!def.error.empty()) {
        clip.error = def.error;
        return false;
    }
    if (def.isAdvancedShader()) {
        return buildAdvancedClip(index, def, clip);
    }
    std::string error;
    GLuint program = buildProgram(clipVertexSource(), clipFragmentSource(def), &error);
    if (!program) {
        logLine("Clip shader '" + def.name + "' failed: " + error);
        clip.error = error.size() > 4000 ? error.substr(0, 4000) : error;
        return false;
    }
    clip.id = program;
    clip.time = glGetUniformLocation(program, "uTime");
    clip.beat = glGetUniformLocation(program, "uBeat");
    clip.bpm = glGetUniformLocation(program, "uBpm");
    clip.res = glGetUniformLocation(program, "uRes");
    clip.colorA = glGetUniformLocation(program, "uColorA");
    clip.colorB = glGetUniformLocation(program, "uColorB");
    clip.rainbow = glGetUniformLocation(program, "uRainbow");
    clip.scale = glGetUniformLocation(program, "uScale");
    clip.audio = glGetUniformLocation(program, "uAudio");
    clip.kick = glGetUniformLocation(program, "uKick");
    clip.audioTex = glGetUniformLocation(program, "uAudioTex");
    return true;
}

void Engine::releaseClipProgram(ClipProgram& clip) {
    if (clip.id) {
        glDeleteProgram(clip.id);
        clip.id = 0;
    }
    for (PassProgram& pass : clip.passes) {
        if (pass.id) {
            glDeleteProgram(pass.id);
        }
    }
    clip.passes.clear();
}

bool Engine::buildPlanPasses(const ShaderPlan& plan, bool shadertoy, const std::string& name, std::vector<PassProgram>& out, std::string& errors) {
    std::vector<PassProgram> passes;
    for (int p = 0; p < static_cast<int>(plan.passes.size()); ++p) {
        std::string error;
        const GLuint program = buildProgram(clipVertexSource(), planPassFragmentSource(plan, shadertoy, p), &error);
        if (!program) {
            errors += plan.passes[static_cast<size_t>(p)].label() + ":\n" + error + "\n";
            continue;
        }
        PassProgram pass;
        pass.id = program;
        pass.time = glGetUniformLocation(program, "uTime");
        pass.beat = glGetUniformLocation(program, "uBeat");
        pass.bpm = glGetUniformLocation(program, "uBpm");
        pass.res = glGetUniformLocation(program, "uRes");
        pass.colorA = glGetUniformLocation(program, "uColorA");
        pass.colorB = glGetUniformLocation(program, "uColorB");
        pass.rainbow = glGetUniformLocation(program, "uRainbow");
        pass.scale = glGetUniformLocation(program, "uScale");
        pass.audio = glGetUniformLocation(program, "uAudio");
        pass.kick = glGetUniformLocation(program, "uKick");
        pass.audioTex = glGetUniformLocation(program, "uAudioTex");
        pass.frame = glGetUniformLocation(program, "uFrame");
        pass.delta = glGetUniformLocation(program, "uDelta");
        pass.mix = glGetUniformLocation(program, "uMix");
        pass.picture = glGetUniformLocation(program, "uPicture");
        for (size_t k = 0; k < plan.params.size() && k < pass.param.size(); ++k) {
            pass.param[k] = glGetUniformLocation(program, plan.params[k].name.c_str());
        }
        for (int c = 0; c < kShaderChannels; ++c) {
            const std::string index = std::to_string(c);
            pass.channel[static_cast<size_t>(c)] = glGetUniformLocation(program, ("uChannel" + index).c_str());
            pass.channelRes[static_cast<size_t>(c)] = glGetUniformLocation(program, ("uChannelRes[" + index + "]").c_str());
            pass.channelTime[static_cast<size_t>(c)] = glGetUniformLocation(program, ("uChannelTime[" + index + "]").c_str());
        }
        passes.push_back(pass);
    }
    if (!errors.empty()) {
        for (const PassProgram& pass : passes) {
            glDeleteProgram(pass.id);
        }
        logLine("Shader '" + name + "' failed: " + errors);
        return false;
    }
    out = std::move(passes);
    return true;
}

bool Engine::buildAdvancedClip(size_t index, const ClipDef& def, ClipProgram& clip) {
    (void)index;
    std::string errors;
    std::vector<PassProgram> passes;
    if (!buildPlanPasses(*def.plan, def.shadertoy, def.name, passes, errors)) {
        clip.error = errors.size() > 4000 ? errors.substr(0, 4000) : errors;
        return false;
    }
    clip.passes = std::move(passes);
    return true;
}

bool Engine::ensureEffect(size_t index) {
    if (index >= effects_.size()) {
        return false;
    }
    EffectProgram& effect = effects_[index];
    if (effect.tried) {
        return effect.id != 0 || !effect.passes.empty();
    }
    effect.tried = true;
    const EffectDef& def = effectLibrary()[index];
    if (def.isGraph && !def.error.empty()) {
        logLine("Node effect '" + def.name + "' has an error: " + def.error);
        return false;
    }
    if (def.isIsf) {
        if (!def.error.empty() || !def.plan || def.plan->passes.empty()) {
            effect.error = def.error;
            return false;
        }
        std::string errors;
        std::vector<PassProgram> passes;
        if (!buildPlanPasses(*def.plan, false, def.name, passes, errors)) {
            effect.error = errors.size() > 4000 ? errors.substr(0, 4000) : errors;
            return false;
        }
        effect.passes = std::move(passes);
        effect.plan = def.plan;
        return true;
    }
    std::string error;
    GLuint program = buildProgram(clipVertexSource(), effectFragmentSource(static_cast<int>(index)), &error);
    if (!program) {
        logLine("Effect shader '" + def.name + "' failed: " + error);
        return false;
    }
    effect.id = program;
    effect.tex = glGetUniformLocation(program, "uTex");
    effect.prev = glGetUniformLocation(program, "uPrev");
    effect.params = glGetUniformLocation(program, "uParams");
    effect.time = glGetUniformLocation(program, "uTime");
    effect.beat = glGetUniformLocation(program, "uBeat");
    effect.aspect = glGetUniformLocation(program, "uAspect");
    effect.res = glGetUniformLocation(program, "uRes");
    effect.audio = glGetUniformLocation(program, "uAudio");
    effect.kick = glGetUniformLocation(program, "uKick");
    effect.audioTex = glGetUniformLocation(program, "uAudioTex");
    effect.mix = glGetUniformLocation(program, "uMix");
    effect.macros = def.usesMacros ? glGetUniformLocation(program, "uMacros") : -1;
    effect.usesPrevious = def.usesPrevious;
    return true;
}

int Engine::prepareAll(std::string& report) {
    int failed = 0;
    for (size_t i = 0; i < clips_.size(); ++i) {
        if (i < scenes_.size() && scenes_[i] && sceneRenderer_.ready() && !scenes_[i]->wait()) {
            ++failed;
            report += "scene " + clipLibrary()[i].name + ": " + scenes_[i]->error() + "\n";
            continue;
        }
        if (i < generators_.size() && generators_[i]) {
            std::string generatorError;
            if (!generatorGl_.ensure(generatorError)) {
                ++failed;
                report += "generator " + clipLibrary()[i].name + ": " + generatorError + "\n";
                continue;
            }
            GeneratorPlayer& player = *generators_[i];
            for (int attempt = 0; attempt < 600; ++attempt) {
                const GeneratorState state = player.poll(out_.height);
                if (state != GeneratorState::Idle && state != GeneratorState::Loading) {
                    break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            if (player.state() != GeneratorState::Ready) {
                ++failed;
                report += "generator " + clipLibrary()[i].name + ": " + player.error() + "\n";
            }
            continue;
        }
        if (!ensureClip(i)) {
            ++failed;
            report += "clip " + clipLibrary()[i].name + "\n";
        }
    }
    for (size_t i = 0; i < effects_.size(); ++i) {
        if (effectLibrary()[i].isGraph || effectLibrary()[i].isIsf) {
            continue;
        }
        if (!ensureEffect(i)) {
            ++failed;
            report += "effect " + effectLibrary()[i].name + "\n";
        }
    }
    failed += checkNodeCatalogue(report);
    renderClipThumbnails(static_cast<int>(clips_.size()));
    return failed;
}

void Engine::shutdown() {
    profiler_.destroy();
    if (gpuQueries_[0]) {
        glDeleteQueries(kGpuQueries, gpuQueries_.data());
        gpuQueries_.fill(0);
        gpuPending_.fill(false);
    }
    previewA_.destroy();
    previewB_.destroy();
    for (RenderTarget& target : previewFeedback_) {
        target.destroy();
    }
    generatorPreview_.destroy();
    chainPreviewA_.destroy();
    chainPreviewB_.destroy();
    for (RenderTarget& target : chainPreviewFeedback_) {
        target.destroy();
    }
    for (SpoutInput& slot : spoutSlots_) {
        slot.release();
    }
    for (std::unique_ptr<LiveInput>& input : liveInputs_) {
        input.reset();
    }
    if (spoutProgram_.id) {
        glDeleteProgram(spoutProgram_.id);
        spoutProgram_ = SpoutProgram{};
    }
    if (mediaProgram_.id) {
        glDeleteProgram(mediaProgram_.id);
        mediaProgram_ = MediaProgram{};
    }
    for (ClipProgram& clip : clips_) {
        releaseClipProgram(clip);
    }
    clips_.clear();
    for (auto& entry : shaderStates_) {
        entry.second.destroy();
    }
    shaderStates_.clear();
    if (blackTexture_) {
        glDeleteTextures(1, &blackTexture_);
        blackTexture_ = 0;
    }
    if (channelSamplers_[0]) {
        glDeleteSamplers(static_cast<GLsizei>(channelSamplers_.size()), channelSamplers_.data());
        channelSamplers_.fill(0);
    }
    deferredThumbs_.clear();
    scenes_.clear();
    media_.clear();
    generators_.clear();
    deferredGeneratorThumbs_.clear();
    textEditor_.reset();
    particleEditor_.reset();
    generatorGl_.release();
    videoGpu_.destroy();
    deferredMediaThumbs_.clear();
    sceneRenderer_.shutdown();
    for (EffectProgram& effect : effects_) {
        if (effect.id) {
            glDeleteProgram(effect.id);
        }
    }
    effects_.clear();
    for (GLuint program : {composite_.id, blend_.id, final_.id, thumb_.id, transition_program_.id}) {
        if (program) {
            glDeleteProgram(program);
        }
    }
    composite_ = CompositeProgram{};
    blend_ = BlendProgram{};
    final_ = FinalProgram{};
    thumb_ = ThumbProgram{};
    transition_program_ = TransitionProgram{};
    for (RenderTarget& thumb : clipThumbs_) {
        thumb.destroy();
    }
    clipThumbs_.clear();
    for (RenderTarget& preview : previews_) {
        preview.destroy();
    }
    destroyFeedback();
    destroyTargets();
    if (audioTexture_) {
        glDeleteTextures(1, &audioTexture_);
        audioTexture_ = 0;
    }
    strip_.shutdown();
    triangle_.destroy();
}

bool Engine::createTargets(int width, int height) {
    destroyTransitionTargets();
    return layerA_.create(width, height) && layerB_.create(width, height) && compA_.create(width, height) && compB_.create(width, height) && out_.create(width, height);
}

void Engine::destroyTransitionTargets() {
    for (RenderTarget& target : deckTex_) {
        target.destroy();
    }
    transitionTargetsReady_ = false;
    transition_ = Transition{};
}

bool Engine::ensureTransitionTargets() {
    if (transitionTargetsReady_) {
        return true;
    }
    if (!out_.fbo) {
        return false;
    }
    if (deckTex_[0].create(out_.width, out_.height) && deckTex_[1].create(out_.width, out_.height)) {
        transitionTargetsReady_ = true;
        transitionIdle_.reset();
        return true;
    }
    for (RenderTarget& target : deckTex_) {
        target.destroy();
    }
    return false;
}

void Engine::destroyTargets() {
    destroyTransitionTargets();
    layerLoA_.destroy();
    layerLoB_.destroy();
    layerA_.destroy();
    layerB_.destroy();
    compA_.destroy();
    compB_.destroy();
    out_.destroy();
}

void Engine::setQuality(int mode, double budgetMs) {
    const int clamped = clampQualityMode(mode);
    if (clamped != qualityMode_) {
        qualityMode_ = clamped;
        qualityController_.reset();
    }
    if (budgetMs > 0.0) {
        qualityBudgetMs_ = budgetMs;
    }
}

void Engine::updateQuality() {
    float scale = fixedQualityScale(qualityMode_);
    if (qualityMode_ == QualityAuto) {
        scale = qualityController_.update(gpuMs_, qualityBudgetMs_);
    }
    if (memoryPolicy_.budgetBytes > 0 && memoryPolicy_.lowerQuality) {
        scale = std::min(scale, gpudiag::layerScaleCap(pressure_.level()));
    }
    if (!out_.fbo) {
        return;
    }
    const bool wantScaled = scale >= 0.001f && scale < 0.999f;
    if (!wantScaled) {
        const bool fullReady = layerA_.fbo && layerB_.fbo && layerA_.width == out_.width && layerA_.height == out_.height;
        if (!fullReady && !(layerA_.create(out_.width, out_.height) && layerB_.create(out_.width, out_.height))) {
            layerA_.destroy();
            layerB_.destroy();
            return;
        }
        layerLoA_.destroy();
        layerLoB_.destroy();
        if (layerScale_ < 0.999f) {
            logLine("Layer resolution 100 %");
        }
        layerScale_ = 1.0f;
        return;
    }
    const int width = scaledDimension(out_.width, scale);
    const int height = scaledDimension(out_.height, scale);
    const bool loReady = layerLoA_.fbo && layerLoB_.fbo && layerLoA_.width == width && layerLoA_.height == height;
    if (!loReady && !(layerLoA_.create(width, height) && layerLoB_.create(width, height))) {
        layerLoA_.destroy();
        layerLoB_.destroy();
        return;
    }
    layerA_.destroy();
    layerB_.destroy();
    if (std::fabs(scale - layerScale_) > 0.001f) {
        logLine("Layer resolution " + std::to_string(static_cast<int>(scale * 100.0f + 0.5f)) + " %");
    }
    layerScale_ = scale;
}

void Engine::destroyFeedback() {
    for (RenderTarget& target : feedback_) {
        target.destroy();
    }
}

bool Engine::setResolutionIndex(int index) {
    if (index < 0 || index >= kResolutionCount) {
        return false;
    }
    if (index == currentResolution_ && out_.fbo) {
        comp.resolution = index;
        return true;
    }
    const Resolution& wanted = kResolutions[index];
    destroyFeedback();
    if (createTargets(wanted.width, wanted.height)) {
        currentResolution_ = index;
        comp.resolution = index;
        return true;
    }
    logLine(std::string("Resolution change to ") + wanted.label + " failed");
    if (currentResolution_ >= 0) {
        const Resolution& previous = kResolutions[currentResolution_];
        createTargets(previous.width, previous.height);
    }
    comp.resolution = currentResolution_;
    return false;
}

void Engine::applyComposition(const Composition& source) {
    int wanted = std::clamp(source.resolution, 0, kResolutionCount - 1);
    transition_ = Transition{};
    cycleSlot_ = -1;
    destroyFeedback();
    comp = source;
    normalizeDecks(comp);
    comp.bpm = std::clamp(comp.bpm, 40.0f, 240.0f);
    comp.resolution = currentResolution_ >= 0 ? currentResolution_ : wanted;
    setResolutionIndex(wanted);
    for (int l = 0; l < kLayerCount; ++l) {
        Layer& layer = comp.layers[static_cast<size_t>(l)];
        if (layer.activeColumn < 0 || layer.activeColumn >= kColumnCount || layer.cells[static_cast<size_t>(layer.activeColumn)] < 0) {
            layer.activeColumn = -1;
        }
        layerClock_[static_cast<size_t>(l)] = 0.0;
    }
    restartShaders(false);
}

void Engine::setAudio(const AudioFeatures& features) {
    audioInput_ = features;
}

float Engine::sourceValue(int source) const {
    if (source >= BandPlugin1 && source <= BandPlugin8) {
        return plugins_ != nullptr ? std::clamp(plugins_->modulation(source - BandPlugin1), 0.0f, 1.0f) : 0.0f;
    }
    return modulationSource(source, audio_, comp.modulators, beat_, comp.beatsPerBar, timecodeValid_ ? timecodeSeconds_ : -1.0);
}

void Engine::shiftBeat(double beats) {
    if (std::isfinite(beats)) {
        pushCompTempo();
        beatClock_.shift(beats);
        beat_ = beatClock_.beat();
    }
}

void Engine::setTimecode(bool valid, double seconds) {
    timecodeValid_ = valid && std::isfinite(seconds) && seconds >= 0.0;
    timecodeSeconds_ = timecodeValid_ ? seconds : 0.0;
}

void Engine::pushCompTempo() {
    if (!clockStarted_ || static_cast<double>(comp.bpm) != lastCompBpm_) {
        beatClock_.setTempo(static_cast<double>(comp.bpm));
        clockStarted_ = true;
        lastCompBpm_ = static_cast<double>(comp.bpm);
        lastClockBpm_ = beatClock_.bpm();
    }
}

void Engine::pullClockTempo() {
    if (beatClock_.bpm() != lastClockBpm_) {
        lastClockBpm_ = beatClock_.bpm();
        comp.bpm = static_cast<float>(std::clamp(lastClockBpm_, 40.0, 240.0));
        lastCompBpm_ = static_cast<double>(comp.bpm);
    }
}

void Engine::computeEffective() {
    audio_ = audioInput_.active ? audioInput_ : virtualAudio(beat_, time_);
    computeEffectiveFor(comp.layers, comp.master, effective_, effectiveLevel_, effectiveMasterGrade_);
    if (transition_.active && !transition_.frozen) {
        outMaster_.level = comp.master.level;
        computeEffectiveFor(outLayers_, outMaster_, outEffective_, outLevel_, outGrade_);
    }
}

void Engine::computeEffectiveFor(const std::array<Layer, kLayerCount>& layers, const Master& master, std::array<Effective, kLayerCount>& effective, float& level, ColorGrade& grade) const {
    for (int l = 0; l < kLayerCount; ++l) {
        const Layer& layer = layers[static_cast<size_t>(l)];
        Effective& eff = effective[static_cast<size_t>(l)];
        eff.opacity = layer.opacity;
        eff.scale = layer.scale;
        eff.speed = layer.speed;
        eff.grade = layer.grade;
        for (const AudioMod& mod : layer.mods) {
            const float v = sourceValue(mod.band) * mod.amount;
            if (v <= 0.0f) {
                continue;
            }
            switch (mod.target) {
            case TargetOpacity:
                eff.opacity += v;
                break;
            case TargetScale:
                eff.scale += v * 1.5f;
                break;
            case TargetSpeed:
                eff.speed += v * 3.0f;
                break;
            case TargetHue:
                eff.grade.hue += v * 0.5f;
                break;
            case TargetBrightness:
                eff.grade.brightness += v * 0.4f;
                break;
            case TargetSaturation:
                eff.grade.saturation += v;
                break;
            default:
                break;
            }
        }
        eff.opacity = std::clamp(eff.opacity, 0.0f, 1.0f);
        eff.scale = std::clamp(eff.scale, 0.25f, 4.0f);
        eff.speed = std::clamp(eff.speed, 0.1f, 6.0f);
        eff.grade.hue = std::clamp(eff.grade.hue, -0.5f, 0.5f);
        eff.grade.brightness = std::clamp(eff.grade.brightness, -0.5f, 0.5f);
        eff.grade.saturation = std::clamp(eff.grade.saturation, 0.0f, 2.0f);
    }
    level = master.level;
    grade = master.grade;
    for (const AudioMod& mod : master.mods) {
        const float v = sourceValue(mod.band) * mod.amount;
        if (v <= 0.0f) {
            continue;
        }
        switch (mod.target) {
        case TargetOpacity:
            level += v;
            break;
        case TargetHue:
            grade.hue += v * 0.5f;
            break;
        case TargetBrightness:
            grade.brightness += v * 0.4f;
            break;
        case TargetSaturation:
            grade.saturation += v;
            break;
        default:
            break;
        }
    }
    level = std::clamp(level, 0.0f, 1.0f);
    grade.hue = std::clamp(grade.hue, -0.5f, 0.5f);
    grade.brightness = std::clamp(grade.brightness, -0.5f, 0.5f);
    grade.saturation = std::clamp(grade.saturation, 0.0f, 2.0f);
}

void Engine::configureSpout(const std::array<std::string, kSpoutInputSlots>& names, const std::string& excluded, bool flip) {
    spoutNames_ = names;
    spoutExcluded_ = excluded;
    spoutFlip_ = flip;
}

const SpoutReceiveStatus& Engine::spoutStatus(int slot) const {
    static const SpoutReceiveStatus none;
    return slot >= 0 && slot < kSpoutInputSlots ? spoutSlots_[static_cast<size_t>(slot)].status() : none;
}

const std::vector<std::string>& Engine::spoutSenders() {
    if (clock_ - spoutListedAt_ >= 1.0) {
        spoutListedAt_ = clock_;
        spoutSenderList_ = spoutSlots_[0].senders();
    }
    return spoutSenderList_;
}

void Engine::prepareSpout(int slot) {
    const size_t index = static_cast<size_t>(slot);
    spoutLastUse_[index] = clock_;
    if (spoutFrame_[index] == frameId_) {
        return;
    }
    spoutFrame_[index] = frameId_;
    spoutSlots_[index].receive(spoutNames_[index], spoutExcluded_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0);
}

bool Engine::drawSpout(int slot, float scale) {
    const SpoutInput& input = spoutSlots_[static_cast<size_t>(slot)];
    if (!input.status().hasFrame || input.texture() == 0 || input.width() <= 0 || input.height() <= 0) {
        return false;
    }
    const float source = static_cast<float>(input.width()) / static_cast<float>(input.height());
    const float target = static_cast<float>(out_.width) / static_cast<float>(out_.height);
    const float zoom = std::max(scale, 0.05f);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(spoutProgram_.id);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, input.texture());
    glUniform1i(spoutProgram_.tex, 0);
    glUniform2f(spoutProgram_.fit, std::max(target / source, 1.0f) / zoom, std::max(source / target, 1.0f) / zoom);
    glUniform1f(spoutProgram_.flip, spoutFlip_ ? 1.0f : 0.0f);
    triangle_.draw();
    glBindTexture(GL_TEXTURE_2D, 0);
    return true;
}

void Engine::configureLive(const std::array<std::string, kLiveInputSlots>& ndi, const std::array<std::string, kLiveInputSlots>& camera, const std::string& excludedNdi, const CameraMode& mode) {
    liveNdi_ = ndi;
    liveCamera_ = camera;
    liveExcluded_ = excludedNdi;
    liveMode_ = mode;
}

LiveInput* Engine::liveSlot(LiveKind kind, int slot) {
    if (slot < 0 || slot >= kLiveInputSlots) {
        return nullptr;
    }
    std::unique_ptr<LiveInput>& input = liveInputs_[liveIndex(kind, slot)];
    if (!input) {
        input = std::make_unique<LiveInput>(videoGpu_, kind);
    }
    return input.get();
}

const LiveInput* Engine::liveInput(LiveKind kind, int slot) const {
    if (slot < 0 || slot >= kLiveInputSlots) {
        return nullptr;
    }
    return liveInputs_[liveIndex(kind, slot)].get();
}

LiveInputInfo Engine::liveInfo(LiveKind kind, int slot) const {
    const LiveInput* input = liveInput(kind, slot);
    return input != nullptr ? input->info() : LiveInputInfo{};
}

void Engine::resetLiveLatency() {
    for (std::unique_ptr<LiveInput>& input : liveInputs_) {
        if (input) {
            input->resetLatencyWindow();
        }
    }
}

namespace {

static_assert(kPluginFrameSlots == kLiveInputSlots, "plugin frame slots must match live input slots");

LiveKind liveKindOf(ClipKind kind) {
    if (kind == ClipKind::NdiSource) {
        return LiveKind::Ndi;
    }
    return kind == ClipKind::PluginSource ? LiveKind::Plugin : LiveKind::Camera;
}

}

void Engine::prepareLive(const ClipDef& def) {
    const LiveKind kind = liveKindOf(def.kind);
    const size_t index = liveIndex(kind, def.sceneIndex);
    LiveInput* input = liveSlot(kind, def.sceneIndex);
    if (input == nullptr) {
        return;
    }
    liveLastUse_[index] = clock_;
    if (liveFrame_[index] == frameId_) {
        return;
    }
    liveFrame_[index] = frameId_;
    LiveInputConfig config;
    config.maxDimension = videoOptions().maxDimension;
    if (kind == LiveKind::Ndi) {
        config.source = liveNdi_[static_cast<size_t>(def.sceneIndex)];
        config.excluded = liveExcluded_;
    } else if (kind == LiveKind::Plugin) {
        config.plugins = plugins_;
        if (plugins_ != nullptr && def.sceneIndex >= 0 && def.sceneIndex < kPluginFrameSlots) {
            config.source = plugins_->config().frames[static_cast<size_t>(def.sceneIndex)];
        }
    } else {
        config.source = liveCamera_[static_cast<size_t>(def.sceneIndex)];
        config.camera = liveMode_;
    }
    input->update(config, clock_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0);
}

bool Engine::drawLive(const ClipDef& def, float scale) {
    const LiveKind kind = liveKindOf(def.kind);
    const LiveInput* input = liveInput(kind, def.sceneIndex);
    if (input == nullptr || !input->hasPicture() || input->texture() == 0 || input->width() <= 0 || input->height() <= 0) {
        return false;
    }
    const float source = static_cast<float>(input->width()) / static_cast<float>(input->height());
    const float target = static_cast<float>(out_.width) / static_cast<float>(out_.height);
    const float zoom = std::max(scale, 0.05f);
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(mediaProgram_.id);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, input->texture());
    glUniform1i(mediaProgram_.tex, 0);
    glUniform2f(mediaProgram_.fit, std::max(target / source, 1.0f) / zoom, std::max(source / target, 1.0f) / zoom);
    glUniform1f(mediaProgram_.premult, input->premultiplied() ? 1.0f : 0.0f);
    glUniform1f(mediaProgram_.backdrop, 0.0f);
    glUniform2f(mediaProgram_.texScale, input->scaleX(), input->scaleY());
    glUniform1f(mediaProgram_.ycocg, 0.0f);
    triangle_.draw();
    glBindTexture(GL_TEXTURE_2D, 0);
    return true;
}

void Engine::serviceLive() {
    for (size_t i = 0; i < liveInputs_.size(); ++i) {
        if (liveInputs_[i] && (liveInputs_[i]->active() || liveInputs_[i]->hasPicture()) && clock_ - liveLastUse_[i] > kLiveIdleSeconds) {
            liveInputs_[i]->releaseAsync();
        }
    }
}

void Engine::serviceSpout() {
    constexpr double kReleaseAfterSeconds = 3.0;
    for (size_t i = 0; i < spoutSlots_.size(); ++i) {
        if (spoutSlots_[i].active() && clock_ - spoutLastUse_[i] > kReleaseAfterSeconds) {
            spoutSlots_[i].release();
        }
    }
}

void Engine::update(double dt) {
    clock_ += std::max(dt, 0.0);
    const double step = paused ? 0.0 : dt;
    time_ += step;
    pushCompTempo();
    beatClock_.advance(dt, !paused);
    beat_ = beatClock_.beat();
    pullClockTempo();
    if (quantizer_.pending() > 0) {
        std::vector<LaunchRequest> due;
        quantizer_.update(quantizeClock(), clock_, due);
        for (const LaunchRequest& request : due) {
            runLaunch(request);
        }
    }
    if (transition_.active) {
        transition_.elapsed += static_cast<float>(dt);
        if (transition_.elapsed >= transition_.duration) {
            endTransition();
        }
    }
    if (comp.deckSettings.autoCycle && usedDeckCount(comp) > 1) {
        const long long slot = static_cast<long long>(std::floor(beat_ / static_cast<double>(comp.deckSettings.cycleBeats)));
        if (cycleSlot_ < 0) {
            cycleSlot_ = slot;
        } else if (slot != cycleSlot_ && !transition_.active) {
            cycleSlot_ = slot;
            switchDeck(nextUsedDeck(comp, comp.activeDeck, 1));
        } else if (slot != cycleSlot_) {
            cycleSlot_ = slot;
        }
    } else {
        cycleSlot_ = -1;
    }
    computeEffective();
    for (int l = 0; l < kLayerCount; ++l) {
        const size_t i = static_cast<size_t>(l);
        layerClock_[i] += step * static_cast<double>(effective_[i].speed);
        if (transition_.active && !transition_.frozen) {
            outClocks_[i] += step * static_cast<double>(outEffective_[i].speed);
        }
    }
}

float Engine::transitionProgress() const {
    if (!transition_.active || transition_.duration <= 0.0f) {
        return 0.0f;
    }
    return std::clamp(transition_.elapsed / transition_.duration, 0.0f, 1.0f);
}

void Engine::endTransition() {
    transition_.active = false;
    transition_.frozen = false;
    transition_.elapsed = 0.0f;
}

bool Engine::switchDeck(int index) {
    if (index < 0 || index >= kDeckCount || index == comp.activeDeck) {
        return false;
    }
    const float duration = transitionSeconds(comp.deckSettings, comp.bpm);
    const int previousDeck = comp.activeDeck;
    const bool smooth = duration >= 0.05f && out_.fbo && ensureTransitionTargets();
    const bool carryShaders = smooth && !transition_.active;
    if (smooth) {
        if (transition_.active) {
            blit(out_, deckTex_[0]);
            transition_.frozen = true;
        } else {
            outLayers_ = comp.layers;
            outMaster_ = comp.master;
            outClocks_ = layerClock_;
            transition_.frozen = false;
        }
    }
    feedbackSet_ ^= 1;
    for (int c = 0; c < kChainsPerSet * kEffectSlots; ++c) {
        feedback_[static_cast<size_t>(feedbackSet_ * kChainsPerSet * kEffectSlots + c)].destroy();
    }
    selectDeck(comp, index);
    layerClock_.fill(0.0);
    restartShaders(carryShaders);
    if (smooth) {
        transition_.active = true;
        transition_.elapsed = 0.0f;
        transition_.duration = duration;
        transition_.type = comp.deckSettings.transition;
        transition_.target = index;
        transition_.from = previousDeck;
        audio_ = audioInput_.active ? audioInput_ : virtualAudio(beat_, time_);
        if (!transition_.frozen) {
            outMaster_.level = comp.master.level;
            computeEffectiveFor(outLayers_, outMaster_, outEffective_, outLevel_, outGrade_);
        }
    } else {
        endTransition();
    }
    computeEffective();
    return true;
}

void Engine::duplicateActiveDeckTo(int index) {
    if (index < 0 || index >= kDeckCount || index == comp.activeDeck) {
        return;
    }
    const size_t to = static_cast<size_t>(index);
    comp.decks[to] = captureDeck(comp);
    comp.deckUsed[to] = true;
    comp.deckNames[to] = comp.deckNames[static_cast<size_t>(comp.activeDeck)];
}

void Engine::clearDeck(int index) {
    if (index < 0 || index >= kDeckCount) {
        return;
    }
    if (index == comp.activeDeck) {
        restoreDeck(comp, emptyDeck());
        layerClock_.fill(0.0);
        restartShaders(false);
        transition_ = Transition{};
        for (int c = 0; c < kChainsPerSet * kEffectSlots; ++c) {
            feedback_[static_cast<size_t>(feedbackSet_ * kChainsPerSet * kEffectSlots + c)].destroy();
        }
        return;
    }
    comp.decks[static_cast<size_t>(index)] = emptyDeck();
    comp.deckUsed[static_cast<size_t>(index)] = true;
}

void Engine::removeDeck(int index) {
    if (index < 0 || index >= kDeckCount || usedDeckCount(comp) <= 1) {
        return;
    }
    if (index == comp.activeDeck) {
        const int next = nextUsedDeck(comp, index, 1);
        if (next == index) {
            return;
        }
        switchDeck(next);
    }
    const size_t slot = static_cast<size_t>(index);
    comp.deckUsed[slot] = false;
    comp.deckNames[slot].clear();
    comp.decks[slot] = emptyDeck();
}

void Engine::loadLook(const Composition& look) {
    const DeckData deck = captureDeck(look);
    restoreDeck(comp, deck);
    for (int l = 0; l < kLayerCount; ++l) {
        Layer& layer = comp.layers[static_cast<size_t>(l)];
        if (layer.activeColumn < 0 || layer.activeColumn >= kColumnCount || layer.cells[static_cast<size_t>(layer.activeColumn)] < 0) {
            layer.activeColumn = -1;
        }
    }
    layerClock_.fill(0.0);
    restartShaders(false);
    for (int c = 0; c < kChainsPerSet * kEffectSlots; ++c) {
        feedback_[static_cast<size_t>(feedbackSet_ * kChainsPerSet * kEffectSlots + c)].destroy();
    }
    if (look.bpm > 0.0f && look.bpm != comp.bpm) {
        comp.bpm = std::clamp(look.bpm, 40.0f, 240.0f);
    }
    computeEffective();
}

void Engine::nudgeBeat(float amount) {
    pushCompTempo();
    beatClock_.nudge(static_cast<double>(amount));
    beat_ = beatClock_.beat();
}

void Engine::uploadAudio() {
    std::array<float, kSpectrumBins * 2> data{};
    for (int i = 0; i < kSpectrumBins; ++i) {
        data[static_cast<size_t>(i)] = audio_.spectrum[static_cast<size_t>(i)];
        data[static_cast<size_t>(kSpectrumBins + i)] = audio_.wave[static_cast<size_t>(i)];
    }
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, audioTexture_);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, kSpectrumBins, 2, GL_RED, GL_FLOAT, data.data());
    glActiveTexture(GL_TEXTURE0);
}

void Engine::setAudioUniforms(GLint audio, GLint kick, GLint audioTex) {
    glUniform4f(audio, audio_.bass, audio_.mid, audio_.high, audio_.level);
    glUniform1f(kick, audio_.kick);
    glUniform1i(audioTex, 2);
}

void Engine::trigger(int layer, int column) {
    if (layer < 0 || layer >= kLayerCount || column < 0 || column >= kColumnCount) {
        return;
    }
    Layer& l = comp.layers[static_cast<size_t>(layer)];
    if (l.cells[static_cast<size_t>(column)] < 0) {
        l.activeColumn = -1;
        return;
    }
    l.activeColumn = column;
    layerClock_[static_cast<size_t>(layer)] = 0.0;
    restartShaderLayer(layer);
}

void Engine::triggerColumn(int column) {
    if (column < 0 || column >= kColumnCount) {
        return;
    }
    for (int l = 0; l < kLayerCount; ++l) {
        if (comp.layers[static_cast<size_t>(l)].cells[static_cast<size_t>(column)] >= 0) {
            trigger(l, column);
        }
    }
}

QuantizeClock Engine::quantizeClock() const {
    const LockState state = beatClock_.state();
    QuantizeClock clock;
    clock.beat = beat_;
    clock.bpm = beatClock_.bpm();
    clock.beatsPerBar = comp.beatsPerBar;
    clock.usable = !paused && (state == LockState::Locked || state == LockState::Free);
    return clock;
}

void Engine::setQuantizeGrid(QuantizeGrid grid) {
    quantizer_.setGrid(grid);
}

void Engine::runLaunch(const LaunchRequest& request) {
    switch (request.kind) {
    case LaunchKind::Clip:
        trigger(request.a, request.b);
        break;
    case LaunchKind::Column:
        triggerColumn(request.a);
        break;
    case LaunchKind::Deck:
        switchDeck(request.a);
        break;
    }
}

void Engine::requestTrigger(int layer, int column) {
    std::vector<LaunchRequest> now;
    quantizer_.request({LaunchKind::Clip, layer, column}, quantizeClock(), clock_, now);
    for (const LaunchRequest& request : now) {
        runLaunch(request);
    }
}

void Engine::requestTriggerColumn(int column) {
    std::vector<LaunchRequest> now;
    quantizer_.request({LaunchKind::Column, column, 0}, quantizeClock(), clock_, now);
    for (const LaunchRequest& request : now) {
        runLaunch(request);
    }
}

bool Engine::requestSwitchDeck(int index) {
    if (index < 0 || index >= kDeckCount || index == comp.activeDeck) {
        return false;
    }
    std::vector<LaunchRequest> now;
    const bool queued = quantizer_.request({LaunchKind::Deck, index, 0}, quantizeClock(), clock_, now);
    if (queued) {
        return true;
    }
    bool switched = false;
    for (const LaunchRequest& request : now) {
        runLaunch(request);
        switched = true;
    }
    return switched;
}

void Engine::clearLayer(int layer) {
    if (layer < 0 || layer >= kLayerCount) {
        return;
    }
    comp.layers[static_cast<size_t>(layer)].activeColumn = -1;
}

void Engine::tap() {
    pushCompTempo();
    beatClock_.tap();
    beat_ = beatClock_.beat();
    pullClockTempo();
}

void Engine::resync() {
    pushCompTempo();
    beatClock_.resync();
    beat_ = beatClock_.beat();
}

GLuint Engine::clipThumbnail(int clip) const {
    if (clip < 0 || clip >= static_cast<int>(clipThumbs_.size())) {
        return 0;
    }
    return clipThumbs_[static_cast<size_t>(clip)].texture;
}

GLuint Engine::layerPreview(int layer) const {
    if (layer < 0 || layer >= kLayerCount) {
        return 0;
    }
    return previews_[static_cast<size_t>(layer)].texture;
}

bool Engine::layerLive(int layer) const {
    if (layer < 0 || layer >= kLayerCount) {
        return false;
    }
    return live_[static_cast<size_t>(layer)];
}

bool Engine::readOutput(std::vector<unsigned char>& rgba, int& width, int& height) {
    if (!out_.fbo) {
        return false;
    }
    width = out_.width;
    height = out_.height;
    rgba.resize(static_cast<size_t>(width) * static_cast<size_t>(height) * 4u);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, out_.fbo);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return true;
}

void Engine::setDmxStrip(bool enabled, bool atTop, const std::uint8_t* grid) {
    stripEnabled_ = enabled;
    stripAtTop_ = atTop;
    if (enabled && grid) {
        strip_.upload(grid);
    }
}

void Engine::drawClip(const ClipProgram& clip, float time, float beat, float bpm, float width, float height, const std::array<float, 3>& colorA, const std::array<float, 3>& colorB, bool rainbow, float scale) {
    glUseProgram(clip.id);
    glUniform1f(clip.time, time);
    glUniform1f(clip.beat, beat);
    glUniform1f(clip.bpm, bpm);
    glUniform2f(clip.res, width, height);
    glUniform3f(clip.colorA, colorA[0], colorA[1], colorA[2]);
    glUniform3f(clip.colorB, colorB[0], colorB[1], colorB[2]);
    glUniform1f(clip.rainbow, rainbow ? 1.0f : 0.0f);
    glUniform1f(clip.scale, scale);
    setAudioUniforms(clip.audio, clip.kick, clip.audioTex);
    triangle_.draw();
}

int Engine::resolveMediaClip(const std::string& name) const {
    const int direct = clipIndexByName(name);
    const int candidates[3] = {direct, clipIndexByName(imageClipName(name)), clipIndexByName(videoClipName(name))};
    for (const int index : candidates) {
        if (index >= 0 && isMediaClip(index)) {
            return index;
        }
    }
    return -1;
}

void Engine::clearShaderStates(int clipIndex) {
    for (auto it = shaderStates_.begin(); it != shaderStates_.end();) {
        if (it->second.clipIndex == clipIndex) {
            it->second.destroy();
            it = shaderStates_.erase(it);
        } else {
            ++it;
        }
    }
}

void Engine::restartShaderLayer(int layer) {
    const auto it = shaderStates_.find(layer);
    if (it != shaderStates_.end()) {
        it->second.restart = true;
    }
}

void Engine::restartShaders(bool carryOver) {
    for (int l = 0; l < kLayerCount; ++l) {
        auto current = shaderStates_.extract(l);
        auto previous = shaderStates_.extract(kLayerCount + l);
        if (previous) {
            previous.mapped().destroy();
        }
        if (current) {
            if (carryOver) {
                current.key() = kLayerCount + l;
                shaderStates_.insert(std::move(current));
            } else {
                current.mapped().restart = true;
                shaderStates_.insert(std::move(current));
            }
        }
    }
}

void Engine::releaseIdleShaderStates() {
    for (auto it = shaderStates_.begin(); it != shaderStates_.end();) {
        if (frameId_ > it->second.lastDrawn && frameId_ - it->second.lastDrawn > kShaderIdleReleaseFrames) {
            it->second.destroy();
            it = shaderStates_.erase(it);
        } else {
            ++it;
        }
    }
}

bool Engine::allocateShaderState(ShaderPassState& state, size_t clipIndex, const ShaderPlan& plan, int width, int height, std::string& error, std::string& note) {
    state.destroy();
    note.clear();
    const unsigned long long used = shaderBufferBytes();
    unsigned long long remaining = used < shaderBudget_ ? shaderBudget_ - used : 0;
    state.clipIndex = static_cast<int>(clipIndex);
    state.width = width;
    state.height = height;
    while (glGetError() != GL_NO_ERROR) {
    }
    const auto scaled = [](int size, float factor) { return std::max(1, static_cast<int>(std::lround(static_cast<float>(size) * factor))); };
    for (const ShaderPassSource& pass : plan.passes) {
        if (pass.image) {
            continue;
        }
        const size_t b = static_cast<size_t>(pass.buffer);
        const float fit = fitShaderScale(pass.scale, width, height, 8, 2, remaining);
        if (fit <= 0.0f) {
            error = "There is not enough video memory budget for the shader buffers";
            state.destroy();
            return false;
        }
        const int w = scaled(width, fit);
        const int h = scaled(height, fit);
        if (!state.buffers[b][0].createFloat(w, h) || !state.buffers[b][1].createFloat(w, h)) {
            error = "The graphics card could not create the shader buffers";
            state.destroy();
            return false;
        }
        remaining -= std::min<unsigned long long>(remaining, static_cast<unsigned long long>(state.buffers[b][0].bytes() + state.buffers[b][1].bytes()));
        state.scale[b] = fit;
        state.used[b] = true;
        if (fit + 0.001f < pass.scale) {
            note = "A shader buffer runs at a lower resolution because of the video memory budget";
        }
    }
    const std::vector<std::string> names = plan.mediaNames();
    for (size_t m = 0; m < names.size() && m < state.media.size(); ++m) {
        const float fit = fitShaderScale(1.0f, width, height, 4, 1, remaining);
        if (fit <= 0.0f) {
            error = "There is not enough video memory budget for the shader buffers";
            state.destroy();
            return false;
        }
        if (!state.media[m].create(scaled(width, fit), scaled(height, fit))) {
            error = "The graphics card could not create the shader buffers";
            state.destroy();
            return false;
        }
        remaining -= std::min<unsigned long long>(remaining, static_cast<unsigned long long>(state.media[m].bytes()));
    }
    if (glGetError() == GL_OUT_OF_MEMORY) {
        error = "The graphics card ran out of video memory for the shader buffers";
        state.destroy();
        return false;
    }
    state.allocated = true;
    state.clear();
    return true;
}

void Engine::drawShaderInstance(size_t clipIndex, int instance, const std::array<float, 3>& colorA, const std::array<float, 3>& colorB, bool rainbow, float scale, double clock, const RenderTarget& target, const ShaderValues& stored) {
    ClipProgram& clip = clips_[clipIndex];
    const ClipDef& def = clipLibrary()[clipIndex];
    ShaderPassState& state = shaderStates_[instance];
    if (!state.matches(clipIndex, target.width, target.height)) {
        std::string error;
        std::string note;
        if (!allocateShaderState(state, clipIndex, *def.plan, target.width, target.height, error, note)) {
            if (clip.runtimeError != error) {
                logLine("Shader '" + def.name + "': " + error);
            }
            clip.runtimeError = error;
            state.destroy();
            target.bind();
            glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
            glClear(GL_COLOR_BUFFER_BIT);
            return;
        }
        clip.note = note;
        if (!note.empty()) {
            logLine("Shader '" + def.name + "': " + note);
        }
        state.lastDrawn = frameId_;
        state.lastClock = clock;
    } else if (state.restart || frameId_ - state.lastDrawn > kShaderRestartGapFrames || clock < state.lastClock - kShaderRestartBackSeconds) {
        state.clear();
    }
    clip.runtimeError.clear();
    runShaderPasses(*def.plan, clip.passes, state, colorA, colorB, rainbow, scale, clock, static_cast<float>(beat_), comp.bpm, target, false, resolveShaderValues(*def.plan, stored), nullptr, 1.0f);
    state.lastDrawn = frameId_;
    state.lastClock = clock;
}

void Engine::renderShaderThumbnail(size_t clipIndex, const RenderTarget& target) {
    const ClipDef& def = clipLibrary()[clipIndex];
    ClipProgram& clip = clips_[clipIndex];
    ShaderPassState state;
    std::string error;
    std::string note;
    if (!allocateShaderState(state, clipIndex, *def.plan, target.width, target.height, error, note)) {
        return;
    }
    const Layer defaults;
    constexpr int kWarmFrames = 8;
    for (int k = 0; k < kWarmFrames; ++k) {
        const double clock = 2.6 - static_cast<double>(kWarmFrames - 1 - k) / 30.0;
        runShaderPasses(*def.plan, clip.passes, state, defaults.colorA, defaults.colorB, false, 1.0f, clock, 2.3f, 128.0f, target, true, resolveShaderValues(*def.plan, unsetShaderValues()), nullptr, 1.0f);
    }
    state.destroy();
}

void Engine::runShaderPasses(const ShaderPlan& plan, const std::vector<PassProgram>& programs, ShaderPassState& state, const std::array<float, 3>& colorA, const std::array<float, 3>& colorB, bool rainbow, float scale, double clock, float beat, float bpm, const RenderTarget& target, bool thumbnail, const ShaderValues& values, const RenderTarget* picture, float mixAmount) {
    const std::vector<std::string> mediaNames = plan.mediaNames();
    glDisable(GL_BLEND);
    for (size_t m = 0; m < mediaNames.size() && m < state.media.size(); ++m) {
        if (!state.media[m].fbo) {
            continue;
        }
        if (!thumbnail && (state.mediaClip[m] < 0 || !isMediaClip(state.mediaClip[m])) && (state.frame % 60 == 0)) {
            state.mediaClip[m] = resolveMediaClip(mediaNames[m]);
        }
        state.media[m].bind();
        bool drawn = false;
        if (!thumbnail && state.mediaClip[m] >= 0 && isMediaClip(state.mediaClip[m])) {
            const size_t mediaIndex = static_cast<size_t>(state.mediaClip[m]);
            if (ensureMedia(mediaIndex)) {
                media_[mediaIndex]->touch();
                drawn = drawMedia(*media_[mediaIndex], 1.0f, false, clock, nullptr);
            }
        }
        if (!drawn) {
            glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
            glClear(GL_COLOR_BUFFER_BIT);
        }
    }

    const float delta = state.frame == 0 ? 0.0f : static_cast<float>(std::clamp(std::abs(clock - state.lastClock), 0.0, 0.25));
    for (size_t p = 0; p < plan.passes.size() && p < programs.size(); ++p) {
        const ShaderPassSource& source = plan.passes[p];
        const PassProgram& program = programs[p];
        const RenderTarget* destination = &target;
        if (!source.image) {
            const size_t b = static_cast<size_t>(source.buffer);
            destination = &state.buffers[b][static_cast<size_t>(1 - state.latest[b])];
        }
        destination->bind();
        glUseProgram(program.id);
        glUniform1f(program.time, static_cast<float>(clock));
        glUniform1f(program.beat, beat);
        glUniform1f(program.bpm, bpm);
        glUniform2f(program.res, static_cast<float>(destination->width), static_cast<float>(destination->height));
        glUniform3f(program.colorA, colorA[0], colorA[1], colorA[2]);
        glUniform3f(program.colorB, colorB[0], colorB[1], colorB[2]);
        glUniform1f(program.rainbow, rainbow ? 1.0f : 0.0f);
        glUniform1f(program.scale, source.image ? scale : 1.0f);
        glUniform1i(program.frame, static_cast<GLint>(std::min<unsigned long long>(state.frame, 2147483647ull)));
        glUniform1f(program.delta, delta);
        for (size_t k = 0; k < plan.params.size() && k < program.param.size(); ++k) {
            const ShaderParam& param = plan.params[k];
            const GLint location = program.param[k];
            if (location < 0) {
                continue;
            }
            const size_t slot = static_cast<size_t>(param.slot);
            switch (param.kind) {
            case ShaderParamKind::Float:
                glUniform1f(location, values[slot]);
                break;
            case ShaderParamKind::Int:
            case ShaderParamKind::Bool:
                glUniform1i(location, static_cast<GLint>(std::lround(values[slot])));
                break;
            case ShaderParamKind::List: {
                const size_t index = static_cast<size_t>(std::clamp(static_cast<int>(std::lround(values[slot])), 0, std::max(0, static_cast<int>(param.values.size()) - 1)));
                glUniform1i(location, param.values.empty() ? 0 : static_cast<GLint>(std::lround(param.values[index])));
                break;
            }
            case ShaderParamKind::Point:
                glUniform2f(location, values[slot], values[slot + 1]);
                break;
            case ShaderParamKind::Color:
                glUniform4f(location, values[slot], values[slot + 1], values[slot + 2], values[slot + 3]);
                break;
            }
        }
        if (plan.filter && picture) {
            glUniform1f(program.mix, mixAmount);
            glActiveTexture(GL_TEXTURE0 + 7);
            glBindTexture(GL_TEXTURE_2D, picture->texture);
            glUniform1i(program.picture, 7);
        }
        setAudioUniforms(program.audio, program.kick, program.audioTex);
        for (int c = 0; c < kShaderChannels; ++c) {
            const ShaderInput& input = source.inputs[static_cast<size_t>(c)];
            GLuint texture = blackTexture_;
            float width = 1.0f;
            float height = 1.0f;
            if (input.kind == ShaderInputKind::Audio) {
                texture = audioTexture_;
                width = static_cast<float>(kSpectrumBins);
                height = 2.0f;
            } else if (input.kind == ShaderInputKind::Picture) {
                if (picture && picture->texture) {
                    texture = picture->texture;
                    width = static_cast<float>(picture->width);
                    height = static_cast<float>(picture->height);
                }
            } else if (input.kind == ShaderInputKind::Buffer) {
                const size_t b = static_cast<size_t>(input.buffer);
                const RenderTarget& buffer = state.buffers[b][static_cast<size_t>(state.latest[b])];
                if (buffer.texture) {
                    texture = buffer.texture;
                    width = static_cast<float>(buffer.width);
                    height = static_cast<float>(buffer.height);
                }
            } else if (input.kind == ShaderInputKind::Media) {
                for (size_t m = 0; m < mediaNames.size() && m < state.media.size(); ++m) {
                    if (mediaNames[m] == input.media && state.media[m].texture) {
                        texture = state.media[m].texture;
                        width = static_cast<float>(state.media[m].width);
                        height = static_cast<float>(state.media[m].height);
                    }
                }
            }
            const GLuint unit = static_cast<GLuint>(3 + c);
            glActiveTexture(GL_TEXTURE0 + unit);
            glBindTexture(GL_TEXTURE_2D, texture);
            glBindSampler(unit, channelSamplers_[(input.nearest ? 1u : 0u) + (input.repeat ? 2u : 0u)]);
            glUniform1i(program.channel[static_cast<size_t>(c)], static_cast<GLint>(unit));
            glUniform3f(program.channelRes[static_cast<size_t>(c)], width, height, 1.0f);
            glUniform1f(program.channelTime[static_cast<size_t>(c)], static_cast<float>(clock));
        }
        glActiveTexture(GL_TEXTURE0);
        triangle_.draw();
        if (!source.image) {
            const size_t b = static_cast<size_t>(source.buffer);
            state.latest[b] = 1 - state.latest[b];
        }
    }
    for (int c = 0; c < kShaderChannels; ++c) {
        const GLuint unit = static_cast<GLuint>(3 + c);
        glActiveTexture(GL_TEXTURE0 + unit);
        glBindSampler(unit, 0);
        glBindTexture(GL_TEXTURE_2D, 0);
    }
    if (plan.filter) {
        glActiveTexture(GL_TEXTURE0 + 7);
        glBindTexture(GL_TEXTURE_2D, 0);
    }
    glActiveTexture(GL_TEXTURE0);
    ++state.frame;
}

void Engine::renderClipThumbnails(int maxCount) {
    if ((nextThumb_ >= clips_.size() && reloadThumbs_.empty()) || maxCount <= 0) {
        return;
    }
    const Layer defaults;
    const AudioFeatures saved = audio_;
    audio_ = virtualAudio(2.3, 2.6);
    uploadAudio();
    glDisable(GL_BLEND);
    int done = 0;
    while (!reloadThumbs_.empty() && done < maxCount) {
        const size_t i = reloadThumbs_.back();
        reloadThumbs_.pop_back();
        ++done;
        if (i < clips_.size() && i < clipThumbs_.size() && clipThumbs_[i].fbo) {
            clipThumbs_[i].bind();
            glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
            if (ensureClip(i)) {
                if (clipLibrary()[i].isAdvancedShader()) {
                    renderShaderThumbnail(i, clipThumbs_[i]);
                } else {
                    drawClip(clips_[i], 2.6f, 2.3f, 128.0f, static_cast<float>(kThumbWidth), static_cast<float>(kThumbHeight), defaults.colorA, defaults.colorB, false, 1.0f);
                }
            }
        }
    }
    while (nextThumb_ < clips_.size() && done < maxCount) {
        const size_t i = nextThumb_++;
        ++done;
        if (i < media_.size() && media_[i]) {
            deferredMediaThumbs_.push_back(i);
            continue;
        }
        if (i < generators_.size() && generators_[i]) {
            deferredGeneratorThumbs_.push_back(i);
            continue;
        }
        if (i < scenes_.size() && scenes_[i]) {
            if ((clipLibrary()[i].kind == ClipKind::BuiltinScene || clipLibrary()[i].kind == ClipKind::ScenePack) && !ensureScene(i) && scenes_[i]->state() == SceneState::Loading) {
                deferredThumbs_.push_back(i);
            }
            continue;
        }
        if (!ensureClip(i) || !clipThumbs_[i].fbo) {
            continue;
        }
        clipThumbs_[i].bind();
        if (clipLibrary()[i].isAdvancedShader()) {
            renderShaderThumbnail(i, clipThumbs_[i]);
        } else {
            drawClip(clips_[i], 2.6f, 2.3f, 128.0f, static_cast<float>(kThumbWidth), static_cast<float>(kThumbHeight), defaults.colorA, defaults.colorB, false, 1.0f);
        }
    }
    glUseProgram(0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    audio_ = saved;
    uploadAudio();
}

void Engine::downsample(const RenderTarget& source, const RenderTarget& target) {
    target.bind();
    glDisable(GL_BLEND);
    glUseProgram(thumb_.id);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, source.texture);
    glUniform1i(thumb_.tex, 0);
    glUniform2f(thumb_.texel, 1.0f / static_cast<float>(source.width), 1.0f / static_cast<float>(source.height));
    triangle_.draw();
}

void Engine::blit(const RenderTarget& source, const RenderTarget& target) {
    glBindFramebuffer(GL_READ_FRAMEBUFFER, source.fbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, target.fbo);
    glBlitFramebuffer(0, 0, source.width, source.height, 0, 0, target.width, target.height, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Engine::drawLayerSource(int clipIndex, int instance, const Layer& layer, const ClipPlayback& playback, float scale, double clock, const RenderTarget& target) {
    const ClipDef& clipDef = clipLibrary()[static_cast<size_t>(clipIndex)];
    const bool spoutClip = clipDef.kind == ClipKind::SpoutSource;
    const bool liveClip = clipDef.isLive();
    if (spoutClip) {
        prepareSpout(clipDef.sceneIndex);
    } else if (liveClip) {
        prepareLive(clipDef);
    }
    target.bind();
    glDisable(GL_BLEND);
    if (scenes_[static_cast<size_t>(clipIndex)]) {
        SceneFrame frame;
        frame.time = static_cast<float>(clock);
        frame.beat = beat_;
        frame.scale = scale;
        frame.bass = audio_.bass;
        frame.mid = audio_.mid;
        frame.high = audio_.high;
        frame.level = audio_.level;
        frame.kick = audio_.kick;
        frame.colorA = layer.colorA;
        frame.colorB = layer.colorB;
        frame.rainbow = layer.rainbow;
        sceneRenderer_.draw(*scenes_[static_cast<size_t>(clipIndex)], frame, target);
    } else if (media_[static_cast<size_t>(clipIndex)]) {
        drawMedia(*media_[static_cast<size_t>(clipIndex)], scale, false, clock, &playback);
    } else if (static_cast<size_t>(clipIndex) < generators_.size() && generators_[static_cast<size_t>(clipIndex)]) {
        drawGenerator(static_cast<size_t>(clipIndex), instance, layer, scale, clock, target);
    } else if (liveClip && drawLive(clipDef, scale)) {
    } else if (clipDef.isAdvancedShader()) {
        drawShaderInstance(static_cast<size_t>(clipIndex), instance, layer.colorA, layer.colorB, layer.rainbow, scale, clock, target, playback.values);
    } else if (liveClip || !spoutClip || !drawSpout(clipDef.sceneIndex, scale)) {
        drawClip(clips_[static_cast<size_t>(clipIndex)], static_cast<float>(clock), static_cast<float>(beat_), comp.bpm, static_cast<float>(target.width), static_cast<float>(target.height), layer.colorA, layer.colorB, layer.rainbow, scale);
    }
}

void Engine::requestPreview(int layer, int column) {
    previewLayer_ = std::clamp(layer, 0, kLayerCount - 1);
    previewColumn_ = std::clamp(column, 0, kColumnCount - 1);
    previewRequested_ = true;
}

void Engine::renderPreview() {
    previewResult_ = nullptr;
    if (!previewRequested_) {
        if (previewA_.fbo) {
            previewA_.destroy();
            previewB_.destroy();
            for (RenderTarget& target : previewFeedback_) {
                target.destroy();
            }
            previewClip_ = -1;
        }
        return;
    }
    previewRequested_ = false;
    const size_t layerIndex = static_cast<size_t>(previewLayer_);
    const Layer& layer = comp.layers[layerIndex];
    if (layer.activeColumn == previewColumn_ && live_[layerIndex]) {
        return;
    }
    const int clipIndex = layer.cells[static_cast<size_t>(previewColumn_)];
    if (clipIndex < 0 || clipIndex >= static_cast<int>(clips_.size()) || !ensureClip(static_cast<size_t>(clipIndex))) {
        return;
    }
    if (!previewA_.fbo && !(previewA_.create(kPreviewBusWidth, kPreviewBusHeight) && previewB_.create(kPreviewBusWidth, kPreviewBusHeight))) {
        previewA_.destroy();
        previewB_.destroy();
        return;
    }
    if (clipIndex != previewClip_) {
        previewClip_ = clipIndex;
        previewClock_ = 0.0;
        previewLastTime_ = time_;
    }
    const double delta = std::clamp(time_ - previewLastTime_, 0.0, 0.25);
    previewLastTime_ = time_;
    previewClock_ += delta * static_cast<double>(effective_[layerIndex].speed);

    previewFeedbackUsed_.fill(false);
    if (profiler_.enabled()) {
        profiler_.mark(PassKind::Preview, 0, -1, 0, clipLibrary()[static_cast<size_t>(clipIndex)].name);
    }
    drawLayerSource(clipIndex, kLayerCount * 2, layer, layer.playback[static_cast<size_t>(previewColumn_)], effective_[layerIndex].scale, previewClock_, previewA_);
    previewResult_ = runChain(layer.fx, previewFeedback_.data(), previewFeedbackUsed_.data(), &previewA_, &previewB_, 0, -1);
    for (size_t k = 0; k < previewFeedback_.size(); ++k) {
        if (previewFeedback_[k].fbo && !previewFeedbackUsed_[k]) {
            previewFeedback_[k].destroy();
        }
    }
}

RenderTarget* Engine::runChain(const std::array<EffectSlot, kEffectSlots>& slots, RenderTarget* feedback, bool* feedbackUsed, RenderTarget* source, RenderTarget* scratch, int deck, int owner) {
    RenderTarget* in = source;
    RenderTarget* out = scratch;
    const float width = static_cast<float>(source->width);
    const float height = static_cast<float>(source->height);

    for (int s = 0; s < kEffectSlots; ++s) {
        const EffectSlot& slot = slots[static_cast<size_t>(s)];
        if (!slot.enabled || slot.effect < 0 || slot.effect >= static_cast<int>(effects_.size())) {
            continue;
        }
        if (!ensureEffect(static_cast<size_t>(slot.effect))) {
            continue;
        }
        const EffectProgram& effect = effects_[static_cast<size_t>(slot.effect)];
        if (profiler_.enabled()) {
            profiler_.mark(PassKind::Effect, deck, owner, s + 1, effectLibrary()[static_cast<size_t>(slot.effect)].name);
        }
        if (effect.usesPrevious && feedback[s].fbo && (feedback[s].width != source->width || feedback[s].height != source->height)) {
            feedback[s].destroy();
        }

        std::array<float, kParamCount> params = slot.params;
        float mixAmount = 1.0f;
        if (slot.reactBand != BandOff) {
            const float level = sourceValue(slot.reactBand);
            if (slot.reactMode == ReactGate) {
                mixAmount = std::clamp(std::pow(std::max(level, 0.0f), 0.8f) * (0.4f + slot.reactAmount * 2.6f), 0.0f, 1.0f);
            } else {
                const size_t target = static_cast<size_t>(std::clamp(slot.reactParam, 0, kParamCount - 1));
                params[target] = std::clamp(params[target] + level * slot.reactAmount, 0.0f, 1.0f);
            }
        }
        if (mixAmount <= 0.002f) {
            if (effect.usesPrevious && feedback[s].fbo) {
                blit(*in, feedback[s]);
                feedbackUsed[s] = true;
            }
            continue;
        }

        if (!effect.passes.empty()) {
            const RenderTarget* slotAddress = &feedback[s];
            const RenderTarget* base = feedback_.data();
            const bool inMain = !std::less<const RenderTarget*>{}(slotAddress, base) && std::less<const RenderTarget*>{}(slotAddress, base + feedback_.size());
            const int key = kEffectKeyBase + (inMain ? static_cast<int>(slotAddress - base) : static_cast<int>(feedback_.size()) + s);
            if (runPlanEffect(static_cast<size_t>(slot.effect), key, slot, params, mixAmount, in, out) == out) {
                std::swap(in, out);
            }
            continue;
        }

        if (effect.usesPrevious && !feedback[s].fbo) {
            if (feedback[s].create(source->width, source->height)) {
                clearTarget(feedback[s]);
            }
        }

        out->bind();
        glDisable(GL_BLEND);
        glUseProgram(effect.id);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, in->texture);
        glUniform1i(effect.tex, 0);
        if (effect.usesPrevious && feedback[s].texture) {
            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D, feedback[s].texture);
            glUniform1i(effect.prev, 1);
            glActiveTexture(GL_TEXTURE0);
        }
        glUniform1f(effect.mix, mixAmount);
        glUniform4f(effect.params, params[0], params[1], params[2], params[3]);
        if (effect.macros >= 0) {
            glUniform1fv(effect.macros, kMacroCount, comp.modulators.macros.data());
        }
        setAudioUniforms(effect.audio, effect.kick, effect.audioTex);
        glUniform1f(effect.time, static_cast<float>(time_));
        glUniform1f(effect.beat, static_cast<float>(beat_));
        glUniform1f(effect.aspect, width / height);
        glUniform2f(effect.res, width, height);
        triangle_.draw();

        if (effect.usesPrevious && feedback[s].fbo) {
            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D, 0);
            glActiveTexture(GL_TEXTURE0);
            blit(*out, feedback[s]);
            feedbackUsed[s] = true;
        }

        std::swap(in, out);
    }
    return in;
}

RenderTarget* Engine::runPlanEffect(size_t effectIndex, int stateKey, const EffectSlot& slot, const std::array<float, kParamCount>& params, float mixAmount, RenderTarget* in, RenderTarget* out) {
    (void)slot;
    EffectProgram& effect = effects_[effectIndex];
    if (!effect.plan) {
        return in;
    }
    const ShaderPlan& plan = *effect.plan;
    const size_t owner = static_cast<size_t>(kEffectStateBase) + effectIndex;
    ShaderPassState& state = shaderStates_[stateKey];
    if (!state.matches(owner, in->width, in->height)) {
        std::string error;
        std::string note;
        if (!allocateShaderState(state, owner, plan, in->width, in->height, error, note)) {
            if (effect.runtimeError != error) {
                logLine("Effect '" + effectLibrary()[effectIndex].name + "': " + error);
            }
            effect.runtimeError = error;
            state.destroy();
            return in;
        }
        state.lastDrawn = frameId_;
        state.lastClock = time_;
    } else if (state.restart || frameId_ - state.lastDrawn > kShaderRestartGapFrames) {
        state.clear();
    }
    effect.runtimeError.clear();
    ShaderValues values = resolveShaderValues(plan, unsetShaderValues());
    const std::vector<int> controls = shaderFilterParams(plan);
    for (size_t k = 0; k < controls.size() && k < params.size(); ++k) {
        const ShaderParam& param = plan.params[static_cast<size_t>(controls[k])];
        values[static_cast<size_t>(param.slot)] = shaderParamFromControl(param, params[k]);
    }
    const std::array<float, 3> white{1.0f, 1.0f, 1.0f};
    runShaderPasses(plan, effect.passes, state, white, white, false, 1.0f, time_, static_cast<float>(beat_), comp.bpm, *out, false, values, in, mixAmount);
    state.lastDrawn = frameId_;
    state.lastClock = time_;
    return out;
}

RenderTarget* Engine::renderDeck(const std::array<Layer, kLayerCount>& layers, const Master& master, const std::array<Effective, kLayerCount>& effective, const std::array<double, kLayerCount>& clocks, int feedbackSet, bool updatePreviews) {
    const int chainBase = feedbackSet * kChainsPerSet;
    const int deck = updatePreviews ? 0 : 1;

    RenderTarget* accum = &compA_;
    RenderTarget* spare = &compB_;
    const bool scaledChains = layerScale_ < 0.999f && layerLoA_.fbo && layerLoB_.fbo;
    RenderTarget* chainSource = scaledChains ? &layerLoA_ : &layerA_;
    RenderTarget* chainScratch = scaledChains ? &layerLoB_ : &layerB_;
    accum->bind();
    glDisable(GL_BLEND);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    bool anySolo = false;
    for (const Layer& layer : layers) {
        anySolo = anySolo || layer.solo;
    }

    for (int l = 0; l < kLayerCount; ++l) {
        const Layer& layer = layers[static_cast<size_t>(l)];
        const size_t li = static_cast<size_t>(l);
        int clipIndex = -1;
        if (!layer.bypass && !(anySolo && !layer.solo) && layer.activeColumn >= 0) {
            clipIndex = layer.cells[static_cast<size_t>(layer.activeColumn)];
        }
        if (clipIndex < 0 || clipIndex >= static_cast<int>(clips_.size()) || !ensureClip(static_cast<size_t>(clipIndex))) {
            if (updatePreviews) {
                if (live_[li]) {
                    clearTarget(previews_[li]);
                }
                live_[li] = false;
            }
            continue;
        }
        if (updatePreviews) {
            live_[li] = true;
        }

        const bool hidden = !layerVisible(effective[li].opacity) && !chainHasFeedback(layer.fx);
        if (hidden && (!updatePreviews || frameId_ - hiddenPreviewFrame_[li] < kHiddenPreviewInterval)) {
            continue;
        }

        if (profiler_.enabled()) {
            profiler_.mark(PassKind::Source, deck, l + 1, 0, clipLibrary()[static_cast<size_t>(clipIndex)].name);
        }
        drawLayerSource(clipIndex, deck * kLayerCount + l, layer, layer.playback[static_cast<size_t>(layer.activeColumn)], effective[li].scale, clocks[li], *chainSource);

        RenderTarget* result = runChain(layer.fx, chainFeedback(chainBase + l), chainUsed(chainBase + l), chainSource, chainScratch, deck, l + 1);
        if (updatePreviews) {
            profiler_.mark(PassKind::Preview, deck, l + 1, 0, "");
            downsample(*result, previews_[li]);
            hiddenPreviewFrame_[li] = frameId_;
        }
        if (hidden) {
            continue;
        }
        profiler_.mark(PassKind::Composite, deck, l + 1, 0, "");

        const ColorGrade& layerGrade = effective[li].grade;
        if (layer.blend >= BlendSubtract) {
            spare->bind();
            glDisable(GL_BLEND);
            glUseProgram(blend_.id);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, result->texture);
            glUniform1i(blend_.tex, 0);
            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D, accum->texture);
            glUniform1i(blend_.back, 1);
            glUniform1f(blend_.opacity, effective[li].opacity);
            glUniform1i(blend_.mode, layer.blend);
            glUniform4f(blend_.grade, layerGrade.hue, layerGrade.saturation, layerGrade.brightness, layerGrade.contrast);
            triangle_.draw();
            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D, 0);
            glActiveTexture(GL_TEXTURE0);
            std::swap(accum, spare);
            continue;
        }
        accum->bind();
        glEnable(GL_BLEND);
        applyBlend(layer.blend);
        glUseProgram(composite_.id);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, result->texture);
        glUniform1i(composite_.tex, 0);
        glUniform1f(composite_.opacity, effective[li].opacity);
        glUniform1i(composite_.mode, layer.blend);
        glUniform4f(composite_.grade, layerGrade.hue, layerGrade.saturation, layerGrade.brightness, layerGrade.contrast);
        triangle_.draw();
    }

    glDisable(GL_BLEND);
    return runChain(master.fx, chainFeedback(chainBase + kLayerCount), chainUsed(chainBase + kLayerCount), accum, spare, deck, 0);
}

void Engine::finalPass(const RenderTarget& source, const ColorGrade& grade, float level, const RenderTarget& destination) {
    profiler_.mark(PassKind::Final, 0, 0, 0, "");
    destination.bind();
    glDisable(GL_BLEND);
    glUseProgram(final_.id);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, source.texture);
    glUniform1i(final_.tex, 0);
    glUniform1f(final_.level, blackout ? 0.0f : level);
    glUniform4f(final_.grade, grade.hue, grade.saturation, grade.brightness, grade.contrast);
    triangle_.draw();
}

void Engine::transitionPass(const RenderTarget& a, const RenderTarget& b, float progress, int type, const RenderTarget& destination) {
    profiler_.mark(PassKind::Transition, 0, 0, 0, "");
    destination.bind();
    glDisable(GL_BLEND);
    glUseProgram(transition_program_.id);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, a.texture);
    glUniform1i(transition_program_.a, 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, b.texture);
    glUniform1i(transition_program_.b, 1);
    glUniform1f(transition_program_.progress, progress);
    glUniform1i(transition_program_.type, type);
    glUniform1f(transition_program_.aspect, static_cast<float>(destination.width) / static_cast<float>(destination.height));
    glUniform1f(transition_program_.time, static_cast<float>(time_));
    triangle_.draw();
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0);
}

void Engine::render() {
    if (!out_.fbo) {
        return;
    }

    ++frameId_;
    updateQuality();
    const bool profiling = profiler_.enabled();
    profiler_.beginFrame();
    profiler_.mark(PassKind::Housekeeping, 0, 0, 0, "");
    pollGpuQueries();
    gpuActive_ = false;
    const size_t slot = static_cast<size_t>(gpuSlot_);
    if (!profiling && gpuTiming_ && gpuQueries_[slot] && !gpuPending_[slot]) {
        glBeginQuery(GL_TIME_ELAPSED, gpuQueries_[slot]);
        gpuActive_ = true;
    }
    renderClipThumbnails(2);
    serviceScenes();
    serviceMedia();
    serviceGenerators();
    prewarmMedia();
    serviceMemory();
    serviceSpout();
    serviceLive();
    releaseIdleShaderStates();

    feedbackUsed_.fill(false);
    uploadAudio();

    if (transitionTargetsReady_ && transitionIdle_.update(transition_.active)) {
        destroyTransitionTargets();
    }

    if (!transition_.active) {
        RenderTarget* master = renderDeck(comp.layers, comp.master, effective_, layerClock_, feedbackSet_, true);
        finalPass(*master, effectiveMasterGrade_, effectiveLevel_, out_);
    } else {
        if (!transition_.frozen) {
            RenderTarget* previous = renderDeck(outLayers_, outMaster_, outEffective_, outClocks_, feedbackSet_ ^ 1, false);
            finalPass(*previous, outGrade_, outLevel_, deckTex_[0]);
        }
        RenderTarget* master = renderDeck(comp.layers, comp.master, effective_, layerClock_, feedbackSet_, true);
        finalPass(*master, effectiveMasterGrade_, effectiveLevel_, deckTex_[1]);
        transitionPass(deckTex_[0], deckTex_[1], transitionProgress(), transition_.type, out_);
    }

    for (size_t k = 0; k < feedback_.size(); ++k) {
        if (feedback_[k].fbo && !feedbackUsed_[k]) {
            feedback_[k].destroy();
        }
    }

    if (stripEnabled_) {
        profiler_.mark(PassKind::Other, 0, 0, 0, "strip");
        strip_.draw(out_, stripAtTop_, triangle_);
    }

    renderPreview();
    renderEditorPreviews();

    if (profiling) {
        profiler_.endFrame();
        if (profiler_.lastFrameMs() > 0.0) {
            gpuMs_ = profiler_.lastFrameMs();
        }
    }
    if (gpuActive_) {
        glEndQuery(GL_TIME_ELAPSED);
        gpuPending_[slot] = true;
        gpuSlot_ = (gpuSlot_ + 1) % kGpuQueries;
        gpuActive_ = false;
    }

    glBindTexture(GL_TEXTURE_2D, 0);
    glUseProgram(0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Engine::pollGpuQueries() {
    for (size_t i = 0; i < gpuQueries_.size(); ++i) {
        if (!gpuPending_[i]) {
            continue;
        }
        GLint available = 0;
        glGetQueryObjectiv(gpuQueries_[i], GL_QUERY_RESULT_AVAILABLE, &available);
        if (available) {
            GLuint64 nanoseconds = 0;
            glGetQueryObjectui64v(gpuQueries_[i], GL_QUERY_RESULT, &nanoseconds);
            gpuMs_ = static_cast<double>(nanoseconds) / 1.0e6;
            gpuPending_[i] = false;
        }
    }
}

}
