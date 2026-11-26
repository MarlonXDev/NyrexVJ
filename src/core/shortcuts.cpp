#include "core/shortcuts.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <system_error>

namespace nvj::shortcuts {

namespace {

namespace fs = std::filesystem;

constexpr size_t kMaxFileBytes = 64 * 1024;

const ActionInfo kActions[] = {
    {Action::Tap, "tap", "Tap tempo"},
    {Action::Blackout, "blackout", "Blackout"},
    {Action::Fullscreen, "fullscreen", "Fullscreen output"},
    {Action::Undo, "undo", "Undo"},
    {Action::Redo, "redo", "Redo"},
    {Action::Settings, "settings", "Open settings"},
    {Action::Record, "record", "Start or stop recording"},
    {Action::Manual, "manual", "Open the manual"},
    {Action::NextDeck, "deck.next", "Next deck"},
    {Action::PreviousDeck, "deck.previous", "Previous deck"},
    {Action::InterfaceLarger, "interface.larger", "Larger interface"},
    {Action::InterfaceSmaller, "interface.smaller", "Smaller interface"},
    {Action::InterfaceReset, "interface.reset", "Normal interface size"},
};

struct LabelPair {
    const char* name;
    const char* label;
};

const LabelPair kSymbols[] = {
    {"Comma", ","},
    {"Period", "."},
    {"Minus", "-"},
    {"Equal", "="},
    {"Slash", "/"},
    {"Semicolon", ";"},
};

std::string lower(const std::string& text) {
    std::string out = text;
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

std::string trim(const std::string& text) {
    size_t begin = 0;
    size_t end = text.size();
    while (begin < end && std::isspace(static_cast<unsigned char>(text[begin]))) {
        ++begin;
    }
    while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1]))) {
        --end;
    }
    return text.substr(begin, end - begin);
}

std::vector<std::string> buildKeyNames() {
    std::vector<std::string> names;
    for (char c = 'A'; c <= 'Z'; ++c) {
        names.emplace_back(1, c);
    }
    for (char c = '0'; c <= '9'; ++c) {
        names.emplace_back(1, c);
    }
    for (int i = 1; i <= 12; ++i) {
        names.push_back("F" + std::to_string(i));
    }
    for (const char* name : {"Space", "Comma", "Period", "Minus", "Equal", "Slash", "Semicolon", "PageUp", "PageDown", "Home", "End", "Insert"}) {
        names.emplace_back(name);
    }
    return names;
}

bool canonicalKey(const std::string& text, std::string& canonical) {
    const std::string wanted = lower(text);
    for (const std::string& name : keyNames()) {
        if (lower(name) == wanted) {
            canonical = name;
            return true;
        }
    }
    for (const LabelPair& symbol : kSymbols) {
        if (text == symbol.label) {
            canonical = symbol.name;
            return true;
        }
    }
    return false;
}

bool readText(const fs::path& path, std::string& text) {
    std::error_code ec;
    const auto size = fs::file_size(path, ec);
    if (ec || size > kMaxFileBytes) {
        return false;
    }
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return false;
    }
    text.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    return true;
}

}

const ActionInfo& info(Action action) {
    return kActions[static_cast<int>(action)];
}

int actionIndex(const std::string& id) {
    for (int i = 0; i < kActionCount; ++i) {
        if (id == kActions[i].id) {
            return i;
        }
    }
    return -1;
}

const std::vector<std::string>& keyNames() {
    static const std::vector<std::string> names = buildKeyNames();
    return names;
}

bool knownKey(const std::string& name) {
    const std::vector<std::string>& names = keyNames();
    return std::find(names.begin(), names.end(), name) != names.end();
}

std::string keyLabel(const std::string& name) {
    for (const LabelPair& symbol : kSymbols) {
        if (name == symbol.name) {
            return symbol.label;
        }
    }
    return name;
}

std::string format(const Binding& binding) {
    if (binding.empty()) {
        return std::string();
    }
    std::string text;
    if (binding.ctrl) {
        text += "Ctrl+";
    }
    if (binding.shift) {
        text += "Shift+";
    }
    if (binding.alt) {
        text += "Alt+";
    }
    return text + keyLabel(binding.key);
}

