#include "render/spout_input.hpp"

#include "core/log.hpp"

#include <Spout.h>

#include <chrono>

namespace nvj {

namespace {

double nowSeconds() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

bool gTestPattern = false;

}

struct SpoutInput::Impl {
    Spout spout;
    GLuint texture = 0;
    int width = 0;
    int height = 0;
    std::string current;
    bool receiving = false;
    bool sized = false;
    double lastLookup = -1.0e9;
    SpoutReceiveStatus status;
};

SpoutInput::SpoutInput() : impl_(std::make_unique<Impl>()) {}

SpoutInput::~SpoutInput() {
    release();
}

bool SpoutInput::supported() {
    return true;
}

void SpoutInput::setTestPattern(bool enabled) {
    gTestPattern = enabled;
}

bool SpoutInput::testPattern() {
    return gTestPattern;
}

const char* SpoutInput::testSenderName() {
    return "Test Pattern";
}

std::vector<std::string> SpoutInput::senders() {
    std::vector<std::string> out;
    const int count = impl_->spout.GetSenderCount();
    for (int i = 0; i < count; ++i) {
        char name[256] = {};
        if (impl_->spout.GetSender(i, name, 256) && name[0]) {
            out.emplace_back(name);
        }
    }
    return out;
}

void SpoutInput::release() {
    if (impl_->receiving || !impl_->current.empty()) {
        impl_->spout.ReleaseReceiver();
        logLine("Spout receiver '" + impl_->current + "' released");
    }
    if (impl_->texture) {
        glDeleteTextures(1, &impl_->texture);
        impl_->texture = 0;
    }
    impl_->width = 0;
    impl_->height = 0;
    impl_->current.clear();
    impl_->receiving = false;
    impl_->sized = false;
    impl_->status = SpoutReceiveStatus{};
}

bool SpoutInput::active() const {
    return impl_->receiving || !impl_->current.empty();
}

unsigned int SpoutInput::texture() const {
    return impl_->texture;
}

int SpoutInput::width() const {
    return impl_->width;
}

int SpoutInput::height() const {
    return impl_->height;
}

const SpoutReceiveStatus& SpoutInput::status() const {
    return impl_->status;
}

bool SpoutInput::receive(const std::string& preferred, const std::string& excluded) {
    std::string wanted = preferred;
    if (wanted.empty()) {
        if (impl_->status.connected && !impl_->current.empty()) {
            wanted = impl_->current;
        } else {
            const double now = nowSeconds();
            if (now - impl_->lastLookup >= 0.5) {
                impl_->lastLookup = now;
                for (const std::string& name : senders()) {
                    if (name != excluded) {
                        wanted = name;
                        break;
                    }
                }
            } else {
                wanted = impl_->current;
            }
        }
    }
    if (wanted.empty()) {
        if (active()) {
            impl_->spout.ReleaseReceiver();
            impl_->current.clear();
            impl_->receiving = false;
            impl_->sized = false;
        }
        impl_->status = SpoutReceiveStatus{};
        return false;
    }
    if (wanted != impl_->current) {
        impl_->spout.ReleaseReceiver();
        impl_->spout.SetReceiverName(wanted.c_str());
        impl_->current = wanted;
        impl_->receiving = false;
        impl_->sized = false;
        impl_->width = 0;
        impl_->height = 0;
        logLine("Spout receiver looks for '" + wanted + "'");
    }
    if (!impl_->texture) {
        glGenTextures(1, &impl_->texture);
        glBindTexture(GL_TEXTURE_2D, impl_->texture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 16, 16, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindTexture(GL_TEXTURE_2D, 0);
        impl_->width = 16;
        impl_->height = 16;
    }
    const bool ok = impl_->spout.ReceiveTexture(impl_->texture, GL_TEXTURE_2D, true, 0);
    if (impl_->spout.IsUpdated()) {
        const int w = static_cast<int>(impl_->spout.GetSenderWidth());
        const int h = static_cast<int>(impl_->spout.GetSenderHeight());
        if (w > 0 && h > 0) {
            glBindTexture(GL_TEXTURE_2D, impl_->texture);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
            glBindTexture(GL_TEXTURE_2D, 0);
            impl_->width = w;
            impl_->height = h;
            impl_->sized = true;
            logLine("Spout receiver '" + impl_->current + "' connected, " + std::to_string(w) + "x" + std::to_string(h));
        }
        impl_->receiving = true;
        impl_->status.connected = true;
        impl_->status.hasFrame = false;
        impl_->status.sender = impl_->current;
        impl_->status.width = impl_->width;
        impl_->status.height = impl_->height;
        return false;
    }
    const bool connected = ok && impl_->spout.IsConnected();
    impl_->receiving = connected;
    impl_->status.connected = connected;
    impl_->status.hasFrame = connected && impl_->sized;
    impl_->status.sender = impl_->current;
    impl_->status.width = connected ? impl_->width : 0;
    impl_->status.height = connected ? impl_->height : 0;
    impl_->status.fps = connected ? impl_->spout.GetSenderFps() : 0.0;
    return impl_->status.hasFrame;
}

}
