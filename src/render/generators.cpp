#include "render/generators.hpp"

#include "core/log.hpp"

#include <algorithm>
#include <cmath>

namespace nvj {

namespace {

const char* kTextVertex = R"glsl(#version 330 core
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUv;
layout(location = 2) in vec4 aColor;
uniform vec2 uSize;
out vec2 vUv;
out vec4 vColor;
void main() {
    vec2 n = aPos / uSize;
    gl_Position = vec4(n.x * 2.0 - 1.0, 1.0 - n.y * 2.0, 0.0, 1.0);
    vUv = aUv;
    vColor = aColor;
}
)glsl";

const char* kTextFragment = R"glsl(#version 330 core
in vec2 vUv;
in vec4 vColor;
uniform sampler2D uAtlas;
out vec4 fragColor;
void main() {
    float coverage = texture(uAtlas, vUv).r;
    fragColor = vColor * coverage;
}
)glsl";

const char* kParticleVertex = R"glsl(#version 330 core
layout(location = 0) in vec4 aA;
layout(location = 1) in vec2 aB;
uniform float uAspect;
uniform float uScale;
uniform vec3 uColorA;
uniform vec3 uColorB;
uniform int uMode;
out vec2 vQuad;
out vec4 vColor;
vec3 rainbow(float h) {
    return clamp(abs(fract(h + vec3(0.0, 2.0 / 3.0, 1.0 / 3.0)) * 6.0 - 3.0) - 1.0, 0.0, 1.0);
}
void main() {
    vec2 corner = vec2(float(gl_VertexID & 1), float((gl_VertexID >> 1) & 1)) * 2.0 - 1.0;
    vec2 p = aA.xy + corner * aA.z * uScale;
    gl_Position = vec4(p.x / uAspect, p.y, 0.0, 1.0);
    vQuad = corner;
    vec3 c = uMode == 1 ? rainbow(aB.y) : (uMode == 2 ? vec3(1.0) : mix(uColorA, uColorB, aB.x));
    vColor = vec4(c, aA.w);
}
)glsl";

const char* kParticleFragment = R"glsl(#version 330 core
in vec2 vQuad;
in vec4 vColor;
uniform float uSoft;
out vec4 fragColor;
void main() {
    float d = length(vQuad);
    float m = 1.0 - smoothstep(1.0 - max(uSoft, 0.02), 1.0, d);
    float a = vColor.a * m;
    fragColor = vec4(vColor.rgb * a, a);
}
)glsl";

std::array<float, 3> hueColor(float h) {
    h -= std::floor(h);
    auto channel = [h](float offset) {
        const float x = std::fabs((h + offset) - std::floor(h + offset)) * 6.0f - 3.0f;
        return std::clamp(std::fabs(x) - 1.0f, 0.0f, 1.0f);
    };
    return {channel(0.0f), channel(2.0f / 3.0f), channel(1.0f / 3.0f)};
}

void pushVertex(std::vector<float>& out, float x, float y, float u, float v, const std::array<float, 3>& color, float alpha) {
    out.push_back(x);
    out.push_back(y);
    out.push_back(u);
    out.push_back(v);
    out.push_back(color[0] * alpha);
    out.push_back(color[1] * alpha);
    out.push_back(color[2] * alpha);
    out.push_back(alpha);
}

}

GeneratorGl::~GeneratorGl() {
    release();
}

