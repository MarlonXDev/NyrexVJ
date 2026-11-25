#include "render/engine.hpp"

#include "core/log.hpp"
#include "render/clip_library.hpp"
#include "render/shader_source.hpp"

#include <algorithm>
#include <chrono>
#include <utility>

namespace nvj {

namespace {

constexpr int kPlaygroundWidth = 480;
constexpr int kPlaygroundHeight = 270;
constexpr double kBuildTimeoutSeconds = 20.0;
constexpr size_t kPlaygroundClip = static_cast<size_t>(-1);
constexpr size_t kMaxErrorBytes = 4000;
constexpr int kProbationReports = 3;

bool parallelAvailable() {
    return GLAD_GL_ARB_parallel_shader_compile != 0 || GLAD_GL_KHR_parallel_shader_compile != 0;
}

std::string shaderLog(GLuint shader) {
    GLint length = 0;
    glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);
    if (length <= 1) {
        return std::string();
    }
    std::vector<char> log(static_cast<size_t>(length), '\0');
    glGetShaderInfoLog(shader, length, nullptr, log.data());
    return std::string(log.data());
}

std::string programLog(GLuint program) {
    GLint length = 0;
    glGetProgramiv(program, GL_INFO_LOG_LENGTH, &length);
    if (length <= 1) {
        return std::string();
    }
    std::vector<char> log(static_cast<size_t>(length), '\0');
    glGetProgramInfoLog(program, length, nullptr, log.data());
    return std::string(log.data());
}

double elapsedSeconds(std::chrono::steady_clock::time_point since) {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - since).count();
}

}

void Engine::requestShaderPlayground(double clock) {
    playground_.wanted = true;
    playground_.clock = clock;
}

void Engine::submitShaderPlayground(const std::string& text, std::uint64_t revision) {
    Playground& pg = playground_;
    pg.queuedText = text;
    pg.queuedRevision = revision;
    pg.queued = true;
}

void Engine::restartShaderPlayground() {
    playground_.restart = true;
}

void Engine::resumeShaderPlayground() {
    playground_.governor.resume();
    playground_.status.paused = false;
    playground_.status.throttled = false;
    playground_.status.interval = 1;
}

void Engine::destroyPlaygroundBuild() {
    Playground& pg = playground_;
    for (PlaygroundPending& item : pg.pending) {
        if (item.vs) {
            glDeleteShader(item.vs);
        }
        if (item.fs) {
            glDeleteShader(item.fs);
        }
        if (item.program) {
            glDeleteProgram(item.program);
        }
    }
    pg.pending.clear();
    pg.building = false;
    pg.buildPlan.reset();
}

void Engine::releaseShaderPlayground() {
    Playground& pg = playground_;
    destroyPlaygroundBuild();
    releaseClipProgram(pg.clip);
    pg.clip = ClipProgram{};
    pg.plan.reset();
    pg.state.destroy();
    pg.target.destroy();
    if (pg.queries[0]) {
        glDeleteQueries(2, pg.queries.data());
    }
    pg.queries = {};
    pg.queryPending = {};
    pg.queryStale = {};
    pg.hasFrame = false;
    pg.restart = false;
    pg.queued = false;
    pg.queuedText.clear();
    pg.governor.reset();
    pg.status = ShaderPlaygroundStatus{};
}

void Engine::startPlaygroundBuild() {
    Playground& pg = playground_;
    ShaderPlaygroundStatus& status = pg.status;
    pg.queued = false;
    const std::string text = std::move(pg.queuedText);
    pg.queuedText.clear();
    pg.buildRevision = pg.queuedRevision;
    pg.buildStarted = std::chrono::steady_clock::now();
    status.attemptedRevision = pg.buildRevision;
    status.filter = false;
    if (text.size() > shaderedit::kMaxTextBytes) {
        status.error = "The shader text is larger than " + std::to_string(shaderedit::kMaxTextBytes / 1024) + " KiB and was not compiled";
        status.failures++;
        status.lastCompileMs = 0.0;
        return;
    }
    const ShaderSource prepared = prepareShaderSource(text);
    if (!prepared.ok) {
        status.error = prepared.error.size() > kMaxErrorBytes ? prepared.error.substr(0, kMaxErrorBytes) : prepared.error;
        status.failures++;
        status.lastCompileMs = 0.0;
        return;
    }
    if (prepared.plan && prepared.plan->filter) {
        status.filter = true;
        status.error.clear();
        return;
    }
    std::vector<std::pair<std::string, std::string>> sources;
    if (prepared.plan && !prepared.plan->passes.empty()) {
        for (int p = 0; p < static_cast<int>(prepared.plan->passes.size()); ++p) {
            sources.emplace_back(prepared.plan->passes[static_cast<size_t>(p)].label(), planPassFragmentSource(*prepared.plan, prepared.shadertoy, p));
        }
    } else {
        ClipDef def;
        def.kind = ClipKind::ShaderFile;
        def.body = prepared.body;
        def.shadertoy = prepared.shadertoy;
        sources.emplace_back("Image", clipFragmentSource(def));
    }
    const bool advanced = prepared.plan && !prepared.plan->passes.empty();
    destroyPlaygroundBuild();
    pg.buildShadertoy = prepared.shadertoy;
    if (advanced) {
        pg.buildPlan = prepared.plan;
    }
    while (glGetError() != GL_NO_ERROR) {
    }
    const std::string& vertex = clipVertexSource();
    for (const auto& source : sources) {
        PlaygroundPending item;
        item.label = source.first;
        item.vs = glCreateShader(GL_VERTEX_SHADER);
        const char* vertexText = vertex.c_str();
        glShaderSource(item.vs, 1, &vertexText, nullptr);
        glCompileShader(item.vs);
        item.fs = glCreateShader(GL_FRAGMENT_SHADER);
        const char* fragmentText = source.second.c_str();
        glShaderSource(item.fs, 1, &fragmentText, nullptr);
        glCompileShader(item.fs);
        item.program = glCreateProgram();
        glAttachShader(item.program, item.vs);
        glAttachShader(item.program, item.fs);
        glLinkProgram(item.program);
        pg.pending.push_back(item);
    }
    pg.building = true;
    status.compiling = true;
    if (parallelAvailable()) {
        if (glMaxShaderCompilerThreadsARB) {
            glMaxShaderCompilerThreadsARB(0xFFFFFFFFu);
        } else if (glMaxShaderCompilerThreadsKHR) {
            glMaxShaderCompilerThreadsKHR(0xFFFFFFFFu);
        }
    }
}

