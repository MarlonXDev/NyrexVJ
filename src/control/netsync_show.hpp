#pragma once

#include "control/control_targets.hpp"
#include "core/composition.hpp"
#include "core/netsync.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace nvj::netsync {

struct ShowLayout {
    std::vector<int> targets;
    std::vector<std::string> ids;
    std::vector<int> indexOfTarget;
    Layout layout;
    std::uint64_t schema = 0;

    static const ShowLayout& get();
    bool isControl(size_t stateIndex) const { return stateIndex < layout.controls; }
};

struct Follow {
    bool controls = true;
    bool clips = true;
    bool clock = true;
    bool operator==(const Follow&) const = default;
};

class ShowHost {
public:
    virtual ~ShowHost() = default;
    virtual float controlValue(int target) const = 0;
    virtual void setControl(int target, float value) = 0;
    virtual void triggerClip(int layer, int column) = 0;
    virtual void clearLayer(int layer) = 0;
    virtual bool switchDeck(int deck) = 0;
    virtual std::uint32_t triggerCount(int layer) const = 0;
    virtual std::string clipName(int clip) const = 0;
    virtual int clipIndex(const std::string& name) const = 0;
};

constexpr std::uint32_t kTriggerCounterMask = 0xffffff;

void buildShowState(const Composition& comp, const ShowHost& host, std::vector<float>& out);
Manifest buildManifest(const Composition& comp, const ShowHost& host);

enum class MismatchKind { MissingClip, DifferentClip, MissingDeck, DifferentDeck };

struct Mismatch {
    MismatchKind kind = MismatchKind::MissingClip;
    int layer = -1;
    int column = -1;
    int deck = -1;
    std::string name;
    std::string local;
    bool operator==(const Mismatch&) const = default;
};

struct ApplyStats {
    std::uint64_t controls = 0;
    std::uint64_t clipsStarted = 0;
    std::uint64_t clipsCleared = 0;
    std::uint64_t clipsSkipped = 0;
    std::uint64_t decksSwitched = 0;
    std::uint64_t decksSkipped = 0;
    std::uint64_t rejected = 0;
};

class ShowFollower {
public:
    void reset();
    void apply(const std::vector<Change>& changes, const std::vector<float>& state, const Follow& follow, Composition& comp, ShowHost& host);
    void evaluate(const Manifest& manifest, const Composition& comp, const ShowHost& host);

    const std::vector<Mismatch>& mismatches() const { return mismatches_; }
    const ApplyStats& stats() const { return stats_; }

private:
    void applyControl(size_t index, float value, ShowHost& host);
    void applyClips(const std::vector<float>& state, const std::vector<char>& dirtyDeck, const std::vector<char>& dirtyLayer, bool snapshot, Composition& comp, ShowHost& host);
    bool clipAllowed(int layer, int column, const Composition& comp, const ShowHost& host) const;

    bool initialized_ = false;
    Follow lastFollow_;
    std::vector<float> latest_;
    ApplyStats stats_;
    std::vector<Mismatch> mismatches_;
    Manifest manifest_;
    bool haveManifest_ = false;
};

}
