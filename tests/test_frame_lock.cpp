#include "check.hpp"
#include "core/frame_lock.hpp"
#include "core/output_map.hpp"
#include "core/output_map_json.hpp"

#include <cmath>
#include <set>
#include <vector>

namespace {

void testClamp() {
    CHECK(nvj::clampDelay(-5) == 0);
    CHECK(nvj::clampDelay(0) == 0);
    CHECK(nvj::clampDelay(3) == 3);
    CHECK(nvj::clampDelay(nvj::kMaxDelayFrames) == nvj::kMaxDelayFrames);
    CHECK(nvj::clampDelay(1000) == nvj::kMaxDelayFrames);
}

void testSyncBars() {
    CHECK(nvj::syncBarValue(0, 0) == 0);
    CHECK(nvj::syncBarValue(5, 0) == 5);
    CHECK(nvj::syncBarValue(5, 2) == 3);
    CHECK(nvj::syncBarValue(1, 5) == 0);
    CHECK(nvj::syncBarValue(4096, 0) == 0);
    CHECK(nvj::syncBarValue(4097 + 3, 3) == 1);
    CHECK(nvj::syncBarValue(10, 100) == 2);

    for (int value : {0, 1, 2, 5, 64, 1000, 2048, 4095}) {
        int decoded = 0;
        for (int column = 0; column < nvj::kSyncBarBits; ++column) {
            decoded = (decoded << 1) | (nvj::syncBarBit(value, column) ? 1 : 0);
        }
        CHECK(decoded == value);
    }
    CHECK(!nvj::syncBarBit(4095, -1));
    CHECK(!nvj::syncBarBit(4095, nvj::kSyncBarBits));
    CHECK(nvj::syncBarBit(2048, 0));
    CHECK(!nvj::syncBarBit(2048, 1));
    CHECK(nvj::syncBarBit(1, nvj::kSyncBarBits - 1));

    std::set<int> seen;
    for (long long frame = 100; frame < 100 + 4096; ++frame) {
        seen.insert(nvj::syncBarValue(frame, 0));
    }
    CHECK(seen.size() == 4096);
}

void testHistoryDisabled() {
    nvj::FrameHistory history;
    CHECK(history.slots() == 0);
    CHECK(history.push() == -1);
    CHECK(history.slotFor(0) == -1);
    history.configure(0);
    CHECK(history.slots() == 0);
    CHECK(history.slotFor(2) == -1);
}

void testHistoryRing() {
    nvj::FrameHistory history;
    history.configure(2);
    CHECK(history.slots() == 3);
    CHECK(history.maxDelay() == 2);
    CHECK(history.slotFor(0) == -1);

    std::vector<int> slotOfFrame;
    for (int frame = 0; frame < 10; ++frame) {
        const int slot = history.push();
        CHECK(slot >= 0 && slot < 3);
        slotOfFrame.push_back(slot);
        CHECK(history.slotFor(0) == slot);
        for (int delay = 1; delay <= 2; ++delay) {
            const int available = std::min(delay, frame);
            CHECK(history.slotFor(delay) == slotOfFrame[static_cast<size_t>(frame - available)]);
        }
        CHECK(history.filled() == std::min(frame + 1, 3));
    }
    CHECK(history.slotFor(2) != history.slotFor(0));
    CHECK(history.slotFor(1) != history.slotFor(0));
    CHECK(history.slotFor(1) != history.slotFor(2));
    CHECK(history.slotFor(50) == history.slotFor(2));
    CHECK(history.slotFor(-3) == history.slotFor(0));
}

void testHistoryReconfigure() {
    nvj::FrameHistory history;
    history.configure(4);
    for (int i = 0; i < 6; ++i) {
        history.push();
    }
    CHECK(history.filled() == 5);
    history.configure(4);
    CHECK(history.filled() == 5);
    history.configure(1);
    CHECK(history.slots() == 2);
    CHECK(history.filled() == 0);
    CHECK(history.slotFor(0) == -1);
    history.push();
    CHECK(history.slotFor(1) == history.slotFor(0));
    history.push();
    CHECK(history.slotFor(1) != history.slotFor(0));
    history.configure(100);
    CHECK(history.maxDelay() == nvj::kMaxDelayFrames);
    CHECK(history.slots() == nvj::kMaxDelayFrames + 1);
    history.clear();
    CHECK(history.filled() == 0);
    history.configure(0);
    CHECK(history.slots() == 0);
    CHECK(history.push() == -1);
}

void testSkew() {
    nvj::SkewTracker skew;
    CHECK(skew.count() == 0);
    CHECK(skew.average() == 0.0 && skew.minimum() == 0.0 && skew.maximum() == 0.0);
    skew.add(1.0);
    skew.add(3.0);
    skew.add(2.0);
    CHECK(skew.count() == 3);
    CHECK(std::abs(skew.average() - 2.0) < 1e-9);
    CHECK(skew.minimum() == 1.0 && skew.maximum() == 3.0);
    CHECK(skew.last() == 2.0);
    skew.add(-4.0);
    CHECK(skew.last() == 0.0 && skew.minimum() == 0.0);
    CHECK(skew.total() == 4);

    skew.reset();
    CHECK(skew.count() == 0 && skew.total() == 0 && skew.last() == 0.0);

    skew.add(50.0);
    for (int i = 0; i < nvj::kSkewWindow; ++i) {
        skew.add(0.5);
    }
    CHECK(skew.count() == nvj::kSkewWindow);
    CHECK(skew.maximum() == 0.5);
    CHECK(std::abs(skew.average() - 0.5) < 1e-9);
    CHECK(skew.total() == static_cast<unsigned long long>(nvj::kSkewWindow) + 1);
}

void testOutputDelayJson() {
    nvj::OutputSpec spec = nvj::defaultOutputSpec(1);
    CHECK(spec.delayFrames == 0);
    spec.delayFrames = 5;
    nlohmann::json doc = nvj::outputToJson(spec);
    CHECK(doc["delayFrames"] == 5);
    nvj::OutputSpec back = nvj::defaultOutputSpec(1);
    CHECK(nvj::outputFromJson(doc, back));
    CHECK(back.delayFrames == 5);
    CHECK(back == spec);

    doc["delayFrames"] = 99;
    CHECK(nvj::outputFromJson(doc, back));
    CHECK(back.delayFrames == nvj::kMaxDelayFrames);
    doc["delayFrames"] = -3;
    CHECK(nvj::outputFromJson(doc, back));
    CHECK(back.delayFrames == 0);
    doc["delayFrames"] = "many";
    back.delayFrames = 4;
    CHECK(nvj::outputFromJson(doc, back));
    CHECK(back.delayFrames == 0);
    doc.erase("delayFrames");
    back.delayFrames = 4;
    CHECK(nvj::outputFromJson(doc, back));
    CHECK(back.delayFrames == 0);

    nvj::OutputSpec loose;
    loose.delayFrames = 400;
    nvj::normalizeSpec(loose);
    CHECK(loose.delayFrames == nvj::kMaxDelayFrames);
    loose.delayFrames = -1;
    nvj::normalizeSpec(loose);
    CHECK(loose.delayFrames == 0);

    nvj::OutputSpec a = nvj::defaultOutputSpec(0);
    nvj::OutputSpec b = a;
    b.delayFrames = 1;
    CHECK(!(a == b));
}

}

int main() {
    testClamp();
    testSyncBars();
    testHistoryDisabled();
    testHistoryRing();
    testHistoryReconfigure();
    testSkew();
    testOutputDelayJson();
    return testing::finish();
}
