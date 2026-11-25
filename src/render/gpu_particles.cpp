#include "render/gpu_particles.hpp"

#include "core/log.hpp"

#include <algorithm>
#include <cmath>

namespace nvj {

namespace {

const char* kUpdateVertex = R"glsl(#version 330 core
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aVel;
layout(location = 2) in vec2 aAL;
layout(location = 3) in vec2 aSH;
uniform uint uCapacity;
uniform uint uHead;
uniform uint uSpawn;
uniform uint uSerial;
uniform uint uIndex;
uniform uint uSeed;
uniform float uLevel;
uniform float uAspect;
uniform float uDt;
uniform float uLife;
uniform float uLifeJitter;
uniform float uSpeed;
uniform float uSpeedJitter;
uniform float uDirection;
uniform float uSpread;
uniform float uGravity;
uniform float uDrag;
uniform float uTurbulence;
uniform float uSwirl;
uniform float uSize;
uniform float uSizeJitter;
uniform float uLevelSpeed;
uniform float uExtent;
uniform vec2 uOrigin;
uniform int uShape;
out vec2 oPos;
out vec2 oVel;
out vec2 oAL;
out vec2 oSH;
uint pcg(uint v) {
    uint state = v * 747796405u + 2891336453u;
    uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}
float unitOf(uint h) {
    return float(h >> 8u) * (1.0 / 16777216.0);
}
uint stream(uint base, uint k) {
    return pcg(base + k * 0x9E3779B9u);
}
void main() {
    uint i = uint(gl_VertexID);
    uint rel = (i + uCapacity - uHead) % uCapacity;
    if (rel < uSpawn) {
        uint serial = uSerial + rel;
        uint base = pcg(uSeed * 0x9E3779B1u + serial * 0x85EBCA6Bu + 0x27D4EB2Fu);
        float r0 = unitOf(stream(base, 0u));
        float r1 = unitOf(stream(base, 1u));
        float r2 = unitOf(stream(base, 2u));
        float r3 = unitOf(stream(base, 3u));
        float r4 = unitOf(stream(base, 4u));
        float r5 = unitOf(stream(base, 5u));
        float r6 = unitOf(stream(base, 6u));
        float angle = (uDirection + (r0 * 2.0 - 1.0) * uSpread * 0.5) * 3.14159265358979323846 / 180.0;
        float speed = uSpeed * (1.0 + uLevelSpeed * uLevel) * max(0.0, 1.0 + (r1 * 2.0 - 1.0) * uSpeedJitter);
        vec2 p = vec2(uOrigin.x * uAspect, uOrigin.y);
        if (uShape == 1) {
            p.x += (r2 * 2.0 - 1.0) * uExtent * uAspect;
        } else if (uShape == 2) {
            float around = r2 * 2.0 * 3.14159265358979323846;
            p += vec2(cos(around), sin(around)) * uExtent;
        } else if (uShape == 3) {
            p.x += (r2 * 2.0 - 1.0) * uExtent * uAspect;
            p.y += (r3 * 2.0 - 1.0) * uExtent;
        }
        oPos = p;
        oVel = vec2(cos(angle), sin(angle)) * speed;
        oAL = vec2(0.0, max(0.05, uLife * (1.0 + (r4 * 2.0 - 1.0) * uLifeJitter)));
        oSH = vec2(uSize * max(0.1, 1.0 + (r5 * 2.0 - 1.0) * uSizeJitter), r6);
        return;
    }
    float age = aAL.x;
    float life = aAL.y;
    if (life <= 0.0 || age >= life) {
        oPos = aPos;
        oVel = vec2(0.0);
        oAL = vec2(0.0);
        oSH = vec2(0.0, aSH.y);
        return;
    }
    vec2 pos = aPos;
    vec2 vel = aVel;
    vel.y -= uGravity * uDt;
    if (uSwirl != 0.0) {
        vec2 r = pos - vec2(uOrigin.x * uAspect, uOrigin.y);
        vel.x -= r.y * uSwirl * uDt;
        vel.y += r.x * uSwirl * uDt;
    }
    if (uTurbulence > 0.0) {
        uint k = pcg(floatBitsToUint(aSH.y) ^ (uIndex * 0x9E3779B1u));
        vel.x += (unitOf(k) * 2.0 - 1.0) * uTurbulence * uDt * 4.0;
        vel.y += (unitOf(pcg(k + 1u)) * 2.0 - 1.0) * uTurbulence * uDt * 4.0;
    }
    float dragFactor = max(0.0, 1.0 - uDrag * uDt);
    vel *= dragFactor;
    pos += vel * uDt;
    age += uDt;
    if (age >= life || abs(pos.x) > uAspect * 1.35 || abs(pos.y) > 1.35) {
        oPos = pos;
        oVel = vec2(0.0);
        oAL = vec2(0.0);
        oSH = vec2(0.0, aSH.y);
        return;
    }
    oPos = pos;
    oVel = vel;
    oAL = vec2(age, life);
    oSH = aSH;
}
)glsl";

const char* kUpdateFragment = R"glsl(#version 330 core
out vec4 fragColor;
void main() {
    fragColor = vec4(0.0);
}
)glsl";

