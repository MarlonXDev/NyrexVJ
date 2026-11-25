#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace nvj::netsync {

constexpr int kProtocol = 1;
constexpr int kDefaultPort = 9030;
constexpr size_t kMaxPacket = 1200;
constexpr size_t kHeaderBytes = 26;
constexpr size_t kTagBytes = 16;
constexpr size_t kKeyBytes = 32;
constexpr int kMaxFollowers = 8;
constexpr size_t kMaxNameBytes = 60;
constexpr size_t kMaxStateValues = 280;
constexpr size_t kMaxItemsPerDelta = 60;
constexpr int kDecks = 16;
constexpr int kLayers = 4;
constexpr int kColumns = 8;
constexpr int kCells = kLayers * kColumns;
constexpr int kExtraStateValues = 1 + kLayers + kLayers;

constexpr double kClockInterval = 0.1;
constexpr double kDeltaInterval = 1.0 / 30.0;
constexpr double kKeyframeInterval = 5.0;
constexpr double kManifestInterval = 5.0;
constexpr double kPingInterval = 1.0;
constexpr double kFollowerTimeout = 6.0;
constexpr double kMasterLostAfter = 3.0;
constexpr double kClockStaleAfter = 2.0;
constexpr double kHelloRetryMin = 0.5;
constexpr double kHelloRetryMax = 5.0;
constexpr double kResyncGap = 0.5;
constexpr double kTailLossWait = 0.4;
constexpr double kWelcomeRetry = 0.25;
constexpr int kWelcomeAttempts = 40;

using Bytes = std::vector<std::uint8_t>;
using Key = std::array<std::uint8_t, kKeyBytes>;

bool keyFromHex(const std::string& text, Key& key);
std::string keyToHex(const Key& key);
bool keyIsZero(const Key& key);

Bytes hmacSha256(const std::uint8_t* key, size_t keySize, const std::uint8_t* data, size_t size);
bool constantTimeEqual(const std::uint8_t* a, const std::uint8_t* b, size_t size);

std::string limitName(const std::string& text);

class Writer {
public:
    void u8(std::uint8_t value);
    void u16(std::uint16_t value);
    void u32(std::uint32_t value);
    void u64(std::uint64_t value);
    void f32(float value);
    void f64(double value);
    void str(const std::string& text);
    const Bytes& bytes() const { return data_; }
    size_t size() const { return data_.size(); }

private:
    Bytes data_;
};

class Reader {
public:
    Reader(const std::uint8_t* data, size_t size) : data_(data), size_(size) {}
    std::uint8_t u8();
    std::uint16_t u16();
    std::uint32_t u32();
    std::uint64_t u64();
    float f32();
    double f64();
    std::string str();
    bool good() const { return ok_; }
    bool atEnd() const { return pos_ == size_; }
    size_t remaining() const { return size_ - pos_; }

private:
    bool take(size_t count);

    const std::uint8_t* data_;
    size_t size_;
    size_t pos_ = 0;
    bool ok_ = true;
};

enum class Type : std::uint8_t {
    Hello = 1,
    Welcome = 2,
    State = 3,
    Delta = 4,
    Clock = 5,
    Ping = 6,
    Pong = 7,
    Resync = 8,
    Manifest = 9,
    Bye = 10,
    Reject = 11
};

struct Header {
    std::uint8_t version = kProtocol;
    Type type = Type::Hello;
    std::uint64_t session = 0;
    std::uint64_t seq = 0;
};

enum class Decode { Ok, TooShort, TooLong, BadMagic, BadLength, BadAuth };

Bytes encodePacket(const Key& key, std::uint8_t version, Type type, std::uint64_t session, std::uint64_t seq, const Bytes& body);
Decode decodePacket(const Key& key, const std::uint8_t* data, size_t size, Header& header, Bytes& body);

enum class Compat : std::uint8_t { Full = 0, ClockOnly = 1, Refused = 2 };

struct Negotiation {
    Compat compat = Compat::Refused;
    std::uint8_t protocol = 0;
};

Negotiation negotiate(std::uint8_t followerMin, std::uint8_t followerMax, std::uint64_t followerSchema, std::uint64_t masterSchema);

enum class RejectCode : std::uint8_t { None = 0, Version = 1, Full = 2, UnknownSession = 3, RateLimited = 4 };

struct Layout {
    size_t controls = 0;
    size_t total() const { return controls + kExtraStateValues; }
    size_t deck() const { return controls; }
    size_t column(int layer) const { return controls + 1 + static_cast<size_t>(layer); }
    size_t trigger(int layer) const { return controls + 1 + kLayers + static_cast<size_t>(layer); }
};

