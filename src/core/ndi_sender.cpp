#include "core/ndi_sender.hpp"

#include "core/ndi_runtime.hpp"

#include <algorithm>
#include <cstring>
#include <mutex>

namespace nvj {

namespace {

std::mutex sendMutex;

}

struct NdiSender::Impl {
    ndi::Api api;
    void* sender = nullptr;
    std::string name;
    std::string error;
    std::vector<uint8_t> frame;
};

NdiSender::NdiSender() : impl_(std::make_unique<Impl>()) {}

NdiSender::~NdiSender() {
    stop();
}

bool NdiSender::available() {
    ndi::Api api;
    std::string error;
    return ndi::loadForSending(api, error);
}

void NdiSender::recheck() {
    ndi::recheck();
}

std::string NdiSender::loadedLibrary() {
    return ndi::loadedLibrary();
}

std::string NdiSender::loadError() {
    return ndi::loadError();
}

void NdiSender::setLibraryOverride(const std::string& path) {
    ndi::setLibraryOverride(path);
}

bool NdiSender::start(const std::string& name) {
    stop();
    impl_->error.clear();
    if (!ndi::loadForSending(impl_->api, impl_->error)) {
        return false;
    }
    impl_->name = name.empty() ? "NyrexVJ" : name.substr(0, 60);
    ndi::SendCreate create{};
    create.name = impl_->name.c_str();
    create.groups = nullptr;
    create.clockVideo = false;
    create.clockAudio = false;
    std::lock_guard<std::mutex> lock(sendMutex);
    impl_->sender = impl_->api.sendCreate(&create);
    if (!impl_->sender) {
        impl_->error = "NDI sender could not be created";
        return false;
    }
    return true;
}

void NdiSender::stop() {
    if (!impl_->sender) {
        return;
    }
    std::lock_guard<std::mutex> lock(sendMutex);
    impl_->api.sendDestroy(impl_->sender);
    impl_->sender = nullptr;
}

bool NdiSender::active() const {
    return impl_->sender != nullptr;
}

int NdiSender::connections() const {
    if (!impl_->sender) {
        return 0;
    }
    std::lock_guard<std::mutex> lock(sendMutex);
    if (!impl_->api.sendConnections) {
        return 1;
    }
    const int count = impl_->api.sendConnections(impl_->sender, 0);
    return count > 0 ? count : 0;
}

bool NdiSender::sendRgbaBottomUp(const uint8_t* rgba, int width, int height) {
    if (!impl_->sender || !rgba || width <= 0 || height <= 0) {
        return false;
    }
    const size_t stride = static_cast<size_t>(width) * 4u;
    impl_->frame.resize(stride * static_cast<size_t>(height));
    for (int y = 0; y < height; ++y) {
        std::memcpy(impl_->frame.data() + static_cast<size_t>(y) * stride, rgba + static_cast<size_t>(height - 1 - y) * stride, stride);
    }
    ndi::VideoFrame frame{};
    frame.xres = width;
    frame.yres = height;
    frame.fourCC = ndi::kFourCCRgbx;
    frame.rateN = 60;
    frame.rateD = 1;
    frame.aspect = static_cast<float>(width) / static_cast<float>(height);
    frame.formatType = ndi::kFormatProgressive;
    frame.timecode = ndi::kTimecodeSynthesize;
    frame.data = impl_->frame.data();
    frame.stride = static_cast<int>(stride);
    frame.metadata = nullptr;
    frame.timestamp = ndi::kTimestampUndefined;
    std::lock_guard<std::mutex> lock(sendMutex);
    impl_->api.sendVideo(impl_->sender, &frame);
    return true;
}

const std::string& NdiSender::name() const {
    return impl_->name;
}

const std::string& NdiSender::error() const {
    return impl_->error;
}

}
