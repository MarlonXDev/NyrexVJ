#include "control/netsync_show.hpp"

#include <algorithm>
#include <cmath>

namespace nvj::netsync {

namespace {

ShowLayout buildLayout() {
    ShowLayout result;
    const auto& targets = controlTargets();
    result.indexOfTarget.assign(targets.size(), -1);
    result.ids.push_back("nyrexvj-show-state/1");
    for (size_t i = 0; i < targets.size(); ++i) {
        const ControlTarget& target = targets[i];
        if (target.kind == ControlKind::Trigger || target.item == ControlItem::Bpm) {
            continue;
        }
        result.indexOfTarget[i] = static_cast<int>(result.targets.size());
        result.targets.push_back(static_cast<int>(i));
        result.ids.push_back(target.id);
    }
    result.layout.controls = result.targets.size();
    result.schema = schemaHash(result.ids);
    return result;
}

std::string cellName(const Composition& comp, const ShowHost& host, int layer, int column) {
    const int clip = comp.layers[static_cast<size_t>(layer)].cells[static_cast<size_t>(column)];
    if (clip < 0) {
        return std::string();
    }
    return limitName(host.clipName(clip));
}

}

const ShowLayout& ShowLayout::get() {
    static const ShowLayout layout = buildLayout();
    return layout;
}

void buildShowState(const Composition& comp, const ShowHost& host, std::vector<float>& out) {
    const ShowLayout& show = ShowLayout::get();
    out.assign(show.layout.total(), 0.0f);
    for (size_t i = 0; i < show.targets.size(); ++i) {
        out[i] = host.controlValue(show.targets[i]);
    }
    out[show.layout.deck()] = static_cast<float>(comp.activeDeck);
    for (int layer = 0; layer < kLayers; ++layer) {
        out[show.layout.column(layer)] = static_cast<float>(comp.layers[static_cast<size_t>(layer)].activeColumn);
        out[show.layout.trigger(layer)] = static_cast<float>(host.triggerCount(layer) & kTriggerCounterMask);
    }
}

Manifest buildManifest(const Composition& comp, const ShowHost& host) {
    Manifest manifest;
    for (int deck = 0; deck < kDecks; ++deck) {
        const size_t i = static_cast<size_t>(deck);
        manifest.deckUsed[i] = comp.deckUsed[i];
        manifest.deckNames[i] = comp.deckUsed[i] ? limitName(deckDisplayName(comp, deck)) : std::string();
    }
    manifest.deck = comp.activeDeck;
    for (int layer = 0; layer < kLayers; ++layer) {
        for (int column = 0; column < kColumns; ++column) {
            manifest.cells[static_cast<size_t>(layer * kColumns + column)] = cellName(comp, host, layer, column);
        }
    }
    return manifest;
}

void ShowFollower::reset() {
    initialized_ = false;
    lastFollow_ = Follow{};
    latest_.clear();
    mismatches_.clear();
    haveManifest_ = false;
    manifest_ = Manifest{};
}

void ShowFollower::applyControl(size_t index, float value, ShowHost& host) {
    const ShowLayout& show = ShowLayout::get();
    if (index >= show.targets.size() || !std::isfinite(value)) {
        ++stats_.rejected;
        return;
    }
    host.setControl(show.targets[index], std::clamp(value, 0.0f, 1.0f));
    ++stats_.controls;
}

bool ShowFollower::clipAllowed(int layer, int column, const Composition& comp, const ShowHost& host) const {
    const int local = comp.layers[static_cast<size_t>(layer)].cells[static_cast<size_t>(column)];
    if (local < 0) {
        return false;
    }
    if (!haveManifest_) {
        return true;
    }
    if (manifest_.deck != comp.activeDeck) {
        return true;
    }
    const size_t cell = static_cast<size_t>(layer * kColumns + column);
    if (!manifest_.cellKnown[cell]) {
        return true;
    }
    return limitName(host.clipName(local)) == manifest_.cells[cell];
}

void ShowFollower::applyClips(const std::vector<float>& state, const std::vector<char>& dirtyDeck, const std::vector<char>& dirtyLayer, bool snapshot, Composition& comp, ShowHost& host) {
    const ShowLayout& show = ShowLayout::get();
    if (!dirtyDeck.empty() && dirtyDeck[0] != 0) {
        const float value = state[show.layout.deck()];
        const int deck = std::isfinite(value) ? static_cast<int>(std::lround(value)) : -1;
        if (deck < 0 || deck >= kDecks) {
            ++stats_.rejected;
        } else if (deck != comp.activeDeck) {
            if (comp.deckUsed[static_cast<size_t>(deck)]) {
                if (host.switchDeck(deck)) {
                    ++stats_.decksSwitched;
                } else {
                    ++stats_.decksSkipped;
                }
            } else {
                ++stats_.decksSkipped;
            }
        }
    }
    for (int layer = 0; layer < kLayers; ++layer) {
        if (dirtyLayer[static_cast<size_t>(layer)] == 0) {
            continue;
        }
        const float value = state[show.layout.column(layer)];
        const int column = std::isfinite(value) ? static_cast<int>(std::lround(value)) : -2;
        if (column < -1 || column >= kColumns) {
            ++stats_.rejected;
            continue;
        }
        const int local = comp.layers[static_cast<size_t>(layer)].activeColumn;
        if (column < 0) {
            if (local >= 0) {
                host.clearLayer(layer);
                ++stats_.clipsCleared;
            }
            continue;
        }
        if (snapshot && local == column) {
            continue;
        }
        if (!clipAllowed(layer, column, comp, host)) {
            ++stats_.clipsSkipped;
            continue;
        }
        host.triggerClip(layer, column);
        ++stats_.clipsStarted;
    }
}

void ShowFollower::apply(const std::vector<Change>& changes, const std::vector<float>& state, const Follow& follow, Composition& comp, ShowHost& host) {
    const ShowLayout& show = ShowLayout::get();
    if (state.size() != show.layout.total()) {
        if (!changes.empty()) {
            ++stats_.rejected;
        }
        return;
    }
    const bool snapshot = !initialized_;
    initialized_ = true;
    std::vector<char> controlDirty(show.layout.controls, 0);
    std::vector<char> dirtyDeck(1, 0);
    std::vector<char> dirtyLayer(kLayers, 0);
    auto markAll = [&](bool controls, bool clips) {
        if (controls) {
            std::fill(controlDirty.begin(), controlDirty.end(), static_cast<char>(1));
        }
        if (clips) {
            dirtyDeck[0] = 1;
            std::fill(dirtyLayer.begin(), dirtyLayer.end(), static_cast<char>(1));
        }
    };
    if (snapshot) {
        markAll(follow.controls, follow.clips);
    } else {
        markAll(follow.controls && !lastFollow_.controls, follow.clips && !lastFollow_.clips);
        for (const Change& change : changes) {
            const size_t index = change.index;
            if (index < show.layout.controls) {
                if (follow.controls) {
                    controlDirty[index] = 1;
                }
            } else if (follow.clips) {
                if (index == show.layout.deck()) {
                    dirtyDeck[0] = 1;
                } else {
                    for (int layer = 0; layer < kLayers; ++layer) {
                        if (index == show.layout.column(layer) || index == show.layout.trigger(layer)) {
                            dirtyLayer[static_cast<size_t>(layer)] = 1;
                        }
                    }
                }
            }
        }
    }
    lastFollow_ = follow;
    latest_ = state;
    if (follow.clips) {
        applyClips(state, dirtyDeck, dirtyLayer, snapshot, comp, host);
    }
    if (follow.controls) {
        for (size_t i = 0; i < controlDirty.size(); ++i) {
            if (controlDirty[i] != 0) {
                applyControl(i, state[i], host);
            }
        }
    }
}

void ShowFollower::evaluate(const Manifest& manifest, const Composition& comp, const ShowHost& host) {
    manifest_ = manifest;
    haveManifest_ = true;
    mismatches_.clear();
    for (int deck = 0; deck < kDecks; ++deck) {
        const size_t i = static_cast<size_t>(deck);
        if (!manifest.deckKnown[i] || !manifest.deckUsed[i]) {
            continue;
        }
        if (!comp.deckUsed[i]) {
            Mismatch mismatch;
            mismatch.kind = MismatchKind::MissingDeck;
            mismatch.deck = deck;
            mismatch.name = manifest.deckNames[i];
            mismatches_.push_back(std::move(mismatch));
        } else if (limitName(deckDisplayName(comp, deck)) != manifest.deckNames[i]) {
            Mismatch mismatch;
            mismatch.kind = MismatchKind::DifferentDeck;
            mismatch.deck = deck;
            mismatch.name = manifest.deckNames[i];
            mismatch.local = limitName(deckDisplayName(comp, deck));
            mismatches_.push_back(std::move(mismatch));
        }
    }
    if (manifest.deck != comp.activeDeck) {
        return;
    }
    for (int layer = 0; layer < kLayers; ++layer) {
        for (int column = 0; column < kColumns; ++column) {
            const size_t cell = static_cast<size_t>(layer * kColumns + column);
            if (!manifest.cellKnown[cell] || manifest.cells[cell].empty()) {
                continue;
            }
            const std::string local = cellName(comp, host, layer, column);
            if (local == manifest.cells[cell]) {
                continue;
            }
            Mismatch mismatch;
            mismatch.layer = layer;
            mismatch.column = column;
            mismatch.name = manifest.cells[cell];
            mismatch.local = local;
            mismatch.kind = host.clipIndex(manifest.cells[cell]) < 0 ? MismatchKind::MissingClip : MismatchKind::DifferentClip;
            mismatches_.push_back(std::move(mismatch));
        }
    }
}

}