std::uint64_t schemaHash(const std::vector<std::string>& ids);

struct Hello {
    std::uint8_t protocolMin = kProtocol;
    std::uint8_t protocolMax = kProtocol;
    std::uint64_t schema = 0;
    std::uint64_t nonce = 0;
    std::string name;
    std::string appVersion;
};

struct Welcome {
    std::uint8_t protocol = kProtocol;
    std::uint64_t nonce = 0;
    std::uint64_t schema = 0;
    Compat compat = Compat::Full;
    std::string name;
    std::string appVersion;
    double masterTime = 0.0;
};

struct StateMsg {
    std::uint32_t seq = 0;
    std::vector<float> values;
};

struct DeltaMsg {
    std::uint32_t base = 0;
    std::uint32_t next = 0;
    std::vector<std::pair<std::uint16_t, float>> items;
};

constexpr std::uint8_t kClockRunning = 1;
constexpr std::uint8_t kClockTimecode = 2;
constexpr std::uint8_t kClockLocked = 4;

struct ClockMsg {
    double masterTime = 0.0;
    double beat = 0.0;
    double timecode = 0.0;
    float bpm = 120.0f;
    std::uint8_t beatsPerBar = 4;
    std::uint8_t flags = 0;
    std::uint8_t timecodeRate = 0;
    std::uint16_t jumps = 0;
    std::uint32_t stateSeq = 0;
    std::uint32_t cues = 0;
};

struct PingMsg {
    double t0 = 0.0;
    std::uint32_t number = 0;
    std::uint32_t stateSeq = 0;
};

struct PongMsg {
    double t0 = 0.0;
    double masterReceive = 0.0;
    double masterSend = 0.0;
    std::uint32_t number = 0;
};

struct ResyncMsg {
    std::uint8_t reason = 0;
    std::uint32_t haveSeq = 0;
};

struct ManifestEntry {
    std::uint8_t index = 0;
    std::uint8_t used = 0;
    std::string name;
};

enum class ManifestKind : std::uint8_t { Decks = 0, Cells = 1 };

struct ManifestMsg {
    ManifestKind kind = ManifestKind::Cells;
    std::uint8_t deck = 0;
    std::vector<ManifestEntry> entries;
};

struct ByeMsg {
    std::uint8_t reason = 0;
};

struct RejectMsg {
    RejectCode code = RejectCode::None;
    std::string text;
};

Bytes encodeBody(const Hello& message);
Bytes encodeBody(const Welcome& message);
Bytes encodeBody(const StateMsg& message);
Bytes encodeBody(const DeltaMsg& message);
Bytes encodeBody(const ClockMsg& message);
Bytes encodeBody(const PingMsg& message);
Bytes encodeBody(const PongMsg& message);
Bytes encodeBody(const ResyncMsg& message);
Bytes encodeBody(const ManifestMsg& message);
Bytes encodeBody(const ByeMsg& message);
Bytes encodeBody(const RejectMsg& message);

bool decodeBody(const Bytes& body, Hello& message);
bool decodeBody(const Bytes& body, Welcome& message);
bool decodeBody(const Bytes& body, StateMsg& message);
bool decodeBody(const Bytes& body, DeltaMsg& message);
bool decodeBody(const Bytes& body, ClockMsg& message);
bool decodeBody(const Bytes& body, PingMsg& message);
bool decodeBody(const Bytes& body, PongMsg& message);
bool decodeBody(const Bytes& body, ResyncMsg& message);
bool decodeBody(const Bytes& body, ManifestMsg& message);
bool decodeBody(const Bytes& body, ByeMsg& message);
bool decodeBody(const Bytes& body, RejectMsg& message);

struct Manifest {
    std::array<std::string, kDecks> deckNames;
    std::array<bool, kDecks> deckUsed{};
    std::array<bool, kDecks> deckKnown{};
    int deck = 0;
    std::array<std::string, kCells> cells;
    std::array<bool, kCells> cellKnown{};
    bool operator==(const Manifest& other) const;
};

std::uint64_t manifestFingerprint(const Manifest& manifest);
std::vector<ManifestMsg> manifestMessages(const Manifest& manifest);
void applyManifest(Manifest& manifest, const ManifestMsg& message);

struct ClockData {
    double beat = 0.0;
    double bpm = 120.0;
    int beatsPerBar = 4;
    bool running = true;
    bool locked = false;
    bool timecodeValid = false;
    double timecode = 0.0;
    int timecodeRate = 0;
    std::uint16_t jumps = 0;
    std::uint32_t cues = 0;
};

