#include "render/engine.hpp"

#include "render/clip_library.hpp"
#include "render/effect_library.hpp"
#include "core/log.hpp"
#include "core/modulation.hpp"
#include "scene3d/procedural.hpp"
#include "scene3d/scene_pack.hpp"

#include <algorithm>
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
void main() {
    vec2 p = (vUv - 0.5) * uFit + 0.5;
    vec2 inside = step(vec2(0.0), p) * step(p, vec2(1.0));
    float mask = inside.x * inside.y;
    vec4 t = texture(uTex, vec2(p.x, 1.0 - p.y));
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
    return true;
}

std::unique_ptr<MediaPlayer> Engine::makeMedia(const ClipDef& def) const {
    if (def.kind == ClipKind::ImageFile || def.kind == ClipKind::VideoFile) {
        return std::make_unique<MediaPlayer>(def.name, def.path, def.kind == ClipKind::VideoFile);
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

void Engine::syncLibrary() {
    const std::vector<ClipDef>& library = clipLibrary();
    const size_t before = clips_.size();
    if (library.size() <= before) {
        return;
    }
    clips_.resize(library.size());
    scenes_.resize(library.size());
    media_.resize(library.size());
    clipThumbs_.resize(library.size());
    for (size_t i = before; i < library.size(); ++i) {
        clipThumbs_[i].create(kThumbWidth, kThumbHeight);
        scenes_[i] = makeScene(library[i]);
        media_[i] = makeMedia(library[i]);
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

bool Engine::drawMedia(MediaPlayer& player, float scale, bool backdrop, double clock) {
    glClearColor(0.0f, 0.0f, 0.0f, backdrop ? 1.0f : 0.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    if (!player.update(clock) || player.texture() == 0 || player.width() <= 0 || player.height() <= 0) {
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
    triangle_.draw();
    glBindTexture(GL_TEXTURE_2D, 0);
    return true;
}

void Engine::serviceMedia() {
    constexpr double kImageIdleSeconds = 120.0;
    constexpr double kVideoIdleSeconds = 6.0;
    for (const Layer& layer : comp.layers) {
        for (const int cell : layer.cells) {
            if (cell >= 0 && static_cast<size_t>(cell) < media_.size() && media_[static_cast<size_t>(cell)] && !media_[static_cast<size_t>(cell)]->isVideo()) {
                ensureMedia(static_cast<size_t>(cell));
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
    for (const Layer& layer : comp.layers) {
        for (const int cell : layer.cells) {
            if (cell >= 0 && static_cast<size_t>(cell) < scenes_.size() && scenes_[static_cast<size_t>(cell)]) {
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

GpuMemory Engine::gpuMemory() const {
    const auto bytes = [](const RenderTarget& target) {
        return target.fbo ? static_cast<size_t>(target.width) * static_cast<size_t>(target.height) * 4 : static_cast<size_t>(0);
    };
    GpuMemory memory;
    for (const RenderTarget* target : {&layerA_, &layerB_, &compA_, &compB_, &out_, &deckTex_[0], &deckTex_[1], &transTex_}) {
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
    for (const std::unique_ptr<MediaPlayer>& player : media_) {
        if (player) {
            memory.media += static_cast<size_t>(player->gpuBytes());
        }
    }
    for (const std::unique_ptr<ScenePlayer>& player : scenes_) {
        if (player && player->state() == SceneState::Ready) {
            memory.sceneGeometry += player->stats().gpuBytes;
        }
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
    ClipProgram& clip = clips_[index];
    if (clip.tried) {
        return clip.id != 0;
    }
    clip.tried = true;
    const ClipDef& def = clipLibrary()[index];
    std::string error;
    GLuint program = buildProgram(clipVertexSource(), clipFragmentSource(def), &error);
    if (!program) {
        logLine("Clip shader '" + def.name + "' failed: " + error);
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

bool Engine::ensureEffect(size_t index) {
    if (index >= effects_.size()) {
        return false;
    }
    EffectProgram& effect = effects_[index];
    if (effect.tried) {
        return effect.id != 0;
    }
    effect.tried = true;
    const EffectDef& def = effectLibrary()[index];
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
        if (!ensureClip(i)) {
            ++failed;
            report += "clip " + clipLibrary()[i].name + "\n";
        }
    }
    for (size_t i = 0; i < effects_.size(); ++i) {
        if (!ensureEffect(i)) {
            ++failed;
            report += "effect " + effectLibrary()[i].name + "\n";
        }
    }
    renderClipThumbnails(static_cast<int>(clips_.size()));
    return failed;
}

void Engine::shutdown() {
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
    for (SpoutInput& slot : spoutSlots_) {
        slot.release();
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
        if (clip.id) {
            glDeleteProgram(clip.id);
        }
    }
    clips_.clear();
    deferredThumbs_.clear();
    scenes_.clear();
    media_.clear();
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
    transTex_.destroy();
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
    if (deckTex_[0].create(out_.width, out_.height) && deckTex_[1].create(out_.width, out_.height) && transTex_.create(out_.width, out_.height)) {
        transitionTargetsReady_ = true;
        return true;
    }
    for (RenderTarget& target : deckTex_) {
        target.destroy();
    }
    transTex_.destroy();
    return false;
}

void Engine::destroyTargets() {
    destroyTransitionTargets();
    layerA_.destroy();
    layerB_.destroy();
    compA_.destroy();
    compB_.destroy();
    out_.destroy();
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
}

void Engine::setAudio(const AudioFeatures& features) {
    audioInput_ = features;
}

float Engine::sourceValue(int source) const {
    return modulationSource(source, audio_, comp.modulators, beat_, comp.beatsPerBar);
}

void Engine::shiftBeat(double beats) {
    if (std::isfinite(beats)) {
        beat_ += beats;
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
    beat_ += step * static_cast<double>(comp.bpm) / 60.0;
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
    for (int c = 0; c < kChainsPerSet * kEffectSlots; ++c) {
        feedback_[static_cast<size_t>(feedbackSet_ * kChainsPerSet * kEffectSlots + c)].destroy();
    }
    if (look.bpm > 0.0f && look.bpm != comp.bpm) {
        comp.bpm = std::clamp(look.bpm, 40.0f, 240.0f);
    }
    computeEffective();
}

void Engine::nudgeBeat(float amount) {
    const double fraction = beat_ - std::floor(beat_);
    const double delta = fraction < 0.5 ? -fraction : 1.0 - fraction;
    beat_ += delta * static_cast<double>(std::clamp(amount, 0.0f, 1.0f));
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

void Engine::clearLayer(int layer) {
    if (layer < 0 || layer >= kLayerCount) {
        return;
    }
    comp.layers[static_cast<size_t>(layer)].activeColumn = -1;
}

void Engine::tap() {
    if (!taps_.empty() && time_ - taps_.back() > 2.0) {
        taps_.clear();
    }
    taps_.push_back(time_);
    while (taps_.size() > 5) {
        taps_.pop_front();
    }
    if (taps_.size() >= 2) {
        double interval = (taps_.back() - taps_.front()) / static_cast<double>(taps_.size() - 1);
        if (interval > 0.0) {
            comp.bpm = static_cast<float>(std::clamp(60.0 / interval, 40.0, 240.0));
        }
    }
    beat_ = std::floor(beat_ + 0.5);
}

void Engine::resync() {
    beat_ = 0.0;
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

void Engine::renderClipThumbnails(int maxCount) {
    if (nextThumb_ >= clips_.size() || maxCount <= 0) {
        return;
    }
    const Layer defaults;
    const AudioFeatures saved = audio_;
    audio_ = virtualAudio(2.3, 2.6);
    uploadAudio();
    glDisable(GL_BLEND);
    int done = 0;
    while (nextThumb_ < clips_.size() && done < maxCount) {
        const size_t i = nextThumb_++;
        ++done;
        if (i < media_.size() && media_[i]) {
            deferredMediaThumbs_.push_back(i);
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
        drawClip(clips_[i], 2.6f, 2.3f, 128.0f, static_cast<float>(kThumbWidth), static_cast<float>(kThumbHeight), defaults.colorA, defaults.colorB, false, 1.0f);
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

void Engine::drawLayerSource(int clipIndex, const Layer& layer, float scale, double clock, const RenderTarget& target) {
    const ClipDef& clipDef = clipLibrary()[static_cast<size_t>(clipIndex)];
    const bool spoutClip = clipDef.kind == ClipKind::SpoutSource;
    if (spoutClip) {
        prepareSpout(clipDef.sceneIndex);
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
        drawMedia(*media_[static_cast<size_t>(clipIndex)], scale, false, clock);
    } else if (!spoutClip || !drawSpout(clipDef.sceneIndex, scale)) {
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
    drawLayerSource(clipIndex, layer, effective_[layerIndex].scale, previewClock_, previewA_);
    previewResult_ = runChain(layer.fx, previewFeedback_.data(), previewFeedbackUsed_.data(), &previewA_, &previewB_);
    for (size_t k = 0; k < previewFeedback_.size(); ++k) {
        if (previewFeedback_[k].fbo && !previewFeedbackUsed_[k]) {
            previewFeedback_[k].destroy();
        }
    }
}

RenderTarget* Engine::runChain(const std::array<EffectSlot, kEffectSlots>& slots, RenderTarget* feedback, bool* feedbackUsed, RenderTarget* source, RenderTarget* scratch) {
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

RenderTarget* Engine::renderDeck(const std::array<Layer, kLayerCount>& layers, const Master& master, const std::array<Effective, kLayerCount>& effective, const std::array<double, kLayerCount>& clocks, int feedbackSet, bool updatePreviews) {
    const int chainBase = feedbackSet * kChainsPerSet;

    RenderTarget* accum = &compA_;
    RenderTarget* spare = &compB_;
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

        drawLayerSource(clipIndex, layer, effective[li].scale, clocks[li], layerA_);

        RenderTarget* result = runChain(layer.fx, chainFeedback(chainBase + l), chainUsed(chainBase + l), &layerA_, &layerB_);
        if (updatePreviews) {
            downsample(*result, previews_[li]);
        }

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
    return runChain(master.fx, chainFeedback(chainBase + kLayerCount), chainUsed(chainBase + kLayerCount), accum, spare);
}

void Engine::finalPass(const RenderTarget& source, const ColorGrade& grade, float level, const RenderTarget& destination) {
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
    pollGpuQueries();
    gpuActive_ = false;
    const size_t slot = static_cast<size_t>(gpuSlot_);
    if (gpuTiming_ && gpuQueries_[slot] && !gpuPending_[slot]) {
        glBeginQuery(GL_TIME_ELAPSED, gpuQueries_[slot]);
        gpuActive_ = true;
    }
    renderClipThumbnails(2);
    serviceScenes();
    serviceMedia();
    serviceSpout();

    feedbackUsed_.fill(false);
    uploadAudio();

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
        strip_.draw(out_, stripAtTop_, triangle_);
    }

    renderPreview();

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
