#pragma once

#include "core/composition.hpp"

#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace nvj::routing {

enum class SourceKind {
    Layer,
    Group,
    Master
};

struct Source {
    SourceKind kind = SourceKind::Master;
    int index = 0;
    bool previousFrame = false;
    bool operator==(const Source&) const = default;
};

constexpr int kSlotCount = kLayerCount + (kLayerCount - 1) + 1;
constexpr int kDeckPasses = 2;
constexpr int kHoldFrames = 90;
constexpr int kMaxSurfaceSide = 8192;

constexpr int kSenderCount = 3;

enum class SenderKind {
    Spout = 0,
    Ndi = 1,
    Recording = 2
};

const char* senderName(SenderKind kind);

bool validSource(const Source& source);
int slotOf(const Source& source);
Source sourceOfSlot(int slot);
int groupLastLayer(const Source& source);
bool includesLayer(const Source& source, int layer);

std::string sourceName(const Source& source);
bool parseSourceName(const std::string& name, Source& out);
const std::vector<Source>& clipSources();
int clipSourceIndex(const Source& source);

std::string senderKey(const Source& source);
bool parseSenderKey(const std::string& key, Source& out);
std::string senderLabel(const Source& source);
const std::vector<Source>& senderChoices();

enum class ConsumerKind {
    Layer,
    Sender
};

struct Consumer {
    ConsumerKind kind = ConsumerKind::Layer;
    int index = 0;
    Source source;
    bool operator==(const Consumer&) const = default;
};

enum class Status {
    Ok,
    Feedback,
    Cycle,
    Inactive,
    Invalid
};

const char* statusName(Status status);

struct Resolved {
    Consumer consumer;
    Status status = Status::Ok;
    int latencyFrames = 0;
};

struct Plan {
    std::array<bool, kSlotCount> capture{};
    std::array<bool, kSlotCount> keepPrevious{};
    std::array<int, kSlotCount> references{};
    std::array<bool, kLayerCount> forceRender{};
    std::vector<Resolved> consumers;
    int cycles = 0;
    int feedbacks = 0;
    int inactive = 0;
    int invalid = 0;

    bool any() const;
};

using RenderedLayers = std::array<bool, kLayerCount>;

Status classify(const Consumer& consumer, const RenderedLayers& rendered);
int latencyFrames(const Consumer& consumer);
Plan makePlan(const std::vector<Consumer>& consumers, const RenderedLayers& rendered);

enum class Alpha {
    Premultiplied,
    Opaque
};

struct Surface {
    int width = 0;
    int height = 0;
    int bytesPerPixel = 4;
    Alpha alpha = Alpha::Premultiplied;
};

struct Compat {
    bool ok = true;
    bool empty = false;
    bool tooLarge = false;
    bool badFormat = false;
    bool scaled = false;
    bool linear = false;
    bool formatChange = false;
    bool flatten = false;
};

Compat checkSurfaces(const Surface& source, const Surface& target);
bool needsResize(int currentWidth, int currentHeight, int wantedWidth, int wantedHeight);
std::uint64_t captureBytes(int width, int height, int bytesPerPixel, int copies);

class CacheTracker {
public:
    explicit CacheTracker(int holdFrames = kHoldFrames);

    void use(int key, std::uint64_t frame);
    bool contains(int key) const;
    std::uint64_t lastUse(int key) const;
    std::vector<int> expired(std::uint64_t frame) const;
    void drop(int key);
    void clear();
    int count() const;
    int holdFrames() const { return holdFrames_; }

private:
    int holdFrames_;
    std::map<int, std::uint64_t> last_;
};

}