struct MasterFrame {
    std::vector<float> state;
    ClockData clock;
    Manifest manifest;
};

struct Peer {
    std::uint32_t address = 0;
    std::uint16_t port = 0;
    bool operator==(const Peer& other) const { return address == other.address && port == other.port; }
};

struct Datagram {
    Peer peer;
    Bytes data;
};

struct Stats {
    std::uint64_t received = 0;
    std::uint64_t sent = 0;
    std::uint64_t badAuth = 0;
    std::uint64_t malformed = 0;
    std::uint64_t stray = 0;
    std::uint64_t replays = 0;
    std::uint64_t refused = 0;
    std::uint64_t limited = 0;
    std::uint64_t unknownSession = 0;
    std::uint64_t duplicates = 0;
    std::uint64_t gaps = 0;
    std::uint64_t resyncs = 0;
    std::uint64_t statesApplied = 0;
    std::uint64_t deltasApplied = 0;
    std::uint64_t clockBeacons = 0;
    std::uint64_t pongs = 0;
    std::uint64_t rejects = 0;
};

using RandomFn = std::function<std::uint64_t()>;

struct MasterConfig {
    Key key{};
    std::string name;
    std::string appVersion;
    std::uint64_t schema = 0;
    size_t stateSize = 0;
    int maxFollowers = kMaxFollowers;
    RandomFn random;
};

struct FollowerInfo {
    std::uint64_t clientId = 0;
    Peer peer;
    std::string name;
    std::string appVersion;
    Compat compat = Compat::Full;
    bool live = false;
    double lastHeard = 0.0;
    double rtt = 0.0;
    std::uint32_t reportedSeq = 0;
    std::uint32_t knownSeq = 0;
    std::uint64_t packetsOut = 0;
    std::uint64_t packetsIn = 0;
};

class Master {
public:
    explicit Master(const MasterConfig& config);

    void receive(const Peer& peer, const std::uint8_t* data, size_t size, double now);
    void update(double now, const MasterFrame& frame);
    void shutdown(double now);
    std::vector<Datagram> takeOutgoing();
    std::vector<FollowerInfo> followers() const;
    const Stats& stats() const { return stats_; }
    double now() const { return lastNow_; }

private:
    struct Slot {
        FollowerInfo info;
        std::uint64_t epoch = 0;
        std::uint64_t outSeq = 0;
        std::uint64_t inSeq = 0;
        std::uint8_t protocol = kProtocol;
        std::vector<float> known;
        std::uint32_t seq = 0;
        bool needState = true;
        bool needManifest = true;
        double lastDelta = -1e9;
        double lastKeyframe = 0.0;
        double lastClock = -1e9;
        double lastManifest = -1e9;
        std::uint64_t manifestHash = 0;
        std::uint64_t nonce = 0;
        bool confirmed = false;
        double lastWelcome = 0.0;
        int welcomes = 0;
    };

    struct Bucket {
        double tokens = 0.0;
        double updated = 0.0;
    };

    void handle(const Peer& peer, const Header& header, const Bytes& body, double now);
    void handleHello(const Peer& peer, const Header& header, const Bytes& body, double now);
    Slot* find(const Peer& peer, std::uint64_t clientId);
    bool allow(const Peer& peer, double now);
    void send(Slot& slot, Type type, const Bytes& body);
    void sendRaw(const Peer& peer, std::uint8_t version, Type type, std::uint64_t session, std::uint64_t seq, const Bytes& body);
    void reject(const Peer& peer, std::uint64_t clientId, std::uint64_t echoSeq, RejectCode code, const std::string& text);
    void sendWelcome(Slot& slot, double now);
    void sendState(Slot& slot, const MasterFrame& frame, double now);
    void sendManifest(Slot& slot, const MasterFrame& frame, double now);
    void sendDeltas(Slot& slot, const MasterFrame& frame, double now);
    void sendClock(Slot& slot, const MasterFrame& frame, double now);
    void remember(std::uint64_t clientId, std::uint64_t seq);

    MasterConfig config_;
    std::vector<Slot> slots_;
    std::unordered_map<std::uint64_t, std::uint64_t> seen_;
    std::deque<std::uint64_t> seenOrder_;
    std::unordered_map<std::uint32_t, Bucket> buckets_;
    std::vector<Datagram> outgoing_;
    Stats stats_;
    double lastNow_ = 0.0;
    double lastReject_ = -1e9;
};

