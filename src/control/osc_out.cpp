#include "control/osc_out.hpp"

#include <algorithm>
#include <cmath>

namespace nvj {

namespace {

constexpr size_t kMaxPacket = 1400;

float unit(float value) {
    return std::isfinite(value) ? std::clamp(value, 0.0f, 1.0f) : 0.0f;
}

OscMessage message(const std::string& address) {
    OscMessage out;
    out.address = address;
    return out;
}

bool sameParams(const std::vector<OscOutParam>& a, const std::vector<OscOutParam>& b) {
    if (a.size() != b.size()) {
        return false;
    }
    for (size_t i = 0; i < a.size(); ++i) {
        if (a[i].name != b[i].name || a[i].target != b[i].target) {
            return false;
        }
    }
    return true;
}

bool sameConfig(const OscOutConfig& a, const OscOutConfig& b) {
    return a.enabled == b.enabled && a.host == b.host && a.port == b.port && a.rate == b.rate && a.spectrum == b.spectrum && a.inputPort == b.inputPort && a.version == b.version && sameParams(a.params, b.params);
}

}

bool validOscParamName(const std::string& name) {
    if (name.empty() || name.size() > 32) {
        return false;
    }
    for (const char c : name) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
        if (!ok) {
            return false;
        }
    }
    return true;
}

void OscSender::resolveParams() {
    resolved_.clear();
    for (const OscOutParam& param : config_.params) {
        if (resolved_.size() >= kOscMaxParams || !validOscParamName(param.name)) {
            continue;
        }
        const int target = controlTargetIndex(param.target);
        if (target < 0 || controlTargets()[static_cast<size_t>(target)].kind == ControlKind::Trigger) {
            continue;
        }
        const std::string address = "/nyrex/out/param/" + param.name;
        const bool duplicate = std::any_of(resolved_.begin(), resolved_.end(), [&address](const Resolved& item) { return item.address == address; });
        if (duplicate) {
            continue;
        }
        resolved_.push_back({address, target});
    }
}

void OscSender::configure(const OscOutConfig& config) {
    if (configured_ && sameConfig(config, config_)) {
        return;
    }
    const bool wasEnabled = configured_ && config_.enabled;
    const bool targetChanged = !configured_ || config.host != config_.host || config.port != config_.port;
    config_ = config;
    config_.rate = std::clamp(config_.rate, 10, 120);
    configured_ = true;
    resolveParams();
    if (!config_.enabled) {
        stop();
        error_.clear();
        return;
    }
    if (!socket_.isOpen() || targetChanged || !wasEnabled) {
        socket_.close();
        std::string error;
        if (!socket_.connectTo(config_.host, config_.port, error)) {
            error_ = error;
            return;
        }
        error_.clear();
        accumulator_ = 0.0;
        nextHello_ = 0.0;
        helloBurst_ = 2;
        haveBeat_ = false;
        haveOnset_ = false;
    }
}

void OscSender::stop() {
    if (socket_.isOpen()) {
        transmit(bye());
        socket_.close();
    }
}

std::vector<OscMessage> OscSender::hello() const {
    OscMessage out = message("/nyrex/out/hello");
    out.arguments.push_back(OscArgument::fromText("NyrexVJ"));
    out.arguments.push_back(OscArgument::fromInt(kOscSchema));
    out.arguments.push_back(OscArgument::fromText(config_.version));
    out.arguments.push_back(OscArgument::fromInt(config_.inputPort));
    return {out};
}

std::vector<OscMessage> OscSender::bye() const {
    OscMessage out = message("/nyrex/out/bye");
    out.arguments.push_back(OscArgument::fromInt(kOscSchema));
    return {out};
}