bool Engine::pollPlaygroundBuild() {
    Playground& pg = playground_;
    if (!pg.building) {
        return true;
    }
    if (parallelAvailable()) {
        bool allDone = true;
        for (const PlaygroundPending& item : pg.pending) {
            GLint done = 0;
            glGetProgramiv(item.program, GL_COMPLETION_STATUS_ARB, &done);
            if (!done) {
                allDone = false;
                break;
            }
        }
        if (!allDone) {
            if (elapsedSeconds(pg.buildStarted) > kBuildTimeoutSeconds) {
                finishPlaygroundBuild(false, "The graphics driver needed more than 20 seconds to compile this shader and the attempt was given up\n");
                return true;
            }
            return false;
        }
    }
    std::string errors;
    bool ok = true;
    for (const PlaygroundPending& item : pg.pending) {
        GLint vertexOk = 0;
        GLint fragmentOk = 0;
        GLint linked = 0;
        glGetShaderiv(item.vs, GL_COMPILE_STATUS, &vertexOk);
        glGetShaderiv(item.fs, GL_COMPILE_STATUS, &fragmentOk);
        glGetProgramiv(item.program, GL_LINK_STATUS, &linked);
        if (!vertexOk || !fragmentOk || !linked) {
            ok = false;
            std::string log = shaderLog(item.vs) + shaderLog(item.fs);
            if (vertexOk && fragmentOk && !linked) {
                log += programLog(item.program);
            }
            if (log.empty()) {
                log = "error: the graphics driver gave no message\n";
            }
            errors += item.label + ":\n" + log + "\n";
        }
    }
    finishPlaygroundBuild(ok, errors);
    return true;
}

void Engine::finishPlaygroundBuild(bool success, const std::string& errors) {
    Playground& pg = playground_;
    ShaderPlaygroundStatus& status = pg.status;
    status.lastCompileMs = elapsedSeconds(pg.buildStarted) * 1000.0;
    status.compiling = false;
    if (!success) {
        status.error = errors.size() > kMaxErrorBytes ? errors.substr(0, kMaxErrorBytes) : errors;
        status.failures++;
        destroyPlaygroundBuild();
        return;
    }
    ClipProgram next;
    next.tried = true;
    if (pg.buildPlan) {
        for (const PlaygroundPending& item : pg.pending) {
            next.passes.push_back(passProgramFor(item.program, *pg.buildPlan));
        }
    } else {
        const GLuint program = pg.pending.front().program;
        next.id = program;
        next.time = glGetUniformLocation(program, "uTime");
        next.beat = glGetUniformLocation(program, "uBeat");
        next.bpm = glGetUniformLocation(program, "uBpm");
        next.res = glGetUniformLocation(program, "uRes");
        next.colorA = glGetUniformLocation(program, "uColorA");
        next.colorB = glGetUniformLocation(program, "uColorB");
        next.rainbow = glGetUniformLocation(program, "uRainbow");
        next.scale = glGetUniformLocation(program, "uScale");
        next.audio = glGetUniformLocation(program, "uAudio");
        next.kick = glGetUniformLocation(program, "uKick");
        next.audioTex = glGetUniformLocation(program, "uAudioTex");
    }
    for (PlaygroundPending& item : pg.pending) {
        glDeleteShader(item.vs);
        glDeleteShader(item.fs);
        item.vs = 0;
        item.fs = 0;
        item.program = 0;
    }
    pg.pending.clear();
    releaseClipProgram(pg.clip);
    pg.clip = std::move(next);
    pg.plan = pg.buildPlan;
    pg.buildPlan.reset();
    pg.building = false;
    pg.state.destroy();
    pg.restart = true;
    pg.governor.reset();
    pg.queryStale = pg.queryPending;
    status.hasWorking = true;
    status.workingRevision = pg.buildRevision;
    status.error.clear();
    status.runtimeError.clear();
    status.note.clear();
    status.compiles++;
    status.shadertoy = pg.buildShadertoy;
    status.advanced = pg.plan != nullptr;
    status.passCount = pg.plan ? static_cast<int>(pg.plan->passes.size()) : 1;
    status.alpha = pg.plan && pg.plan->alpha;
    status.paused = false;
    status.throttled = false;
    status.interval = 1;
}