bool GeneratorGl::ensure(std::string& error) {
    if (ready()) {
        return true;
    }
    if (tried_) {
        error = failure_;
        return false;
    }
    tried_ = true;
    std::string log;
    textProgram = buildProgram(kTextVertex, kTextFragment, &log);
    if (textProgram) {
        textSize = glGetUniformLocation(textProgram, "uSize");
        textAtlas = glGetUniformLocation(textProgram, "uAtlas");
        particleProgram = buildProgram(kParticleVertex, kParticleFragment, &log);
    }
    if (!textProgram || !particleProgram) {
        failure_ = "The shaders of the text and particle clips did not compile";
        logLine("Text and particle shaders failed: " + log);
        release();
        tried_ = true;
        error = failure_;
        return false;
    }
    particleAspect = glGetUniformLocation(particleProgram, "uAspect");
    particleScale = glGetUniformLocation(particleProgram, "uScale");
    particleColorA = glGetUniformLocation(particleProgram, "uColorA");
    particleColorB = glGetUniformLocation(particleProgram, "uColorB");
    particleMode = glGetUniformLocation(particleProgram, "uMode");
    particleSoft = glGetUniformLocation(particleProgram, "uSoft");
    glGenVertexArrays(1, &vao);
    return true;
}

void GeneratorGl::simulateCompileFailure() {
    release();
    tried_ = true;
    failure_ = "The shaders of the text and particle clips did not compile";
}

void GeneratorGl::allowRetry() {
    if (!ready()) {
        tried_ = false;
        failure_.clear();
    }
}

void GeneratorGl::release() {
    if (textProgram) {
        glDeleteProgram(textProgram);
        textProgram = 0;
    }
    if (particleProgram) {
        glDeleteProgram(particleProgram);
        particleProgram = 0;
    }
    if (vao) {
        glDeleteVertexArrays(1, &vao);
        vao = 0;
    }
}

int textBakeHeight(float neededPixels) {
    static const int buckets[] = {24, 32, 48, 64, 96, 128, 192, 256, 320};
    for (int bucket : buckets) {
        if (static_cast<float>(bucket) >= neededPixels) {
            return bucket;
        }
    }
    return buckets[sizeof(buckets) / sizeof(buckets[0]) - 1];
}

TextBakeOutput bakeText(const TextBakeInput& input) {
    TextBakeOutput out;
    out.text = input.text;
    out.requestedHeight = input.pixelHeight;
    text::FontEntry entry;
    std::string fontNote;
    if (!text::resolveFont(input.font, entry)) {
        if (!text::resolveFont(std::string(), entry)) {
            out.error = "No font was found on this computer";
            return out;
        }
        fontNote = "The chosen font was not found and another one is used";
    }
    text::FontFace primary;
    std::string error;
    if (!text::loadFontFile(entry.path, primary, error)) {
        out.error = error;
        return out;
    }
    text::BakeRequest request;
    request.text = input.text;
    request.pixelHeight = static_cast<float>(input.pixelHeight);
    request.useKerning = input.kerning;
    request.faces = {&primary};
    text::Atlas atlas;
    if (!text::bakeAtlas(request, atlas, error)) {
        out.error = error;
        return out;
    }
    text::FontFace fallback;
    if (!atlas.missing.empty()) {
        const std::filesystem::path path = text::firstExisting(text::cjkFontCandidates());
        std::string fallbackError;
        if (!path.empty() && text::loadFontFile(path, fallback, fallbackError)) {
            request.faces = {&primary, &fallback};
            text::Atlas second;
            if (text::bakeAtlas(request, second, fallbackError) && second.missing.size() < atlas.missing.size()) {
                atlas = std::move(second);
            }
        }
    }
    out.note = fontNote;
    if (!atlas.missing.empty()) {
        out.note = "Some characters are not in the font and show as a question mark";
    }
    out.atlas = std::move(atlas);
    return out;
}

TextPlayer::TextPlayer(std::string label, std::string name) : name_(std::move(name)) {
    label_ = std::move(label);
}

TextPlayer::~TextPlayer() {
    if (job_.valid()) {
        job_.wait();
    }
    freeTexture();
    if (buffer_) {
        glDeleteBuffers(1, &buffer_);
    }
}

void TextPlayer::setDocument(const text::TextDoc& doc) {
    doc_ = doc;
    text::clampDoc(doc_);
    decoded_ = text::decodeUtf8(doc_.text).text;
    fromFile_ = false;
    docLoaded_ = true;
    ++docVersion_;
}

