#include "check.hpp"

#include "control/netsync_show.hpp"

#include <cmath>
#include <string>
#include <vector>

namespace {

using namespace nvj;
using namespace nvj::netsync;

class FakeControlHost final : public ControlHost {
public:
    void triggerClip(int, int) override {}
    void triggerColumn(int) override {}
    void clearLayer(int) override {}
    void switchDeck(int) override {}
    void tap() override {}
    void resync() override {}
    bool blackout() const override { return blackoutOn; }
    void setBlackout(bool on) override { blackoutOn = on; }
    bool blackoutOn = false;
};

struct Rig final : public ShowHost {
    Rig() : router(comp, controlHost) {
        for (int i = 0; i < 12; ++i) {
            names.push_back("Clip " + std::to_string(i));
        }
        counts.fill(0);
    }

    float controlValue(int target) const override { return router.value(target); }
    void setControl(int target, float value) override { router.set(target, value, false); }
    void triggerClip(int layer, int column) override {
        Layer& l = comp.layers[static_cast<size_t>(layer)];
        l.activeColumn = l.cells[static_cast<size_t>(column)] >= 0 ? column : -1;
        ++counts[static_cast<size_t>(layer)];
        started.push_back({layer, column});
    }
    void clearLayer(int layer) override {
        comp.layers[static_cast<size_t>(layer)].activeColumn = -1;
        ++cleared;
    }
    bool switchDeck(int deck) override { return selectDeck(comp, deck); }
    std::uint32_t triggerCount(int layer) const override { return counts[static_cast<size_t>(layer)]; }
    std::string clipName(int clip) const override { return clip >= 0 && clip < static_cast<int>(names.size()) ? names[static_cast<size_t>(clip)] : std::string(); }
    int clipIndex(const std::string& name) const override {
        for (size_t i = 0; i < names.size(); ++i) {
            if (names[i] == name) {
                return static_cast<int>(i);
            }
        }
        return -1;
    }