bool parse(const std::string& text, Binding& out, std::string& error) {
    out = Binding{};
    const std::string clean = trim(text);
    if (clean.empty()) {
        return true;
    }
    std::vector<std::string> parts;
    size_t start = 0;
    while (true) {
        const size_t plus = clean.find('+', start);
        if (plus == std::string::npos) {
            parts.push_back(trim(clean.substr(start)));
            break;
        }
        parts.push_back(trim(clean.substr(start, plus - start)));
        start = plus + 1;
    }
    if (parts.back().empty()) {
        error = "A shortcut needs a key";
        return false;
    }
    Binding result;
    for (size_t i = 0; i + 1 < parts.size(); ++i) {
        const std::string modifier = lower(parts[i]);
        bool* target = nullptr;
        if (modifier == "ctrl" || modifier == "control") {
            target = &result.ctrl;
        } else if (modifier == "shift") {
            target = &result.shift;
        } else if (modifier == "alt") {
            target = &result.alt;
        }
        if (target == nullptr) {
            error = "Unknown modifier '" + parts[i] + "'";
            return false;
        }
        if (*target) {
            error = "The modifier '" + parts[i] + "' is given twice";
            return false;
        }
        *target = true;
    }
    if (!canonicalKey(parts.back(), result.key)) {
        error = "Unknown key '" + parts.back() + "'";
        return false;
    }
    out = result;
    return true;
}

Binding defaultBinding(Action action) {
    Binding binding;
    switch (action) {
    case Action::Tap:
        binding.key = "Space";
        break;
    case Action::Blackout:
        binding.key = "B";
        break;
    case Action::Fullscreen:
        binding.key = "F11";
        break;
    case Action::Undo:
        binding.key = "Z";
        binding.ctrl = true;
        break;
    case Action::Redo:
        binding.key = "Y";
        binding.ctrl = true;
        break;
    case Action::Settings:
        binding.key = "Comma";
        binding.ctrl = true;
        break;
    case Action::Record:
        binding.key = "R";
        binding.ctrl = true;
        binding.shift = true;
        break;
    case Action::Manual:
        binding.key = "F1";
        break;
    case Action::NextDeck:
        binding.key = "PageDown";
        break;
    case Action::PreviousDeck:
        binding.key = "PageUp";
        break;
    case Action::InterfaceLarger:
        binding.key = "Equal";
        binding.ctrl = true;
        break;
    case Action::InterfaceSmaller:
        binding.key = "Minus";
        binding.ctrl = true;
        break;
    case Action::InterfaceReset:
        binding.key = "0";
        binding.ctrl = true;
        break;
    case Action::Count:
        break;
    }
    return binding;
}

std::string reservedReason(const Binding& binding) {
    if (!knownKey(binding.key)) {
        return "That key cannot be used for a shortcut";
    }
    const bool digit = binding.key.size() == 1 && binding.key[0] >= '1' && binding.key[0] <= '8';
    if (digit && !binding.alt && (!binding.shift || binding.ctrl)) {
        return "The number keys 1 to 8 trigger columns and switch decks and cannot be reassigned";
    }
    if (binding.key == "Z" && binding.ctrl && binding.shift && !binding.alt) {
        return "Ctrl+Shift+Z always redoes";
    }
    if (binding.key == "F4" && binding.alt && !binding.ctrl && !binding.shift) {
        return "Alt+F4 closes the window";
    }
    return std::string();
}

Table::Table() {
    resetAll();
}

const Binding& Table::get(Action action) const {
    return bindings_[static_cast<size_t>(action)];
}

SetResult Table::set(Action action, const Binding& binding) {
    SetResult result;
    const size_t index = static_cast<size_t>(action);
    if (binding.empty()) {
        bindings_[index] = Binding{};
        result.ok = true;
        return result;
    }
    const std::string reason = reservedReason(binding);
    if (!reason.empty()) {
        result.error = reason;
        return result;
    }
    const int other = find(binding);
    if (other >= 0 && other != static_cast<int>(index)) {
        result.error = "That shortcut is already used";
        result.conflict = other;
        return result;
    }
    bindings_[index] = binding;
    result.ok = true;
    return result;
}

void Table::reset(Action action) {
    bindings_[static_cast<size_t>(action)] = defaultBinding(action);
}

void Table::resetAll() {
    for (int i = 0; i < kActionCount; ++i) {
        bindings_[static_cast<size_t>(i)] = defaultBinding(static_cast<Action>(i));
    }
}

bool Table::isDefault() const {
    for (int i = 0; i < kActionCount; ++i) {
        if (!isDefault(static_cast<Action>(i))) {
            return false;
        }
    }
    return true;
}

bool Table::isDefault(Action action) const {
    return bindings_[static_cast<size_t>(action)] == defaultBinding(action);
}

