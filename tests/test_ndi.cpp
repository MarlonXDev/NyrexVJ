#include "check.hpp"

#include "core/ndi_sender.hpp"

#include <cstdint>
#include <vector>

#define CHECK(expr) testing::check((expr), #expr, __LINE__)

extern "C" {
void fake_ndi_set_connections(int value);
int fake_ndi_frames();
int fake_ndi_created();
int fake_ndi_destroyed();
int fake_ndi_width();
int fake_ndi_height();
int fake_ndi_stride();
uint32_t fake_ndi_fourcc();
int fake_ndi_format();
int fake_ndi_rate_n();
int fake_ndi_rate_d();
int64_t fake_ndi_timecode();
const char* fake_ndi_name();
const uint8_t* fake_ndi_last();
int fake_ndi_last_size();
}

namespace {

void testMissingLibrary() {
    nvj::NdiSender::setLibraryOverride("/nonexistent/path/libndi_missing.so");
    nvj::NdiSender sender;
    CHECK(!nvj::NdiSender::available());
    CHECK(!nvj::NdiSender::loadError().empty());
    CHECK(!sender.start("Test"));
    CHECK(!sender.active());
    CHECK(!sender.error().empty());
    uint8_t pixel[4] = {1, 2, 3, 255};
    CHECK(!sender.sendRgbaBottomUp(pixel, 1, 1));
    CHECK(sender.connections() == 0);
}

void testSending() {
    nvj::NdiSender::setLibraryOverride(NYREXVJ_FAKE_NDI_PATH);
    nvj::NdiSender::recheck();
    CHECK(nvj::NdiSender::available());
    CHECK(!nvj::NdiSender::loadedLibrary().empty());
    CHECK(nvj::NdiSender::loadError().empty());

    nvj::NdiSender sender;
    CHECK(sender.start("Nyrex Test"));
    CHECK(sender.active());
    CHECK(sender.name() == "Nyrex Test");
    CHECK(std::string(fake_ndi_name()) == "Nyrex Test");
    CHECK(fake_ndi_created() == 1);

    fake_ndi_set_connections(3);
    CHECK(sender.connections() == 3);
    fake_ndi_set_connections(0);
    CHECK(sender.connections() == 0);

    const int width = 4;
    const int height = 3;
    std::vector<uint8_t> rgba(static_cast<size_t>(width * height * 4));
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const size_t at = static_cast<size_t>((y * width + x) * 4);
            rgba[at] = static_cast<uint8_t>(x * 10);
            rgba[at + 1] = static_cast<uint8_t>(y * 20);
            rgba[at + 2] = static_cast<uint8_t>(100 + x + y);
            rgba[at + 3] = 255;
        }
    }
    CHECK(sender.sendRgbaBottomUp(rgba.data(), width, height));
    CHECK(fake_ndi_frames() == 1);
    CHECK(fake_ndi_width() == width && fake_ndi_height() == height);
    CHECK(fake_ndi_stride() == width * 4);
    CHECK(fake_ndi_fourcc() == (static_cast<uint32_t>('R') | (static_cast<uint32_t>('G') << 8) | (static_cast<uint32_t>('B') << 16) | (static_cast<uint32_t>('X') << 24)));
    CHECK(fake_ndi_format() == 1);
    CHECK(fake_ndi_rate_n() == 60 && fake_ndi_rate_d() == 1);
    CHECK(fake_ndi_timecode() == INT64_MAX);
    CHECK(fake_ndi_last_size() == width * height * 4);

    const uint8_t* sent = fake_ndi_last();
    bool flipped = true;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const size_t out = static_cast<size_t>((y * width + x) * 4);
            const size_t in = static_cast<size_t>(((height - 1 - y) * width + x) * 4);
            for (size_t c = 0; c < 4; ++c) {
                flipped = flipped && sent[out + c] == rgba[in + c];
            }
        }
    }
    CHECK(flipped);

    CHECK(!sender.sendRgbaBottomUp(nullptr, width, height));
    CHECK(!sender.sendRgbaBottomUp(rgba.data(), 0, height));
    CHECK(fake_ndi_frames() == 1);

    CHECK(sender.start("Second"));
    CHECK(fake_ndi_destroyed() == 1);
    CHECK(fake_ndi_created() == 2);
    CHECK(std::string(fake_ndi_name()) == "Second");
    sender.stop();
    CHECK(!sender.active());
    CHECK(fake_ndi_destroyed() == 2);
    sender.stop();
    CHECK(fake_ndi_destroyed() == 2);
}

void testLongName() {
    nvj::NdiSender sender;
    CHECK(sender.start(std::string(200, 'x')));
    CHECK(sender.name().size() == 60);
    CHECK(sender.start(""));
    CHECK(sender.name() == "NyrexVJ");
}

}

int main() {
    testMissingLibrary();
    testSending();
    testLongName();
    return testing::finish();
}
