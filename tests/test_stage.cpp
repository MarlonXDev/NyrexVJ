#include "check.hpp"

#include "control/stage_out.hpp"
#include "control/udp_socket.hpp"
#include "core/settings.hpp"
#include "core/stage.hpp"
#include "core/stage_json.hpp"

#include <cstdint>
#include <chrono>
#include <cstring>
#include <limits>
#include <thread>
#include <vector>

namespace {

float noSource(int) {
    return 0.0f;
}

void testBinding() {
    nvj::SourceBinding fixed;
    fixed.source = 0;
    fixed.fixed = 0.5f;
    CHECK(nvj::evaluateBinding(fixed, noSource) == 0.5f);

    nvj::SourceBinding live;
    live.source = 3;
    auto source = [](int id) { return id == 3 ? 0.25f : 0.9f; };
    CHECK(nvj::evaluateBinding(live, source) == 0.25f);

    live.invert = true;
    CHECK(nvj::evaluateBinding(live, source) == 0.75f);

    live.invert = false;
    live.low = 0.5f;
    live.high = 1.0f;
    CHECK(nvj::evaluateBinding(live, source) == 0.625f);

    live.low = 1.0f;
    live.high = 0.0f;
    CHECK(nvj::evaluateBinding(live, source) == 0.75f);

    nvj::SourceBinding nan;
    nan.source = 1;
    auto bad = [](int) { return std::numeric_limits<float>::infinity(); };
    const float value = nvj::evaluateBinding(nan, bad);
    CHECK(value >= 0.0f && value <= 1.0f);

    auto over = [](int) { return 7.0f; };
    CHECK(nvj::evaluateBinding(nan, over) == 1.0f);
    CHECK(nvj::evaluateBinding(nan, nullptr) == 0.0f);
}

void testNormalize() {
    nvj::DmxConfig config;
    config.artnetNet = 999;
    config.artnetFirstUniverse = 999;
    config.artnetRate = 0;
    config.artnetHost.clear();
    nvj::DmxLane lane;
    lane.universe = 9;
    lane.channel = 999;
    lane.fine = true;
    lane.binding.source = 99;
    lane.binding.low = -4.0f;
    lane.binding.high = 5.0f;
    config.lanes.push_back(lane);
    config.lanes.resize(500, lane);
    nvj::normalizeDmx(config);
    CHECK(config.artnetNet == 127);
    CHECK(config.artnetFirstUniverse == 251);
    CHECK(config.artnetRate == 1);
    CHECK(config.artnetHost == "127.0.0.1");
    CHECK(config.lanes.size() == nvj::kMaxDmxLanes);
    CHECK(config.lanes[0].universe == nvj::kDmxUniverses);
    CHECK(config.lanes[0].channel == nvj::kDmxChannels);
    CHECK(!config.lanes[0].fine);
    CHECK(config.lanes[0].binding.source == nvj::kStageSourceCount - 1);
    CHECK(config.lanes[0].binding.low == 0.0f);
    CHECK(config.lanes[0].binding.high == 1.0f);

    nvj::VrchatConfig vr;
    vr.port = 0;
    vr.rate = 900;
    vr.host.clear();
    for (int i = 0; i < 40; ++i) {
        nvj::VrchatParam param;
        param.name = "Param" + std::to_string(i);
        vr.params.push_back(param);
    }
    nvj::VrchatParam dup;
    dup.name = "Param1";
    vr.params.push_back(dup);
    nvj::VrchatParam bad;
    bad.name = "has space";
    vr.params.insert(vr.params.begin(), bad);
    nvj::normalizeVrchat(vr);
    CHECK(vr.port == 1);
    CHECK(vr.rate == 60);
    CHECK(vr.host == "127.0.0.1");
    CHECK(vr.params.size() == nvj::kMaxVrchatParams);
    CHECK(vr.params[0].name == "Param0");

    CHECK(nvj::validVrchatName("Dmx_Intensity-1.a"));
    CHECK(!nvj::validVrchatName(""));
    CHECK(!nvj::validVrchatName("a/b"));
    CHECK(!nvj::validVrchatName(std::string(65, 'a')));
}

void testCompute() {
    nvj::DmxConfig config;
    nvj::DmxLane a;
    a.universe = 1;
    a.channel = 1;
    a.binding.source = 0;
    a.binding.fixed = 1.0f;
    nvj::DmxLane b;
    b.universe = 2;
    b.channel = 10;
    b.binding.source = 2;
    nvj::DmxLane c;
    c.universe = 1;
    c.channel = 100;
    c.fine = true;
    c.binding.source = 0;
    c.binding.fixed = 0.5f;
    config.lanes = {a, b, c};
    nvj::normalizeDmx(config);

    nvj::DmxUniverses out{};
    auto source = [](int id) { return id == 2 ? 0.5f : 0.0f; };
    nvj::computeDmx(config, source, out);
    CHECK(out[0][0] == 255);
    CHECK(out[1][9] == 128);
    const int wide = (out[0][99] << 8) | out[0][100];
    CHECK(wide == 32768);
    CHECK(out[0][1] == 0);
    CHECK(out[3][511] == 0);

    nvj::computeDmx(nvj::DmxConfig{}, source, out);
    CHECK(out[0][0] == 0);

    nvj::fillDmxFull(out);
    CHECK(out[0][0] == 255 && out[3][511] == 255);
    nvj::fillDmxRamp(out);
    CHECK(out[0][0] == 0 && out[0][511] == 255 && out[2][256] == 128);
    nvj::fillDmxChase(out, 5, 10);
    CHECK(out[0][5] == 255 && out[0][4] == 0);
    nvj::fillDmxChase(out, -1, 10);
    CHECK(out[0][9] == 255);
}

void testArtnet() {
    std::vector<std::uint8_t> data(512);
    for (size_t i = 0; i < data.size(); ++i) {
        data[i] = static_cast<std::uint8_t>(i);
    }
    const auto packet = nvj::artnetDmxPacket(0x0123, 7, data.data(), 512);
    CHECK(packet.size() == 18u + 512u);
    CHECK(std::memcmp(packet.data(), "Art-Net\0", 8) == 0);
    CHECK(packet[8] == 0x00 && packet[9] == 0x50);
    CHECK(packet[10] == 0 && packet[11] == 14);
    CHECK(packet[12] == 7);
    CHECK(packet[13] == 0);
    CHECK(packet[14] == 0x23 && packet[15] == 0x01);
    CHECK(packet[16] == 0x02 && packet[17] == 0x00);
    CHECK(packet[18] == 0 && packet[19] == 1 && packet[18 + 511] == 255);

    const auto odd = nvj::artnetDmxPacket(1, 0, data.data(), 5);
    CHECK(odd.size() == 18u + 6u);
    CHECK(odd[16] == 0 && odd[17] == 6);

    const auto tiny = nvj::artnetDmxPacket(1, 0, data.data(), 0);
    CHECK(tiny.size() == 20u);

    nvj::DmxConfig config;
    config.artnetNet = 2;
    config.artnetFirstUniverse = 3;
    CHECK(nvj::artnetUniverse(config, 0) == 0x0203);
    CHECK(nvj::artnetUniverse(config, 3) == 0x0206);
}

void testVrsl() {
    CHECK(nvj::vrslCell(1, 1, false) == 0);
    CHECK(nvj::vrslCell(1, 512, false) == 511);
    CHECK(nvj::vrslCell(2, 1, false) == 512);
    CHECK(nvj::vrslCell(3, 512, false) == 1535);
    CHECK(nvj::vrslCell(4, 1, false) == 1536);
    CHECK(nvj::vrslCell(4, 24, false) == 1559);
    CHECK(nvj::vrslCell(4, 25, false) == -1);
    CHECK(nvj::vrslCell(0, 1, false) == -1);
    CHECK(nvj::vrslCell(1, 0, false) == -1);

    CHECK(nvj::vrslCell(1, 1, true) == 0);
    CHECK(nvj::vrslCell(2, 1, true) == 520);
    CHECK(nvj::vrslCell(3, 512, true) == 1040 + 511);
    CHECK(nvj::vrslCell(4, 1, true) == -1);

    int column = -1;
    int row = -1;
    nvj::vrslPixel(0, column, row);
    CHECK(column == 0 && row == 0);
    nvj::vrslPixel(12, column, row);
    CHECK(column == 0 && row == 12);
    nvj::vrslPixel(13, column, row);
    CHECK(column == 1 && row == 0);
    nvj::vrslPixel(1559, column, row);
    CHECK(column == 119 && row == 12);

    CHECK(nvj::vrslPlace(1, 1, false) == "1/1");
    CHECK(nvj::vrslPlace(1, 14, false) == "2/1");
    CHECK(nvj::vrslPlace(4, 99, false).empty());

    nvj::DmxUniverses universes{};
    universes[0][0] = 10;
    universes[0][13] = 20;
    universes[1][0] = 30;
    universes[2][511] = 40;
    std::vector<std::uint8_t> grid(nvj::kVrslCells, 99);
    nvj::renderVrslGrid(universes, false, grid.data());
    CHECK(grid[0 * nvj::kVrslColumns + 0] == 10);
    CHECK(grid[0 * nvj::kVrslColumns + 1] == 20);
    int c = 0;
    int r = 0;
    nvj::vrslPixel(512, c, r);
    CHECK(grid[r * nvj::kVrslColumns + c] == 30);
    nvj::vrslPixel(1535, c, r);
    CHECK(grid[r * nvj::kVrslColumns + c] == 40);
    CHECK(grid[5 * nvj::kVrslColumns + 5] == 0);

    nvj::renderVrslGrid(universes, true, grid.data());
    nvj::vrslPixel(520, c, r);
    CHECK(c == 40 && r == 0);
    CHECK(grid[r * nvj::kVrslColumns + c] == 30);

    int nonZero = 0;
    for (const std::uint8_t v : grid) {
        nonZero += v != 0 ? 1 : 0;
    }
    CHECK(nonZero == 4);
}

void testVrslBijective() {
    for (const bool pad : {false, true}) {
        std::vector<int> seen(nvj::kVrslCells, 0);
        int mapped = 0;
        for (int u = 1; u <= nvj::kDmxUniverses; ++u) {
            for (int ch = 1; ch <= nvj::kDmxChannels; ++ch) {
                const int cell = nvj::vrslCell(u, ch, pad);
                if (cell < 0) {
                    continue;
                }
                CHECK(cell < nvj::kVrslCells);
                ++seen[static_cast<size_t>(cell)];
                ++mapped;
            }
        }
        bool unique = true;
        for (const int count : seen) {
            unique = unique && count <= 1;
        }
        CHECK(unique);
        CHECK(mapped == (pad ? 1536 : 1560));
    }
}


int drain(nvj::UdpSocket& socket, std::vector<std::vector<unsigned char>>& packets) {
    unsigned char buffer[2048];
    int count = 0;
    for (int attempt = 0; attempt < 50; ++attempt) {
        const int size = socket.receive(buffer, sizeof(buffer));
        if (size > 0) {
            packets.emplace_back(buffer, buffer + size);
            ++count;
            attempt = 0;
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }
    return count;
}

void testArtnetSending() {
    nvj::UdpSocket receiver;
    std::string error;
    CHECK(receiver.open(46454, false, error));

    nvj::DmxConfig dmx;
    dmx.artnetEnabled = true;
    dmx.artnetHost = "127.0.0.1";
    dmx.artnetPort = 46454;
    dmx.artnetRate = 40;
    dmx.artnetFirstUniverse = 5;
    dmx.artnetNet = 1;
    nvj::DmxLane lane;
    lane.universe = 2;
    lane.channel = 3;
    lane.binding.source = 4;
    dmx.lanes = {lane};
    nvj::VrchatConfig vrchat;

    nvj::StageOutput output;
    output.configure(dmx, vrchat);
    CHECK(output.artnetSending());
    CHECK(output.artnetError().empty());
    auto source = [](int id) { return id == 4 ? 1.0f : 0.0f; };
    for (int i = 0; i < 4; ++i) {
        output.update(0.05, source);
    }
    CHECK(output.universes()[1][2] == 255);
    CHECK(output.artnetPackets() == 4);

    std::vector<std::vector<unsigned char>> packets;
    CHECK(drain(receiver, packets) == 4);
    if (packets.size() == 4) {
        const auto& p = packets[0];
        CHECK(p.size() == 18u + 512u);
        CHECK(std::memcmp(p.data(), "Art-Net\0", 8) == 0);
        CHECK(p[14] == 6 && p[15] == 1);
        CHECK(p[18 + 2] == 255);
        CHECK(packets[1][12] == 2 && packets[3][12] == 4);
    }

    output.setTest(nvj::StageTest::Full);
    output.update(0.05, source);
    packets.clear();
    CHECK(drain(receiver, packets) == nvj::kDmxUniverses);

    output.setTest(nvj::StageTest::Off);
    dmx.artnetEnabled = false;
    output.configure(dmx, vrchat);
    CHECK(!output.artnetSending());
    output.update(0.1, source);
    packets.clear();
    CHECK(drain(receiver, packets) == 0);

    dmx.artnetEnabled = true;
    dmx.artnetHost = "not.an.address";
    output.configure(dmx, vrchat);
    CHECK(!output.artnetSending());
    CHECK(!output.artnetError().empty());
}

void testVrchatSending() {
    nvj::UdpSocket receiver;
    std::string error;
    CHECK(receiver.open(49000, false, error));

    nvj::DmxConfig dmx;
    nvj::VrchatConfig vrchat;
    vrchat.enabled = true;
    vrchat.port = 49000;
    vrchat.rate = 20;
    nvj::VrchatParam level;
    level.name = "Level";
    level.binding.source = 4;
    nvj::VrchatParam kick;
    kick.name = "KickOn";
    kick.type = nvj::VrchatType::Bool;
    kick.binding.source = 5;
    nvj::VrchatParam step;
    step.name = "Step";
    step.type = nvj::VrchatType::Int;
    step.binding.source = 4;
    vrchat.params = {level, kick, step};

    nvj::StageOutput output;
    output.configure(dmx, vrchat);
    CHECK(output.vrchatSending());

    float amount = 0.5f;
    auto source = [&amount](int id) { return id == 4 ? amount : (id == 5 ? 1.0f : 0.0f); };
    auto first = output.composeVrchat(source, false);
    CHECK(first.size() == 3);
    CHECK(first[0].address == "/avatar/parameters/Level");
    CHECK(first[2].address == "/avatar/parameters/Step");
    auto second = output.composeVrchat(source, false);
    CHECK(second.empty());
    amount = 0.75f;
    auto third = output.composeVrchat(source, false);
    CHECK(third.size() == 2);
    auto forced = output.composeVrchat(source, true);
    CHECK(forced.size() == 3);

    for (int i = 0; i < 3; ++i) {
        output.update(0.06, source);
    }
    std::vector<std::vector<unsigned char>> packets;
    CHECK(drain(receiver, packets) >= 1);
    if (!packets.empty()) {
        CHECK(std::memcmp(packets[0].data(), "#bundle", 8) == 0);
    }
    CHECK(output.vrchatPackets() >= 1);
}


void testJson() {
    nvj::DmxConfig dmx;
    dmx.artnetEnabled = true;
    dmx.artnetHost = "192.168.1.20";
    dmx.artnetPort = 6455;
    dmx.artnetNet = 3;
    dmx.artnetFirstUniverse = 7;
    dmx.artnetRate = 25;
    dmx.stripEnabled = true;
    dmx.stripAtTop = false;
    dmx.stripPadUniverses = true;
    nvj::DmxLane lane;
    lane.name = "Dimmer";
    lane.universe = 3;
    lane.channel = 44;
    lane.fine = true;
    lane.binding.source = 12;
    lane.binding.low = 0.1f;
    lane.binding.high = 0.9f;
    lane.binding.invert = true;
    dmx.lanes = {lane};
    nvj::DmxConfig back;
    nvj::dmxFromJson(nvj::dmxToJson(dmx), back);
    CHECK(back == dmx);

    nvj::VrchatConfig vr;
    vr.enabled = true;
    vr.host = "127.0.0.1";
    vr.port = 9001;
    vr.rate = 20;
    nvj::VrchatParam param;
    param.name = "Dmx_Strobe";
    param.type = nvj::VrchatType::Int;
    param.binding.source = 18;
    param.binding.fixed = 0.25f;
    vr.params = {param};
    nvj::VrchatConfig vrBack;
    nvj::vrchatFromJson(nvj::vrchatToJson(vr), vrBack);
    CHECK(vrBack == vr);

    nvj::DmxConfig fromJunk;
    nvj::dmxFromJson(nlohmann::json::parse("{\"artnetPort\": \"x\", \"lanes\": [1, {\"channel\": 9999}, null]}"), fromJunk);
    CHECK(fromJunk.artnetPort == 6454);
    CHECK(fromJunk.lanes.size() == 1);
    CHECK(fromJunk.lanes[0].channel == nvj::kDmxChannels);
    nvj::dmxFromJson(nlohmann::json(5), fromJunk);
    CHECK(fromJunk.lanes.empty());
    nvj::vrchatFromJson(nlohmann::json::parse("{\"params\": [{\"name\": \"bad name\"}, {\"name\": \"ok\", \"type\": 9}]}"), vrBack);
    CHECK(vrBack.params.size() == 1);
    CHECK(vrBack.params[0].type == nvj::VrchatType::Int);

    nvj::Settings settings;
    CHECK(!settings.dmx.artnetEnabled);
    CHECK(!settings.vrchat.enabled);
}

}

int main() {
    testBinding();
    testNormalize();
    testCompute();
    testArtnet();
    testVrsl();
    testVrslBijective();
    testJson();
    testArtnetSending();
    testVrchatSending();
    return testing::finish();
}
