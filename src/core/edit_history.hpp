#pragma once

#include "core/composition.hpp"

#include <array>
#include <deque>
#include <string>

namespace nvj {

class EditHistory {
public:
    static constexpr size_t kLimit = 64;
    static constexpr double kSettleSeconds = 0.35;

    void reset(const Composition& comp);
    void observe(const Composition& comp, double dt, bool interacting);
    bool undo(Composition& comp, int clipCount);
    bool redo(Composition& comp, int clipCount);
    bool canUndo() const { return !undo_.empty() || dirty_; }
    bool canRedo() const { return !redo_.empty(); }
    size_t undoCount() const { return undo_.size(); }
    size_t redoCount() const { return redo_.size(); }

private:
    struct State {
        DeckSettings deckSettings;
        std::array<Lfo, kLfoCount> lfos;
        int beatsPerBar = 4;
        std::array<bool, kDeckCount> deckUsed{};
        std::array<std::string, kDeckCount> deckNames;
        std::array<DeckData, kDeckCount> decks;
        bool operator==(const State&) const = default;
    };

    static State capture(const Composition& comp);
    static void apply(Composition& comp, const State& state, int clipCount);
    void commit(const State& current);
    bool step(Composition& comp, int clipCount, std::deque<State>& from, std::deque<State>& to);

    std::deque<State> undo_;
    std::deque<State> redo_;
    State baseline_;
    State last_;
    bool ready_ = false;
    bool dirty_ = false;
    double settle_ = 0.0;
};

}
