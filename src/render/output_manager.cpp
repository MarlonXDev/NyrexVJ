#include "render/output_manager.hpp"

namespace nvj {

bool OutputManager::open(int index, GLFWwindow* share) {
    if (!valid(index) || specs_ == nullptr) {
        return false;
    }
    OutputSpec& spec = (*specs_)[static_cast<size_t>(index)];
    if (windows_[static_cast<size_t>(index)].isOpen()) {
        return true;
    }
    return windows_[static_cast<size_t>(index)].open(share, index, spec.monitor, spec.fullscreen);
}

int OutputManager::openEnabled(GLFWwindow* share, bool fullscreen) {
    if (specs_ == nullptr) {
        return 0;
    }
    int opened = 0;
    for (int i = 0; i < kMaxOutputs; ++i) {
        OutputSpec& spec = (*specs_)[static_cast<size_t>(i)];
        if (!spec.enabled && i != 0) {
            continue;
        }
        if (fullscreen) {
            spec.fullscreen = true;
        }
        if (open(i, share)) {
            ++opened;
        }
    }
    return opened;
}

void OutputManager::close(int index) {
    if (valid(index)) {
        windows_[static_cast<size_t>(index)].close();
    }
}

void OutputManager::closeAll() {
    for (OutputWindow& window : windows_) {
        window.close();
    }
}

bool OutputManager::isOpen(int index) const {
    return valid(index) && windows_[static_cast<size_t>(index)].isOpen();
}

bool OutputManager::anyOpen() const {
    return openCount() > 0;
}

int OutputManager::openCount() const {
    int count = 0;
    for (const OutputWindow& window : windows_) {
        if (window.isOpen()) {
            ++count;
        }
    }
    return count;
}

bool OutputManager::isFullscreen(int index) const {
    return valid(index) && windows_[static_cast<size_t>(index)].isFullscreen();
}

bool OutputManager::anyFullscreen() const {
    for (const OutputWindow& window : windows_) {
        if (window.isOpen() && window.isFullscreen()) {
            return true;
        }
    }
    return false;
}

void OutputManager::setFullscreen(int index, bool fullscreen) {
    if (!valid(index) || specs_ == nullptr) {
        return;
    }
    OutputSpec& spec = (*specs_)[static_cast<size_t>(index)];
    spec.fullscreen = fullscreen;
    OutputWindow& window = windows_[static_cast<size_t>(index)];
    if (window.isOpen()) {
        window.setFullscreen(fullscreen, spec.monitor);
    }
}

void OutputManager::toggleFullscreen(GLFWwindow* share) {
    if (!anyOpen()) {
        openEnabled(share, true);
        return;
    }
    const bool target = !anyFullscreen();
    for (int i = 0; i < kMaxOutputs; ++i) {
        if (isOpen(i)) {
            setFullscreen(i, target);
        }
    }
}

void OutputManager::setTestPattern(int index, bool on) {
    if (valid(index)) {
        patterns_[static_cast<size_t>(index)] = on;
    }
}

bool OutputManager::testPattern(int index) const {
    return valid(index) && patterns_[static_cast<size_t>(index)];
}

void OutputManager::render(GLuint texture, int width, int height) {
    if (specs_ == nullptr) {
        return;
    }
    for (int i = 0; i < kMaxOutputs; ++i) {
        OutputWindow& window = windows_[static_cast<size_t>(i)];
        if (!window.isOpen()) {
            continue;
        }
        OutputSpec& spec = (*specs_)[static_cast<size_t>(i)];
        window.render(texture, width, height, spec, patterns_[static_cast<size_t>(i)]);
        if (window.isOpen()) {
            spec.fullscreen = window.isFullscreen();
        }
    }
}

bool OutputManager::capture(int index, GLuint texture, int width, int height, OutputCapture& out) {
    if (!isOpen(index) || specs_ == nullptr) {
        return false;
    }
    windows_[static_cast<size_t>(index)].render(texture, width, height, (*specs_)[static_cast<size_t>(index)], patterns_[static_cast<size_t>(index)], &out);
    return out.width > 0 && out.height > 0;
}

size_t OutputManager::gpuBytes() const {
    size_t total = 0;
    for (const OutputWindow& window : windows_) {
        total += window.gpuBytes();
    }
    return total;
}

}
