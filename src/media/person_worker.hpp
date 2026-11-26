#pragma once

#include "core/person_mask.hpp"
#include "core/stoppable_thread.hpp"

#include <memory>
#include <string>

namespace nvj {

struct PersonStats {
    person::Tier requestedTier = person::Tier::Balanced;
    person::Tier tier = person::Tier::Balanced;
    int requestedRate = person::kDefaultRate;
    int rate = person::kDefaultRate;
    bool disabled = false;
    unsigned long long submitted = 0;
    unsigned long long completed = 0;
    unsigned long long failed = 0;
    unsigned long long superseded = 0;
    unsigned long long lowered = 0;
    unsigned long long lostTicks = 0;
    double lastMs = 0.0;
    double averageMs = 0.0;
    double p95Ms = 0.0;
    double budgetMs = 0.0;
    int width = 0;
    int height = 0;
    person::State state = person::State::Off;
    std::string lastError;
    std::string provenance;
};

class PersonWorker {
public:
    PersonWorker();
    ~PersonWorker();
    PersonWorker(const PersonWorker&) = delete;
    PersonWorker& operator=(const PersonWorker&) = delete;

    void configure(const person::Settings& settings);
    bool submit(person::Picture&& picture);
    void lost(double seconds);
    void learn();
    std::shared_ptr<const person::Result> latest() const;
    PersonStats stats() const;
    person::Tier tier() const;
    int rate() const;
    bool idle() const;
    bool waitForGeneration(unsigned long long generation, int milliseconds) const;

private:
    struct Impl;
    std::shared_ptr<Impl> impl_;
    StoppableThread thread_;
};

}
