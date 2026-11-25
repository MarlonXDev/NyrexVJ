#pragma once

#include <array>
#include <filesystem>
#include <string>
#include <vector>

namespace nvj::shortcuts {

enum class Action {
    Tap,
    Blackout,
    Fullscreen,
    Undo,
    Redo,
    Settings,
    Record,
    Manual,
    NextDeck,
    PreviousDeck,
    InterfaceLarger,
    InterfaceSmaller,
    InterfaceReset,
    Count
};

constexpr int kActionCount = static_cast<int>(Action::Count);
constexpr int kFormat = 1;

struct Binding {
    std::string key;
    bool ctrl = false;
    bool shift = false;
    bool alt = false;

    bool empty() const {
        return key.empty();
    }
    bool operator==(const Binding& other) const {
        return key == other.key && ctrl == other.ctrl && shift == other.shift && alt == other.alt;
    }
    bool operator!=(const Binding& other) const {
        return !(*this == other);
    }
};

struct ActionInfo {
    Action action;
    const char* id;
    const char* label;
};

struct SetResult {
    bool ok = false;
    std::string error;
    int conflict = -1;
};

enum class LoadState {
    Missing,
    Loaded,
    Damaged,
    Newer
};

struct LoadResult {
    LoadState state = LoadState::Missing;
    std::string error;
    std::vector<std::string> notes;
};

const ActionInfo& info(Action action);
int actionIndex(const std::string& id);

const std::vector<std::string>& keyNames();
bool knownKey(const std::string& name);
std::string keyLabel(const std::string& name);

std::string format(const Binding& binding);
bool parse(const std::string& text, Binding& out, std::string& error);
Binding defaultBinding(Action action);
std::string reservedReason(const Binding& binding);

class Table {
public:
    Table();

    const Binding& get(Action action) const;
    SetResult set(Action action, const Binding& binding);
    void reset(Action action);
    void resetAll();
    bool isDefault() const;
    bool isDefault(Action action) const;
    int find(const Binding& binding) const;
    bool matches(Action action, const std::string& key, bool ctrl, bool shift, bool alt) const;

    std::string toJson() const;
    LoadResult fromJson(const std::string& text);

private:
    std::array<Binding, kActionCount> bindings_;
};

LoadResult loadFile(const std::filesystem::path& path, Table& table);
bool saveFile(const std::filesystem::path& path, const Table& table);

}