void Engine::drawPlayground() {
    Playground& pg = playground_;
    ShaderPlaygroundStatus& status = pg.status;
    if (!pg.target.fbo && !pg.target.create(kPlaygroundWidth, kPlaygroundHeight)) {
        pg.target.destroy();
        status.runtimeError = "The preview picture could not be created";
        return;
    }
    const Layer defaults;
    const float beat = static_cast<float>(beat_);
    const GLboolean blendWas = glIsEnabled(GL_BLEND);
    pg.target.bind();
    glDisable(GL_BLEND);
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    if (pg.plan) {
        const int width = pg.target.width;
        const int height = pg.target.height;
        if (!pg.state.matches(kPlaygroundClip, width, height)) {
            std::string error;
            std::string note;
            if (!allocateShaderState(pg.state, kPlaygroundClip, *pg.plan, width, height, error, note)) {
                status.runtimeError = error;
                pg.state.destroy();
                pg.hasFrame = true;
                if (blendWas) {
                    glEnable(GL_BLEND);
                }
                return;
            }
            status.note = note;
            pg.state.lastClock = pg.clock;
            pg.restart = false;
        } else if (pg.restart || pg.clock < pg.state.lastClock - kShaderRestartBackSeconds) {
            pg.state.clear();
            pg.restart = false;
        }
        status.runtimeError.clear();
        runShaderPasses(*pg.plan, pg.clip.passes, pg.state, defaults.colorA, defaults.colorB, false, 1.0f, pg.clock, beat, comp.bpm, pg.target, true, resolveShaderValues(*pg.plan, unsetShaderValues()), nullptr, 1.0f);
        pg.state.lastDrawn = frameId_;
        pg.state.lastClock = pg.clock;
    } else {
        pg.restart = false;
        status.runtimeError.clear();
        drawClip(pg.clip, static_cast<float>(pg.clock), beat, comp.bpm, static_cast<float>(pg.target.width), static_cast<float>(pg.target.height), defaults.colorA, defaults.colorB, false, 1.0f);
    }
    if (blendWas) {
        glEnable(GL_BLEND);
    }
    pg.hasFrame = true;
}

void Engine::servicePlayground() {
    Playground& pg = playground_;
    if (!pg.wanted) {
        if (pg.target.fbo || pg.building || pg.clip.ready() || pg.status.hasWorking || pg.status.active) {
            releaseShaderPlayground();
        }
        return;
    }
    pg.wanted = false;
    ShaderPlaygroundStatus& status = pg.status;
    status.active = true;
    status.parallelCompile = parallelAvailable();
    if (!pg.building && pg.queued) {
        startPlaygroundBuild();
    }
    if (pg.building) {
        pollPlaygroundBuild();
    }
    status.compiling = pg.building;
    if (!pg.clip.ready()) {
        return;
    }
    for (size_t slot = 0; slot < pg.queries.size(); ++slot) {
        if (!pg.queryPending[slot]) {
            continue;
        }
        GLint available = 0;
        glGetQueryObjectiv(pg.queries[slot], GL_QUERY_RESULT_AVAILABLE, &available);
        if (available) {
            GLuint64 nanoseconds = 0;
            glGetQueryObjectui64v(pg.queries[slot], GL_QUERY_RESULT, &nanoseconds);
            pg.queryPending[slot] = false;
            if (pg.queryStale[slot]) {
                pg.queryStale[slot] = false;
            } else {
                pg.governor.report(static_cast<double>(nanoseconds) / 1.0e6);
            }
        }
    }
    status.previewMs = pg.governor.averageMs();
    status.paused = pg.governor.state() == shaderedit::PreviewGovernor::State::Paused;
    status.throttled = pg.governor.state() == shaderedit::PreviewGovernor::State::Throttled;
    status.interval = pg.governor.interval();
    if (pg.governor.reports() < kProbationReports && (pg.queryPending[0] || pg.queryPending[1])) {
        return;
    }
    if (!pg.governor.shouldDraw()) {
        return;
    }
    if (!pg.queries[0]) {
        glGenQueries(2, pg.queries.data());
    }
    const size_t slot = static_cast<size_t>(pg.querySlot);
    const bool timed = pg.queries[slot] != 0 && !pg.queryPending[slot];
    if (timed) {
        glBeginQuery(GL_TIME_ELAPSED, pg.queries[slot]);
    }
    drawPlayground();
    if (timed) {
        glEndQuery(GL_TIME_ELAPSED);
        pg.queryPending[slot] = true;
        pg.querySlot = 1 - pg.querySlot;
    }
    status.frames++;
}

}