void TextPlayer::reload() {
    if (fromFile_) {
        docLoaded_ = false;
    }
}

void TextPlayer::freeTexture() {
    if (texture_) {
        glDeleteTextures(1, &texture_);
        texture_ = 0;
    }
}

void TextPlayer::release() {
    if (job_.valid()) {
        job_.wait();
        job_ = std::future<TextBakeOutput>();
    }
    freeTexture();
    atlas_ = text::Atlas();
    layout_ = text::Layout();
    layoutKey_ = -1;
    bakedVersion_ = -1;
    thumbnailDone = thumbnailDone && false;
    state_ = GeneratorState::Idle;
}

unsigned long long TextPlayer::gpuBytes() const {
    return texture_ ? static_cast<unsigned long long>(atlas_.width) * static_cast<unsigned long long>(atlas_.height) : 0ull;
}

void TextPlayer::startBake(int pixelHeight) {
    TextBakeInput input;
    input.text = decoded_;
    input.font = doc_.font;
    input.pixelHeight = pixelHeight;
    input.kerning = doc_.kerning;
    jobVersion_ = docVersion_;
    lastBake_ = std::chrono::steady_clock::now();
    job_ = std::async(std::launch::async, [input]() { return bakeText(input); });
}

void TextPlayer::consume(TextBakeOutput output) {
    bakedVersion_ = jobVersion_;
    bakedRequest_ = output.requestedHeight;
    if (!output.error.empty()) {
        error_ = output.error;
        if (!texture_) {
            state_ = GeneratorState::Failed;
        }
        return;
    }
    freeTexture();
    atlas_ = std::move(output.atlas);
    bakedText_ = std::move(output.text);
    glGenTextures(1, &texture_);
    glBindTexture(GL_TEXTURE_2D, texture_);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, atlas_.width, atlas_.height, 0, GL_RED, GL_UNSIGNED_BYTE, atlas_.pixels.data());
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
    atlas_.pixels.clear();
    atlas_.pixels.shrink_to_fit();
    layoutKey_ = -1;
    note_ = output.note;
    error_.clear();
    ++bakeCount_;
    state_ = GeneratorState::Ready;
}

GeneratorState TextPlayer::poll(int outputHeight) {
    if (!docLoaded_) {
        text::TextDoc loaded;
        std::string error;
        if (!text::loadTextDoc(name_, loaded, error)) {
            error_ = error;
            state_ = GeneratorState::Failed;
            return state_;
        }
        doc_ = loaded;
        decoded_ = text::decodeUtf8(doc_.text).text;
        docLoaded_ = true;
        ++docVersion_;
        error_.clear();
    }
    if (job_.valid() && job_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        TextBakeOutput output = job_.get();
        job_ = std::future<TextBakeOutput>();
        consume(std::move(output));
    }
    if (!job_.valid()) {
        const int wanted = std::max(wantedHeight_, textBakeHeight(doc_.size * static_cast<float>(std::max(outputHeight, 1))));
        const double since = std::chrono::duration<double>(std::chrono::steady_clock::now() - lastBake_).count();
        bool bake = false;
        if (bakedVersion_ != docVersion_) {
            bake = true;
        } else if (texture_ != 0 && wanted > bakedRequest_ && since > 1.0) {
            bake = true;
        } else if (texture_ != 0 && wanted * 2 <= bakedRequest_ && since > 5.0) {
            bake = true;
        }
        if (bake) {
            startBake(wanted);
        }
    }
    if (texture_ != 0) {
        state_ = GeneratorState::Ready;
    } else if (job_.valid()) {
        state_ = GeneratorState::Loading;
    }
    return state_;
}

