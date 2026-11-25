#pragma once

#include "core/gpu_particles.hpp"
#include "core/particles.hpp"
#include "render/gl_util.hpp"

#include <array>
#include <memory>
#include <string>
#include <vector>

namespace nvj {

struct GpuParticleView {
    float aspect = 16.0f / 9.0f;
    float scale = 1.0f;
    float level = 0.0f;
    std::array<float, 3> colorA{1.0f, 1.0f, 1.0f};
    std::array<float, 3> colorB{1.0f, 1.0f, 1.0f};
    bool rainbow = false;
};

class GpuParticleGl {
public:
    GpuParticleGl();
    ~GpuParticleGl();

    bool ensure(std::string& error, const char* fragmentSource);
    void release();
    bool ready() const { return updateProgram != 0 && drawProgram != 0; }
    bool failed() const { return tried_ && !ready(); }
    const std::string& failure() const { return failure_; }
    void simulateFailure();
    void allowRetry();

    GLuint updateProgram = 0;
    GLuint drawProgram = 0;
    GLint u[25] = {};
    GLint d[9] = {};
    std::shared_ptr<gpuparticles::Budget> budget;

private:
    bool tried_ = false;
    std::string failure_;
};

class GpuParticleSystem {
public:
    GpuParticleSystem(GpuParticleGl& gl);
    ~GpuParticleSystem();
    GpuParticleSystem(const GpuParticleSystem&) = delete;
    GpuParticleSystem& operator=(const GpuParticleSystem&) = delete;

    bool create(const particles::Params& params, std::string& error);
    void destroy();
    bool reconfigure(const particles::Params& params);
    void reset();
    bool advance(const particles::Drive& drive, float aspect);
    bool draw(const GpuParticleView& view);
    bool readback(std::vector<gpuparticles::State>& out) const;

    bool valid() const { return buffer_[0] != 0; }
    int capacity() const { return capacity_; }
    int used() const { return scheduler_.used(); }
    std::uint64_t spawned() const { return scheduler_.spawned(); }
    std::uint64_t recycled() const { return scheduler_.recycled(); }
    std::size_t gpuBytes() const { return valid() ? gpuparticles::bytesFor(capacity_) : 0; }
    const particles::Params& params() const { return params_; }
    const std::vector<gpuparticles::Step>& lastSteps() const { return steps_; }
    bool healthy() const { return healthy_; }

private:
    void runStep(const gpuparticles::Step& step, float aspect);

    GpuParticleGl& gl_;
    std::shared_ptr<gpuparticles::Budget> budget_;
    particles::Params params_;
    gpuparticles::Scheduler scheduler_;
    std::vector<gpuparticles::Step> steps_;
    GLuint buffer_[2] = {0, 0};
    GLuint updateVao_[2] = {0, 0};
    GLuint drawVao_[2] = {0, 0};
    int read_ = 0;
    int capacity_ = 0;
    std::uint64_t reserved_ = 0;
    int checked_ = 0;
    bool healthy_ = true;
};

}