const char* kDrawVertex = R"glsl(#version 330 core
layout(location = 0) in vec2 aPos;
layout(location = 2) in vec2 aAL;
layout(location = 3) in vec2 aSH;
uniform float uAspect;
uniform float uScale;
uniform float uSizeEnd;
uniform float uFadeIn;
uniform float uSizeBoost;
uniform vec3 uColorA;
uniform vec3 uColorB;
uniform int uMode;
out vec2 vQuad;
out vec4 vColor;
vec3 rainbow(float h) {
    return clamp(abs(fract(h + vec3(0.0, 2.0 / 3.0, 1.0 / 3.0)) * 6.0 - 3.0) - 1.0, 0.0, 1.0);
}
void main() {
    if (aAL.y <= 0.0) {
        gl_Position = vec4(0.0);
        vQuad = vec2(0.0);
        vColor = vec4(0.0);
        return;
    }
    float t = clamp(aAL.x / aAL.y, 0.0, 1.0);
    float scale = 1.0 + (uSizeEnd - 1.0) * t;
    float fade = uFadeIn > 0.001 ? min(1.0, t / uFadeIn) : 1.0;
    float alpha = fade * (1.0 - t);
    float size = aSH.x * scale * uSizeBoost;
    vec2 corner = vec2(float(gl_VertexID & 1), float((gl_VertexID >> 1) & 1)) * 2.0 - 1.0;
    vec2 p = aPos + corner * size * uScale;
    gl_Position = vec4(p.x / uAspect, p.y, 0.0, 1.0);
    vQuad = corner;
    vec3 c = uMode == 1 ? rainbow(aSH.y) : (uMode == 2 ? vec3(1.0) : mix(uColorA, uColorB, t));
    vColor = vec4(c, alpha);
}
)glsl";

const char* kUpdateNames[] = {"uCapacity", "uHead", "uSpawn", "uSerial", "uIndex", "uSeed", "uLevel", "uAspect", "uDt", "uLife", "uLifeJitter", "uSpeed", "uSpeedJitter", "uDirection", "uSpread", "uGravity", "uDrag", "uTurbulence", "uSwirl", "uSize", "uSizeJitter", "uLevelSpeed", "uExtent", "uOrigin", "uShape"};
const char* kDrawNames[] = {"uAspect", "uScale", "uSizeEnd", "uFadeIn", "uSizeBoost", "uColorA", "uColorB", "uMode", "uSoft"};

enum UpdateSlot { UCapacity, UHead, USpawn, USerial, UIndex, USeed, ULevel, UAspect, UDt, ULife, ULifeJitter, USpeed, USpeedJitter, UDirection, USpread, UGravity, UDrag, UTurbulence, USwirl, USize, USizeJitter, ULevelSpeed, UExtent, UOrigin, UShape };
enum DrawSlot { DAspect, DScale, DSizeEnd, DFadeIn, DSizeBoost, DColorA, DColorB, DMode, DSoft };

