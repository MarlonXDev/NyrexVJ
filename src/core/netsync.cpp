#include "core/netsync.hpp"

#include "core/sha256.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace nvj::netsync {

namespace {

constexpr std::uint8_t kMagic[4] = {'N', 'V', 'J', 'S'};
constexpr size_t kMaxManifestEntries = 16;
constexpr size_t kMaxEstimatorSamples = 40;
constexpr double kBucketRate = 400.0;
constexpr double kBucketBurst = 800.0;
constexpr size_t kMaxBuckets = 256;
constexpr size_t kMaxSeenClients = 64;
constexpr std::uint64_t kRejectWindow = 64;

int hexValue(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

Bytes hexToBytes(const std::string& hex) {
    Bytes out;
    out.reserve(hex.size() / 2);
    for (size_t i = 0; i + 1 < hex.size(); i += 2) {
        out.push_back(static_cast<std::uint8_t>(hexValue(hex[i]) * 16 + hexValue(hex[i + 1])));
    }
    return out;
}

Bytes sha256Bytes(const std::uint8_t* data, size_t size) {
    Sha256 hash;
    hash.update(data, size);
    return hexToBytes(hash.finalHex());
}

void put16(Bytes& out, size_t at, std::uint16_t value) {
    out[at] = static_cast<std::uint8_t>(value >> 8);
    out[at + 1] = static_cast<std::uint8_t>(value);
}

std::uint64_t fnv(std::uint64_t hash, const std::string& text) {
    for (const char c : text) {
        hash ^= static_cast<unsigned char>(c);
        hash *= 1099511628211ull;
    }
    hash ^= 0xffu;
    hash *= 1099511628211ull;
    return hash;
}

bool finiteValue(float value) {
    return std::isfinite(value);
}

}

bool keyFromHex(const std::string& text, Key& key) {
    if (text.size() != kKeyBytes * 2) {
        return false;
    }
    Key parsed{};
    for (size_t i = 0; i < kKeyBytes; ++i) {
        const int high = hexValue(text[i * 2]);
        const int low = hexValue(text[i * 2 + 1]);
        if (high < 0 || low < 0) {
            return false;
        }
        parsed[i] = static_cast<std::uint8_t>(high * 16 + low);
    }
    key = parsed;
    return true;
}

std::string keyToHex(const Key& key) {
    static const char digits[] = "0123456789abcdef";
    std::string out;
    out.reserve(kKeyBytes * 2);
    for (const std::uint8_t byte : key) {
        out.push_back(digits[byte >> 4]);
        out.push_back(digits[byte & 15]);
    }
    return out;
}

bool keyIsZero(const Key& key) {
    for (const std::uint8_t byte : key) {
        if (byte != 0) {
            return false;
        }
    }
    return true;
}

Bytes hmacSha256(const std::uint8_t* key, size_t keySize, const std::uint8_t* data, size_t size) {
    std::array<std::uint8_t, 64> block{};
    if (keySize > block.size()) {
        const Bytes digest = sha256Bytes(key, keySize);
        std::copy(digest.begin(), digest.end(), block.begin());
    } else if (keySize > 0) {
        std::memcpy(block.data(), key, keySize);
    }
    std::array<std::uint8_t, 64> inner{};
    std::array<std::uint8_t, 64> outer{};
    for (size_t i = 0; i < block.size(); ++i) {
        inner[i] = block[i] ^ 0x36;
        outer[i] = block[i] ^ 0x5c;
    }
    Sha256 first;
    first.update(inner.data(), inner.size());
    if (size > 0) {
        first.update(data, size);
    }
    const Bytes innerDigest = hexToBytes(first.finalHex());
    Sha256 second;
    second.update(outer.data(), outer.size());
    second.update(innerDigest.data(), innerDigest.size());
    return hexToBytes(second.finalHex());
}

bool constantTimeEqual(const std::uint8_t* a, const std::uint8_t* b, size_t size) {
    std::uint8_t difference = 0;
    for (size_t i = 0; i < size; ++i) {
        difference |= static_cast<std::uint8_t>(a[i] ^ b[i]);
    }
    return difference == 0;
}

std::string limitName(const std::string& text) {
    if (text.size() <= kMaxNameBytes) {
        return text;
    }
    size_t cut = kMaxNameBytes;
    while (cut > 0 && (static_cast<unsigned char>(text[cut]) & 0xC0) == 0x80) {
        --cut;
    }
    return text.substr(0, cut);
}

void Writer::u8(std::uint8_t value) {
    data_.push_back(value);
}

void Writer::u16(std::uint16_t value) {
    data_.push_back(static_cast<std::uint8_t>(value >> 8));
    data_.push_back(static_cast<std::uint8_t>(value));
}

void Writer::u32(std::uint32_t value) {
    for (int shift = 24; shift >= 0; shift -= 8) {
        data_.push_back(static_cast<std::uint8_t>(value >> shift));
    }
}

void Writer::u64(std::uint64_t value) {
    for (int shift = 56; shift >= 0; shift -= 8) {
        data_.push_back(static_cast<std::uint8_t>(value >> shift));
    }
}

void Writer::f32(float value) {
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    u32(bits);
}

void Writer::f64(double value) {
    std::uint64_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    u64(bits);
}

void Writer::str(const std::string& text) {
    const std::string limited = limitName(text);
    u8(static_cast<std::uint8_t>(limited.size()));
    data_.insert(data_.end(), limited.begin(), limited.end());
}

bool Reader::take(size_t count) {
    if (!ok_ || size_ - pos_ < count) {
        ok_ = false;
        return false;
    }
    return true;
}

std::uint8_t Reader::u8() {
    if (!take(1)) {
        return 0;
    }
    return data_[pos_++];
}

std::uint16_t Reader::u16() {
    if (!take(2)) {
        return 0;
    }
    const std::uint16_t value = static_cast<std::uint16_t>((data_[pos_] << 8) | data_[pos_ + 1]);
    pos_ += 2;
    return value;
}

std::uint32_t Reader::u32() {
    if (!take(4)) {
        return 0;
    }
    std::uint32_t value = 0;
    for (int i = 0; i < 4; ++i) {
        value = (value << 8) | data_[pos_ + static_cast<size_t>(i)];
    }
    pos_ += 4;
    return value;
}

std::uint64_t Reader::u64() {
    if (!take(8)) {
        return 0;
    }
    std::uint64_t value = 0;
    for (int i = 0; i < 8; ++i) {
        value = (value << 8) | data_[pos_ + static_cast<size_t>(i)];
    }
    pos_ += 8;
    return value;
}

float Reader::f32() {
    const std::uint32_t bits = u32();
    float value = 0.0f;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

double Reader::f64() {
    const std::uint64_t bits = u64();
    double value = 0.0;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

std::string Reader::str() {
    const size_t length = u8();
    if (length > kMaxNameBytes || !take(length)) {
        ok_ = false;
        return std::string();
    }
    std::string text(reinterpret_cast<const char*>(data_ + pos_), length);
    pos_ += length;
    return text;
}

Bytes encodePacket(const Key& key, std::uint8_t version, Type type, std::uint64_t session, std::uint64_t seq, const Bytes& body) {
    if (kHeaderBytes + body.size() + kTagBytes > kMaxPacket) {
        return Bytes();
    }
    Bytes out;
    out.reserve(kHeaderBytes + body.size() + kTagBytes);
    out.insert(out.end(), kMagic, kMagic + 4);
    out.push_back(version);
    out.push_back(static_cast<std::uint8_t>(type));
    out.push_back(0);
    out.push_back(0);
    for (int shift = 56; shift >= 0; shift -= 8) {
        out.push_back(static_cast<std::uint8_t>(session >> shift));
    }
    for (int shift = 56; shift >= 0; shift -= 8) {
        out.push_back(static_cast<std::uint8_t>(seq >> shift));
    }
    out.push_back(0);
    out.push_back(0);
    put16(out, kHeaderBytes - 2, static_cast<std::uint16_t>(body.size()));
    out.insert(out.end(), body.begin(), body.end());
    const Bytes tag = hmacSha256(key.data(), key.size(), out.data(), out.size());
    out.insert(out.end(), tag.begin(), tag.begin() + static_cast<std::ptrdiff_t>(kTagBytes));
    return out;
}

Decode decodePacket(const Key& key, const std::uint8_t* data, size_t size, Header& header, Bytes& body) {
    if (size < kHeaderBytes + kTagBytes) {
        return Decode::TooShort;
    }
    if (size > kMaxPacket) {
        return Decode::TooLong;
    }
    if (std::memcmp(data, kMagic, 4) != 0) {
        return Decode::BadMagic;
    }
    const size_t bodySize = static_cast<size_t>((data[kHeaderBytes - 2] << 8) | data[kHeaderBytes - 1]);
    if (kHeaderBytes + bodySize + kTagBytes != size) {
        return Decode::BadLength;
    }
    const Bytes tag = hmacSha256(key.data(), key.size(), data, kHeaderBytes + bodySize);
    if (!constantTimeEqual(tag.data(), data + kHeaderBytes + bodySize, kTagBytes)) {
        return Decode::BadAuth;
    }
    Header parsed;
    parsed.version = data[4];
    parsed.type = static_cast<Type>(data[5]);
    parsed.session = 0;
    parsed.seq = 0;
    for (int i = 0; i < 8; ++i) {
        parsed.session = (parsed.session << 8) | data[8 + i];
        parsed.seq = (parsed.seq << 8) | data[16 + i];
    }
    header = parsed;
    body.assign(data + kHeaderBytes, data + kHeaderBytes + bodySize);
    return Decode::Ok;
}

Negotiation negotiate(std::uint8_t followerMin, std::uint8_t followerMax, std::uint64_t followerSchema, std::uint64_t masterSchema) {
    Negotiation result;
    const int low = std::max<int>(followerMin, 1);
    const int high = std::min<int>(followerMax, kProtocol);
    if (followerMin > followerMax || low > high) {
        return result;
    }
    result.protocol = static_cast<std::uint8_t>(high);
    result.compat = followerSchema == masterSchema ? Compat::Full : Compat::ClockOnly;
    return result;
}

std::uint64_t schemaHash(const std::vector<std::string>& ids) {
    std::string text;
    for (const std::string& id : ids) {
        text += id;
        text.push_back('\n');
    }
    text += "extra:" + std::to_string(kExtraStateValues) + ":" + std::to_string(kLayers) + ":" + std::to_string(kColumns);
    const std::string hex = sha256Hex(text);
    std::uint64_t value = 0;
    for (size_t i = 0; i < 16 && i < hex.size(); ++i) {
        value = (value << 4) | static_cast<std::uint64_t>(hexValue(hex[i]));
    }
    return value;
}

Bytes encodeBody(const Hello& m) {
    Writer w;
    w.u8(m.protocolMin);
    w.u8(m.protocolMax);
    w.u64(m.schema);
    w.u64(m.nonce);
    w.str(m.name);
    w.str(m.appVersion);
    return w.bytes();
}

Bytes encodeBody(const Welcome& m) {
    Writer w;
    w.u8(m.protocol);
    w.u64(m.nonce);
    w.u64(m.schema);
    w.u8(static_cast<std::uint8_t>(m.compat));
    w.str(m.name);
    w.str(m.appVersion);
    w.f64(m.masterTime);
    return w.bytes();
}

Bytes encodeBody(const StateMsg& m) {
    Writer w;
    w.u32(m.seq);
    w.u16(static_cast<std::uint16_t>(m.values.size()));
    for (const float value : m.values) {
        w.f32(value);
    }
    return w.bytes();
}

Bytes encodeBody(const DeltaMsg& m) {
    Writer w;
    w.u32(m.base);
    w.u32(m.next);
    w.u8(static_cast<std::uint8_t>(m.items.size()));
    for (const auto& item : m.items) {
        w.u16(item.first);
        w.f32(item.second);
    }
    return w.bytes();
}

Bytes encodeBody(const ClockMsg& m) {
    Writer w;
    w.f64(m.masterTime);
    w.f64(m.beat);
    w.f64(m.timecode);
    w.f32(m.bpm);
    w.u8(m.beatsPerBar);
    w.u8(m.flags);
    w.u8(m.timecodeRate);
    w.u16(m.jumps);
    w.u32(m.stateSeq);
    w.u32(m.cues);
    if (m.extended) {
        w.u32(m.epoch);
        w.u8(m.health);
        w.u64(m.showHash);
    }
    return w.bytes();
}

Bytes encodeBody(const PingMsg& m) {
    Writer w;
    w.f64(m.t0);
    w.u32(m.number);
    w.u32(m.stateSeq);
    if (m.extended) {
        w.u8(m.readiness);
        w.u8(m.health);
        w.u64(m.showHash);
    }
    return w.bytes();
}

Bytes encodeBody(const TakeoverMsg& m) {
    Writer w;
    w.u32(m.epoch);
    w.u16(m.port);
    w.str(m.name);
    return w.bytes();
}

Bytes encodeBody(const PongMsg& m) {
    Writer w;
    w.f64(m.t0);
    w.f64(m.masterReceive);
    w.f64(m.masterSend);
    w.u32(m.number);
    return w.bytes();
}

Bytes encodeBody(const ResyncMsg& m) {
    Writer w;
    w.u8(m.reason);
    w.u32(m.haveSeq);
    return w.bytes();
}

Bytes encodeBody(const ManifestMsg& m) {
    Writer w;
    w.u8(static_cast<std::uint8_t>(m.kind));
    w.u8(m.deck);
    w.u8(static_cast<std::uint8_t>(m.entries.size()));
    for (const ManifestEntry& entry : m.entries) {
        w.u8(entry.index);
        w.u8(entry.used);
        w.str(entry.name);
    }
    return w.bytes();
}

Bytes encodeBody(const ByeMsg& m) {
    Writer w;
    w.u8(m.reason);
    return w.bytes();
}

Bytes encodeBody(const RejectMsg& m) {
    Writer w;
    w.u8(static_cast<std::uint8_t>(m.code));
    w.str(m.text);
    return w.bytes();
}

bool decodeBody(const Bytes& body, Hello& m) {
    Reader r(body.data(), body.size());
    m.protocolMin = r.u8();
    m.protocolMax = r.u8();
    m.schema = r.u64();
    m.nonce = r.u64();
    m.name = r.str();
    m.appVersion = r.str();
    return r.good() && r.atEnd();
}

bool decodeBody(const Bytes& body, Welcome& m) {
    Reader r(body.data(), body.size());
    m.protocol = r.u8();
    m.nonce = r.u64();
    m.schema = r.u64();
    const std::uint8_t compat = r.u8();
    m.name = r.str();
    m.appVersion = r.str();
    m.masterTime = r.f64();
    if (compat > static_cast<std::uint8_t>(Compat::ClockOnly)) {
        return false;
    }
    m.compat = static_cast<Compat>(compat);
    return r.good() && r.atEnd() && std::isfinite(m.masterTime);
}

bool decodeBody(const Bytes& body, StateMsg& m) {
    Reader r(body.data(), body.size());
    m.seq = r.u32();
    const size_t count = r.u16();
    if (count > kMaxStateValues || r.remaining() != count * 4) {
        return false;
    }
    m.values.resize(count);
    for (float& value : m.values) {
        value = r.f32();
        if (!finiteValue(value)) {
            return false;
        }
    }
    return r.good() && r.atEnd();
}

bool decodeBody(const Bytes& body, DeltaMsg& m) {
    Reader r(body.data(), body.size());
    m.base = r.u32();
    m.next = r.u32();
    const size_t count = r.u8();
    if (m.next <= m.base || count > kMaxItemsPerDelta || r.remaining() != count * 6) {
        return false;
    }
    m.items.resize(count);
    for (auto& item : m.items) {
        item.first = r.u16();
        item.second = r.f32();
        if (!finiteValue(item.second)) {
            return false;
        }
    }
    return r.good() && r.atEnd();
}

bool decodeBody(const Bytes& body, ClockMsg& m) {
    Reader r(body.data(), body.size());
    m.masterTime = r.f64();
    m.beat = r.f64();
    m.timecode = r.f64();
    m.bpm = r.f32();
    m.beatsPerBar = r.u8();
    m.flags = r.u8();
    m.timecodeRate = r.u8();
    m.jumps = r.u16();
    m.stateSeq = r.u32();
    m.cues = r.u32();
    m.extended = false;
    if (r.good() && !r.atEnd()) {
        m.epoch = r.u32();
        m.health = r.u8();
        m.showHash = r.u64();
        m.extended = true;
    }
    if (!r.good() || !r.atEnd()) {
        return false;
    }
    return std::isfinite(m.masterTime) && std::isfinite(m.beat) && std::isfinite(m.timecode) && std::isfinite(m.bpm) && m.bpm > 0.0f && m.bpm < 1000.0f;
}

bool decodeBody(const Bytes& body, PingMsg& m) {
    Reader r(body.data(), body.size());
    m.t0 = r.f64();
    m.number = r.u32();
    m.stateSeq = r.u32();
    m.extended = false;
    if (r.good() && !r.atEnd()) {
        m.readiness = r.u8();
        m.health = r.u8();
        m.showHash = r.u64();
        m.extended = true;
        if (m.readiness > kReadinessNotReady) {
            return false;
        }
    }
    return r.good() && r.atEnd() && std::isfinite(m.t0);
}

bool decodeBody(const Bytes& body, TakeoverMsg& m) {
    Reader r(body.data(), body.size());
    m.epoch = r.u32();
    m.port = r.u16();
    m.name = r.str();
    return r.good() && r.atEnd() && m.epoch > 0 && m.port > 0;
}

bool decodeBody(const Bytes& body, PongMsg& m) {
    Reader r(body.data(), body.size());
    m.t0 = r.f64();
    m.masterReceive = r.f64();
    m.masterSend = r.f64();
    m.number = r.u32();
    return r.good() && r.atEnd() && std::isfinite(m.t0) && std::isfinite(m.masterReceive) && std::isfinite(m.masterSend);
}

bool decodeBody(const Bytes& body, ResyncMsg& m) {
    Reader r(body.data(), body.size());
    m.reason = r.u8();
    m.haveSeq = r.u32();
    return r.good() && r.atEnd();
}

bool decodeBody(const Bytes& body, ManifestMsg& m) {
    Reader r(body.data(), body.size());
    const std::uint8_t kind = r.u8();
    m.deck = r.u8();
    const size_t count = r.u8();
    if (kind > 1 || count > kMaxManifestEntries) {
        return false;
    }
    m.kind = static_cast<ManifestKind>(kind);
    m.entries.clear();
    for (size_t i = 0; i < count; ++i) {
        ManifestEntry entry;
        entry.index = r.u8();
        entry.used = r.u8();
        entry.name = r.str();
        if (!r.good()) {
            return false;
        }
        m.entries.push_back(std::move(entry));
    }
    return r.good() && r.atEnd();
}

bool decodeBody(const Bytes& body, ByeMsg& m) {
    Reader r(body.data(), body.size());
    m.reason = r.u8();
    return r.good() && r.atEnd();
}

bool decodeBody(const Bytes& body, RejectMsg& m) {
    Reader r(body.data(), body.size());
    const std::uint8_t code = r.u8();
    m.text = r.str();
    if (code == 0 || code > static_cast<std::uint8_t>(RejectCode::RateLimited)) {
        return false;
    }
    m.code = static_cast<RejectCode>(code);
    return r.good() && r.atEnd();
}

bool Manifest::operator==(const Manifest& other) const {
    return deckNames == other.deckNames && deckUsed == other.deckUsed && deck == other.deck && cells == other.cells;
}

std::uint64_t manifestFingerprint(const Manifest& manifest) {
    std::uint64_t hash = 1469598103934665603ull;
    for (int i = 0; i < kDecks; ++i) {
        hash = fnv(hash, manifest.deckUsed[static_cast<size_t>(i)] ? manifest.deckNames[static_cast<size_t>(i)] : std::string("-"));
    }
    hash = fnv(hash, std::to_string(manifest.deck));
    for (const std::string& cell : manifest.cells) {
        hash = fnv(hash, cell);
    }
    return hash;
}

std::vector<ManifestMsg> manifestMessages(const Manifest& manifest) {
    std::vector<ManifestMsg> out;
    for (int first = 0; first < kDecks; first += 8) {
        ManifestMsg message;
        message.kind = ManifestKind::Decks;
        message.deck = static_cast<std::uint8_t>(manifest.deck);
        for (int i = first; i < first + 8 && i < kDecks; ++i) {
            ManifestEntry entry;
            entry.index = static_cast<std::uint8_t>(i);
            entry.used = manifest.deckUsed[static_cast<size_t>(i)] ? 1 : 0;
            entry.name = entry.used ? limitName(manifest.deckNames[static_cast<size_t>(i)]) : std::string();
            message.entries.push_back(std::move(entry));
        }
        out.push_back(std::move(message));
    }
    for (int first = 0; first < kCells; first += 12) {
        ManifestMsg message;
        message.kind = ManifestKind::Cells;
        message.deck = static_cast<std::uint8_t>(manifest.deck);
        for (int i = first; i < first + 12 && i < kCells; ++i) {
            ManifestEntry entry;
            entry.index = static_cast<std::uint8_t>(i);
            entry.name = limitName(manifest.cells[static_cast<size_t>(i)]);
            entry.used = entry.name.empty() ? 0 : 1;
            message.entries.push_back(std::move(entry));
        }
        out.push_back(std::move(message));
    }
    return out;
}

void applyManifest(Manifest& manifest, const ManifestMsg& message) {
    if (message.kind == ManifestKind::Decks) {
        for (const ManifestEntry& entry : message.entries) {
            if (entry.index < kDecks) {
                const size_t i = entry.index;
                manifest.deckUsed[i] = entry.used != 0;
                manifest.deckNames[i] = entry.used ? entry.name : std::string();
                manifest.deckKnown[i] = true;
            }
        }
        return;
    }
    if (message.deck != manifest.deck) {
        manifest.cells.fill(std::string());
        manifest.cellKnown.fill(false);
    }
    manifest.deck = std::min<int>(message.deck, kDecks - 1);
    for (const ManifestEntry& entry : message.entries) {
        if (entry.index < kCells) {
            const size_t i = entry.index;
            manifest.cells[i] = entry.used ? entry.name : std::string();
            manifest.cellKnown[i] = true;
        }
    }
}

void ClockEstimator::reset() {
    samples_.clear();
    dirty_ = true;
}

void ClockEstimator::add(double localTime, double offset, double roundTrip) {
    if (!std::isfinite(localTime) || !std::isfinite(offset) || !std::isfinite(roundTrip) || roundTrip < 0.0) {
        return;
    }
    samples_.push_back({localTime, offset, roundTrip});
    while (samples_.size() > kMaxEstimatorSamples) {
        samples_.pop_front();
    }
    dirty_ = true;
}

bool ClockEstimator::ready() const {
    return samples_.size() >= 3;
}

void ClockEstimator::fit() const {
    if (!dirty_) {
        return;
    }
    dirty_ = false;
    meanTime_ = 0.0;
    meanOffset_ = 0.0;
    slope_ = 0.0;
    jitter_ = 0.0;
    bestRtt_ = 0.0;
    used_ = 0;
    if (samples_.empty()) {
        return;
    }
    std::vector<Sample> sorted(samples_.begin(), samples_.end());
    std::sort(sorted.begin(), sorted.end(), [](const Sample& a, const Sample& b) { return a.rtt < b.rtt; });
    bestRtt_ = sorted.front().rtt;
    size_t take = std::clamp<size_t>(sorted.size() / 3, 3, 12);
    take = std::min(take, sorted.size());
    double timeSum = 0.0;
    double offsetSum = 0.0;
    double minTime = sorted[0].time;
    double maxTime = sorted[0].time;
    for (size_t i = 0; i < take; ++i) {
        timeSum += sorted[i].time;
        offsetSum += sorted[i].offset;
        minTime = std::min(minTime, sorted[i].time);
        maxTime = std::max(maxTime, sorted[i].time);
    }
    used_ = take;
    meanTime_ = timeSum / static_cast<double>(take);
    meanOffset_ = offsetSum / static_cast<double>(take);
    if (take >= 4 && maxTime - minTime >= 10.0) {
        double numerator = 0.0;
        double denominator = 0.0;
        for (size_t i = 0; i < take; ++i) {
            const double dt = sorted[i].time - meanTime_;
            numerator += dt * (sorted[i].offset - meanOffset_);
            denominator += dt * dt;
        }
        if (denominator > 1e-9) {
            const double candidate = numerator / denominator;
            double residual = 0.0;
            for (size_t i = 0; i < take; ++i) {
                const double dt = sorted[i].time - meanTime_;
                const double error = sorted[i].offset - meanOffset_ - candidate * dt;
                residual += error * error;
            }
            const double sigma = std::sqrt(residual / static_cast<double>(take - 2));
            const double sigmaSlope = sigma / std::sqrt(denominator);
            if (std::fabs(candidate) > 3.0 * sigmaSlope) {
                slope_ = std::clamp(candidate, -200e-6, 200e-6);
            }
        }
    }
    double squares = 0.0;
    for (size_t i = 0; i < take; ++i) {
        const double predicted = meanOffset_ + slope_ * (sorted[i].time - meanTime_);
        const double error = sorted[i].offset - predicted;
        squares += error * error;
    }
    jitter_ = std::sqrt(squares / static_cast<double>(take));
}

double ClockEstimator::offsetAt(double localTime) const {
    fit();
    return meanOffset_ + slope_ * (localTime - meanTime_);
}

double ClockEstimator::roundTrip() const {
    fit();
    return bestRtt_;
}

double ClockEstimator::jitter() const {
    fit();
    return jitter_;
}

double ClockEstimator::driftPpm() const {
    fit();
    return slope_ * 1e6;
}

Master::Master(const MasterConfig& config) : config_(config) {
    config_.maxFollowers = std::clamp(config_.maxFollowers, 1, kMaxFollowers);
    if (config_.stateSize > kMaxStateValues) {
        config_.stateSize = kMaxStateValues;
    }
}

bool Master::allow(const Peer& peer, double now) {
    if (buckets_.size() > kMaxBuckets) {
        buckets_.clear();
    }
    Bucket& bucket = buckets_[peer.address];
    if (bucket.updated == 0.0 && bucket.tokens == 0.0) {
        bucket.tokens = kBucketBurst;
        bucket.updated = now;
    }
    bucket.tokens = std::min(kBucketBurst, bucket.tokens + std::max(now - bucket.updated, 0.0) * kBucketRate);
    bucket.updated = now;
    if (bucket.tokens < 1.0) {
        return false;
    }
    bucket.tokens -= 1.0;
    return true;
}

void Master::sendRaw(const Peer& peer, std::uint8_t version, Type type, std::uint64_t session, std::uint64_t seq, const Bytes& body) {
    Bytes packet = encodePacket(config_.key, version, type, session, seq, body);
    if (packet.empty()) {
        return;
    }
    outgoing_.push_back({peer, std::move(packet)});
    ++stats_.sent;
}

void Master::send(Slot& slot, Type type, const Bytes& body) {
    sendRaw(slot.info.peer, slot.protocol, type, slot.epoch, ++slot.outSeq, body);
    ++slot.info.packetsOut;
}

void Master::reject(const Peer& peer, std::uint64_t clientId, std::uint64_t echoSeq, RejectCode code, const std::string& text) {
    RejectMsg message;
    message.code = code;
    message.text = text;
    sendRaw(peer, kProtocol, Type::Reject, clientId, echoSeq, encodeBody(message));
}

Master::Slot* Master::find(const Peer& peer, std::uint64_t clientId) {
    (void)peer;
    for (Slot& slot : slots_) {
        if (slot.info.clientId == clientId) {
            return &slot;
        }
    }
    return nullptr;
}

void Master::remember(std::uint64_t clientId, std::uint64_t seq) {
    auto found = seen_.find(clientId);
    if (found == seen_.end()) {
        seenOrder_.push_back(clientId);
        while (seenOrder_.size() > kMaxSeenClients) {
            seen_.erase(seenOrder_.front());
            seenOrder_.pop_front();
        }
        seen_[clientId] = seq;
    } else {
        found->second = seq;
    }
}

void Master::receive(const Peer& peer, const std::uint8_t* data, size_t size, double now) {
    lastNow_ = now;
    ++stats_.received;
    if (!allow(peer, now)) {
        ++stats_.limited;
        return;
    }
    Header header;
    Bytes body;
    const Decode result = decodePacket(config_.key, data, size, header, body);
    if (result == Decode::BadAuth) {
        ++stats_.badAuth;
        return;
    }
    if (result != Decode::Ok || header.version < 1 || header.version > kProtocol) {
        ++stats_.malformed;
        return;
    }
    handle(peer, header, body, now);
}

void Master::handleHello(const Peer& peer, const Header& header, const Bytes& body, double now) {
    Hello hello;
    if (!decodeBody(body, hello)) {
        ++stats_.malformed;
        return;
    }
    const std::uint64_t clientId = header.session;
    const auto seen = seen_.find(clientId);
    if (seen != seen_.end() && header.seq <= seen->second) {
        ++stats_.replays;
        return;
    }
    remember(clientId, header.seq);
    const Negotiation negotiation = negotiate(hello.protocolMin, hello.protocolMax, hello.schema, config_.schema);
    if (negotiation.compat == Compat::Refused) {
        ++stats_.refused;
        reject(peer, clientId, header.seq, RejectCode::Version, "No common protocol version");
        return;
    }
    for (Slot& existing : slots_) {
        if (existing.info.clientId == clientId && existing.nonce == hello.nonce) {
            existing.info.peer = peer;
            existing.info.lastHeard = now;
            existing.inSeq = std::max(existing.inSeq, header.seq);
            sendWelcome(existing, now);
            return;
        }
    }
    for (size_t i = 0; i < slots_.size();) {
        if (slots_[i].info.clientId == clientId || slots_[i].info.peer == peer) {
            slots_.erase(slots_.begin() + static_cast<std::ptrdiff_t>(i));
        } else {
            ++i;
        }
    }
    if (static_cast<int>(slots_.size()) >= config_.maxFollowers) {
        ++stats_.refused;
        reject(peer, clientId, header.seq, RejectCode::Full, "Too many followers");
        return;
    }
    Slot slot;
    slot.info.clientId = clientId;
    slot.info.peer = peer;
    slot.info.name = limitName(hello.name);
    slot.info.appVersion = limitName(hello.appVersion);
    slot.info.compat = negotiation.compat;
    slot.info.lastHeard = now;
    slot.protocol = negotiation.protocol;
    slot.epoch = config_.random ? config_.random() : clientId ^ 0x9e3779b97f4a7c15ull;
    slot.inSeq = header.seq;
    slot.nonce = hello.nonce;
    slot.known.assign(config_.stateSize, 0.0f);
    slot.lastKeyframe = now;
    slots_.push_back(std::move(slot));
    sendWelcome(slots_.back(), now);
}

void Master::sendWelcome(Slot& slot, double now) {
    Welcome welcome;
    welcome.protocol = slot.protocol;
    welcome.nonce = slot.nonce;
    welcome.schema = config_.schema;
    welcome.compat = slot.info.compat;
    welcome.name = config_.name;
    welcome.appVersion = config_.appVersion;
    welcome.masterTime = now;
    send(slot, Type::Welcome, encodeBody(welcome));
    slot.lastWelcome = now;
    ++slot.welcomes;
}

void Master::handle(const Peer& peer, const Header& header, const Bytes& body, double now) {
    if (header.type == Type::Hello) {
        handleHello(peer, header, body, now);
        return;
    }
    if (header.type == Type::Takeover) {
        TakeoverMsg takeover;
        if (!decodeBody(body, takeover)) {
            ++stats_.malformed;
            return;
        }
        if (takeover.epoch > config_.epoch && takeover.epoch > takeoverSeen_) {
            takeoverSeen_ = takeover.epoch;
            takeovers_.push_back({peer, takeover.epoch, takeover.port, limitName(takeover.name)});
        }
        return;
    }
    Slot* slot = find(peer, header.session);
    if (slot == nullptr) {
        if (header.type == Type::Reject || header.type == Type::Bye || header.type == Type::Welcome || header.type == Type::Pong) {
            ++stats_.stray;
            return;
        }
        ++stats_.unknownSession;
        if (now - lastReject_ >= 1.0) {
            lastReject_ = now;
            reject(peer, header.session, header.seq, RejectCode::UnknownSession, "Unknown session");
        }
        return;
    }
    if (header.seq <= slot->inSeq) {
        ++stats_.replays;
        return;
    }
    slot->inSeq = header.seq;
    slot->confirmed = true;
    slot->info.peer = peer;
    slot->info.lastHeard = now;
    ++slot->info.packetsIn;
    switch (header.type) {
    case Type::Ping: {
        PingMsg ping;
        if (!decodeBody(body, ping)) {
            ++stats_.malformed;
            return;
        }
        slot->info.reportedSeq = ping.stateSeq;
        if (ping.extended) {
            slot->info.reports = true;
            slot->info.readiness = ping.readiness;
            slot->info.health = ping.health;
            slot->info.showHash = ping.showHash;
            slot->info.reportedAt = now;
        }
        PongMsg pong;
        pong.t0 = ping.t0;
        pong.number = ping.number;
        pong.masterReceive = now;
        pong.masterSend = now;
        send(*slot, Type::Pong, encodeBody(pong));
        break;
    }
    case Type::Resync: {
        ResyncMsg resync;
        if (!decodeBody(body, resync)) {
            ++stats_.malformed;
            return;
        }
        slot->needState = true;
        slot->needManifest = true;
        ++stats_.resyncs;
        break;
    }
    case Type::Bye: {
        for (size_t i = 0; i < slots_.size(); ++i) {
            if (&slots_[i] == slot) {
                slots_.erase(slots_.begin() + static_cast<std::ptrdiff_t>(i));
                break;
            }
        }
        break;
    }
    default:
        ++stats_.malformed;
        break;
    }
}

void Master::sendState(Slot& slot, const MasterFrame& frame, double now) {
    if (frame.state.size() != config_.stateSize) {
        return;
    }
    StateMsg message;
    message.seq = ++slot.seq;
    message.values = frame.state;
    for (float& value : message.values) {
        if (!finiteValue(value)) {
            value = 0.0f;
        }
    }
    send(slot, Type::State, encodeBody(message));
    slot.known = message.values;
    slot.needState = false;
    slot.lastKeyframe = now;
    slot.lastDelta = now;
    slot.info.knownSeq = slot.seq;
}

void Master::sendDeltas(Slot& slot, const MasterFrame& frame, double now) {
    if (frame.state.size() != config_.stateSize || now - slot.lastDelta < kDeltaInterval) {
        return;
    }
    std::vector<std::uint16_t> changed;
    for (size_t i = 0; i < frame.state.size(); ++i) {
        const float value = frame.state[i];
        if (finiteValue(value) && value != slot.known[i]) {
            changed.push_back(static_cast<std::uint16_t>(i));
        }
    }
    if (changed.empty()) {
        return;
    }
    if (changed.size() > std::max<size_t>(frame.state.size() / 2, kMaxItemsPerDelta)) {
        sendState(slot, frame, now);
        return;
    }
    for (size_t first = 0; first < changed.size(); first += kMaxItemsPerDelta) {
        DeltaMsg message;
        message.base = slot.seq;
        message.next = slot.seq + 1;
        const size_t last = std::min(first + kMaxItemsPerDelta, changed.size());
        for (size_t k = first; k < last; ++k) {
            const std::uint16_t index = changed[k];
            message.items.emplace_back(index, frame.state[index]);
            slot.known[index] = frame.state[index];
        }
        slot.seq = message.next;
        send(slot, Type::Delta, encodeBody(message));
    }
    slot.info.knownSeq = slot.seq;
    slot.lastDelta = now;
}

void Master::sendClock(Slot& slot, const MasterFrame& frame, double now) {
    if (now - slot.lastClock < kClockInterval) {
        return;
    }
    slot.lastClock = now;
    ClockMsg message;
    message.masterTime = now;
    message.beat = frame.clock.beat;
    message.bpm = static_cast<float>(frame.clock.bpm);
    message.beatsPerBar = static_cast<std::uint8_t>(std::clamp(frame.clock.beatsPerBar, 1, 32));
    message.flags = static_cast<std::uint8_t>((frame.clock.running ? kClockRunning : 0) | (frame.clock.timecodeValid ? kClockTimecode : 0) | (frame.clock.locked ? kClockLocked : 0));
    message.timecode = frame.clock.timecode;
    message.timecodeRate = static_cast<std::uint8_t>(std::clamp(frame.clock.timecodeRate, 0, 255));
    message.jumps = frame.clock.jumps;
    message.stateSeq = slot.info.compat == Compat::Full ? slot.seq : 0;
    message.cues = frame.clock.cues;
    if (slot.protocol >= kProtocolFailover) {
        message.extended = true;
        message.epoch = config_.epoch;
        message.health = frame.clock.health;
        message.showHash = frame.clock.showHash;
    }
    send(slot, Type::Clock, encodeBody(message));
}

void Master::sendManifest(Slot& slot, const MasterFrame& frame, double now) {
    const std::uint64_t hash = manifestFingerprint(frame.manifest);
    if (!slot.needManifest && hash == slot.manifestHash && now - slot.lastManifest < kManifestInterval) {
        return;
    }
    for (const ManifestMsg& message : manifestMessages(frame.manifest)) {
        send(slot, Type::Manifest, encodeBody(message));
    }
    slot.needManifest = false;
    slot.manifestHash = hash;
    slot.lastManifest = now;
}

void Master::update(double now, const MasterFrame& frame) {
    lastNow_ = now;
    for (size_t i = 0; i < slots_.size();) {
        if (now - slots_[i].info.lastHeard > kFollowerTimeout) {
            slots_.erase(slots_.begin() + static_cast<std::ptrdiff_t>(i));
        } else {
            ++i;
        }
    }
    for (Slot& slot : slots_) {
        if (!slot.confirmed) {
            if (now - slot.lastWelcome >= kWelcomeRetry && slot.welcomes < kWelcomeAttempts) {
                sendWelcome(slot, now);
            }
            continue;
        }
        if (slot.info.compat == Compat::Full) {
            if (slot.needState) {
                sendState(slot, frame, now);
            }
            if (!slot.needState) {
                sendDeltas(slot, frame, now);
                if (now - slot.lastKeyframe >= kKeyframeInterval) {
                    sendState(slot, frame, now);
                }
            }
            slot.info.live = !slot.needState;
        } else {
            slot.info.live = true;
        }
        if (slot.info.live) {
            sendClock(slot, frame, now);
            sendManifest(slot, frame, now);
        }
    }
}

void Master::shutdown(double now, std::uint8_t reason) {
    lastNow_ = now;
    for (Slot& slot : slots_) {
        ByeMsg bye;
        bye.reason = reason;
        send(slot, Type::Bye, encodeBody(bye));
    }
    slots_.clear();
}

void Master::announceTakeover(const Peer& peer, std::uint32_t epoch, std::uint16_t port, const std::string& name) {
    TakeoverMsg message;
    message.epoch = epoch;
    message.port = port;
    message.name = limitName(name);
    sendRaw(peer, kProtocolFailover, Type::Takeover, 0, ++takeoverSeq_, encodeBody(message));
}

std::vector<TakeoverNotice> Master::takeTakeovers() {
    std::vector<TakeoverNotice> out;
    out.swap(takeovers_);
    return out;
}

std::vector<Datagram> Master::takeOutgoing() {
    std::vector<Datagram> out;
    out.swap(outgoing_);
    return out;
}

std::vector<FollowerInfo> Master::followers() const {
    std::vector<FollowerInfo> out;
    out.reserve(slots_.size());
    for (const Slot& slot : slots_) {
        out.push_back(slot.info);
    }
    return out;
}

Follower::Follower(const FollowerConfig& config) : config_(config) {
    if (config_.stateSize > kMaxStateValues) {
        config_.stateSize = kMaxStateValues;
    }
}

void Follower::start(double now) {
    clientId_ = config_.random ? config_.random() : 0x1234567890abcdefull;
    nonce_ = config_.random ? config_.random() : 1;
    if (clientId_ == 0) {
        clientId_ = 1;
    }
    outSeq_ = 0;
    epoch_ = 0;
    inSeq_ = 0;
    protocol_ = kProtocol;
    phase_ = FollowerPhase::Connecting;
    compat_ = Compat::Full;
    lastReject_ = RejectCode::None;
    helloDelay_ = kHelloRetryMin;
    nextHello_ = now;
    lastHeard_ = now;
    state_.clear();
    haveState_ = false;
    waitingForState_ = true;
    needResync_ = false;
    stateSeq_ = 0;
    masterSeq_ = 0;
    masterSeqSince_ = 0.0;
    changes_.clear();
    estimator_.reset();
    haveBeacon_ = false;
    jumpPending_ = false;
    manifest_ = Manifest();
    manifestVersion_ = 0;
    outgoing_.clear();
    masterName_.clear();
    masterVersion_.clear();
    byeSeen_ = false;
    byeReason_ = 0;
    primaryKnown_ = false;
    primaryEpoch_ = 0;
    primaryHealth_ = 0;
    primaryShowHash_ = 0;
}

void Follower::stop(double now) {
    (void)now;
    if ((phase_ == FollowerPhase::Syncing || phase_ == FollowerPhase::Live) && epoch_ != 0) {
        ByeMsg bye;
        sendPacket(Type::Bye, encodeBody(bye));
    }
    phase_ = FollowerPhase::Idle;
    haveState_ = false;
    haveBeacon_ = false;
}

void Follower::sendPacket(Type type, const Bytes& body) {
    Bytes packet = encodePacket(config_.key, protocol_, type, clientId_, ++outSeq_, body);
    if (packet.empty()) {
        return;
    }
    outgoing_.push_back(std::move(packet));
    ++stats_.sent;
}

void Follower::sendHello(double now) {
    (void)now;
    Hello hello;
    hello.protocolMin = 1;
    hello.protocolMax = kProtocol;
    hello.schema = config_.schema;
    hello.nonce = nonce_;
    hello.name = config_.name;
    hello.appVersion = config_.appVersion;
    Bytes packet = encodePacket(config_.key, 1, Type::Hello, clientId_, ++outSeq_, encodeBody(hello));
    if (!packet.empty()) {
        outgoing_.push_back(std::move(packet));
        ++stats_.sent;
    }
}

void Follower::requestResync(double now, std::uint8_t reason) {
    if (now - lastResync_ < kResyncGap) {
        return;
    }
    lastResync_ = now;
    ResyncMsg message;
    message.reason = reason;
    message.haveSeq = stateSeq_;
    sendPacket(Type::Resync, encodeBody(message));
    ++stats_.resyncs;
}

void Follower::dropConnection(double now) {
    phase_ = FollowerPhase::Lost;
    nextHello_ = now;
    helloDelay_ = kHelloRetryMin;
    nonce_ = config_.random ? config_.random() : nonce_ + 1;
    waitingForState_ = true;
    epoch_ = 0;
}

void Follower::update(double now) {
    if (phase_ == FollowerPhase::Idle) {
        return;
    }
    if (phase_ == FollowerPhase::Connecting || phase_ == FollowerPhase::Lost) {
        if (now >= nextHello_) {
            sendHello(now);
            helloDelay_ = std::min(helloDelay_ * 1.6, kHelloRetryMax);
            nextHello_ = now + helloDelay_;
        }
        return;
    }
    if (now - lastHeard_ > kMasterLostAfter) {
        dropConnection(now);
        sendHello(now);
        nextHello_ = now + helloDelay_;
        return;
    }
    if (now >= nextPing_) {
        PingMsg ping;
        ping.t0 = now;
        ping.number = ++pingNumber_;
        ping.stateSeq = stateSeq_;
        if (protocol_ >= kProtocolFailover) {
            ping.extended = true;
            ping.readiness = reportReadiness_;
            ping.health = reportHealth_;
            ping.showHash = reportShowHash_;
        }
        sendPacket(Type::Ping, encodeBody(ping));
        if (pingBurst_ > 0) {
            --pingBurst_;
            nextPing_ = now + 0.15;
        } else {
            nextPing_ = now + kPingInterval;
        }
    }
    if (compat_ == Compat::Full && haveBeacon_ && masterSeq_ > stateSeq_) {
        if (masterSeqSince_ == 0.0) {
            masterSeqSince_ = now;
        } else if (now - masterSeqSince_ > kTailLossWait) {
            needResync_ = true;
            masterSeqSince_ = now;
        }
    } else {
        masterSeqSince_ = 0.0;
    }
    if (needResync_ && compat_ == Compat::Full) {
        requestResync(now, 2);
    }
}

void Follower::applyState(const std::vector<float>& values) {
    for (size_t i = 0; i < values.size(); ++i) {
        if (!haveState_ || i >= state_.size() || state_[i] != values[i]) {
            changes_.push_back({static_cast<std::uint16_t>(i), values[i]});
        }
    }
    state_ = values;
    haveState_ = true;
}

void Follower::receive(const std::uint8_t* data, size_t size, double now) {
    if (phase_ == FollowerPhase::Idle) {
        return;
    }
    ++stats_.received;
    Header header;
    Bytes body;
    const Decode result = decodePacket(config_.key, data, size, header, body);
    if (result == Decode::BadAuth) {
        ++stats_.badAuth;
        return;
    }
    if (result != Decode::Ok || header.version < 1 || header.version > kProtocol) {
        ++stats_.malformed;
        return;
    }
    handle(header, body, now);
}

void Follower::handle(const Header& header, const Bytes& body, double now) {
    if (header.type == Type::Welcome) {
        Welcome welcome;
        if (!decodeBody(body, welcome)) {
            ++stats_.malformed;
            return;
        }
        if (welcome.nonce != nonce_ || (phase_ != FollowerPhase::Connecting && phase_ != FollowerPhase::Lost)) {
            ++stats_.duplicates;
            return;
        }
        if (welcome.protocol < 1 || welcome.protocol > kProtocol || header.seq == 0) {
            ++stats_.malformed;
            return;
        }
        epoch_ = header.session;
        inSeq_ = header.seq;
        protocol_ = welcome.protocol;
        byeSeen_ = false;
        byeReason_ = 0;
        compat_ = welcome.compat;
        masterName_ = welcome.name;
        masterVersion_ = welcome.appVersion;
        phase_ = FollowerPhase::Syncing;
        lastHeard_ = now;
        lastReject_ = RejectCode::None;
        pingBurst_ = 6;
        nextPing_ = now;
        estimator_.reset();
        haveBeacon_ = false;
        waitingForState_ = true;
        stateSeq_ = 0;
        masterSeq_ = 0;
        masterSeqSince_ = 0.0;
        needResync_ = false;
        if (compat_ == Compat::ClockOnly) {
            waitingForState_ = false;
            phase_ = FollowerPhase::Live;
        }
        return;
    }
    if (header.type == Type::Reject) {
        RejectMsg reject;
        if (!decodeBody(body, reject) || header.session != clientId_ || header.seq > outSeq_ || outSeq_ - header.seq >= kRejectWindow) {
            ++stats_.malformed;
            return;
        }
        ++stats_.rejects;
        lastReject_ = reject.code;
        if (reject.code == RejectCode::UnknownSession && (phase_ == FollowerPhase::Syncing || phase_ == FollowerPhase::Live)) {
            dropConnection(now);
        } else if (reject.code == RejectCode::Version || reject.code == RejectCode::Full) {
            helloDelay_ = kHelloRetryMax;
            nextHello_ = now + helloDelay_;
            if (phase_ != FollowerPhase::Lost) {
                phase_ = FollowerPhase::Connecting;
            }
        }
        return;
    }
    if ((phase_ != FollowerPhase::Syncing && phase_ != FollowerPhase::Live) || header.session != epoch_) {
        ++stats_.stray;
        return;
    }
    if (header.seq <= inSeq_) {
        if (header.seq == inSeq_) {
            ++stats_.duplicates;
        } else {
            ++stats_.replays;
        }
        return;
    }
    inSeq_ = header.seq;
    lastHeard_ = now;
    switch (header.type) {
    case Type::State: {
        StateMsg message;
        if (compat_ != Compat::Full || !decodeBody(body, message) || message.values.size() != config_.stateSize) {
            ++stats_.malformed;
            return;
        }
        stateSeq_ = message.seq;
        masterSeq_ = std::max(masterSeq_, message.seq);
        applyState(message.values);
        waitingForState_ = false;
        needResync_ = false;
        phase_ = FollowerPhase::Live;
        ++stats_.statesApplied;
        break;
    }
    case Type::Delta: {
        DeltaMsg message;
        if (compat_ != Compat::Full || !decodeBody(body, message)) {
            ++stats_.malformed;
            return;
        }
        if (waitingForState_) {
            break;
        }
        if (message.next <= stateSeq_) {
            ++stats_.duplicates;
            break;
        }
        for (const auto& item : message.items) {
            if (item.first < state_.size() && state_[item.first] != item.second) {
                state_[item.first] = item.second;
                changes_.push_back({item.first, item.second});
            }
        }
        if (message.base != stateSeq_) {
            ++stats_.gaps;
            needResync_ = true;
            requestResync(now, 1);
        }
        stateSeq_ = message.next;
        masterSeq_ = std::max(masterSeq_, message.next);
        ++stats_.deltasApplied;
        break;
    }
    case Type::Clock: {
        ClockMsg message;
        if (!decodeBody(body, message)) {
            ++stats_.malformed;
            return;
        }
        if (haveBeacon_ && message.jumps != lastJumps_) {
            jumpPending_ = true;
        }
        lastJumps_ = message.jumps;
        if (message.extended) {
            primaryKnown_ = true;
            primaryEpoch_ = message.epoch;
            primaryHealth_ = message.health;
            primaryShowHash_ = message.showHash;
        }
        beacon_ = message;
        beaconLocal_ = now;
        haveBeacon_ = true;
        if (compat_ == Compat::Full) {
            masterSeq_ = std::max(masterSeq_, message.stateSeq);
        }
        ++stats_.clockBeacons;
        break;
    }
    case Type::Pong: {
        PongMsg message;
        if (!decodeBody(body, message)) {
            ++stats_.malformed;
            return;
        }
        const double elapsed = now - message.t0;
        const double held = message.masterSend - message.masterReceive;
        const double roundTrip = elapsed - held;
        if (elapsed < 0.0 || elapsed > 5.0 || roundTrip < -1e-3) {
            break;
        }
        const double offset = ((message.masterReceive - message.t0) + (message.masterSend - now)) / 2.0;
        estimator_.add((message.t0 + now) / 2.0, offset, std::max(roundTrip, 0.0));
        ++stats_.pongs;
        break;
    }
    case Type::Manifest: {
        ManifestMsg message;
        if (!decodeBody(body, message)) {
            ++stats_.malformed;
            return;
        }
        applyManifest(manifest_, message);
        ++manifestVersion_;
        break;
    }
    case Type::Bye: {
        ByeMsg bye;
        byeSeen_ = true;
        byeReason_ = decodeBody(body, bye) ? bye.reason : kByeEnded;
        phase_ = FollowerPhase::Lost;
        nextHello_ = now + kHelloRetryMin;
        helloDelay_ = kHelloRetryMin;
        nonce_ = config_.random ? config_.random() : nonce_ + 1;
        waitingForState_ = true;
        epoch_ = 0;
        break;
    }
    default:
        ++stats_.malformed;
        break;
    }
}

std::vector<Bytes> Follower::takeOutgoing() {
    std::vector<Bytes> out;
    out.swap(outgoing_);
    return out;
}

std::vector<Change> Follower::takeChanges() {
    std::vector<Change> out;
    out.swap(changes_);
    std::sort(out.begin(), out.end(), [](const Change& a, const Change& b) { return a.index < b.index; });
    return out;
}

bool Follower::takeJump() {
    const bool value = jumpPending_;
    jumpPending_ = false;
    return value;
}

PrimaryView Follower::primary() const {
    PrimaryView view;
    view.known = primaryKnown_;
    view.extended = primaryKnown_;
    view.epoch = primaryEpoch_;
    view.health = primaryHealth_;
    view.showHash = primaryShowHash_;
    view.byeSeen = byeSeen_;
    view.byeReason = byeReason_;
    view.lastHeard = lastHeard_;
    return view;
}

void Follower::setReport(std::uint8_t readiness, std::uint8_t health, std::uint64_t showHash) {
    reportReadiness_ = readiness;
    reportHealth_ = health;
    reportShowHash_ = showHash;
}

ClockView Follower::clock(double now) const {
    ClockView view;
    if (!haveBeacon_ || !estimator_.ready() || (phase_ != FollowerPhase::Syncing && phase_ != FollowerPhase::Live)) {
        return view;
    }
    view.age = now - beaconLocal_;
    view.offset = estimator_.offsetAt(now);
    view.roundTrip = estimator_.roundTrip();
    view.jitter = estimator_.jitter();
    view.driftPpm = estimator_.driftPpm();
    view.running = (beacon_.flags & kClockRunning) != 0;
    view.bpm = beacon_.bpm;
    view.beatsPerBar = beacon_.beatsPerBar;
    const double masterNow = now + view.offset;
    view.beat = beacon_.beat + (view.running ? (masterNow - beacon_.masterTime) * static_cast<double>(beacon_.bpm) / 60.0 : 0.0);
    view.timecodeValid = (beacon_.flags & kClockTimecode) != 0;
    view.timecodeRate = beacon_.timecodeRate;
    view.timecode = beacon_.timecode + (view.timecodeValid && view.running ? (masterNow - beacon_.masterTime) : 0.0);
    view.cues = beacon_.cues;
    view.valid = view.age <= kClockStaleAfter;
    view.locked = view.valid && view.jitter < 0.02 && estimator_.samples() >= 4;
    return view;
}

}