bool TextPlayer::draw(GeneratorGl& gl, const GeneratorFrame& frame, int) {
    if (state_ != GeneratorState::Ready || texture_ == 0 || atlas_.pixelHeight <= 0.0f || frame.width <= 0 || frame.height <= 0) {
        return false;
    }
    const float scale = std::max(frame.scale, 0.05f);
    const float textPx = doc_.size * static_cast<float>(frame.height) * scale;
    wantedHeight_ = textBakeHeight(textPx);
    const float k = textPx / atlas_.pixelHeight;
    const float blockWidth = (doc_.wrap > 0.0f ? doc_.wrap : 1.0f) * static_cast<float>(frame.width);
    const long long key = (static_cast<long long>(frame.width) << 40) ^ (static_cast<long long>(frame.height) << 24) ^ (static_cast<long long>(std::lround(scale * 100.0f)) << 8) ^ static_cast<long long>(docVersion_ * 7919 + bakeCount_);
    if (key != layoutKey_) {
        text::LayoutParams params;
        params.wrapWidth = doc_.wrap > 0.0f ? blockWidth / k : 0.0f;
        params.lineSpacing = doc_.lineSpacing;
        params.tracking = doc_.tracking;
        params.hAlign = doc_.hAlign;
        params.vAlign = doc_.vAlign;
        params.kerning = doc_.kerning;
        layout_ = text::layoutText(atlas_, bakedText_, params);
        layoutKey_ = key;
    }
    if (layout_.glyphs.empty()) {
        return true;
    }
    float anchorX = static_cast<float>(frame.width) * 0.5f + doc_.posX * static_cast<float>(frame.width) * 0.5f;
    if (doc_.hAlign == text::HAlign::Left) {
        anchorX -= blockWidth * 0.5f;
    } else if (doc_.hAlign == text::HAlign::Right) {
        anchorX += blockWidth * 0.5f;
    }
    float anchorY = static_cast<float>(frame.height) * 0.5f;
    if (doc_.vAlign == text::VAlign::Top) {
        anchorY = static_cast<float>(frame.height) * 0.05f;
    } else if (doc_.vAlign == text::VAlign::Bottom) {
        anchorY = static_cast<float>(frame.height) * 0.95f;
    }
    anchorY -= doc_.posY * static_cast<float>(frame.height) * 0.5f;

    text::AnimationInput animation;
    animation.animation = doc_.animation;
    animation.time = frame.clock;
    animation.beat = frame.beat;
    animation.level = frame.level;
    animation.speed = doc_.animSpeed;
    animation.amount = doc_.animAmount;

    vertices_.clear();
    vertices_.reserve(layout_.glyphs.size() * 12 * 8);
    const bool shadow = doc_.shadow > 0.001f;
    for (int pass = shadow ? 0 : 1; pass < 2; ++pass) {
        for (const text::PlacedGlyph& g : layout_.glyphs) {
            const text::GlyphPose pose = text::poseGlyph(g, layout_.visibleCount, animation);
            if (pose.alpha <= 0.002f) {
                continue;
            }
            const float offX = pose.offsetX * textPx + (pass == 0 ? textPx * 0.04f : 0.0f);
            const float offY = -pose.offsetY * textPx + (pass == 0 ? textPx * 0.04f : 0.0f);
            const float x0 = anchorX + (g.cx + (g.x0 - g.cx) * pose.scale) * k + offX;
            const float x1 = anchorX + (g.cx + (g.x1 - g.cx) * pose.scale) * k + offX;
            const float y0 = anchorY + (g.cy + (g.y0 - g.cy) * pose.scale) * k + offY;
            const float y1 = anchorY + (g.cy + (g.y1 - g.cy) * pose.scale) * k + offY;
            std::array<float, 3> color = frame.colorA;
            float alpha = pose.alpha;
            if (pass == 0) {
                color = {0.0f, 0.0f, 0.0f};
                alpha *= doc_.shadow * 0.8f;
            } else if (doc_.color == text::ColorMode::White) {
                color = {1.0f, 1.0f, 1.0f};
            } else if (doc_.color == text::ColorMode::Rainbow || frame.rainbow) {
                color = hueColor(static_cast<float>(g.index) / static_cast<float>(std::max(layout_.visibleCount, 1)) + static_cast<float>(frame.clock) * 0.1f);
            }
            pushVertex(vertices_, x0, y0, g.u0, g.v0, color, alpha);
            pushVertex(vertices_, x1, y0, g.u1, g.v0, color, alpha);
            pushVertex(vertices_, x0, y1, g.u0, g.v1, color, alpha);
            pushVertex(vertices_, x1, y0, g.u1, g.v0, color, alpha);
            pushVertex(vertices_, x1, y1, g.u1, g.v1, color, alpha);
            pushVertex(vertices_, x0, y1, g.u0, g.v1, color, alpha);
        }
    }
    if (vertices_.empty()) {
        return true;
    }
    if (!buffer_) {
        glGenBuffers(1, &buffer_);
    }
    glBindVertexArray(gl.vao);
    glBindBuffer(GL_ARRAY_BUFFER, buffer_);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertices_.size() * sizeof(float)), vertices_.data(), GL_STREAM_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), reinterpret_cast<const void*>(0));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), reinterpret_cast<const void*>(2 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, 8 * sizeof(float), reinterpret_cast<const void*>(4 * sizeof(float)));
    glUseProgram(gl.textProgram);
    glUniform2f(gl.textSize, static_cast<float>(frame.width), static_cast<float>(frame.height));
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture_);
    glUniform1i(gl.textAtlas, 0);
    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(vertices_.size() / 8));
    glDisable(GL_BLEND);
    glBindTexture(GL_TEXTURE_2D, 0);
    glDisableVertexAttribArray(0);
    glDisableVertexAttribArray(1);
    glDisableVertexAttribArray(2);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
    glUseProgram(0);
    return true;
}

