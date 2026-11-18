#include "render/spout_output.hpp"

namespace nvj {

struct SpoutOutput::Impl {
    std::string name;
};

SpoutOutput::SpoutOutput() : impl_(std::make_unique<Impl>()) {}

SpoutOutput::~SpoutOutput() = default;

bool SpoutOutput::supported() {
    return false;
}

bool SpoutOutput::start(const std::string&) {
    return false;
}

void SpoutOutput::stop() {}

void SpoutOutput::send(unsigned int, int, int) {}

bool SpoutOutput::active() const {
    return false;
}

bool SpoutOutput::connected() const {
    return false;
}

const std::string& SpoutOutput::name() const {
    return impl_->name;
}

}
