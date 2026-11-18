#include "render/spout_input.hpp"

#include "core/log.hpp"

#include <glad/gl.h>

#include <cmath>
#include <vector>

namespace nvj {

namespace {

bool gTestPattern = false;
constexpr int kPatternWidth = 640;
constexpr int kPatternHeight = 360;

}

struct SpoutInput::Impl {
    GLuint texture = 0;
    unsigned long long frame = 0;
    std::vector<unsigned char> pixels;
    SpoutReceiveStatus status;
};

SpoutInput::SpoutInput() : impl_(std::make_unique<Impl>()) {}

SpoutInput::~SpoutInput() {
    release();
}

bool SpoutInput::supported() {
    return false;
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
    if (gTestPattern) {
        return {testSenderName()};
    }
    return {};
}

void SpoutInput::release() {
    if (impl_->texture) {
        glDeleteTextures(1, &impl_->texture);
        impl_->texture = 0;
        logLine("Spout test receiver released");
    }
    impl_->frame = 0;
    impl_->status = SpoutReceiveStatus{};
}

bool SpoutInput::active() const {
    return impl_->texture != 0;
}

unsigned int SpoutInput::texture() const {
    return impl_->texture;
}

int SpoutInput::width() const {
    return impl_->texture ? kPatternWidth : 0;
}

int SpoutInput::height() const {
    return impl_->texture ? kPatternHeight : 0;
}

const SpoutReceiveStatus& SpoutInput::status() const {
    return impl_->status;
}

bool SpoutInput::receive(const std::string& preferred, const std::string& excluded) {
    const std::string name = testSenderName();
    const bool wanted = gTestPattern && (preferred.empty() ? excluded != name : preferred == name);
    if (!wanted) {
        release();
        return false;
    }
    if (!impl_->texture) {
        glGenTextures(1, &impl_->texture);
        glBindTexture(GL_TEXTURE_2D, impl_->texture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, kPatternWidth, kPatternHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        impl_->pixels.assign(static_cast<size_t>(kPatternWidth) * static_cast<size_t>(kPatternHeight) * 4u, 255);
        logLine("Spout test receiver connected, 640x360");
    }
    const int bar = static_cast<int>((impl_->frame * 4u) % static_cast<unsigned long long>(kPatternWidth));
    const float pulse = 0.5f + 0.5f * std::sin(static_cast<float>(impl_->frame) * 0.05f);
    for (int y = 0; y < kPatternHeight; ++y) {
        for (int x = 0; x < kPatternWidth; ++x) {
            unsigned char* p = &impl_->pixels[(static_cast<size_t>(y) * kPatternWidth + static_cast<size_t>(x)) * 4u];
            float r = static_cast<float>(x) / static_cast<float>(kPatternWidth);
            float g = static_cast<float>(y) / static_cast<float>(kPatternHeight);
            float b = 0.25f + 0.5f * pulse;
            if (x >= bar && x < bar + 6) {
                r = g = b = 1.0f;
            }
            if (x < 4 || y < 4 || x >= kPatternWidth - 4 || y >= kPatternHeight - 4) {
                r = g = b = 1.0f;
            }
            if (x >= 8 && x < 56 && y >= kPatternHeight - 56 && y < kPatternHeight - 8) {
                r = 1.0f;
                g = 0.0f;
                b = 0.0f;
            }
            if (x >= kPatternWidth - 56 && x < kPatternWidth - 8 && y >= 8 && y < 56) {
                r = 0.0f;
                g = 0.2f;
                b = 1.0f;
            }
            p[0] = static_cast<unsigned char>(r * 255.0f);
            p[1] = static_cast<unsigned char>(g * 255.0f);
            p[2] = static_cast<unsigned char>(b * 255.0f);
            p[3] = 255;
        }
    }
    glBindTexture(GL_TEXTURE_2D, impl_->texture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, kPatternWidth, kPatternHeight, GL_RGBA, GL_UNSIGNED_BYTE, impl_->pixels.data());
    glBindTexture(GL_TEXTURE_2D, 0);
    ++impl_->frame;
    impl_->status.connected = true;
    impl_->status.hasFrame = true;
    impl_->status.sender = name;
    impl_->status.width = kPatternWidth;
    impl_->status.height = kPatternHeight;
    impl_->status.fps = 60.0;
    return true;
}

}
