#include "check.hpp"

#include "core/routing.hpp"

#include <algorithm>
#include <cstdint>
#include <random>
#include <set>
#include <string>
#include <vector>

namespace {

using namespace nvj;
using namespace nvj::routing;

RenderedLayers rendering(unsigned mask) {
    RenderedLayers layers{};
    for (int l = 0; l < kLayerCount; ++l) {
        layers[static_cast<size_t>(l)] = (mask >> l) & 1u;
    }
    return layers;
}

Consumer layerConsumer(int layer, const Source& source) {
    return Consumer{ConsumerKind::Layer, layer, source};
}

Consumer senderConsumer(int index, const Source& source) {
    return Consumer{ConsumerKind::Sender, index, source};
}

void testSlots() {
    CHECK(kSlotCount == 8);
    std::set<int> seen;
    for (int slot = 0; slot < kSlotCount; ++slot) {
        const Source source = sourceOfSlot(slot);
        CHECK(validSource(source));
        CHECK(!source.previousFrame);
        CHECK(slotOf(source) == slot);
        seen.insert(slot);
    }
    CHECK(seen.size() == static_cast<size_t>(kSlotCount));
    CHECK(slotOf(Source{SourceKind::Layer, -1, false}) == -1);
    CHECK(slotOf(Source{SourceKind::Layer, kLayerCount, false}) == -1);
    CHECK(slotOf(Source{SourceKind::Group, 1, false}) == -1);
    CHECK(slotOf(Source{SourceKind::Group, kLayerCount + 1, false}) == -1);
    CHECK(slotOf(Source{SourceKind::Master, 1, false}) == -1);
    CHECK(!validSource(Source{SourceKind::Group, 0, false}));
    CHECK(sourceOfSlot(-5).kind == SourceKind::Master);
    CHECK(sourceOfSlot(99).kind == SourceKind::Master);
    CHECK(sourceOfSlot(0).kind == SourceKind::Layer);
    CHECK(sourceOfSlot(kLayerCount).kind == SourceKind::Group);
    CHECK(sourceOfSlot(kLayerCount).index == 2);
    CHECK(sourceOfSlot(kSlotCount - 2).index == kLayerCount);
}

void testGroups() {
    const Source group{SourceKind::Group, 3, false};
    CHECK(groupLastLayer(group) == 2);
    CHECK(includesLayer(group, 0));
    CHECK(includesLayer(group, 2));
    CHECK(!includesLayer(group, 3));
    CHECK(!includesLayer(group, -1));
    CHECK(groupLastLayer(Source{SourceKind::Layer, 1, false}) == -1);
    CHECK(includesLayer(Source{SourceKind::Layer, 1, false}, 1));
    CHECK(!includesLayer(Source{SourceKind::Layer, 1, false}, 0));
    CHECK(includesLayer(Source{SourceKind::Master, 0, false}, 3));
    CHECK(!includesLayer(Source{SourceKind::Master, 0, false}, 4));
}

void testNames() {
    const std::vector<Source>& list = clipSources();
    CHECK(list.size() == 13);
    std::set<std::string> names;
    std::set<int> slots;
    for (size_t i = 0; i < list.size(); ++i) {
        const std::string name = sourceName(list[i]);
        CHECK(!name.empty());
        names.insert(name);
        Source parsed;
        CHECK(parseSourceName(name, parsed));
        CHECK(parsed == list[i]);
        CHECK(clipSourceIndex(list[i]) == static_cast<int>(i));
        CHECK(validSource(list[i]));
        slots.insert(slotOf(list[i]));
    }
    CHECK(names.size() == list.size());
    CHECK(slots.size() == static_cast<size_t>(kSlotCount));
    CHECK(sourceName(Source{SourceKind::Layer, 0, false}) == "Route: Layer 1");
    CHECK(sourceName(Source{SourceKind::Layer, 3, true}) == "Feedback: Layer 4");
    CHECK(sourceName(Source{SourceKind::Group, 2, false}) == "Route: Group 1-2");
    CHECK(sourceName(Source{SourceKind::Group, 4, true}) == "Feedback: Group 1-4");
    CHECK(sourceName(Source{SourceKind::Master, 0, true}) == "Feedback: Master");
    CHECK(sourceName(Source{SourceKind::Layer, 9, false}).empty());
    Source none;
    CHECK(!parseSourceName("", none));
    CHECK(!parseSourceName("Route: Layer 4", none));
    CHECK(!parseSourceName("Route: Master", none));
    CHECK(!parseSourceName("route: layer 1", none));
    CHECK(!parseSourceName("Route: Layer 1 ", none));
    CHECK(!parseSourceName("Spout Input 1", none));
    CHECK(clipSourceIndex(Source{SourceKind::Layer, 3, false}) == -1);
    CHECK(clipSourceIndex(Source{SourceKind::Master, 0, false}) == -1);
    int same = 0;
    int feedback = 0;
    for (const Source& source : list) {
        (source.previousFrame ? feedback : same)++;
    }
    CHECK(same == 5);
    CHECK(feedback == 8);
}

void testSenderKeys() {
    const std::vector<Source>& choices = senderChoices();
    CHECK(choices.size() == 8);
    std::set<std::string> keys;
    for (const Source& source : choices) {
        CHECK(validSource(source));
        CHECK(!source.previousFrame);
        const std::string key = senderKey(source);
        keys.insert(key);
        Source parsed;
        CHECK(parseSenderKey(key, parsed));
        CHECK(parsed == source);
        CHECK(!senderLabel(source).empty());
    }
    CHECK(keys.size() == choices.size());
    CHECK(choices.front().kind == SourceKind::Master);
    CHECK(senderKey(Source{SourceKind::Master, 0, false}) == "master");
    CHECK(senderKey(Source{SourceKind::Layer, 1, false}) == "layer2");
    CHECK(senderKey(Source{SourceKind::Group, 3, false}) == "group13");
    CHECK(senderKey(Source{SourceKind::Layer, 1, true}) == "master");
    CHECK(senderKey(Source{SourceKind::Layer, 77, false}) == "master");
    CHECK(senderLabel(Source{SourceKind::Group, 4, false}) == "Group 1-4");
    CHECK(senderLabel(Source{SourceKind::Layer, 0, false}) == "Layer 1");
    CHECK(senderLabel(Source{SourceKind::Master, 0, false}) == "Master");
    Source parsed;
    CHECK(!parseSenderKey("", parsed));
    CHECK(!parseSenderKey("Master", parsed));
    CHECK(!parseSenderKey("layer5", parsed));
    CHECK(!parseSenderKey("layer0", parsed));
    CHECK(!parseSenderKey("group11", parsed));
    CHECK(!parseSenderKey("group15", parsed));
    CHECK(!parseSenderKey(std::string(5000, 'x'), parsed));
    CHECK(std::string(senderName(SenderKind::Spout)) == "Spout");
    CHECK(std::string(senderName(SenderKind::Ndi)) == "NDI");
    CHECK(std::string(senderName(SenderKind::Recording)) == "Recording");
}

void testClassifyLayers() {
    const RenderedLayers all = rendering(15);
    for (int consumer = 0; consumer < kLayerCount; ++consumer) {
        for (int source = 0; source < kLayerCount; ++source) {
            const Status same = classify(layerConsumer(consumer, Source{SourceKind::Layer, source, false}), all);
            CHECK(same == (source < consumer ? Status::Ok : Status::Cycle));
            const Status previous = classify(layerConsumer(consumer, Source{SourceKind::Layer, source, true}), all);
            CHECK(previous == Status::Feedback);
        }
        for (int layers = 2; layers <= kLayerCount; ++layers) {
            const Status same = classify(layerConsumer(consumer, Source{SourceKind::Group, layers, false}), all);
            CHECK(same == (layers <= consumer ? Status::Ok : Status::Cycle));
            const Status previous = classify(layerConsumer(consumer, Source{SourceKind::Group, layers, true}), all);
            CHECK(previous == Status::Feedback);
        }
        CHECK(classify(layerConsumer(consumer, Source{SourceKind::Master, 0, false}), all) == Status::Cycle);
        CHECK(classify(layerConsumer(consumer, Source{SourceKind::Master, 0, true}), all) == Status::Feedback);
    }
    CHECK(classify(layerConsumer(-1, Source{SourceKind::Layer, 0, true}), all) == Status::Invalid);
    CHECK(classify(layerConsumer(kLayerCount, Source{SourceKind::Layer, 0, true}), all) == Status::Invalid);
    CHECK(classify(layerConsumer(1, Source{SourceKind::Layer, 9, false}), all) == Status::Invalid);
    CHECK(classify(layerConsumer(1, Source{SourceKind::Group, 1, false}), all) == Status::Invalid);
    CHECK(classify(layerConsumer(1, Source{SourceKind::Master, 5, true}), all) == Status::Invalid);
}

void testClassifyInactive() {
    for (unsigned mask = 0; mask < 16; ++mask) {
        const RenderedLayers rendered = rendering(mask);
        for (int consumer = 1; consumer < kLayerCount; ++consumer) {
            for (int source = 0; source < consumer; ++source) {
                const Status status = classify(layerConsumer(consumer, Source{SourceKind::Layer, source, false}), rendered);
                CHECK(status == (rendered[static_cast<size_t>(source)] ? Status::Ok : Status::Inactive));
            }
            for (int layers = 2; layers <= consumer; ++layers) {
                const bool any = (mask & ((1u << layers) - 1u)) != 0;
                const Status status = classify(layerConsumer(consumer, Source{SourceKind::Group, layers, false}), rendered);
                CHECK(status == (any ? Status::Ok : Status::Inactive));
            }
        }
        for (int source = 0; source < kLayerCount; ++source) {
            const Status previous = classify(layerConsumer(0, Source{SourceKind::Layer, source, true}), rendered);
            CHECK(previous == (rendered[static_cast<size_t>(source)] ? Status::Feedback : Status::Inactive));
        }
        CHECK(classify(layerConsumer(2, Source{SourceKind::Master, 0, true}), rendered) == Status::Feedback);
    }
}

void testClassifySenders() {
    for (unsigned mask = 0; mask < 16; ++mask) {
        const RenderedLayers rendered = rendering(mask);
        for (const Source& source : senderChoices()) {
            const Status status = classify(senderConsumer(0, source), rendered);
            bool expected = true;
            if (source.kind == SourceKind::Layer) {
                expected = rendered[static_cast<size_t>(source.index)];
            } else if (source.kind == SourceKind::Group) {
                expected = (mask & ((1u << source.index) - 1u)) != 0;
            }
            CHECK(status == (expected ? Status::Ok : Status::Inactive));
        }
        CHECK(classify(senderConsumer(0, Source{SourceKind::Layer, 0, true}), rendered) == Status::Invalid);
        CHECK(classify(senderConsumer(0, Source{SourceKind::Group, 7, false}), rendered) == Status::Invalid);
    }
}

void testLatency() {
    CHECK(latencyFrames(layerConsumer(1, Source{SourceKind::Layer, 0, false})) == 0);
    CHECK(latencyFrames(layerConsumer(1, Source{SourceKind::Layer, 1, true})) == 1);
    CHECK(latencyFrames(layerConsumer(0, Source{SourceKind::Master, 0, true})) == 1);
    CHECK(latencyFrames(senderConsumer(1, Source{SourceKind::Master, 0, false})) == 0);
    CHECK(latencyFrames(senderConsumer(2, Source{SourceKind::Layer, 3, false})) == 0);
}

void testPlan() {
    const RenderedLayers all = rendering(15);
    const Plan empty = makePlan({}, all);
    CHECK(!empty.any());
    CHECK(empty.consumers.empty());
    CHECK(empty.cycles == 0 && empty.feedbacks == 0 && empty.inactive == 0 && empty.invalid == 0);

    const Source layer0{SourceKind::Layer, 0, false};
    const Plan two = makePlan({layerConsumer(1, layer0), layerConsumer(2, layer0)}, all);
    CHECK(two.any());
    CHECK(two.capture[0]);
    CHECK(two.references[0] == 2);
    CHECK(two.forceRender[0]);
    CHECK(!two.forceRender[1]);
    CHECK(!two.keepPrevious[0]);
    CHECK(two.consumers.size() == 2);
    CHECK(two.consumers[0].status == Status::Ok && two.consumers[1].status == Status::Ok);

    const Plan cycle = makePlan({layerConsumer(0, layer0), layerConsumer(1, Source{SourceKind::Layer, 2, false})}, all);
    CHECK(!cycle.any());
    CHECK(cycle.cycles == 2);
    CHECK(!cycle.forceRender[0] && !cycle.forceRender[2]);
    CHECK(cycle.references[0] == 0);

    const Plan feedback = makePlan({layerConsumer(0, Source{SourceKind::Layer, 0, true}), layerConsumer(3, Source{SourceKind::Master, 0, true})}, all);
    CHECK(feedback.feedbacks == 2);
    CHECK(feedback.capture[0] && feedback.keepPrevious[0] && feedback.forceRender[0]);
    CHECK(feedback.capture[kSlotCount - 1] && feedback.keepPrevious[kSlotCount - 1]);
    CHECK(!feedback.forceRender[3]);

    const Plan group = makePlan({layerConsumer(3, Source{SourceKind::Group, 2, false}), senderConsumer(0, Source{SourceKind::Group, 4, false})}, all);
    CHECK(group.capture[slotOf(Source{SourceKind::Group, 2, false})]);
    CHECK(group.capture[slotOf(Source{SourceKind::Group, 4, false})]);
    CHECK(!group.forceRender[0] && !group.forceRender[1]);

    const Plan inactive = makePlan({layerConsumer(2, layer0)}, rendering(6));
    CHECK(inactive.inactive == 1);
    CHECK(!inactive.any());
    CHECK(inactive.references[0] == 1);
    CHECK(!inactive.forceRender[0]);

    const Plan invalid = makePlan({layerConsumer(9, layer0), senderConsumer(0, Source{SourceKind::Layer, 0, true})}, all);
    CHECK(invalid.invalid == 2);
    CHECK(!invalid.any());

    const Plan sender = makePlan({senderConsumer(0, Source{SourceKind::Layer, 3, false}), senderConsumer(1, Source{SourceKind::Master, 0, false})}, all);
    CHECK(sender.capture[3]);
    CHECK(sender.forceRender[3]);
    CHECK(sender.capture[kSlotCount - 1]);
    CHECK(sender.references[3] == 1);
}

bool dependsAcyclic(const std::vector<Resolved>& resolved) {
    std::set<std::pair<int, int>> edges;
    for (const Resolved& item : resolved) {
        if (item.status != Status::Ok || item.consumer.kind != ConsumerKind::Layer) {
            continue;
        }
        for (int layer = 0; layer < kLayerCount; ++layer) {
            if (includesLayer(item.consumer.source, layer)) {
                edges.insert({layer, item.consumer.index});
            }
        }
    }
    std::vector<int> order;
    std::set<int> done;
    for (int round = 0; round < kLayerCount; ++round) {
        for (int node = 0; node < kLayerCount; ++node) {
            if (done.count(node)) {
                continue;
            }
            bool ready = true;
            for (const auto& edge : edges) {
                if (edge.second == node && edge.first != node && !done.count(edge.first)) {
                    ready = false;
                }
                if (edge.second == node && edge.first == node) {
                    return false;
                }
            }
            if (ready) {
                done.insert(node);
                order.push_back(node);
            }
        }
    }
    return order.size() == static_cast<size_t>(kLayerCount);
}

void testNoCycleCanBeBuilt() {
    std::mt19937 rng(20261125);
    const std::vector<Source>& list = clipSources();
    for (int round = 0; round < 4000; ++round) {
        std::vector<Consumer> consumers;
        const int count = static_cast<int>(rng() % 10);
        for (int k = 0; k < count; ++k) {
            const Source& source = list[rng() % list.size()];
            if (rng() % 4 == 0) {
                const auto& choices = senderChoices();
                consumers.push_back(senderConsumer(static_cast<int>(rng() % kSenderCount), choices[rng() % choices.size()]));
            } else {
                consumers.push_back(layerConsumer(static_cast<int>(rng() % kLayerCount), source));
            }
        }
        const Plan plan = makePlan(consumers, rendering(rng() % 16));
        CHECK(dependsAcyclic(plan.consumers));
        CHECK(plan.consumers.size() == consumers.size());
        int cycles = 0;
        int feedbacks = 0;
        int inactive = 0;
        int invalid = 0;
        int referenced = 0;
        for (const Resolved& item : plan.consumers) {
            cycles += item.status == Status::Cycle;
            feedbacks += item.status == Status::Feedback;
            inactive += item.status == Status::Inactive;
            invalid += item.status == Status::Invalid;
            referenced += item.status == Status::Ok || item.status == Status::Feedback || item.status == Status::Inactive;
        }
        CHECK(cycles == plan.cycles && feedbacks == plan.feedbacks && inactive == plan.inactive && invalid == plan.invalid);
        int references = 0;
        for (int slot = 0; slot < kSlotCount; ++slot) {
            references += plan.references[static_cast<size_t>(slot)];
            if (plan.capture[static_cast<size_t>(slot)]) {
                CHECK(plan.references[static_cast<size_t>(slot)] > 0);
            }
            if (plan.keepPrevious[static_cast<size_t>(slot)]) {
                CHECK(plan.capture[static_cast<size_t>(slot)]);
            }
        }
        CHECK(references == referenced);
        for (int layer = 0; layer < kLayerCount; ++layer) {
            if (plan.forceRender[static_cast<size_t>(layer)]) {
                CHECK(plan.capture[static_cast<size_t>(layer)]);
            }
        }
    }
}

void testSurfaces() {
    Surface source;
    source.width = 1920;
    source.height = 1080;
    Surface target = source;
    Compat same = checkSurfaces(source, target);
    CHECK(same.ok && !same.scaled && !same.linear && !same.formatChange && !same.flatten);

    target.width = 960;
    target.height = 540;
    Compat scaled = checkSurfaces(source, target);
    CHECK(scaled.ok && scaled.scaled && scaled.linear);

    target = source;
    target.bytesPerPixel = 8;
    CHECK(checkSurfaces(source, target).formatChange);
    CHECK(checkSurfaces(target, source).formatChange);

    target = source;
    target.alpha = Alpha::Opaque;
    CHECK(checkSurfaces(source, target).flatten);
    CHECK(!checkSurfaces(target, source).flatten);
    CHECK(!checkSurfaces(target, target).flatten);

    Surface empty;
    CHECK(!checkSurfaces(empty, source).ok && checkSurfaces(empty, source).empty);
    CHECK(!checkSurfaces(source, empty).ok);
    Surface huge = source;
    huge.width = kMaxSurfaceSide + 1;
    CHECK(!checkSurfaces(huge, source).ok && checkSurfaces(huge, source).tooLarge);
    CHECK(!checkSurfaces(source, huge).ok);
    Surface edge = source;
    edge.width = kMaxSurfaceSide;
    edge.height = kMaxSurfaceSide;
    CHECK(checkSurfaces(edge, source).ok);
    Surface odd = source;
    odd.bytesPerPixel = 3;
    CHECK(!checkSurfaces(odd, source).ok && checkSurfaces(odd, source).badFormat);
    odd.bytesPerPixel = 16;
    CHECK(!checkSurfaces(source, odd).ok);
    Surface negative = source;
    negative.height = -4;
    CHECK(!checkSurfaces(negative, source).ok);
}

void testSizes() {
    CHECK(!needsResize(1920, 1080, 1920, 1080));
    CHECK(needsResize(1920, 1080, 1280, 720));
    CHECK(needsResize(0, 0, 1280, 720));
    CHECK(needsResize(1920, 1080, 1920, 1081));
    CHECK(captureBytes(1920, 1080, 4, 1) == 8294400ull);
    CHECK(captureBytes(1920, 1080, 4, 2) == 16588800ull);
    CHECK(captureBytes(8192, 8192, 8, 2) == 1073741824ull);
    CHECK(captureBytes(0, 1080, 4, 1) == 0);
    CHECK(captureBytes(1920, 1080, 4, 0) == 0);
    CHECK(captureBytes(-1, 1080, 4, 1) == 0);
    CHECK(captureBytes(1920, 1080, 0, 1) == 0);
}

void testCache() {
    CacheTracker cache(10);
    CHECK(cache.count() == 0);
    CHECK(cache.holdFrames() == 10);
    CHECK(!cache.contains(3));
    CHECK(cache.lastUse(3) == 0);
    CHECK(cache.expired(1000).empty());
    cache.use(3, 100);
    cache.use(5, 105);
    CHECK(cache.count() == 2);
    CHECK(cache.contains(3) && cache.contains(5));
    CHECK(cache.lastUse(3) == 100);
    CHECK(cache.expired(100).empty());
    CHECK(cache.expired(110).empty());
    CHECK(cache.expired(111) == std::vector<int>({3}));
    CHECK(cache.expired(116) == std::vector<int>({3, 5}));
    cache.use(3, 112);
    CHECK(cache.expired(116) == std::vector<int>({5}));
    cache.drop(5);
    CHECK(!cache.contains(5));
    CHECK(cache.expired(116).empty());
    cache.drop(77);
    CHECK(cache.count() == 1);
    CHECK(cache.expired(50).empty());
    cache.clear();
    CHECK(cache.count() == 0);
    CacheTracker zero(-4);
    CHECK(zero.holdFrames() == 0);
    zero.use(1, 10);
    CHECK(zero.expired(10).empty());
    CHECK(zero.expired(11) == std::vector<int>({1}));
    CacheTracker defaults;
    CHECK(defaults.holdFrames() == kHoldFrames);
}

void testSwitchingKeepsInvariants() {
    const RenderedLayers all = rendering(15);
    const std::vector<Source>& list = clipSources();
    CacheTracker cache;
    std::uint64_t frame = 0;
    int target = 0;
    for (int step = 0; step < 600; ++step) {
        ++frame;
        if (step % 7 == 0) {
            target = static_cast<int>((step / 7) % static_cast<int>(list.size()));
        }
        const Plan plan = makePlan({layerConsumer(2, list[static_cast<size_t>(target)])}, all);
        for (int slot = 0; slot < kSlotCount; ++slot) {
            if (plan.capture[static_cast<size_t>(slot)]) {
                cache.use(slot, frame);
            }
        }
        for (int key : cache.expired(frame)) {
            cache.drop(key);
        }
        CHECK(cache.count() <= kSlotCount);
        CHECK(cache.count() <= 1 + (kHoldFrames / 7) + 1);
    }
}

}

int main() {
    testSlots();
    testGroups();
    testNames();
    testSenderKeys();
    testClassifyLayers();
    testClassifyInactive();
    testClassifySenders();
    testLatency();
    testPlan();
    testNoCycleCanBeBuilt();
    testSurfaces();
    testSizes();
    testCache();
    testSwitchingKeepsInvariants();
    return testing::finish();
}