GLuint compile(GLenum type, const char* source, std::string& log) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    GLint ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char text[1024] = {};
        glGetShaderInfoLog(shader, sizeof(text) - 1, nullptr, text);
        log = text;
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

GLuint link(GLuint vertex, GLuint fragment, bool feedback, std::string& log) {
    GLuint program = glCreateProgram();
    glAttachShader(program, vertex);
    glAttachShader(program, fragment);
    if (feedback) {
        const char* varyings[] = {"oPos", "oVel", "oAL", "oSH"};
        glTransformFeedbackVaryings(program, 4, varyings, GL_INTERLEAVED_ATTRIBS);
    }
    glLinkProgram(program);
    GLint ok = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    glDetachShader(program, vertex);
    glDetachShader(program, fragment);
    if (!ok) {
        char text[1024] = {};
        glGetProgramInfoLog(program, sizeof(text) - 1, nullptr, text);
        log = text;
        glDeleteProgram(program);
        return 0;
    }
    return program;
}

bool buildPair(const char* vertexSource, const char* fragmentSource, bool feedback, GLuint& program, std::string& log) {
    GLuint vertex = compile(GL_VERTEX_SHADER, vertexSource, log);
    if (!vertex) {
        return false;
    }
    GLuint fragment = compile(GL_FRAGMENT_SHADER, fragmentSource, log);
    if (!fragment) {
        glDeleteShader(vertex);
        return false;
    }
    program = link(vertex, fragment, feedback, log);
    glDeleteShader(vertex);
    glDeleteShader(fragment);
    return program != 0;
}

void setAttributes(GLuint vao, GLuint buffer, GLuint divisor) {
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, buffer);
    const GLsizei stride = static_cast<GLsizei>(gpuparticles::kStateFloats * sizeof(float));
    for (GLuint index = 0; index < 4; ++index) {
        glEnableVertexAttribArray(index);
        glVertexAttribPointer(index, 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const void*>(static_cast<std::uintptr_t>(index * 2 * sizeof(float))));
        glVertexAttribDivisor(index, divisor);
    }
}

}

GpuParticleGl::GpuParticleGl() : budget(std::make_shared<gpuparticles::Budget>()) {}

GpuParticleGl::~GpuParticleGl() {
    release();
}

bool GpuParticleGl::ensure(std::string& error, const char* fragmentSource) {
    if (ready()) {
        return true;
    }
    if (tried_) {
        error = failure_;
        return false;
    }
    tried_ = true;
    if (!glTransformFeedbackVaryings || !glBeginTransformFeedback || !glEndTransformFeedback || !glBindBufferBase || !glVertexAttribDivisor || !glUniform1ui || !glGetBufferSubData) {
        failure_ = "This graphics driver has no transform feedback";
        error = failure_;
        return false;
    }
    std::string log;
    GLuint update = 0;
    GLuint draw = 0;
    if (!buildPair(kUpdateVertex, kUpdateFragment, true, update, log) || !buildPair(kDrawVertex, fragmentSource, false, draw, log)) {
        if (update) {
            glDeleteProgram(update);
        }
        failure_ = "The GPU particle shaders did not compile";
        if (!log.empty()) {
            logLine("GPU particle shaders: " + log);
        }
        error = failure_;
        return false;
    }
    updateProgram = update;
    drawProgram = draw;
    for (int i = 0; i < 25; ++i) {
        u[i] = glGetUniformLocation(updateProgram, kUpdateNames[i]);
    }
    for (int i = 0; i < 9; ++i) {
        d[i] = glGetUniformLocation(drawProgram, kDrawNames[i]);
    }
    return true;
}

void GpuParticleGl::release() {
    if (updateProgram) {
        glDeleteProgram(updateProgram);
        updateProgram = 0;
    }
    if (drawProgram) {
        glDeleteProgram(drawProgram);
        drawProgram = 0;
    }
}

void GpuParticleGl::simulateFailure() {
    release();
    tried_ = true;
    failure_ = "The GPU particle shaders did not compile";
}

void GpuParticleGl::allowRetry() {
    if (!ready()) {
        tried_ = false;
        failure_.clear();
    }
}