ParticlePlayer::ParticlePlayer(std::string label, std::string name, bool builtin, int limit) : name_(std::move(name)), builtin_(builtin), fromFile_(!builtin), limit_(particles::clampLimit(limit)) {
    label_ = std::move(label);
}

ParticlePlayer::~ParticlePlayer() {
    if (buffer_) {
        glDeleteBuffers(1, &buffer_);
    }
}

void ParticlePlayer::setParams(const particles::Params& params) {
    params_ = params;
    particles::clampParams(params_);
    fromFile_ = false;
    builtin_ = false;
    loaded_ = true;
    ++version_;
    state_ = GeneratorState::Ready;
}

void ParticlePlayer::setLimit(int limit) {
    limit_ = particles::clampLimit(limit);
}

void ParticlePlayer::reload() {
    if (fromFile_) {
        loaded_ = false;
    }
}

void ParticlePlayer::release() {
    slots_.clear();
    thumbnailDone = false;
}

unsigned long long ParticlePlayer::gpuBytes() const {
    return buffer_ ? static_cast<unsigned long long>(bufferCapacity_) : 0ull;
}

int ParticlePlayer::alive(int instance) const {
    auto it = slots_.find(instance);
    return it == slots_.end() || !it->second.system ? 0 : it->second.system->alive();
}

long long ParticlePlayer::dropped() const {
    long long total = 0;
    for (const auto& entry : slots_) {
        if (entry.second.system) {
            total += entry.second.system->dropped();
        }
    }
    return total;
}

GeneratorState ParticlePlayer::poll(int) {
    if (!loaded_) {
        particles::Params loaded;
        std::string error;
        bool ok = false;
        if (builtin_) {
            ok = particles::findBuiltin(name_, loaded);
            if (!ok) {
                error = "The particle preset is not known";
            }
        } else {
            ok = particles::loadParticleDoc(name_, loaded, error);
        }
        if (!ok) {
            error_ = error;
            state_ = GeneratorState::Failed;
            return state_;
        }
        params_ = loaded;
        loaded_ = true;
        ++version_;
        error_.clear();
    }
    state_ = GeneratorState::Ready;
    return state_;
}

