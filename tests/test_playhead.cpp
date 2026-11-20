#include "core/playhead.hpp"

#include "check.hpp"

#include <cmath>
#include <vector>

#define CHECK(expr) testing::check((expr), #expr, __LINE__)

namespace {

constexpr double kPeriod = 0.04;

bool near(double a, double b, double tolerance = 1e-6) {
    return std::fabs(a - b) <= tolerance;
}

nvj::Playhead make(const nvj::ClipPlayback& playback, double duration = 10.0) {
    nvj::Playhead head;
    head.configure(playback, duration, kPeriod);
    head.restart();
    return head;
}

void run(nvj::Playhead& head, double seconds, double dt = 0.01) {
    for (double t = 0.0; t < seconds - 1e-9; t += dt) {
        head.advance(dt, 0.0);
    }
}

void testNames() {
    CHECK(nvj::loopModeFromName("pingpong") == nvj::LoopMode::PingPong);
    CHECK(nvj::loopModeFromName("hold") == nvj::LoopMode::OnceHold);
    CHECK(nvj::loopModeFromName("stop") == nvj::LoopMode::OnceStop);
    CHECK(nvj::loopModeFromName("loop") == nvj::LoopMode::Loop);
    CHECK(nvj::loopModeFromName("nonsense") == nvj::LoopMode::Loop);
    for (int i = 0; i < nvj::kLoopModeCount; ++i) {
        const nvj::LoopMode mode = static_cast<nvj::LoopMode>(i);
        CHECK(nvj::loopModeFromName(nvj::loopModeName(mode)) == mode);
    }
    CHECK(nvj::syncBeatsChoice(0) == 0.0f);
    CHECK(nvj::syncBeatsChoice(5) == 4.0f);
    CHECK(nvj::syncBeatsChoice(-3) == 0.0f);
    CHECK(nvj::syncBeatsChoice(999) == 128.0f);
    CHECK(nvj::syncBeatsIndex(4.0f) == 5);
    CHECK(nvj::syncBeatsIndex(3.9f) == 5);
    CHECK(nvj::syncBeatsIndex(-1.0f) == 0);
}

void testClamping() {
    nvj::ClipPlayback bad;
    bad.in = -5.0;
    bad.out = -1.0;
    bad.speed = 99.0f;
    bad.syncBeats = -2.0f;
    const nvj::ClipPlayback fixed = nvj::clampedPlayback(bad);
    CHECK(fixed.in == 0.0 && fixed.out == 0.0);
    CHECK(fixed.speed == nvj::kMaxPlaySpeed);
    CHECK(fixed.syncBeats == 0.0f);
    bad.speed = -99.0f;
    CHECK(nvj::clampedPlayback(bad).speed == nvj::kMinPlaySpeed);
    bad.speed = NAN;
    CHECK(nvj::clampedPlayback(bad).speed == 1.0f);
    bad.in = INFINITY;
    CHECK(nvj::clampedPlayback(bad).in == 0.0);
    CHECK(nvj::ClipPlayback{}.isDefault());
    nvj::ClipPlayback changed;
    changed.speed = 2.0f;
    CHECK(!changed.isDefault());

    const nvj::PlayRange full = nvj::resolveRange(nvj::ClipPlayback{}, 10.0, kPeriod);
    CHECK(near(full.in, 0.0) && near(full.out, 10.0));
    nvj::ClipPlayback custom;
    custom.in = 2.0;
    custom.out = 5.0;
    const nvj::PlayRange range = nvj::resolveRange(custom, 10.0, kPeriod);
    CHECK(near(range.in, 2.0) && near(range.out, 5.0) && near(range.length(), 3.0));
    custom.out = 99.0;
    CHECK(near(nvj::resolveRange(custom, 10.0, kPeriod).out, 10.0));
    custom.in = 9.999;
    custom.out = 1.0;
    const nvj::PlayRange tiny = nvj::resolveRange(custom, 10.0, kPeriod);
    CHECK(tiny.length() >= kPeriod - 1e-9 && tiny.in >= 0.0 && tiny.out <= 10.0);
    custom.in = 4.0;
    custom.out = 4.0;
    const nvj::PlayRange equal = nvj::resolveRange(custom, 10.0, kPeriod);
    CHECK(equal.length() >= kPeriod - 1e-9);
    const nvj::PlayRange none = nvj::resolveRange(custom, 0.0, kPeriod);
    CHECK(none.length() == 0.0);
}

void testLoopForward() {
    nvj::ClipPlayback playback;
    nvj::Playhead head = make(playback);
    CHECK(near(head.position(), 0.0));
    CHECK(!head.reverse());
    run(head, 4.0);
    CHECK(near(head.position(), 4.0, 1e-6));
    CHECK(near(head.timeline(), 4.0, 1e-6));
    run(head, 7.0);
    CHECK(near(head.position(), 1.0, 1e-6));
    CHECK(near(head.timeline(), 11.0, 1e-6));
    CHECK(!head.finished() && head.visible());
    double previous = head.timeline();
    bool monotonic = true;
    for (int i = 0; i < 3000; ++i) {
        head.advance(0.01, 0.0);
        monotonic = monotonic && head.timeline() >= previous;
        previous = head.timeline();
    }
    CHECK(monotonic);
    nvj::ClipPlayback range;
    range.in = 2.0;
    range.out = 4.0;
    nvj::Playhead ranged = make(range);
    CHECK(near(ranged.position(), 2.0));
    run(ranged, 1.0);
    CHECK(near(ranged.position(), 3.0));
    run(ranged, 1.5);
    CHECK(near(ranged.position(), 2.5, 1e-6));
    CHECK(near(ranged.timeline(), 4.5 - 2.0, 1e-6));
    bool inside = true;
    for (int i = 0; i < 2000; ++i) {
        ranged.advance(0.0137, 0.0);
        inside = inside && ranged.position() >= 2.0 && ranged.position() < 4.0;
    }
    CHECK(inside);
}

void testLoopReverse() {
    nvj::ClipPlayback playback;
    playback.speed = -1.0f;
    nvj::Playhead head = make(playback);
    CHECK(head.reverse());
    CHECK(head.position() > 9.9 && head.position() < 10.0);
    run(head, 3.0);
    CHECK(near(head.position(), 10.0 - 0.02 - 3.0, 1e-6));
    run(head, 8.0);
    CHECK(head.position() >= 0.0 && head.position() < 10.0);
    CHECK(near(head.timeline(), head.position()));
    nvj::ClipPlayback range;
    range.in = 1.0;
    range.out = 3.0;
    range.speed = -2.0f;
    nvj::Playhead ranged = make(range);
    bool inside = true;
    for (int i = 0; i < 3000; ++i) {
        ranged.advance(0.0113, 0.0);
        inside = inside && ranged.position() >= 1.0 && ranged.position() < 3.0;
    }
    CHECK(inside);
}

void testPingPong() {
    nvj::ClipPlayback playback;
    playback.loop = nvj::LoopMode::PingPong;
    playback.in = 1.0;
    playback.out = 3.0;
    nvj::Playhead head = make(playback);
    CHECK(!head.reverse());
    run(head, 1.0);
    CHECK(near(head.position(), 2.0));
    run(head, 0.97);
    CHECK(!head.reverse());
    run(head, 0.05);
    CHECK(head.reverse());
    CHECK(head.position() < 3.0 && head.position() > 2.9);
    run(head, 1.0);
    CHECK(head.reverse());
    run(head, 1.5);
    CHECK(!head.reverse());
    bool inside = true;
    for (int i = 0; i < 5000; ++i) {
        head.advance(0.0171, 0.0);
        inside = inside && head.position() >= 1.0 - 1e-9 && head.position() <= 3.0;
    }
    CHECK(inside);
    nvj::ClipPlayback negative = playback;
    negative.speed = -1.0f;
    nvj::Playhead back = make(negative);
    CHECK(back.reverse());
    CHECK(back.position() > 2.9);
    run(back, 0.5);
    CHECK(back.reverse());
    run(back, 2.0);
    CHECK(!back.reverse());
    nvj::Playhead jump = make(playback);
    jump.advance(1000.0, 0.0);
    CHECK(jump.position() >= 1.0 && jump.position() <= 3.0);
}

void testOnce() {
    nvj::ClipPlayback hold;
    hold.loop = nvj::LoopMode::OnceHold;
    hold.in = 1.0;
    hold.out = 2.0;
    nvj::Playhead head = make(hold);
    run(head, 0.5);
    CHECK(!head.finished());
    run(head, 1.0);
    CHECK(head.finished() && head.visible() && head.paused());
    const double end = head.position();
    CHECK(end < 2.0 && end > 1.9);
    run(head, 1.0);
    CHECK(near(head.position(), end));
    head.restart();
    CHECK(!head.finished() && near(head.position(), 1.0));

    nvj::ClipPlayback stop = hold;
    stop.loop = nvj::LoopMode::OnceStop;
    nvj::Playhead stopped = make(stop);
    CHECK(stopped.visible());
    run(stopped, 2.0);
    CHECK(stopped.finished() && !stopped.visible());
    stopped.restart();
    CHECK(stopped.visible() && !stopped.finished());

    nvj::ClipPlayback reverse = hold;
    reverse.speed = -1.0f;
    nvj::Playhead back = make(reverse);
    CHECK(back.reverse() && back.position() > 1.9);
    run(back, 0.5);
    CHECK(!back.finished());
    run(back, 1.0);
    CHECK(back.finished() && near(back.position(), 1.0));
}

void testSpeedAndPause() {
    nvj::ClipPlayback playback;
    playback.speed = 2.0f;
    nvj::Playhead fast = make(playback);
    run(fast, 1.0);
    CHECK(near(fast.position(), 2.0, 1e-6));
    nvj::ClipPlayback quarter;
    quarter.speed = 0.25f;
    nvj::Playhead slow = make(quarter);
    run(slow, 4.0);
    CHECK(near(slow.position(), 1.0, 1e-6));
    nvj::ClipPlayback frozen;
    frozen.speed = 0.0f;
    nvj::Playhead still = make(frozen);
    CHECK(still.paused());
    run(still, 5.0);
    CHECK(near(still.position(), 0.0));
    still.seek(3.0);
    run(still, 5.0);
    CHECK(near(still.position(), 3.0));
    nvj::ClipPlayback extreme;
    extreme.speed = 4.0f;
    nvj::Playhead x4 = make(extreme);
    run(x4, 2.0);
    CHECK(near(x4.position(), 8.0, 1e-6));
}

void testSeekAndStep() {
    nvj::ClipPlayback playback;
    playback.in = 2.0;
    playback.out = 6.0;
    nvj::Playhead head = make(playback);
    head.seek(4.5);
    CHECK(near(head.position(), 4.5));
    CHECK(near(head.timeline(), 2.5));
    head.seek(-3.0);
    CHECK(near(head.position(), 2.0));
    head.seek(99.0);
    CHECK(head.position() < 6.0 && head.position() > 5.9);
    head.seek(3.0);
    head.step(5);
    CHECK(near(head.position(), 3.0 + 5 * kPeriod));
    head.step(-10);
    CHECK(near(head.position(), 3.0 - 5 * kPeriod));
    head.step(-1000);
    CHECK(near(head.position(), 2.0));
    head.step(1000);
    CHECK(head.position() > 5.9);
}

void testBeatSync() {
    nvj::ClipPlayback playback;
    playback.syncBeats = 4.0f;
    playback.in = 0.0;
    playback.out = 8.0;
    nvj::Playhead head = make(playback);
    head.advance(5.0, 0.0);
    CHECK(near(head.position(), 0.0));
    head.advance(0.0, 1.0);
    CHECK(near(head.position(), 2.0, 1e-9));
    head.advance(0.0, 3.0);
    CHECK(near(head.position(), 0.0, 1e-9));
    for (int i = 0; i < 100; ++i) {
        head.advance(0.0, 0.25);
    }
    CHECK(near(head.position(), 2.0, 1e-6));
    nvj::ClipPlayback twice = playback;
    twice.speed = 2.0f;
    nvj::Playhead fast = make(twice);
    fast.advance(0.0, 1.0);
    CHECK(near(fast.position(), 4.0, 1e-9));
    nvj::ClipPlayback back = playback;
    back.speed = -1.0f;
    nvj::Playhead reverse = make(back);
    const double start = reverse.position();
    reverse.advance(0.0, 1.0);
    CHECK(near(reverse.position(), start - 2.0, 1e-9));
    nvj::Playhead steady = make(playback);
    steady.advance(0.0, 2.0);
    const double halfway = steady.position();
    nvj::Playhead pieces = make(playback);
    for (int i = 0; i < 200; ++i) {
        pieces.advance(0.0, 0.01);
    }
    CHECK(near(halfway, pieces.position(), 1e-6));
}

void testDegenerate() {
    nvj::Playhead head;
    head.configure(nvj::ClipPlayback{}, 0.0, kPeriod);
    head.restart();
    head.advance(1.0, 1.0);
    CHECK(near(head.position(), 0.0) && near(head.timeline(), 0.0));
    head.seek(3.0);
    CHECK(near(head.position(), 0.0));
    head.step(3);
    CHECK(near(head.position(), 0.0));
    nvj::ClipPlayback playback;
    nvj::Playhead short_clip;
    short_clip.configure(playback, 0.03, kPeriod);
    short_clip.restart();
    short_clip.advance(5.0, 0.0);
    CHECK(short_clip.position() >= 0.0 && short_clip.position() <= 0.03);
    nvj::Playhead reconfigure = make(playback);
    reconfigure.seek(9.0);
    nvj::ClipPlayback narrowed;
    narrowed.in = 1.0;
    narrowed.out = 2.0;
    reconfigure.configure(narrowed, 10.0, kPeriod);
    CHECK(reconfigure.position() >= 1.0 && reconfigure.position() <= 2.0);
    narrowed.loop = nvj::LoopMode::OnceHold;
    reconfigure.configure(narrowed, 10.0, kPeriod);
    reconfigure.advance(10.0, 0.0);
    CHECK(reconfigure.finished());
    narrowed.loop = nvj::LoopMode::Loop;
    reconfigure.configure(narrowed, 10.0, kPeriod);
    CHECK(!reconfigure.finished());
}

}

int main() {
    testNames();
    testClamping();
    testLoopForward();
    testLoopReverse();
    testPingPong();
    testOnce();
    testSpeedAndPause();
    testSeekAndStep();
    testBeatSync();
    testDegenerate();
    return testing::finish();
}