int Table::find(const Binding& binding) const {
    if (binding.empty()) {
        return -1;
    }
    for (int i = 0; i < kActionCount; ++i) {
        if (bindings_[static_cast<size_t>(i)] == binding) {
            return i;
        }
    }
    return -1;
}

bool Table::matches(Action action, const std::string& key, bool ctrl, bool shift, bool alt) const {
    const Binding& binding = bindings_[static_cast<size_t>(action)];
    return !binding.empty() && binding.key == key && binding.ctrl == ctrl && binding.shift == shift && binding.alt == alt;
}

std::string Table::toJson() const {
    nlohmann::json doc;
    doc["version"] = kFormat;
    nlohmann::json map = nlohmann::json::object();
    for (int i = 0; i < kActionCount; ++i) {
        map[kActions[i].id] = format(bindings_[static_cast<size_t>(i)]);
    }
    doc["bindings"] = map;
    return doc.dump(2) + "\n";
}

LoadResult Table::fromJson(const std::string& text) {
    LoadResult result;
    const nlohmann::json doc = nlohmann::json::parse(text, nullptr, false);
    if (doc.is_discarded() || !doc.is_object()) {
        result.state = LoadState::Damaged;
        result.error = "The shortcut file is not valid JSON";
        return result;
    }
    const auto version = doc.find("version");
    if (version == doc.end() || !version->is_number_integer()) {
        result.state = LoadState::Damaged;
        result.error = "The shortcut file has no version";
        return result;
    }
    if (version->get<int>() > kFormat) {
        result.state = LoadState::Newer;
        result.error = "The shortcut file was written by a newer version of NyrexVJ";
        return result;
    }
    const auto map = doc.find("bindings");
    if (map == doc.end() || !map->is_object()) {
        result.state = LoadState::Damaged;
        result.error = "The shortcut file has no bindings";
        return result;
    }
    Table loaded;
    std::array<bool, kActionCount> given{};
    for (int i = 0; i < kActionCount; ++i) {
        loaded.bindings_[static_cast<size_t>(i)] = Binding{};
    }
    for (int i = 0; i < kActionCount; ++i) {
        const auto entry = map->find(kActions[i].id);
        if (entry == map->end()) {
            continue;
        }
        given[static_cast<size_t>(i)] = true;
        Binding binding;
        std::string error;
        if (!entry->is_string() || !parse(entry->get<std::string>(), binding, error)) {
            result.notes.push_back(std::string(kActions[i].id) + ": " + (entry->is_string() ? error : std::string("not a text")) + ", the default is used");
            given[static_cast<size_t>(i)] = false;
            continue;
        }
        const SetResult set = loaded.set(static_cast<Action>(i), binding);
        if (!set.ok) {
            result.notes.push_back(std::string(kActions[i].id) + ": " + set.error + ", the default is used");
            given[static_cast<size_t>(i)] = false;
        }
    }
    for (int i = 0; i < kActionCount; ++i) {
        if (given[static_cast<size_t>(i)]) {
            continue;
        }
        const Binding fallback = defaultBinding(static_cast<Action>(i));
        const SetResult set = loaded.set(static_cast<Action>(i), fallback);
        if (!set.ok) {
            result.notes.push_back(std::string(kActions[i].id) + ": the default is taken by another action, so none is assigned");
        }
    }
    bindings_ = loaded.bindings_;
    result.state = LoadState::Loaded;
    return result;
}

LoadResult loadFile(const fs::path& path, Table& table) {
    LoadResult result;
    std::error_code ec;
    if (!fs::exists(path, ec)) {
        return result;
    }
    std::string text;
    if (!readText(path, text)) {
        result.state = LoadState::Damaged;
        result.error = "The shortcut file could not be read";
        return result;
    }
    Table candidate;
    result = candidate.fromJson(text);
    if (result.state == LoadState::Loaded) {
        table = candidate;
    }
    return result;
}

bool saveFile(const fs::path& path, const Table& table) {
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    fs::path temporary = path;
    temporary += ".tmp";
    {
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        if (!file) {
            return false;
        }
        file << table.toJson();
        file.flush();
        if (!file) {
            file.close();
            fs::remove(temporary, ec);
            return false;
        }
    }
    fs::rename(temporary, path, ec);
    if (ec) {
        std::error_code removeEc;
        fs::remove(path, removeEc);
        ec.clear();
        fs::rename(temporary, path, ec);
    }
    if (ec) {
        std::error_code removeEc;
        fs::remove(temporary, removeEc);
        return false;
    }
    return true;
}

}