std::vector<OscMessage> OscSender::compose(const OscOutFrame& frame, const ControlRouter& router, bool tick) {
    std::vector<OscMessage> out;
    const double whole = std::floor(std::max(frame.beat, 0.0));
    if (tick) {
        OscMessage audio = message("/nyrex/out/audio");
        audio.arguments.push_back(OscArgument::fromFloat(unit(frame.audio.bass)));
        audio.arguments.push_back(OscArgument::fromFloat(unit(frame.audio.mid)));
        audio.arguments.push_back(OscArgument::fromFloat(unit(frame.audio.high)));
        audio.arguments.push_back(OscArgument::fromFloat(unit(frame.audio.level)));
        audio.arguments.push_back(OscArgument::fromFloat(unit(frame.audio.kick)));
        out.push_back(std::move(audio));

        if (config_.spectrum) {
            OscMessage spectrum = message("/nyrex/out/spectrum");
            constexpr int perBand = kSpectrumBins / kOscSpectrumBands;
            for (int band = 0; band < kOscSpectrumBands; ++band) {
                float sum = 0.0f;
                for (int k = 0; k < perBand; ++k) {
                    sum += frame.audio.spectrum[static_cast<size_t>(band * perBand + k)];
                }
                spectrum.arguments.push_back(OscArgument::fromFloat(unit(sum / static_cast<float>(perBand))));
            }
            out.push_back(std::move(spectrum));
        }

        OscMessage clock = message("/nyrex/out/clock");
        clock.arguments.push_back(OscArgument::fromFloat(frame.bpm));
        clock.arguments.push_back(OscArgument::fromInt(static_cast<int>(whole)));
        clock.arguments.push_back(OscArgument::fromFloat(static_cast<float>(std::max(frame.beat, 0.0) - whole)));
        out.push_back(std::move(clock));

        for (const Resolved& param : resolved_) {
            OscMessage item = message(param.address);
            item.arguments.push_back(OscArgument::fromFloat(unit(router.value(param.target))));
            out.push_back(std::move(item));
        }
    }

    const long long beatNumber = static_cast<long long>(whole);
    if (!haveBeat_) {
        haveBeat_ = true;
        lastBeat_ = beatNumber;
    } else if (beatNumber > lastBeat_) {
        lastBeat_ = beatNumber;
        OscMessage beat = message("/nyrex/out/beat");
        beat.arguments.push_back(OscArgument::fromInt(static_cast<int>(beatNumber)));
        beat.arguments.push_back(OscArgument::fromInt(static_cast<int>(beatNumber % 4)));
        out.push_back(std::move(beat));
    } else if (beatNumber < lastBeat_) {
        lastBeat_ = beatNumber;
    }

    if (!haveOnset_) {
        haveOnset_ = true;
        lastOnset_ = frame.audio.beatCount;
    } else if (frame.audio.beatCount != lastOnset_) {
        lastOnset_ = frame.audio.beatCount;
        OscMessage onset = message("/nyrex/out/onset");
        onset.arguments.push_back(OscArgument::fromInt(static_cast<int>(frame.audio.beatCount & 0x7fffffffu)));
        out.push_back(std::move(onset));
    }
    return out;
}

void OscSender::transmit(const std::vector<OscMessage>& messages) {
    std::vector<OscMessage> chunk;
    size_t bytes = 16;
    auto flush = [this, &chunk, &bytes]() {
        if (!chunk.empty()) {
            const std::vector<unsigned char> packet = encodeOscBundle(chunk);
            if (socket_.send(packet.data(), static_cast<int>(packet.size()))) {
                ++packets_;
            }
            chunk.clear();
            bytes = 16;
        }
    };
    for (const OscMessage& item : messages) {
        const size_t size = encodeOscMessage(item).size() + 4;
        if (!chunk.empty() && bytes + size > kMaxPacket) {
            flush();
        }
        chunk.push_back(item);
        bytes += size;
    }
    flush();
}

void OscSender::update(double dt, const OscOutFrame& frame, const ControlRouter& router) {
    if (!config_.enabled || !socket_.isOpen()) {
        return;
    }
    const double interval = 1.0 / static_cast<double>(config_.rate);
    accumulator_ += std::max(dt, 0.0);
    bool tick = false;
    if (accumulator_ >= interval) {
        tick = true;
        accumulator_ -= interval;
        if (accumulator_ >= interval) {
            accumulator_ = 0.0;
        }
    }
    std::vector<OscMessage> messages;
    nextHello_ -= std::max(dt, 0.0);
    if (nextHello_ <= 0.0) {
        messages = hello();
        if (helloBurst_ > 0) {
            --helloBurst_;
            nextHello_ = 0.25;
        } else {
            nextHello_ = 2.0;
        }
    }
    std::vector<OscMessage> frameMessages = compose(frame, router, tick);
    messages.insert(messages.end(), frameMessages.begin(), frameMessages.end());
    if (!messages.empty()) {
        transmit(messages);
    }
}

}