    Composition comp;
    FakeControlHost controlHost;
    ControlRouter router;
    std::vector<std::string> names;
    std::array<std::uint32_t, kLayers> counts{};
    std::vector<std::pair<int, int>> started;
    int cleared = 0;
};

std::vector<Change> diff(const std::vector<float>& before, const std::vector<float>& after) {
    std::vector<Change> changes;
    for (size_t i = 0; i < after.size(); ++i) {
        if (before.empty() || before[i] != after[i]) {
            changes.push_back({static_cast<std::uint16_t>(i), after[i]});
        }
    }
    return changes;
}

int target(const std::string& id) {
    return controlTargetIndex(id);
}

void fillClips(Rig& rig) {
    for (int layer = 0; layer < kLayers; ++layer) {
        for (int column = 0; column < 4; ++column) {
            rig.comp.layers[static_cast<size_t>(layer)].cells[static_cast<size_t>(column)] = layer * 2 + column;
        }
    }
}

void testLayout() {
    const ShowLayout& show = ShowLayout::get();
    CHECK(&show == &ShowLayout::get());
    CHECK(show.layout.total() <= kMaxStateValues);
    CHECK(show.layout.controls == show.targets.size());
    CHECK(show.ids.size() == show.targets.size() + 1);
    CHECK(show.schema != 0);
    const auto& targets = controlTargets();
    for (size_t i = 0; i < show.targets.size(); ++i) {
        const ControlTarget& t = targets[static_cast<size_t>(show.targets[i])];
        CHECK(t.kind != ControlKind::Trigger);
        CHECK(t.item != ControlItem::Bpm);
        CHECK(show.indexOfTarget[static_cast<size_t>(show.targets[i])] == static_cast<int>(i));
    }
    CHECK(show.indexOfTarget[static_cast<size_t>(target("master/bpm"))] == -1);
    CHECK(show.indexOfTarget[static_cast<size_t>(target("layer/1/clip/1"))] == -1);
    CHECK(show.indexOfTarget[static_cast<size_t>(target("layer/2/opacity"))] >= 0);
    CHECK(show.layout.deck() == show.layout.controls);
    CHECK(show.layout.trigger(kLayers - 1) + 1 == show.layout.total());
    std::vector<std::string> other = show.ids;
    other.push_back("extra");
    CHECK(schemaHash(other) != show.schema);
    CHECK(schemaHash(show.ids) == show.schema);
}

void testBuildState() {
    Rig rig;
    fillClips(rig);
    rig.comp.layers[1].opacity = 0.25f;
    rig.comp.master.level = 0.5f;
    rig.comp.layers[2].activeColumn = 3;
    rig.counts[2] = 7;
    rig.controlHost.blackoutOn = true;
    std::vector<float> state;
    buildShowState(rig.comp, rig, state);
    const ShowLayout& show = ShowLayout::get();
    CHECK(state.size() == show.layout.total());
    const size_t opacity = static_cast<size_t>(show.indexOfTarget[static_cast<size_t>(target("layer/2/opacity"))]);
    const size_t level = static_cast<size_t>(show.indexOfTarget[static_cast<size_t>(target("master/level"))]);
    const size_t blackout = static_cast<size_t>(show.indexOfTarget[static_cast<size_t>(target("master/blackout"))]);
    CHECK(std::fabs(state[opacity] - 0.25f) < 1.0e-6f);
    CHECK(std::fabs(state[level] - 0.5f) < 1.0e-6f);
    CHECK(state[blackout] == 1.0f);
    CHECK(state[show.layout.deck()] == 0.0f);
    CHECK(state[show.layout.column(2)] == 3.0f);
    CHECK(state[show.layout.column(0)] == -1.0f);
    CHECK(state[show.layout.trigger(2)] == 7.0f);
    rig.counts[0] = kTriggerCounterMask + 5;
    buildShowState(rig.comp, rig, state);
    CHECK(state[show.layout.trigger(0)] == 4.0f);
}

void testManifest() {
    Rig rig;
    fillClips(rig);
    rig.comp.deckNames[0] = "Opening";
    rig.comp.deckUsed[3] = true;
    const Manifest manifest = buildManifest(rig.comp, rig);
    CHECK(manifest.deckUsed[0] && manifest.deckUsed[3] && !manifest.deckUsed[1]);
    CHECK(manifest.deckNames[0] == "Opening");
    CHECK(manifest.deckNames[3] == "Deck 4");
    CHECK(manifest.deckNames[1].empty());
    CHECK(manifest.deck == 0);
    CHECK(manifest.cells[0] == "Clip 0");
    CHECK(manifest.cells[static_cast<size_t>(kColumns + 2)] == "Clip 4");
    CHECK(manifest.cells[7].empty());
    const std::uint64_t first = manifestFingerprint(manifest);
    rig.comp.layers[0].cells[0] = 9;
    CHECK(manifestFingerprint(buildManifest(rig.comp, rig)) != first);
}

void testRoundTrip() {
    Rig master;
    Rig follower;
    fillClips(master);
    fillClips(follower);
    master.comp.layers[0].opacity = 0.3f;
    master.comp.layers[3].speed = 2.0f;
    master.comp.layers[1].scale = 0.5f;
    master.comp.layers[2].grade.hue = 0.2f;
    master.comp.master.level = 0.7f;
    master.comp.master.grade.contrast = 1.5f;
    master.comp.layers[0].fx[2].params[1] = 0.9f;
    master.comp.layers[0].fx[2].enabled = false;
    master.comp.master.fx[1].params[3] = 0.1f;
    master.comp.modulators.macros[4] = 0.8f;
    master.comp.layers[2].bypass = true;
    master.comp.layers[1].solo = true;
    master.controlHost.blackoutOn = true;
    master.comp.layers[0].activeColumn = 2;
    master.counts[0] = 1;
    std::vector<float> state;
    buildShowState(master.comp, master, state);
    ShowFollower applier;
    applier.apply(diff({}, state), state, Follow{}, follower.comp, follower);
    const ShowLayout& show = ShowLayout::get();
    int mismatched = 0;
    for (const int t : show.targets) {
        if (std::fabs(master.router.value(t) - follower.router.value(t)) > 1.0e-4f) {
            ++mismatched;
        }
    }
    CHECK(mismatched == 0);
    CHECK(follower.controlHost.blackoutOn);
    CHECK(follower.comp.layers[0].activeColumn == 2);
    CHECK(follower.comp.layers[0].opacity > 0.29f && follower.comp.layers[0].opacity < 0.31f);
    CHECK(applier.stats().controls == show.targets.size());
    master.comp.layers[0].opacity = 0.6f;
    std::vector<float> next;
    buildShowState(master.comp, master, next);
    const std::vector<Change> changes = diff(state, next);
    CHECK(changes.size() == 1);
    const std::uint64_t before = applier.stats().controls;
    applier.apply(changes, next, Follow{}, follower.comp, follower);
    CHECK(applier.stats().controls == before + 1);
    CHECK(std::fabs(follower.comp.layers[0].opacity - 0.6f) < 1.0e-4f);
}

void testSnapshotDoesNotRestartSameClip() {
    Rig master;
    Rig follower;
    fillClips(master);
    fillClips(follower);
    master.comp.layers[0].activeColumn = 1;
    master.comp.layers[1].activeColumn = 2;
    follower.comp.layers[0].activeColumn = 1;
    follower.comp.layers[1].activeColumn = 0;
    std::vector<float> state;
    buildShowState(master.comp, master, state);
    ShowFollower applier;
    applier.apply(diff({}, state), state, Follow{}, follower.comp, follower);
    CHECK(follower.started.size() == 1);
    CHECK(follower.started[0].first == 1 && follower.started[0].second == 2);
    CHECK(follower.comp.layers[0].activeColumn == 1);
    master.counts[0] = 1;
    std::vector<float> next;
    buildShowState(master.comp, master, next);
    applier.apply(diff(state, next), next, Follow{}, follower.comp, follower);
    CHECK(follower.started.size() == 2);
    CHECK(follower.started[1].first == 0 && follower.started[1].second == 1);
}

void testClipChangesAndClear() {
    Rig master;
    Rig follower;
    fillClips(master);
    fillClips(follower);
    std::vector<float> state;
    buildShowState(master.comp, master, state);
    ShowFollower applier;
    applier.apply(diff({}, state), state, Follow{}, follower.comp, follower);
    master.comp.layers[2].activeColumn = 3;
    master.counts[2] = 1;
    std::vector<float> next;
    buildShowState(master.comp, master, next);
    applier.apply(diff(state, next), next, Follow{}, follower.comp, follower);
    CHECK(follower.comp.layers[2].activeColumn == 3);
    state = next;
    master.comp.layers[2].activeColumn = -1;
    buildShowState(master.comp, master, next);
    applier.apply(diff(state, next), next, Follow{}, follower.comp, follower);
    CHECK(follower.comp.layers[2].activeColumn == -1);
    CHECK(follower.cleared == 1);
    CHECK(applier.stats().clipsCleared == 1);
}

void testDeckSwitchAndOrder() {
    Rig master;
    Rig follower;
    fillClips(master);
    fillClips(follower);
    master.comp.deckUsed[2] = true;
    follower.comp.deckUsed[2] = true;
    DeckData deck = emptyDeck();
    deck.layers[0].opacity = 0.4f;
    master.comp.decks[2] = deck;
    follower.comp.decks[2] = deck;
    std::vector<float> state;
    buildShowState(master.comp, master, state);
    ShowFollower applier;
    applier.apply(diff({}, state), state, Follow{}, follower.comp, follower);
    selectDeck(master.comp, 2);
    master.comp.layers[0].opacity = 0.9f;
    std::vector<float> next;
    buildShowState(master.comp, master, next);
    applier.apply(diff(state, next), next, Follow{}, follower.comp, follower);
    CHECK(follower.comp.activeDeck == 2);
    CHECK(std::fabs(follower.comp.layers[0].opacity - 0.9f) < 1.0e-4f);
    CHECK(applier.stats().decksSwitched == 1);
}

void testUnusedDeckSkipped() {
    Rig master;
    Rig follower;
    master.comp.deckUsed[5] = true;
    std::vector<float> state;
    buildShowState(master.comp, master, state);
    ShowFollower applier;
    applier.apply(diff({}, state), state, Follow{}, follower.comp, follower);
    selectDeck(master.comp, 5);
    std::vector<float> next;
    buildShowState(master.comp, master, next);
    applier.apply(diff(state, next), next, Follow{}, follower.comp, follower);
    CHECK(follower.comp.activeDeck == 0);
    CHECK(!follower.comp.deckUsed[5]);
    CHECK(applier.stats().decksSkipped == 1);
}

void testGatingByManifest() {
    Rig master;
    Rig follower;
    fillClips(master);
    fillClips(follower);
    follower.comp.layers[0].cells[1] = 11;
    follower.comp.layers[0].cells[2] = -1;
    const Manifest manifest = buildManifest(master.comp, master);
    Manifest received = manifest;
    for (size_t i = 0; i < received.cellKnown.size(); ++i) {
        received.cellKnown[i] = true;
    }
    for (size_t i = 0; i < received.deckKnown.size(); ++i) {
        received.deckKnown[i] = true;
    }
    ShowFollower applier;
    applier.evaluate(received, follower.comp, follower);
    int different = 0;
    int missing = 0;
    for (const Mismatch& m : applier.mismatches()) {
        if (m.kind == MismatchKind::DifferentClip) {
            ++different;
            if (m.column == 1) {
                CHECK(m.layer == 0 && m.name == "Clip 1" && m.local == "Clip 11");
            } else {
                CHECK(m.layer == 0 && m.column == 2 && m.name == "Clip 2" && m.local.empty());
            }
        }
        if (m.kind == MismatchKind::MissingClip) {
            ++missing;
        }
    }
    CHECK(different == 2);
    CHECK(missing == 0);
    CHECK(applier.mismatches().size() == 2);
    master.comp.layers[0].activeColumn = 1;
    master.comp.layers[1].activeColumn = 0;
    std::vector<float> state;
    buildShowState(master.comp, master, state);
    applier.apply(diff({}, state), state, Follow{}, follower.comp, follower);
    CHECK(follower.comp.layers[0].activeColumn == -1);
    CHECK(follower.comp.layers[1].activeColumn == 0);
    CHECK(applier.stats().clipsSkipped == 1);
    CHECK(applier.stats().clipsStarted == 1);
}

void testMissingMedia() {
    Rig master;
    Rig follower;
    master.names.push_back("Video: club.mp4");
    master.comp.layers[1].cells[5] = 12;
    follower.comp.layers[1].cells[5] = -1;
    Manifest received = buildManifest(master.comp, master);
    received.cellKnown.fill(true);
    received.deckKnown.fill(true);
    ShowFollower applier;
    applier.evaluate(received, follower.comp, follower);
    CHECK(applier.mismatches().size() == 1);
    CHECK(applier.mismatches()[0].kind == MismatchKind::MissingClip);
    CHECK(applier.mismatches()[0].layer == 1 && applier.mismatches()[0].column == 5);
    CHECK(applier.mismatches()[0].name == "Video: club.mp4");
    CHECK(applier.mismatches()[0].local.empty());
    follower.comp.layers[1].cells[5] = 3;
    applier.evaluate(received, follower.comp, follower);
    CHECK(applier.mismatches().size() == 1);
    CHECK(applier.mismatches()[0].kind == MismatchKind::MissingClip);
    follower.names.push_back("Video: club.mp4");
    applier.evaluate(received, follower.comp, follower);
    CHECK(applier.mismatches().size() == 1);
    CHECK(applier.mismatches()[0].kind == MismatchKind::DifferentClip);
    follower.comp.layers[1].cells[5] = 12;
    applier.evaluate(received, follower.comp, follower);
    CHECK(applier.mismatches().empty());
}

void testDeckMismatchReported() {
    Rig master;
    Rig follower;
    master.comp.deckUsed[4] = true;
    master.comp.deckNames[4] = "Finale";
    master.comp.deckUsed[6] = true;
    follower.comp.deckUsed[6] = true;
    follower.comp.deckNames[6] = "Other";
    Manifest received = buildManifest(master.comp, master);
    received.cellKnown.fill(true);
    received.deckKnown.fill(true);
    ShowFollower applier;
    applier.evaluate(received, follower.comp, follower);
    int missing = 0;
    int different = 0;
    for (const Mismatch& m : applier.mismatches()) {
        if (m.kind == MismatchKind::MissingDeck && m.deck == 4 && m.name == "Finale") {
            ++missing;
        }
        if (m.kind == MismatchKind::DifferentDeck && m.deck == 6) {
            ++different;
        }
    }
    CHECK(missing == 1);
    CHECK(different == 1);
}

void testFollowFlags() {
    Rig master;
    Rig follower;
    fillClips(master);
    fillClips(follower);
    std::vector<float> state;
    buildShowState(master.comp, master, state);
    ShowFollower applier;
    Follow off;
    off.controls = false;
    applier.apply(diff({}, state), state, off, follower.comp, follower);
    master.comp.layers[0].opacity = 0.2f;
    master.comp.layers[1].activeColumn = 1;
    master.counts[1] = 1;
    std::vector<float> next;
    buildShowState(master.comp, master, next);
    applier.apply(diff(state, next), next, off, follower.comp, follower);
    CHECK(follower.comp.layers[0].opacity == 1.0f);
    CHECK(follower.comp.layers[1].activeColumn == 1);
    CHECK(applier.stats().controls == 0);
    applier.apply({}, next, Follow{}, follower.comp, follower);
    CHECK(std::fabs(follower.comp.layers[0].opacity - 0.2f) < 1.0e-4f);
    Follow noClips;
    noClips.clips = false;
    state = next;
    master.comp.layers[2].activeColumn = 0;
    master.counts[2] = 1;
    buildShowState(master.comp, master, next);
    applier.apply(diff(state, next), next, noClips, follower.comp, follower);
    CHECK(follower.comp.layers[2].activeColumn == -1);
    applier.apply({}, next, Follow{}, follower.comp, follower);
    CHECK(follower.comp.layers[2].activeColumn == 0);
}

void testLocalEditsSurviveUntilMasterChanges() {
    Rig master;
    Rig follower;
    std::vector<float> state;
    buildShowState(master.comp, master, state);
    ShowFollower applier;
    applier.apply(diff({}, state), state, Follow{}, follower.comp, follower);
    follower.comp.layers[0].opacity = 0.1f;
    follower.comp.layers[1].opacity = 0.1f;
    master.comp.layers[1].opacity = 0.8f;
    std::vector<float> next;
    buildShowState(master.comp, master, next);
    applier.apply(diff(state, next), next, Follow{}, follower.comp, follower);
    CHECK(follower.comp.layers[0].opacity == 0.1f);
    CHECK(std::fabs(follower.comp.layers[1].opacity - 0.8f) < 1.0e-4f);
}

void testHostileValues() {
    Rig follower;
    fillClips(follower);
    const ShowLayout& show = ShowLayout::get();
    std::vector<float> state(show.layout.total(), 0.5f);
    state[show.layout.deck()] = 99.0f;
    state[show.layout.column(0)] = 42.0f;
    state[show.layout.column(1)] = std::nanf("");
    state[0] = std::nanf("");
    state[1] = 7.0f;
    state[2] = -3.0f;
    std::vector<Change> changes;
    for (size_t i = 0; i < state.size(); ++i) {
        changes.push_back({static_cast<std::uint16_t>(i), state[i]});
    }
    changes.push_back({60000, 1.0f});
    ShowFollower applier;
    applier.apply(changes, state, Follow{}, follower.comp, follower);
    CHECK(applier.stats().rejected >= 3);
    CHECK(follower.comp.activeDeck == 0);
    CHECK(follower.comp.layers[0].activeColumn == -1);
    CHECK(follower.comp.master.level >= 0.0f && follower.comp.master.level <= 1.0f);
    ShowFollower wrongSize;
    std::vector<float> small(3, 0.0f);
    wrongSize.apply({{0, 1.0f}}, small, Follow{}, follower.comp, follower);
    CHECK(wrongSize.stats().rejected == 1);
}

void testResetStartsSnapshotAgain() {
    Rig master;
    Rig follower;
    fillClips(master);
    fillClips(follower);
    master.comp.layers[0].activeColumn = 2;
    follower.comp.layers[0].activeColumn = 2;
    std::vector<float> state;
    buildShowState(master.comp, master, state);
    ShowFollower applier;
    applier.apply(diff({}, state), state, Follow{}, follower.comp, follower);
    CHECK(follower.started.empty());
    applier.reset();
    follower.comp.layers[0].activeColumn = 1;
    applier.apply(diff({}, state), state, Follow{}, follower.comp, follower);
    CHECK(follower.started.size() == 1);
    CHECK(follower.comp.layers[0].activeColumn == 2);
}

}

int main() {
    testLayout();
    testBuildState();
    testManifest();
    testRoundTrip();
    testSnapshotDoesNotRestartSameClip();
    testClipChangesAndClear();
    testDeckSwitchAndOrder();
    testUnusedDeckSkipped();
    testGatingByManifest();
    testMissingMedia();
    testDeckMismatchReported();
    testFollowFlags();
    testLocalEditsSurviveUntilMasterChanges();
    testHostileValues();
    testResetStartsSnapshotAgain();
    return testing::finish();
}