GpuParticleSystem::GpuParticleSystem(GpuParticleGl& gl) : gl_(gl), budget_(gl.budget) {}

GpuParticleSystem::~GpuParticleSystem() {
    destroy();
}

bool GpuParticleSystem::create(const particles::Params& params, std::string& error) {
    destroy();
    params_ = params;
    particles::clampParams(params_);
    if (!gl_.ready()) {
        error = "The GPU particle shaders are not available";
        return false;
    }
    const int capacity = gpuparticles::capacityFor(params_.gpuCount, budget_->available());
    if (capacity <= 0) {
        error = "The graphics memory budget for GPU particles is used up";
        return false;
    }
    const std::uint64_t bytes = gpuparticles::bytesFor(capacity);
    if (!budget_->tryReserve(bytes)) {
        error = "The graphics memory budget for GPU particles is used up";
        return false;
    }
    reserved_ = bytes;
    capacity_ = capacity;
    while (glGetError() != GL_NO_ERROR) {
    }
    glGenBuffers(2, buffer_);
    glGenVertexArrays(2, updateVao_);
    glGenVertexArrays(2, drawVao_);
    const GLsizeiptr size = static_cast<GLsizeiptr>(capacity) * static_cast<GLsizeiptr>(gpuparticles::kStateFloats * sizeof(float));
    for (int i = 0; i < 2; ++i) {
        glBindBuffer(GL_ARRAY_BUFFER, buffer_[i]);
        glBufferData(GL_ARRAY_BUFFER, size, nullptr, GL_DYNAMIC_COPY);
        setAttributes(updateVao_[i], buffer_[i], 0);
        setAttributes(drawVao_[i], buffer_[i], 1);
    }
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    if (glGetError() != GL_NO_ERROR) {
        error = "The graphics driver refused the GPU particle buffers";
        destroy();
        return false;
    }
    scheduler_.configure(params_, capacity_);
    scheduler_.reset();
    read_ = 0;
    checked_ = 0;
    healthy_ = true;
    return true;
}

void GpuParticleSystem::destroy() {
    if (buffer_[0]) {
        glDeleteBuffers(2, buffer_);
        glDeleteVertexArrays(2, updateVao_);
        glDeleteVertexArrays(2, drawVao_);
    }
    buffer_[0] = buffer_[1] = 0;
    updateVao_[0] = updateVao_[1] = 0;
    drawVao_[0] = drawVao_[1] = 0;
    if (reserved_) {
        budget_->release(reserved_);
        reserved_ = 0;
    }
    capacity_ = 0;
}

bool GpuParticleSystem::reconfigure(const particles::Params& params) {
    if (!valid() || !healthy_) {
        return false;
    }
    particles::Params next = params;
    particles::clampParams(next);
    if (gpuparticles::capacityFor(next.gpuCount, budget_->available() + reserved_) != capacity_) {
        return false;
    }
    params_ = next;
    reset();
    return true;
}

void GpuParticleSystem::reset() {
    scheduler_.configure(params_, capacity_);
    scheduler_.reset();
    read_ = 0;
}