bool ParticlePlayer::drawSystem(GeneratorGl& gl, const GeneratorFrame& frame, particles::System& system, const particles::Drive& drive) {
    system.collect(vertices_, drive);
    if (vertices_.empty()) {
        return true;
    }
    if (!buffer_) {
        glGenBuffers(1, &buffer_);
    }
    const size_t bytes = vertices_.size() * sizeof(particles::Vertex);
    glBindVertexArray(gl.vao);
    glBindBuffer(GL_ARRAY_BUFFER, buffer_);
    const size_t capacity = particles::gpuBytes(limit_);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(std::max(capacity, bytes)), nullptr, GL_STREAM_DRAW);
    bufferCapacity_ = std::max(capacity, bytes);
    glBufferSubData(GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(bytes), vertices_.data());
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, sizeof(particles::Vertex), reinterpret_cast<const void*>(0));
    glVertexAttribDivisor(0, 1);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(particles::Vertex), reinterpret_cast<const void*>(4 * sizeof(float)));
    glVertexAttribDivisor(1, 1);
    glUseProgram(gl.particleProgram);
    glUniform1f(gl.particleAspect, static_cast<float>(frame.width) / static_cast<float>(std::max(frame.height, 1)));
    glUniform1f(gl.particleScale, std::max(frame.scale, 0.05f));
    glUniform3f(gl.particleColorA, frame.colorA[0], frame.colorA[1], frame.colorA[2]);
    glUniform3f(gl.particleColorB, frame.colorB[0], frame.colorB[1], frame.colorB[2]);
    int mode = params_.color == particles::ColorMode::Rainbow ? 1 : (params_.color == particles::ColorMode::White ? 2 : 0);
    if (frame.rainbow) {
        mode = 1;
    }
    glUniform1i(gl.particleMode, mode);
    glUniform1f(gl.particleSoft, params_.softness);
    glEnable(GL_BLEND);
    if (params_.additive) {
        glBlendFuncSeparate(GL_ONE, GL_ONE, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    } else {
        glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    }
    glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, static_cast<GLsizei>(vertices_.size()));
    glDisable(GL_BLEND);
    glVertexAttribDivisor(0, 0);
    glVertexAttribDivisor(1, 0);
    glDisableVertexAttribArray(0);
    glDisableVertexAttribArray(1);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
    glUseProgram(0);
    return true;
}

bool ParticlePlayer::draw(GeneratorGl& gl, const GeneratorFrame& frame, int instance) {
    if (state_ != GeneratorState::Ready || frame.width <= 0 || frame.height <= 0) {
        return false;
    }
    auto found = slots_.find(instance);
    if (found == slots_.end()) {
        if (slots_.size() >= 16) {
            return false;
        }
        Slot slot;
        slot.system = std::make_unique<particles::System>(limit_);
        found = slots_.emplace(instance, std::move(slot)).first;
    }
    Slot& slot = found->second;
    particles::System& system = *slot.system;
    if (slot.version != version_) {
        system.setParams(params_);
        system.reset();
        slot.version = version_;
    }
    if (system.limit() != particles::clampLimit(limit_)) {
        system.setLimit(limit_);
    }
    system.setAspect(static_cast<float>(frame.width) / static_cast<float>(frame.height));
    particles::Drive drive;
    drive.clock = frame.clock;
    drive.beat = frame.beat;
    drive.level = frame.level;
    system.advance(drive);
    return drawSystem(gl, frame, system, drive);
}

bool ParticlePlayer::drawThumbnail(GeneratorGl& gl, const GeneratorFrame& frame) {
    if (state_ != GeneratorState::Ready || frame.width <= 0 || frame.height <= 0) {
        return false;
    }
    particles::System system(limit_);
    system.setParams(params_);
    system.setAspect(static_cast<float>(frame.width) / static_cast<float>(frame.height));
    system.reset();
    particles::Drive drive;
    drive.level = 0.6f;
    for (int i = 0; i <= 150; ++i) {
        drive.clock = static_cast<double>(i) / 60.0;
        drive.beat = drive.clock * 2.0;
        system.advance(drive);
    }
    return drawSystem(gl, frame, system, drive);
}

}
