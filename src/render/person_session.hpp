#pragma once

#include "core/live_source.hpp"
#include "core/person_mask.hpp"
#include "media/person_worker.hpp"
#include "render/gl_util.hpp"

#include <atomic>
#include <memory>
#include <string>

namespace nvj {

struct PersonSessionInfo {
    person::State state = person::State::Off;
    PersonStats worker;
    person::Tier tier = person::Tier::Balanced;
    int rate = person::kDefaultRate;
    int maskWidth = 0;
    int maskHeight = 0;
    int persons = 0;
    int foregroundPercent = 0;
    unsigned long long gpuBytes = 0;
    unsigned long long offered = 0;
    unsigned long long accepted = 0;
    unsigned long long rejected = 0;
    unsigned long long uploads = 0;
    double reduceMs = 0.0;
    double analysisMs = 0.0;
    double ageMs = 0.0;
    std::string note;
    bool sourceSeen = false;
};

class PersonSession final : public LiveFrameObserver {
public:
    PersonSession();
    ~PersonSession() override;
    PersonSession(const PersonSession&) = delete;
    PersonSession& operator=(const PersonSession&) = delete;

    void configure(const person::Settings& settings);
    void offer(const LiveFrame& frame) override;
    void update(bool sourceLive);
    void learn();
    void releaseGl();

    GLuint texture() const { return texture_; }
    bool maskReady() const { return texture_ != 0 && maskWidth_ > 0 && state_ != person::State::Failed && state_ != person::State::Off; }
    unsigned long long gpuBytes() const { return static_cast<unsigned long long>(maskWidth_) * static_cast<unsigned long long>(maskHeight_); }
    float sourceAspect() const { return sourceAspect_.load(); }
    const person::Signals& signals() const { return signals_; }
    std::shared_ptr<const person::Result> result() const { return result_; }
    PersonSessionInfo info() const;

private:
    void upload(const person::Result& result);

    PersonWorker worker_;
    person::Settings settings_;
    bool configured_ = false;
    GLuint texture_ = 0;
    int maskWidth_ = 0;
    int maskHeight_ = 0;
    person::State state_ = person::State::Off;
    person::Signals signals_;
    std::shared_ptr<const person::Result> result_;
    unsigned long long uploadedGeneration_ = 0;
    std::atomic<unsigned long long> offered_{0};
    std::atomic<unsigned long long> accepted_{0};
    std::atomic<unsigned long long> rejected_{0};
    unsigned long long uploads_ = 0;
    double lastAccepted_ = -1.0e9;
    std::atomic<double> lastFrameAt_{-1.0e9};
    double lastUpdate_ = -1.0;
    std::atomic<double> reduceMs_{0.0};
    double resultAt_ = 0.0;
    std::atomic<float> sourceAspect_{16.0f / 9.0f};
    std::atomic<bool> sourceSeen_{false};
    person::Picture scratch_;
};

}