void GpuParticleSystem::runStep(const gpuparticles::Step& step, float aspect) {
    const particles::Params& p = params_;
    const GLint* l = gl_.u;
    glUniform1ui(l[UCapacity], static_cast<GLuint>(capacity_));
    glUniform1ui(l[UHead], step.head);
    glUniform1ui(l[USpawn], step.spawn);
    glUniform1ui(l[USerial], step.serial);
    glUniform1ui(l[UIndex], step.index);
    glUniform1ui(l[USeed], p.seed);
    glUniform1f(l[ULevel], step.level);
    glUniform1f(l[UAspect], aspect);
    glUniform1f(l[UDt], static_cast<float>(particles::kStep));
    glUniform1f(l[ULife], p.life);
    glUniform1f(l[ULifeJitter], p.lifeJitter);
    glUniform1f(l[USpeed], p.speed);
    glUniform1f(l[USpeedJitter], p.speedJitter);
    glUniform1f(l[UDirection], p.direction);
    glUniform1f(l[USpread], p.spread);
    glUniform1f(l[UGravity], p.gravity);
    glUniform1f(l[UDrag], p.drag);
    glUniform1f(l[UTurbulence], p.turbulence);
    glUniform1f(l[USwirl], p.swirl);
    glUniform1f(l[USize], p.size);
    glUniform1f(l[USizeJitter], p.sizeJitter);
    glUniform1f(l[ULevelSpeed], p.levelSpeed);
    glUniform1f(l[UExtent], p.extent);
    glUniform2f(l[UOrigin], p.originX, p.originY);
    glUniform1i(l[UShape], static_cast<GLint>(p.shape));
    const int write = 1 - read_;
    glBindVertexArray(updateVao_[read_]);
    glBindBufferBase(GL_TRANSFORM_FEEDBACK_BUFFER, 0, buffer_[write]);
    glBeginTransformFeedback(GL_POINTS);
    glDrawArrays(GL_POINTS, 0, static_cast<GLsizei>(step.used));
    glEndTransformFeedback();
    read_ = write;
}

bool GpuParticleSystem::advance(const particles::Drive& drive, float aspect) {
    if (!valid() || !healthy_) {
        return false;
    }
    const int count = scheduler_.advance(drive, steps_);
    if (count == 0) {
        return true;
    }
    glUseProgram(gl_.updateProgram);
    glEnable(GL_RASTERIZER_DISCARD);
    for (const gpuparticles::Step& step : steps_) {
        if (step.used > 0) {
            runStep(step, std::clamp(aspect, 0.25f, 8.0f));
        }
    }
    glDisable(GL_RASTERIZER_DISCARD);
    glBindBufferBase(GL_TRANSFORM_FEEDBACK_BUFFER, 0, 0);
    glBindVertexArray(0);
    glUseProgram(0);
    if (checked_ < 3) {
        ++checked_;
        if (glGetError() != GL_NO_ERROR) {
            healthy_ = false;
            return false;
        }
    }
    return true;
}

bool GpuParticleSystem::draw(const GpuParticleView& view) {
    if (!valid() || !healthy_ || scheduler_.used() <= 0) {
        return valid() && healthy_;
    }
    const particles::Params& p = params_;
    const GLint* l = gl_.d;
    glUseProgram(gl_.drawProgram);
    glUniform1f(l[DAspect], view.aspect);
    glUniform1f(l[DScale], std::max(view.scale, 0.05f));
    glUniform1f(l[DSizeEnd], p.sizeEnd);
    glUniform1f(l[DFadeIn], p.fadeIn);
    glUniform1f(l[DSizeBoost], 1.0f + p.levelSize * std::clamp(view.level, 0.0f, 1.0f));
    glUniform3f(l[DColorA], view.colorA[0], view.colorA[1], view.colorA[2]);
    glUniform3f(l[DColorB], view.colorB[0], view.colorB[1], view.colorB[2]);
    int mode = p.color == particles::ColorMode::Rainbow ? 1 : (p.color == particles::ColorMode::White ? 2 : 0);
    if (view.rainbow) {
        mode = 1;
    }
    glUniform1i(l[DMode], mode);
    glUniform1f(l[DSoft], p.softness);
    glEnable(GL_BLEND);
    if (p.additive) {
        glBlendFuncSeparate(GL_ONE, GL_ONE, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    } else {
        glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    }
    glBindVertexArray(drawVao_[read_]);
    glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, static_cast<GLsizei>(scheduler_.used()));
    glBindVertexArray(0);
    glDisable(GL_BLEND);
    glUseProgram(0);
    return true;
}

bool GpuParticleSystem::readback(std::vector<gpuparticles::State>& out) const {
    out.clear();
    if (!valid()) {
        return false;
    }
    const int used = scheduler_.used();
    out.resize(static_cast<size_t>(used));
    if (used == 0) {
        return true;
    }
    glBindBuffer(GL_ARRAY_BUFFER, buffer_[read_]);
    glGetBufferSubData(GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(used) * static_cast<GLsizeiptr>(sizeof(gpuparticles::State)), out.data());
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    return true;
}

}