class ClockEstimator {
public:
    void reset();
    void add(double localTime, double offset, double roundTrip);
    size_t samples() const { return samples_.size(); }
    bool ready() const;
    double offsetAt(double localTime) const;
    double roundTrip() const;
    double jitter() const;
    double driftPpm() const;

private:
    struct Sample {
        double time = 0.0;
        double offset = 0.0;
        double rtt = 0.0;
    };

    void fit() const;

    std::deque<Sample> samples_;
    mutable bool dirty_ = true;
    mutable double meanTime_ = 0.0;
    mutable double meanOffset_ = 0.0;
    mutable double slope_ = 0.0;
    mutable double jitter_ = 0.0;
    mutable double bestRtt_ = 0.0;
    mutable size_t used_ = 0;
};

enum class FollowerPhase { Idle, Connecting, Syncing, Live, Lost };

struct Change {
    std::uint16_t index = 0;
    float value = 0.0f;
};

struct ClockView {
    bool valid = false;
    bool running = false;
    bool locked = false;
    double beat = 0.0;
    double bpm = 120.0;
    int beatsPerBar = 4;
    bool timecodeValid = false;
    double timecode = 0.0;
    int timecodeRate = 0;
    double offset = 0.0;
    double roundTrip = 0.0;
    double jitter = 0.0;
    double driftPpm = 0.0;
    double age = 0.0;
    std::uint32_t cues = 0;
};

struct FollowerConfig {
    Key key{};
    std::string name;
    std::string appVersion;
    std::uint64_t schema = 0;
    size_t stateSize = 0;
    RandomFn random;
};

class Follower {
public:
    explicit Follower(const FollowerConfig& config);

    void start(double now);
    void stop(double now);
    void receive(const std::uint8_t* data, size_t size, double now);
    void update(double now);
    std::vector<Bytes> takeOutgoing();

    FollowerPhase phase() const { return phase_; }
    Compat compat() const { return compat_; }
    RejectCode lastReject() const { return lastReject_; }
    const std::string& masterName() const { return masterName_; }
    const std::string& masterVersion() const { return masterVersion_; }
    const std::vector<float>& state() const { return state_; }
    bool hasState() const { return haveState_; }
    std::uint32_t stateSeq() const { return stateSeq_; }
    bool resyncing() const { return needResync_; }
    std::vector<Change> takeChanges();
    ClockView clock(double now) const;
    bool takeJump();
    const Manifest& manifest() const { return manifest_; }
    std::uint64_t manifestVersion() const { return manifestVersion_; }
    const Stats& stats() const { return stats_; }
    const ClockEstimator& estimator() const { return estimator_; }
    double lastHeard() const { return lastHeard_; }

private:
    void handle(const Header& header, const Bytes& body, double now);
    void sendHello(double now);
    void sendPacket(Type type, const Bytes& body);
    void applyState(const std::vector<float>& values);
    void requestResync(double now, std::uint8_t reason);
    void dropConnection(double now);

    FollowerConfig config_;
    FollowerPhase phase_ = FollowerPhase::Idle;
    Compat compat_ = Compat::Full;
    RejectCode lastReject_ = RejectCode::None;
    std::string masterName_;
    std::string masterVersion_;
    std::uint64_t clientId_ = 0;
    std::uint64_t nonce_ = 0;
    std::uint64_t outSeq_ = 0;
    std::uint64_t epoch_ = 0;
    std::uint64_t inSeq_ = 0;
    std::uint8_t protocol_ = kProtocol;
    double nextHello_ = 0.0;
    double helloDelay_ = kHelloRetryMin;
    double lastHeard_ = 0.0;
    double nextPing_ = 0.0;
    int pingBurst_ = 0;
    std::uint32_t pingNumber_ = 0;
    std::vector<float> state_;
    bool haveState_ = false;
    bool waitingForState_ = true;
    bool needResync_ = false;
    std::uint32_t stateSeq_ = 0;
    std::uint32_t masterSeq_ = 0;
    double masterSeqSince_ = 0.0;
    double lastResync_ = -1e9;
    std::vector<Change> changes_;
    ClockEstimator estimator_;
    ClockMsg beacon_;
    double beaconLocal_ = 0.0;
    bool haveBeacon_ = false;
    std::uint16_t lastJumps_ = 0;
    bool jumpPending_ = false;
    Manifest manifest_;
    std::uint64_t manifestVersion_ = 0;
    std::vector<Bytes> outgoing_;
    Stats stats_;
};

}
