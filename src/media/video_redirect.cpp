#include "media/video_redirect.hpp"

#include <mutex>

namespace nvj::redirect {

namespace {

std::mutex& guard() {
    static std::mutex mutex;
    return mutex;
}

Hooks& current() {
    static Hooks hooks;
    return hooks;
}

Hooks snapshot() {
    std::lock_guard<std::mutex> lock(guard());
    return current();
}

}

void install(Hooks hooks) {
    std::lock_guard<std::mutex> lock(guard());
    current() = std::move(hooks);
}

void uninstall() {
    std::lock_guard<std::mutex> lock(guard());
    current() = Hooks();
}

bool installed() {
    std::lock_guard<std::mutex> lock(guard());
    return static_cast<bool>(current().lookup);
}

std::string lookup(const std::string& original) {
    const Hooks hooks = snapshot();
    return hooks.lookup ? hooks.lookup(original) : std::string();
}

void markBad(const std::string& original, const std::string& reason) {
    const Hooks hooks = snapshot();
    if (hooks.markBad) {
        hooks.markBad(original, reason);
    }
}

std::uint64_t generation() {
    const Hooks hooks = snapshot();
    return hooks.generation ? hooks.generation() : 0;
}

}
