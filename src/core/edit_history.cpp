#include "core/edit_history.hpp"

#include <algorithm>
#include <utility>

namespace nvj {

EditHistory::State EditHistory::capture(const Composition& comp) {
    State state;
    state.deckSettings = comp.deckSettings;
    state.deckUsed = comp.deckUsed;
    state.deckNames = comp.deckNames;
    state.decks = comp.decks;
    const size_t active = static_cast<size_t>(std::clamp(comp.activeDeck, 0, kDeckCount - 1));
    state.decks[active] = captureDeck(comp);
    for (DeckData& deck : state.decks) {
        for (Layer& layer : deck.layers) {
            layer.activeColumn = -1;
        }
    }
    return state;
}

void EditHistory::apply(Composition& comp, const State& state, int clipCount) {
    const size_t active = static_cast<size_t>(std::clamp(comp.activeDeck, 0, kDeckCount - 1));
    comp.deckSettings = state.deckSettings;
    comp.deckUsed = state.deckUsed;
    comp.deckNames = state.deckNames;
    for (size_t d = 0; d < static_cast<size_t>(kDeckCount); ++d) {
        for (size_t l = 0; l < static_cast<size_t>(kLayerCount); ++l) {
            const int keep = comp.decks[d].layers[l].activeColumn;
            comp.decks[d].layers[l] = state.decks[d].layers[l];
            comp.decks[d].layers[l].activeColumn = keep;
        }
        comp.decks[d].master = state.decks[d].master;
    }
    comp.master = state.decks[active].master;
    for (size_t l = 0; l < static_cast<size_t>(kLayerCount); ++l) {
        const int keep = comp.layers[l].activeColumn;
        comp.layers[l] = state.decks[active].layers[l];
        comp.layers[l].activeColumn = keep;
    }
    auto sanitize = [clipCount](Layer& layer) {
        for (int& cell : layer.cells) {
            if (cell >= clipCount) {
                cell = -1;
            }
        }
    };
    for (Layer& layer : comp.layers) {
        sanitize(layer);
    }
    for (DeckData& deck : comp.decks) {
        for (Layer& layer : deck.layers) {
            sanitize(layer);
        }
    }
    normalizeDecks(comp);
}

void EditHistory::reset(const Composition& comp) {
    undo_.clear();
    redo_.clear();
    baseline_ = capture(comp);
    last_ = baseline_;
    ready_ = true;
    dirty_ = false;
    settle_ = 0.0;
}

void EditHistory::commit(const State& current) {
    if (!(current == baseline_)) {
        undo_.push_back(std::move(baseline_));
        while (undo_.size() > kLimit) {
            undo_.pop_front();
        }
        redo_.clear();
        baseline_ = current;
    }
    last_ = current;
    dirty_ = false;
    settle_ = 0.0;
}

void EditHistory::observe(const Composition& comp, double dt, bool interacting) {
    if (!ready_) {
        reset(comp);
        return;
    }
    State current = capture(comp);
    if (!(current == last_)) {
        last_ = std::move(current);
        settle_ = 0.0;
        dirty_ = !(last_ == baseline_);
        return;
    }
    settle_ += dt;
    if (dirty_ && !interacting && settle_ >= kSettleSeconds) {
        commit(last_);
    }
}

bool EditHistory::step(Composition& comp, int clipCount, std::deque<State>& from, std::deque<State>& to) {
    if (!ready_) {
        reset(comp);
    }
    commit(capture(comp));
    if (&from == &redo_ ? redo_.empty() : undo_.empty()) {
        return false;
    }
    to.push_back(std::move(baseline_));
    while (to.size() > kLimit) {
        to.pop_front();
    }
    baseline_ = std::move(from.back());
    from.pop_back();
    apply(comp, baseline_, clipCount);
    baseline_ = capture(comp);
    last_ = baseline_;
    dirty_ = false;
    settle_ = 0.0;
    return true;
}

bool EditHistory::undo(Composition& comp, int clipCount) {
    return step(comp, clipCount, undo_, redo_);
}

bool EditHistory::redo(Composition& comp, int clipCount) {
    return step(comp, clipCount, redo_, undo_);
}

}
