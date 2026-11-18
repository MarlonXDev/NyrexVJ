#include "render/spout_output.hpp"

#include "core/log.hpp"

#include <Spout.h>

namespace nvj {

struct SpoutOutput::Impl {
    Spout spout;
    std::string name;
    bool active = false;
    bool sent = false;
};

SpoutOutput::SpoutOutput() : impl_(std::make_unique<Impl>()) {}

SpoutOutput::~SpoutOutput() {
    stop();
}

bool SpoutOutput::supported() {
    return true;
}

bool SpoutOutput::start(const std::string& name) {
    stop();
    impl_->name = name.empty() ? "NyrexVJ" : name;
    impl_->spout.SetSenderName(impl_->name.c_str());
    impl_->active = true;
    impl_->sent = false;
    logLine("Spout sender '" + impl_->name + "' started");
    return true;
}

void SpoutOutput::stop() {
    if (impl_->active) {
        impl_->spout.ReleaseSender();
        logLine("Spout sender '" + impl_->name + "' stopped");
    }
    impl_->active = false;
    impl_->sent = false;
}

void SpoutOutput::send(unsigned int texture, int width, int height) {
    if (!impl_->active || texture == 0 || width <= 0 || height <= 0) {
        return;
    }
    impl_->sent = impl_->spout.SendTexture(texture, 0x0DE1, static_cast<unsigned int>(width), static_cast<unsigned int>(height), true, 0);
}

bool SpoutOutput::active() const {
    return impl_->active;
}

bool SpoutOutput::connected() const {
    return impl_->active && impl_->sent && impl_->spout.IsInitialized();
}

const std::string& SpoutOutput::name() const {
    return impl_->name;
}

}
