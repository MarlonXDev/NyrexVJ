#pragma once

#include <array>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace nvj::api {

constexpr int kApiVersion = 1;
constexpr int kLayers = 4;
constexpr int kColumns = 8;
constexpr int kSlots = 4;
constexpr int kParams = 4;
constexpr int kDecks = 16;
constexpr size_t kMaxQueuedCommands = 64;

struct ClipInfo {
    std::string name;
    std::string kind;
};

struct EffectInfo {
    std::string name;
    std::vector<std::string> params;
    bool graph = false;
};

struct Library {
    std::uint64_t signature = 0;
    std::vector<ClipInfo> clips;
    std::vector<EffectInfo> effects;
    std::vector<std::string> factoryPresets;
};

struct SlotInfo {
    int effect = -1;
    bool enabled = true;
    std::array<float, kParams> params{};
};

struct LayerInfo {
    std::array<int, kColumns> cells{};
    int activeColumn = -1;
    float opacity = 1.0f;
    bool bypass = false;
    bool solo = false;
    float speed = 1.0f;
    float scale = 1.0f;
    std::array<SlotInfo, kSlots> fx{};
};

struct OutputInfo {
    std::string name;
    bool enabled = false;
    bool open = false;
    bool fullscreen = false;
    int monitor = 0;
    bool ndi = false;
    std::string ndiName;
};

struct Snapshot {
    std::uint64_t revision = 0;
    std::string appVersion;
    std::string composition;
    float bpm = 128.0f;
    int beatsPerBar = 4;
    int activeDeck = 0;
    bool blackout = false;
    float masterLevel = 1.0f;
    float fps = 0.0f;
    int width = 0;
    int height = 0;
    bool audioActive = false;
    std::array<std::string, kDecks> deckNames;
    std::array<bool, kDecks> deckUsed{};
    std::array<LayerInfo, kLayers> layers{};
    std::array<SlotInfo, kSlots> masterFx{};
    std::vector<float> controlValues;
    std::vector<OutputInfo> outputs;
    std::shared_ptr<const Library> library;
};

struct Result {
    bool ok = false;
    int status = 500;
    std::string code;
    std::string message;
    float value = 0.0f;
};

enum class CommandType { Control, LoadFactoryPreset, LoadUserPreset };

struct Command {
    CommandType type = CommandType::Control;
    int control = -1;
    float value = 0.0f;
    std::string name;
    std::string text;
};

class Ticket {
public:
    enum class State { Pending, Running, Done, Cancelled };

    bool begin();
    void complete(const Result& result);
    bool cancel();
    bool poll(Result& result);
    State state();

private:
    std::mutex mutex_;
    State state_ = State::Pending;
    Result result_;
};

class Hub {
public:
    void publish(std::shared_ptr<const Snapshot> snapshot);
    std::shared_ptr<const Snapshot> snapshot() const;

    std::shared_ptr<Ticket> submit(const Command& command);

    template <typename Executor>
    int drain(int limit, Executor&& executor) {
        int applied = 0;
        while (applied < limit) {
            std::pair<Command, std::shared_ptr<Ticket>> item;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (queue_.empty()) {
                    break;
                }
                item = std::move(queue_.front());
                queue_.pop_front();
            }
            if (!item.second->begin()) {
                continue;
            }
            item.second->complete(executor(item.first));
            ++applied;
        }
        return applied;
    }

    size_t queued() const;
    void clear();
    void reset();

private:
    mutable std::mutex mutex_;
    std::shared_ptr<const Snapshot> snapshot_;
    std::deque<std::pair<Command, std::shared_ptr<Ticket>>> queue_;
};

}
