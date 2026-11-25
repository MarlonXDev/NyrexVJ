#include "core/routing.hpp"

#include <algorithm>

namespace nvj::routing {

namespace {

std::string layerNumber(int index) {
    return std::to_string(index + 1);
}

std::string groupSpan(int layers) {
    return "1-" + std::to_string(layers);
}

std::vector<Source> buildClipSources() {
    std::vector<Source> list;
    for (int layer = 0; layer < kLayerCount - 1; ++layer) {
        list.push_back(Source{SourceKind::Layer, layer, false});
    }
    for (int layers = 2; layers < kLayerCount; ++layers) {
        list.push_back(Source{SourceKind::Group, layers, false});
    }
    for (int layer = 0; layer < kLayerCount; ++layer) {
        list.push_back(Source{SourceKind::Layer, layer, true});
    }
    for (int layers = 2; layers <= kLayerCount; ++layers) {
        list.push_back(Source{SourceKind::Group, layers, true});
    }
    list.push_back(Source{SourceKind::Master, 0, true});
    return list;
}

std::vector<Source> buildSenderChoices() {
    std::vector<Source> list;
    list.push_back(Source{SourceKind::Master, 0, false});
    for (int layer = 0; layer < kLayerCount; ++layer) {
        list.push_back(Source{SourceKind::Layer, layer, false});
    }
    for (int layers = 2; layers <= kLayerCount; ++layers) {
        list.push_back(Source{SourceKind::Group, layers, false});
    }
    return list;
}

bool anyRendered(const RenderedLayers& rendered, int layers) {
    for (int layer = 0; layer < layers && layer < kLayerCount; ++layer) {
        if (rendered[static_cast<size_t>(layer)]) {
            return true;
        }
    }
    return false;
}

bool sourceRendered(const Source& source, const RenderedLayers& rendered) {
    switch (source.kind) {
    case SourceKind::Layer:
        return rendered[static_cast<size_t>(source.index)];
    case SourceKind::Group:
        return anyRendered(rendered, source.index);
    case SourceKind::Master:
        return true;
    }
    return false;
}

}

const char* senderName(SenderKind kind) {
    switch (kind) {
    case SenderKind::Spout:
        return "Spout";
    case SenderKind::Ndi:
        return "NDI";
    case SenderKind::Recording:
        return "Recording";
    }
    return "";
}

bool validSource(const Source& source) {
    switch (source.kind) {
    case SourceKind::Layer:
        return source.index >= 0 && source.index < kLayerCount;
    case SourceKind::Group:
        return source.index >= 2 && source.index <= kLayerCount;
    case SourceKind::Master:
        return source.index == 0;
    }
    return false;
}

int slotOf(const Source& source) {
    if (!validSource(source)) {
        return -1;
    }
    switch (source.kind) {
    case SourceKind::Layer:
        return source.index;
    case SourceKind::Group:
        return kLayerCount + (source.index - 2);
    case SourceKind::Master:
        return kSlotCount - 1;
    }
    return -1;
}

Source sourceOfSlot(int slot) {
    Source source;
    if (slot >= 0 && slot < kLayerCount) {
        source.kind = SourceKind::Layer;
        source.index = slot;
    } else if (slot >= kLayerCount && slot < kSlotCount - 1) {
        source.kind = SourceKind::Group;
        source.index = slot - kLayerCount + 2;
    } else {
        source.kind = SourceKind::Master;
        source.index = 0;
    }
    return source;
}

int groupLastLayer(const Source& source) {
    return source.kind == SourceKind::Group ? source.index - 1 : -1;
}

bool includesLayer(const Source& source, int layer) {
    switch (source.kind) {
    case SourceKind::Layer:
        return source.index == layer;
    case SourceKind::Group:
        return layer >= 0 && layer < source.index;
    case SourceKind::Master:
        return layer >= 0 && layer < kLayerCount;
    }
    return false;
}

std::string sourceName(const Source& source) {
    if (!validSource(source)) {
        return "";
    }
    const std::string prefix = source.previousFrame ? "Feedback: " : "Route: ";
    switch (source.kind) {
    case SourceKind::Layer:
        return prefix + "Layer " + layerNumber(source.index);
    case SourceKind::Group:
        return prefix + "Group " + groupSpan(source.index);
    case SourceKind::Master:
        return prefix + "Master";
    }
    return "";
}

const std::vector<Source>& clipSources() {
    static const std::vector<Source> list = buildClipSources();
    return list;
}

int clipSourceIndex(const Source& source) {
    const std::vector<Source>& list = clipSources();
    for (size_t i = 0; i < list.size(); ++i) {
        if (list[i] == source) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

bool parseSourceName(const std::string& name, Source& out) {
    for (const Source& source : clipSources()) {
        if (sourceName(source) == name) {
            out = source;
            return true;
        }
    }
    return false;
}

std::string senderKey(const Source& source) {
    if (!validSource(source) || source.previousFrame) {
        return "master";
    }
    switch (source.kind) {
    case SourceKind::Layer:
        return "layer" + layerNumber(source.index);
    case SourceKind::Group:
        return "group1" + std::to_string(source.index);
    case SourceKind::Master:
        return "master";
    }
    return "master";
}

bool parseSenderKey(const std::string& key, Source& out) {
    for (const Source& source : senderChoices()) {
        if (senderKey(source) == key) {
            out = source;
            return true;
        }
    }
    return false;
}

std::string senderLabel(const Source& source) {
    switch (source.kind) {
    case SourceKind::Layer:
        return "Layer " + layerNumber(source.index);
    case SourceKind::Group:
        return "Group " + groupSpan(source.index);
    case SourceKind::Master:
        return "Master";
    }
    return "Master";
}

const std::vector<Source>& senderChoices() {
    static const std::vector<Source> list = buildSenderChoices();
    return list;
}

const char* statusName(Status status) {
    switch (status) {
    case Status::Ok:
        return "ok";
    case Status::Feedback:
        return "feedback";
    case Status::Cycle:
        return "cycle";
    case Status::Inactive:
        return "inactive";
    case Status::Invalid:
        return "invalid";
    }
    return "invalid";
}

bool Plan::any() const {
    for (bool wanted : capture) {
        if (wanted) {
            return true;
        }
    }
    return false;
}

Status classify(const Consumer& consumer, const RenderedLayers& rendered) {
    const Source& source = consumer.source;
    if (!validSource(source)) {
        return Status::Invalid;
    }
    if (consumer.kind == ConsumerKind::Sender) {
        if (source.previousFrame) {
            return Status::Invalid;
        }
        return sourceRendered(source, rendered) ? Status::Ok : Status::Inactive;
    }
    if (consumer.index < 0 || consumer.index >= kLayerCount) {
        return Status::Invalid;
    }
    if (source.previousFrame) {
        return sourceRendered(source, rendered) ? Status::Feedback : Status::Inactive;
    }
    switch (source.kind) {
    case SourceKind::Layer:
        if (source.index >= consumer.index) {
            return Status::Cycle;
        }
        break;
    case SourceKind::Group:
        if (source.index > consumer.index) {
            return Status::Cycle;
        }
        break;
    case SourceKind::Master:
        return Status::Cycle;
    }
    return sourceRendered(source, rendered) ? Status::Ok : Status::Inactive;
}

int latencyFrames(const Consumer& consumer) {
    return consumer.kind == ConsumerKind::Layer && consumer.source.previousFrame ? 1 : 0;
}

Plan makePlan(const std::vector<Consumer>& consumers, const RenderedLayers& rendered) {
    Plan plan;
    for (const Consumer& consumer : consumers) {
        Resolved resolved;
        resolved.consumer = consumer;
        resolved.status = classify(consumer, rendered);
        resolved.latencyFrames = latencyFrames(consumer);
        plan.consumers.push_back(resolved);
        const int slotIndex = slotOf(consumer.source);
        if (slotIndex < 0 || slotIndex >= kSlotCount) {
            if (resolved.status == Status::Invalid) {
                ++plan.invalid;
            }
            continue;
        }
        const size_t slot = static_cast<size_t>(slotIndex);
        switch (resolved.status) {
        case Status::Cycle:
            ++plan.cycles;
            continue;
        case Status::Invalid:
            ++plan.invalid;
            continue;
        case Status::Inactive:
            ++plan.inactive;
            ++plan.references[slot];
            continue;
        case Status::Feedback:
            ++plan.feedbacks;
            break;
        case Status::Ok:
            break;
        }
        plan.capture[slot] = true;
        ++plan.references[slot];
        if (resolved.status == Status::Feedback) {
            plan.keepPrevious[slot] = true;
        }
        if (consumer.source.kind == SourceKind::Layer) {
            plan.forceRender[static_cast<size_t>(consumer.source.index)] = true;
        }
    }
    return plan;
}

Compat checkSurfaces(const Surface& source, const Surface& target) {
    Compat result;
    if (source.width <= 0 || source.height <= 0 || target.width <= 0 || target.height <= 0) {
        result.ok = false;
        result.empty = true;
        return result;
    }
    if (source.width > kMaxSurfaceSide || source.height > kMaxSurfaceSide || target.width > kMaxSurfaceSide || target.height > kMaxSurfaceSide) {
        result.ok = false;
        result.tooLarge = true;
        return result;
    }
    const auto supported = [](int bytes) { return bytes == 4 || bytes == 8; };
    if (!supported(source.bytesPerPixel) || !supported(target.bytesPerPixel)) {
        result.ok = false;
        result.badFormat = true;
        return result;
    }
    result.scaled = source.width != target.width || source.height != target.height;
    result.linear = result.scaled;
    result.formatChange = source.bytesPerPixel != target.bytesPerPixel;
    result.flatten = source.alpha == Alpha::Premultiplied && target.alpha == Alpha::Opaque;
    return result;
}

bool needsResize(int currentWidth, int currentHeight, int wantedWidth, int wantedHeight) {
    return currentWidth != wantedWidth || currentHeight != wantedHeight;
}

std::uint64_t captureBytes(int width, int height, int bytesPerPixel, int copies) {
    if (width <= 0 || height <= 0 || bytesPerPixel <= 0 || copies <= 0) {
        return 0;
    }
    return static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height) * static_cast<std::uint64_t>(bytesPerPixel) * static_cast<std::uint64_t>(copies);
}

CacheTracker::CacheTracker(int holdFrames) : holdFrames_(std::max(holdFrames, 0)) {}

void CacheTracker::use(int key, std::uint64_t frame) {
    last_[key] = frame;
}

bool CacheTracker::contains(int key) const {
    return last_.find(key) != last_.end();
}

std::uint64_t CacheTracker::lastUse(int key) const {
    const auto it = last_.find(key);
    return it == last_.end() ? 0 : it->second;
}

std::vector<int> CacheTracker::expired(std::uint64_t frame) const {
    std::vector<int> keys;
    for (const auto& entry : last_) {
        if (frame > entry.second && frame - entry.second > static_cast<std::uint64_t>(holdFrames_)) {
            keys.push_back(entry.first);
        }
    }
    return keys;
}

void CacheTracker::drop(int key) {
    last_.erase(key);
}

void CacheTracker::clear() {
    last_.clear();
}

int CacheTracker::count() const {
    return static_cast<int>(last_.size());
}

}
