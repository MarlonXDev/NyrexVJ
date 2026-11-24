#include "core/plugin_host.hpp"

#include "core/crash_handler.hpp"
#include "core/dynlib.hpp"
#include "core/log.hpp"

#include "nyrexvj_plugin.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstddef>
#include <cstring>
#include <map>
#include <mutex>
#include <set>
#include <thread>
#include <vector>

namespace nvj {

namespace fs = std::filesystem;

namespace {

constexpr size_t kGuardBytes = 64;
constexpr uint8_t kGuardByte = 0xA5;
constexpr size_t kMaxLogLength = 400;

std::atomic<int> gThreads{0};

std::mutex& threadMutex() {
    static std::mutex* mutex = new std::mutex();
    return *mutex;
}

std::condition_variable& threadEnded() {
    static std::condition_variable* variable = new std::condition_variable();
    return *variable;
}

void threadStarted() {
    ++gThreads;
}

void threadFinished() {
    {
        std::lock_guard<std::mutex> lock(threadMutex());
        --gThreads;
    }
    threadEnded().notify_all();
}

std::string boundedString(const char* text, size_t maxLength) {
    if (text == nullptr) {
        return std::string();
    }
    size_t length = 0;
    while (length < maxLength && text[length] != '\0') {
        ++length;
    }
    return std::string(text, length);
}

bool printable(const std::string& text, size_t maxLength) {
    if (text.empty() || text.size() > maxLength) {
        return false;
    }
    for (unsigned char c : text) {
        if (c < 0x20 || c == 0x7f) {
            return false;
        }
    }
    return true;
}

struct SnapshotBox {
    std::mutex mutex;
    PluginHostSnapshot data;
};

struct ParamBlock {
    std::array<std::atomic<float>, kPluginParameters> values;

    ParamBlock() {
        for (std::atomic<float>& value : values) {
            value.store(0.5f);
        }
    }
};

struct HostContext {
    std::string pluginId;
    int maxInstances = kPluginMaxInstancesPerPlugin;
    std::shared_ptr<SnapshotBox> snapshot;
    std::shared_ptr<ParamBlock> params;
    std::atomic<int> logBudget{kPluginLogsPerSecond};
    std::mutex errorMutex;
    std::string pluginError;

    void storeError(const std::string& text) {
        std::lock_guard<std::mutex> lock(errorMutex);
        pluginError = text;
    }

    std::string takeError() {
        std::lock_guard<std::mutex> lock(errorMutex);
        std::string text;
        text.swap(pluginError);
        return text;
    }
};

template <typename T>
struct Guarded {
    T value{};
    uint8_t guard[kGuardBytes];

    Guarded() { std::memset(guard, kGuardByte, sizeof(guard)); }

    bool intact() const {
        for (size_t i = 0; i < kGuardBytes; ++i) {
            if (guard[i] != kGuardByte) {
                return false;
            }
        }
        return true;
    }
};

std::string cleanText(const std::string& text) {
    std::string out = text;
    for (char& c : out) {
        if (static_cast<unsigned char>(c) < 0x20 || static_cast<unsigned char>(c) == 0x7f) {
            c = ' ';
        }
    }
    return out;
}

void hostLog(void* context, int32_t level, const char* message) {
    HostContext* host = static_cast<HostContext*>(context);
    if (host == nullptr || message == nullptr) {
        return;
    }
    if (host->logBudget.fetch_sub(1) <= 0) {
        host->logBudget.store(-1);
        return;
    }
    const char* label = level == NVJ_LOG_ERROR ? "error" : (level == NVJ_LOG_WARNING ? "warning" : "info");
    std::string text = boundedString(message, kMaxLogLength);
    for (char& c : text) {
        if (static_cast<unsigned char>(c) < 0x20) {
            c = ' ';
        }
    }
    logLine("plugin " + host->pluginId + " " + label + ": " + text);
}

PluginHostSnapshot readSnapshot(HostContext* host) {
    PluginHostSnapshot copy;
    if (host != nullptr && host->snapshot) {
        std::lock_guard<std::mutex> lock(host->snapshot->mutex);
        copy = host->snapshot->data;
    }
    return copy;
}

int32_t hostAudio(void* context, NvjAudio* out) {
    HostContext* host = static_cast<HostContext*>(context);
    if (host == nullptr || out == nullptr || out->size < sizeof(uint32_t)) {
        return NVJ_FAILED;
    }
    const PluginHostSnapshot snapshot = readSnapshot(host);
    NvjAudio full{};
    full.size = sizeof(NvjAudio);
    full.bass = snapshot.bass;
    full.mid = snapshot.mid;
    full.high = snapshot.high;
    full.level = snapshot.level;
    full.kick = snapshot.kick;
    const uint32_t given = out->size;
    std::memcpy(out, &full, std::min<size_t>(given, sizeof(full)));
    out->size = given;
    return NVJ_OK;
}

int32_t hostBeat(void* context, NvjBeat* out) {
    HostContext* host = static_cast<HostContext*>(context);
    if (host == nullptr || out == nullptr || out->size < sizeof(uint32_t)) {
        return NVJ_FAILED;
    }
    const PluginHostSnapshot snapshot = readSnapshot(host);
    NvjBeat full{};
    full.size = sizeof(NvjBeat);
    full.beat = std::isfinite(snapshot.beat) ? snapshot.beat : 0.0;
    full.bpm = snapshot.bpm;
    full.beats_per_bar = snapshot.beatsPerBar;
    const uint32_t given = out->size;
    std::memcpy(out, &full, std::min<size_t>(given, sizeof(full)));
    out->size = given;
    return NVJ_OK;
}

float hostParameter(void* context, uint32_t index) {
    HostContext* host = static_cast<HostContext*>(context);
    if (host == nullptr || !host->params || index >= static_cast<uint32_t>(kPluginParameters)) {
        return 0.5f;
    }
    return host->params->values[index].load();
}

int32_t hostHasCapability(void* context, const char* name) {
    if (context == nullptr || name == nullptr) {
        return 0;
    }
    return pluginHostHasCapability(boundedString(name, kMaxCapabilityLength + 1)) ? 1 : 0;
}

int32_t hostLimits(void* context, NvjLimits* out) {
    HostContext* host = static_cast<HostContext*>(context);
    if (host == nullptr || out == nullptr || out->size < sizeof(uint32_t)) {
        return NVJ_FAILED;
    }
    NvjLimits full{};
    full.size = sizeof(NvjLimits);
    full.max_sources = NYREXVJ_PLUGIN_MAX_SOURCES;
    full.max_parameters = NYREXVJ_PLUGIN_MAX_PARAMETERS;
    full.frame_width = static_cast<uint32_t>(kPluginFrameWidth);
    full.frame_height = static_cast<uint32_t>(kPluginFrameHeight);
    full.frame_rate = static_cast<uint32_t>(kPluginFrameFps);
    full.modulation_rate = static_cast<uint32_t>(kModulationRate);
    full.modulation_budget_ms = static_cast<uint32_t>(kPluginModulationBudget * 1000.0);
    full.frame_budget_ms = static_cast<uint32_t>(kPluginFrameBudget * 1000.0);
    full.create_budget_ms = static_cast<uint32_t>(kPluginCreateBudget * 1000.0);
    full.load_budget_ms = static_cast<uint32_t>(kPluginLoadBudget * 1000.0);
    full.max_instances = static_cast<uint32_t>(std::max(0, host->maxInstances));
    full.log_lines_per_second = static_cast<uint32_t>(kPluginLogsPerSecond);
    full.log_line_length = static_cast<uint32_t>(kMaxLogLength);
    const uint32_t given = out->size;
    std::memcpy(out, &full, std::min<size_t>(given, sizeof(full)));
    out->size = given;
    return NVJ_OK;
}

void hostSetError(void* context, const char* message) {
    HostContext* host = static_cast<HostContext*>(context);
    if (host == nullptr || message == nullptr) {
        return;
    }
    host->storeError(cleanText(boundedString(message, kPluginMaxErrorText)));
}

NvjHost makeHostTable(HostContext* context) {
    NvjHost table{};
    table.size = sizeof(NvjHost);
    table.api_major = NYREXVJ_PLUGIN_API_MAJOR;
    table.api_minor = NYREXVJ_PLUGIN_API_MINOR;
    table.context = context;
    table.log = hostLog;
    table.get_audio = hostAudio;
    table.get_beat = hostBeat;
    table.get_parameter = hostParameter;
    table.has_capability = hostHasCapability;
    table.get_limits = hostLimits;
    table.set_error = hostSetError;
    return table;
}

struct Runner {
    NvjPlugin api{};
    std::string pluginId;
    std::string key;
    std::string sourceName;
    uint32_t sourceIndex = 0;
    int kind = 0;
    int width = kPluginFrameWidth;
    int height = kPluginFrameHeight;
    int markerSlot = -1;
    HostContext context;
    NvjHost table{};
    std::mutex mutex;
    std::condition_variable wake;
    std::atomic<bool> stop{false};
    std::atomic<bool> failed{false};
    std::atomic<bool> finished{false};
    std::atomic<bool> started{false};
    std::atomic<bool> inCall{false};
    std::atomic<double> callStart{0.0};
    std::atomic<double> budget{kPluginCreateBudget};
    std::atomic<double> lastUse{0.0};
    std::atomic<float> value{0.0f};
    std::atomic<unsigned long long> calls{0};
    std::atomic<unsigned long long> frames{0};
    std::atomic<double> lastCallMs{0.0};
    std::atomic<double> maxCallMs{0.0};
    std::atomic<double> fps{0.0};
    double maxDuty = kPluginMaxDuty;
    std::atomic<double> duty{0.0};
    std::atomic<double> throttledAt{0.0};
    std::atomic<bool> throttleLogged{false};
    std::atomic<int> errorKind{static_cast<int>(PluginErrorKind::None)};
    std::string error;
    LiveFrameBox box;

    void requestStop() {
        stop.store(true);
        std::lock_guard<std::mutex> lock(mutex);
        wake.notify_all();
    }

    std::string errorText() {
        std::lock_guard<std::mutex> lock(mutex);
        return error;
    }
};

void failRunner(Runner& runner, PluginErrorKind kind, const std::string& reason) {
    bool first = false;
    std::string text = reason;
    {
        std::lock_guard<std::mutex> lock(runner.mutex);
        if (!runner.failed.load()) {
            const std::string said = runner.context.takeError();
            if (!said.empty()) {
                text += " (the plugin says: " + said + ")";
            }
            runner.error = text;
            runner.errorKind.store(static_cast<int>(kind));
            runner.failed.store(true);
            first = true;
        }
        runner.wake.notify_all();
    }
    if (first) {
        logLine("plugin " + runner.pluginId + " disabled for this session [" + pluginErrorKindName(kind) + "]: " + text);
    }
}

template <typename Fn>
bool guardedCall(Runner& runner, double budget, const char* what, Fn&& fn) {
    runner.budget.store(budget);
    const double begin = steadySeconds();
    runner.callStart.store(begin);
    runner.inCall.store(true);
    crashMarkerEnter(runner.markerSlot);
    bool ok = false;
    bool threw = false;
    try {
        ok = fn();
    } catch (...) {
        threw = true;
    }
    crashMarkerLeave(runner.markerSlot);
    runner.inCall.store(false);
    const double ms = (steadySeconds() - begin) * 1000.0;
    runner.lastCallMs.store(ms);
    if (ms > runner.maxCallMs.load()) {
        runner.maxCallMs.store(ms);
    }
    ++runner.calls;
    if (threw) {
        failRunner(runner, PluginErrorKind::Exception, std::string(what) + " threw an exception");
        return false;
    }
    if (!ok) {
        failRunner(runner, PluginErrorKind::CallFailed, std::string(what) + " returned an error");
        return false;
    }
    if (ms > budget * 1000.0) {
        failRunner(runner, PluginErrorKind::Timeout, std::string(what) + " took " + std::to_string(static_cast<int>(ms)) + " ms, over the time budget of " + std::to_string(static_cast<int>(budget * 1000.0)) + " ms");
        return false;
    }
    return !runner.failed.load();
}

void runnerMain(std::shared_ptr<Runner> runner) {
    Runner& r = *runner;
    void* instance = nullptr;
    const bool created = guardedCall(r, kPluginCreateBudget, "create", [&]() {
        instance = r.api.create(r.sourceIndex, &r.table);
        return instance != nullptr;
    });
    if (created) {
        r.started.store(true);
        const bool frameKind = r.kind == kPluginSourceKindFrame;
        const double period = frameKind ? 1.0 / static_cast<double>(kPluginFrameFps) : 1.0 / kModulationRate;
        const double startTime = steadySeconds();
        double previous = startTime;
        double next = startTime;
        const size_t frameBytes = static_cast<size_t>(r.width) * static_cast<size_t>(r.height) * 4u;
        std::vector<uint8_t> work;
        std::vector<uint8_t> spare;
        unsigned long long frameId = 0;
        double fpsAverage = 0.0;
        double lastFrameAt = 0.0;
        double lastCallBegin = 0.0;
        double dutyAverage = 0.0;
        const auto settleCycle = [&]() {
            const double end = steadySeconds();
            const double busy = r.lastCallMs.load() / 1000.0;
            const double gap = busy * (1.0 / r.maxDuty - 1.0);
            if (next < end + gap) {
                next = end + gap;
                r.throttledAt.store(end);
                if (!r.throttleLogged.exchange(true)) {
                    logLine("plugin " + r.pluginId + " is slowed down: its calls use more than " + std::to_string(static_cast<int>(r.maxDuty * 100.0)) + " percent of the time of one thread");
                }
            }
        };
        while (true) {
            {
                std::unique_lock<std::mutex> lock(r.mutex);
                const auto until = std::chrono::steady_clock::now() + std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(std::max(0.0, next - steadySeconds())));
                r.wake.wait_until(lock, until, [&]() { return r.stop.load() || r.failed.load(); });
            }
            if (r.stop.load() || r.failed.load()) {
                break;
            }
            const double now = steadySeconds();
            const double delta = std::max(0.0, now - previous);
            previous = now;
            next += period;
            if (next < now) {
                next = now + period;
            }
            const double time = now - startTime;
            if (lastCallBegin > 0.0 && now > lastCallBegin) {
                const double busyBefore = r.lastCallMs.load() / 1000.0;
                dutyAverage = dutyAverage * 0.8 + std::min(1.0, busyBefore / (now - lastCallBegin)) * 0.2;
                r.duty.store(dutyAverage);
            }
            lastCallBegin = now;
            if (!frameKind) {
                float output = 0.0f;
                if (!guardedCall(r, kPluginModulationBudget, "modulation", [&]() { return r.api.modulation(instance, time, delta, &output) == NVJ_OK; })) {
                    break;
                }
                settleCycle();
                if (!std::isfinite(output)) {
                    output = 0.0f;
                }
                r.value.store(std::clamp(output, 0.0f, 1.0f));
                continue;
            }
            work.assign(frameBytes + kGuardBytes, 0);
            std::fill(work.begin() + static_cast<std::ptrdiff_t>(frameBytes), work.end(), kGuardByte);
            NvjImage image{};
            image.size = sizeof(NvjImage);
            image.width = static_cast<uint32_t>(r.width);
            image.height = static_cast<uint32_t>(r.height);
            image.stride = static_cast<uint32_t>(r.width) * 4u;
            image.data = work.data();
            if (!guardedCall(r, kPluginFrameBudget, "frame", [&]() { return r.api.frame(instance, time, delta, &image) == NVJ_OK; })) {
                break;
            }
            settleCycle();
            bool intact = image.width == static_cast<uint32_t>(r.width) && image.height == static_cast<uint32_t>(r.height) && image.stride == static_cast<uint32_t>(r.width) * 4u && image.data == work.data();
            if (!intact) {
                failRunner(r, PluginErrorKind::BadOutput, "frame changed the image description");
                break;
            }
            for (size_t i = 0; i < kGuardBytes; ++i) {
                if (work[frameBytes + i] != kGuardByte) {
                    intact = false;
                    break;
                }
            }
            if (!intact) {
                failRunner(r, PluginErrorKind::Guard, "frame wrote outside of the image");
                break;
            }
            LiveFrame frame;
            frame.format = LiveFormat::Rgba;
            frame.width = r.width;
            frame.height = r.height;
            frame.stride = r.width * 4;
            frame.id = ++frameId;
            const double capturedAt = steadySeconds();
            frame.capturedAt = capturedAt;
            if (spare.size() == frameBytes) {
                frame.data = std::move(spare);
            }
            frame.data.assign(work.begin(), work.begin() + static_cast<std::ptrdiff_t>(frameBytes));
            r.box.publish(frame);
            spare = std::move(frame.data);
            ++r.frames;
            if (lastFrameAt > 0.0) {
                const double instant = 1.0 / std::max(1.0e-3, capturedAt - lastFrameAt);
                fpsAverage = fpsAverage <= 0.0 ? instant : fpsAverage * 0.9 + instant * 0.1;
                r.fps.store(fpsAverage);
            }
            lastFrameAt = capturedAt;
        }
        if (!r.failed.load() && r.api.destroy != nullptr) {
            guardedCall(r, kPluginCreateBudget, "destroy", [&]() {
                r.api.destroy(instance);
                return true;
            });
        }
    }
    crashMarkerRelease(r.markerSlot);
    r.finished.store(true);
    threadFinished();
}

class PluginFrameSource : public LiveSource {
public:
    explicit PluginFrameSource(std::shared_ptr<Runner> runner) : runner_(std::move(runner)) {}
    ~PluginFrameSource() override { stop(); }

    void stop() override { runner_->requestStop(); }

    bool running() const override { return !runner_->stop.load() && !runner_->failed.load() && !runner_->finished.load(); }

    bool take(LiveFrame& out) override { return runner_->box.take(out); }

    LiveStatus status() const override {
        LiveStatus status;
        status.source = runner_->sourceName;
        status.width = runner_->width;
        status.height = runner_->height;
        status.frames = runner_->frames.load();
        status.dropped = runner_->box.dropped();
        status.fps = runner_->fps.load();
        if (runner_->failed.load()) {
            status.state = LiveState::SignalLost;
            status.error = runner_->errorText();
        } else if (runner_->frames.load() > 0) {
            status.state = LiveState::Live;
        } else {
            status.state = LiveState::Connecting;
        }
        return status;
    }

private:
    std::shared_ptr<Runner> runner_;
};

struct LoadJob {
    std::string id;
    fs::path path;
    PluginManifest manifest;
    HostContext context;
    NvjHost table{};
    DynamicLibrary library;
    Guarded<NvjPlugin> raw;
    NvjPlugin api{};
    std::vector<PluginSourceDesc> sources;
    std::string error;
    PluginErrorKind kind = PluginErrorKind::None;
    std::atomic<bool> done{false};
    bool ok = false;
};

bool describeSources(LoadJob& job, std::string& error, PluginErrorKind& kind) {
    const NvjPlugin& api = job.api;
    kind = PluginErrorKind::Load;
    if (api.size < offsetof(NvjPlugin, source_count) + sizeof(uint32_t)) {
        error = "the plugin table is too small";
        return false;
    }
    if (api.api_major != NYREXVJ_PLUGIN_API_MAJOR || api.api_minor > NYREXVJ_PLUGIN_API_MINOR) {
        error = "the library reports API " + std::to_string(api.api_major) + "." + std::to_string(api.api_minor);
        kind = PluginErrorKind::Incompatible;
        return false;
    }
    if (api.api_major != job.manifest.apiMajor || api.api_minor != job.manifest.apiMinor) {
        error = "the library reports API " + std::to_string(api.api_major) + "." + std::to_string(api.api_minor) + " but plugin.json says " + std::to_string(job.manifest.apiMajor) + "." + std::to_string(job.manifest.apiMinor);
        kind = PluginErrorKind::Incompatible;
        return false;
    }
    if (boundedString(api.id, kMaxPluginIdLength + 1) != job.manifest.id) {
        error = "the library id differs from plugin.json";
        kind = PluginErrorKind::Incompatible;
        return false;
    }
    if (!NYREXVJ_HAS_FIELD(&api, NvjPlugin, get_source) || !NYREXVJ_HAS_FIELD(&api, NvjPlugin, create) || !NYREXVJ_HAS_FIELD(&api, NvjPlugin, destroy) || api.get_source == nullptr || api.create == nullptr || api.destroy == nullptr) {
        error = "the plugin does not provide get_source, create and destroy";
        return false;
    }
    if (api.source_count == 0 || api.source_count > NYREXVJ_PLUGIN_MAX_SOURCES) {
        error = "the plugin reports " + std::to_string(api.source_count) + " sources (1 to " + std::to_string(NYREXVJ_PLUGIN_MAX_SOURCES) + " are allowed)";
        return false;
    }
    for (uint32_t i = 0; i < api.source_count; ++i) {
        Guarded<NvjSourceInfo> infoBox;
        NvjSourceInfo& info = infoBox.value;
        info.size = sizeof(NvjSourceInfo);
        if (api.get_source(i, &info) != NVJ_OK) {
            error = "get_source failed for source " + std::to_string(i);
            return false;
        }
        if (!infoBox.intact()) {
            error = "get_source wrote behind the source description of source " + std::to_string(i);
            kind = PluginErrorKind::Guard;
            return false;
        }
        PluginSourceDesc desc;
        desc.id = boundedString(info.id, kMaxPluginIdLength + 1);
        desc.name = boundedString(info.name, kMaxPluginNameLength + 1);
        desc.kind = static_cast<int>(info.kind);
        desc.parameterCount = static_cast<int>(std::min<uint32_t>(info.parameter_count, NYREXVJ_PLUGIN_MAX_PARAMETERS));
        if (!validPluginId(desc.id) || !printable(desc.name, kMaxPluginNameLength)) {
            error = "source " + std::to_string(i) + " has an invalid id or name";
            return false;
        }
        if (info.parameter_count > NYREXVJ_PLUGIN_MAX_PARAMETERS) {
            error = "source " + desc.id + " has more than " + std::to_string(NYREXVJ_PLUGIN_MAX_PARAMETERS) + " parameters";
            return false;
        }
        if (desc.kind == kPluginSourceKindModulation) {
            if (!NYREXVJ_HAS_FIELD(&api, NvjPlugin, modulation) || api.modulation == nullptr) {
                error = "source " + desc.id + " is a modulation source but the plugin has no modulation function";
                return false;
            }
        } else if (desc.kind == kPluginSourceKindFrame) {
            if (!NYREXVJ_HAS_FIELD(&api, NvjPlugin, frame) || api.frame == nullptr) {
                error = "source " + desc.id + " is a frame source but the plugin has no frame function";
                return false;
            }
        } else {
            error = "source " + desc.id + " has an unknown kind";
            return false;
        }
        for (int p = 0; p < kPluginParameters; ++p) {
            std::string name = p < desc.parameterCount ? boundedString(info.parameter_names[p], 25) : std::string();
            if (p < desc.parameterCount && !printable(name, 24)) {
                name = "Parameter " + std::to_string(p + 1);
            }
            desc.parameterNames[static_cast<size_t>(p)] = name;
        }
        for (const PluginSourceDesc& other : job.sources) {
            if (other.id == desc.id) {
                error = "two sources use the id " + desc.id;
                return false;
            }
        }
        desc.key = pluginSourceKey(job.id, desc.id);
        job.sources.push_back(std::move(desc));
    }
    return true;
}

void loadMain(std::shared_ptr<LoadJob> job) {
    const int slot = crashMarkerAcquire(job->id);
    crashMarkerEnter(slot);
    std::string error;
    PluginErrorKind kind = PluginErrorKind::Load;
    bool ok = false;
    try {
        if (!job->library.open(job->path, error)) {
            error = "the library could not be loaded: " + error;
        } else {
            void* symbol = job->library.symbol(NYREXVJ_PLUGIN_ENTRY_NAME);
            if (symbol == nullptr) {
                error = std::string("the library has no ") + NYREXVJ_PLUGIN_ENTRY_NAME + " function";
            } else {
                NvjEntryFn entry = reinterpret_cast<NvjEntryFn>(symbol);
                job->raw.value.size = sizeof(NvjPlugin);
                if (entry(&job->table, &job->raw.value) != NVJ_OK) {
                    error = "the entry function returned an error";
                } else if (!job->raw.intact()) {
                    error = "the entry function wrote behind the plugin table";
                    kind = PluginErrorKind::Guard;
                } else if (job->raw.value.size > sizeof(NvjPlugin)) {
                    error = "the plugin reports a table of " + std::to_string(job->raw.value.size) + " bytes but the host offered " + std::to_string(sizeof(NvjPlugin));
                    kind = PluginErrorKind::Guard;
                } else {
                    job->api = job->raw.value;
                    ok = describeSources(*job, error, kind);
                }
            }
        }
    } catch (...) {
        ok = false;
        error = "the plugin threw an exception while loading";
        kind = PluginErrorKind::Exception;
    }
    crashMarkerLeave(slot);
    crashMarkerRelease(slot);
    if (!ok) {
        const std::string said = job->context.takeError();
        if (!said.empty()) {
            error += " (the plugin says: " + said + ")";
        }
    }
    job->ok = ok;
    job->error = error;
    job->kind = ok ? PluginErrorKind::None : kind;
    job->done.store(true);
    threadFinished();
}

template <typename T>
bool launchThread(void (*entry)(std::shared_ptr<T>), const std::shared_ptr<T>& argument) {
    threadStarted();
    try {
        std::thread(entry, argument).detach();
        return true;
    } catch (...) {
        threadFinished();
        return false;
    }
}

struct Tracked {
    std::shared_ptr<Runner> runner;
    bool handled = false;
};

}

struct PluginHost::Impl {
    struct ModSlot {
        std::string key;
        std::shared_ptr<Runner> runner;
        std::string refused;
    };

    fs::path dataDir;
    bool allowLoading = true;
    PluginLimits limits;
    PluginConfig config;
    std::vector<PluginSafeEntry> safeList;
    std::vector<PluginInfo> plugins;
    std::map<std::string, NvjPlugin> apis;
    std::map<std::string, fs::path> apiPaths;
    std::map<std::string, std::vector<PluginSourceDesc>> sourceCache;
    std::set<std::string> pending;
    std::set<std::string> refusalLogged;
    std::map<std::string, std::shared_ptr<LoadJob>> jobs;
    std::map<std::string, double> jobStart;
    std::shared_ptr<SnapshotBox> snapshot = std::make_shared<SnapshotBox>();
    std::map<std::string, std::shared_ptr<ParamBlock>> params;
    std::vector<Tracked> runners;
    std::array<ModSlot, kPluginModulationSlots> modSlots;
    int abandoned = 0;
    double logRefresh = 0.0;

    PluginInfo* record(const std::string& id) {
        for (PluginInfo& info : plugins) {
            if (info.entry.manifest.id == id) {
                return &info;
            }
        }
        return nullptr;
    }

    std::shared_ptr<ParamBlock> paramBlock(const std::string& key) {
        std::shared_ptr<ParamBlock>& block = params[key];
        if (!block) {
            block = std::make_shared<ParamBlock>();
            const std::array<float, kPluginParameters> values = config.parametersFor(key);
            for (size_t i = 0; i < values.size(); ++i) {
                block->values[i].store(values[i]);
            }
        }
        return block;
    }

    void startLoad(PluginInfo& info) {
        std::shared_ptr<LoadJob> job = std::make_shared<LoadJob>();
        job->id = info.entry.manifest.id;
        job->path = info.entry.libraryPath;
        job->manifest = info.entry.manifest;
        job->context.pluginId = job->id;
        job->context.maxInstances = limits.instancesPerPlugin;
        job->context.snapshot = snapshot;
        job->table = makeHostTable(&job->context);
        jobs[job->id] = job;
        jobStart[job->id] = steadySeconds();
        info.state = PluginState::Loading;
        info.message.clear();
        if (!launchThread(loadMain, job)) {
            jobs.erase(job->id);
            jobStart.erase(job->id);
            refuse(info, PluginErrorKind::Limit, "could not start a thread for loading");
        }
    }

    int liveRunnersOf(const std::string& id) const {
        int count = 0;
        for (const Tracked& tracked : runners) {
            if (tracked.runner->pluginId == id && !tracked.runner->finished.load()) {
                ++count;
            }
        }
        return count;
    }

    int liveRunnersTotal() const {
        int count = 0;
        for (const Tracked& tracked : runners) {
            if (!tracked.runner->finished.load()) {
                ++count;
            }
        }
        return count;
    }

    void refuse(PluginInfo& info, PluginErrorKind kind, const std::string& reason) {
        info.state = PluginState::Failed;
        info.errorKind = kind;
        info.message = reason;
        logLine("plugin " + info.entry.manifest.id + " " + reason);
    }

    void activate(PluginInfo& info) {
        const std::string id = info.entry.manifest.id;
        info.message.clear();
        info.errorKind = PluginErrorKind::None;
        if (jobs.count(id) != 0) {
            info.state = PluginState::Loading;
            return;
        }
        if (abandoned >= limits.abandoned) {
            refuse(info, PluginErrorKind::Limit, "was not loaded: " + std::to_string(abandoned) + " plugin calls are stuck in this session (the limit is " + std::to_string(limits.abandoned) + "). Restart NyrexVJ.");
            return;
        }
        const auto api = apis.find(id);
        const auto path = apiPaths.find(id);
        if (api != apis.end() && path != apiPaths.end() && path->second == info.entry.libraryPath) {
            info.sources = sourceCache[id];
            if (liveRunnersOf(id) > 0) {
                info.state = PluginState::Loading;
                pending.insert(id);
            } else {
                info.state = PluginState::Loaded;
            }
            return;
        }
        startLoad(info);
    }

    void settlePending() {
        const std::vector<std::string> ids(pending.begin(), pending.end());
        for (const std::string& id : ids) {
            PluginInfo* info = record(id);
            if (info == nullptr || info->state != PluginState::Loading || !config.isEnabled(id)) {
                if (info != nullptr && info->state == PluginState::Loading) {
                    info->state = PluginState::NotEnabled;
                }
                pending.erase(id);
            } else if (liveRunnersOf(id) == 0) {
                info->state = PluginState::Loaded;
                pending.erase(id);
            }
        }
    }

    static void dropSlot(ModSlot& slot) {
        if (slot.runner) {
            slot.runner->requestStop();
            slot.runner.reset();
        }
    }

    void stopRunnersOf(const std::string& id) {
        for (Tracked& tracked : runners) {
            if (tracked.runner->pluginId == id) {
                tracked.runner->requestStop();
            }
        }
        for (ModSlot& slot : modSlots) {
            if (slot.runner && slot.runner->pluginId == id) {
                dropSlot(slot);
            }
        }
    }

    void failPlugin(const std::string& id, PluginErrorKind kind, const std::string& reason) {
        PluginInfo* info = record(id);
        if (info != nullptr && info->state != PluginState::Failed) {
            info->state = PluginState::Failed;
            info->errorKind = kind;
            info->message = reason;
        }
        pending.erase(id);
        stopRunnersOf(id);
    }

    void harvestLoads(double now) {
        std::vector<std::string> finished;
        for (auto& item : jobs) {
            const std::string& id = item.first;
            LoadJob& job = *item.second;
            PluginInfo* info = record(id);
            if (info == nullptr) {
                finished.push_back(id);
                continue;
            }
            if (job.done.load()) {
                if (job.ok) {
                    apis[id] = job.api;
                    apiPaths[id] = job.path;
                    sourceCache[id] = job.sources;
                    if (info->state == PluginState::Loading) {
                        info->sources = job.sources;
                        info->state = PluginState::Loaded;
                        info->message.clear();
                        info->errorKind = PluginErrorKind::None;
                        logLine("plugin " + id + " loaded with " + std::to_string(job.sources.size()) + " sources");
                    } else {
                        logLine("plugin " + id + " finished loading after it was switched off, it stays off");
                    }
                } else {
                    info->state = PluginState::Failed;
                    info->message = job.error;
                    info->errorKind = job.kind;
                    logLine("plugin " + id + " could not be loaded [" + pluginErrorKindName(job.kind) + "]: " + job.error);
                }
                finished.push_back(id);
            } else if (now - jobStart[id] > kPluginLoadBudget) {
                info->state = PluginState::Failed;
                info->errorKind = PluginErrorKind::Timeout;
                info->message = "loading did not finish within " + std::to_string(static_cast<int>(kPluginLoadBudget * 1000.0)) + " ms";
                logLine("plugin " + id + " " + info->message);
                ++abandoned;
                finished.push_back(id);
            }
        }
        for (const std::string& id : finished) {
            jobs.erase(id);
            jobStart.erase(id);
            pending.erase(id);
        }
    }

    void noteRefusal(const std::string& pluginId, const std::string& text) {
        if (refusalLogged.insert(pluginId + text).second) {
            logLine("plugin " + pluginId + " " + text);
        }
    }

    std::shared_ptr<Runner> startRunner(const std::string& key, int kind, std::string& error) {
        std::string pluginId;
        std::string sourceId;
        if (!splitPluginSourceKey(key, pluginId, sourceId)) {
            error = "no source is assigned";
            return nullptr;
        }
        PluginInfo* info = record(pluginId);
        if (info == nullptr || info->state != PluginState::Loaded || apis.find(pluginId) == apis.end()) {
            error = info != nullptr && info->state == PluginState::Failed ? info->message : "the plugin is not loaded";
            return nullptr;
        }
        uint32_t index = 0;
        const PluginSourceDesc* desc = nullptr;
        for (size_t i = 0; i < info->sources.size(); ++i) {
            if (info->sources[i].id == sourceId) {
                desc = &info->sources[i];
                index = static_cast<uint32_t>(i);
                break;
            }
        }
        if (desc == nullptr || desc->kind != kind) {
            error = "the source does not exist or has another kind";
            return nullptr;
        }
        if (abandoned >= limits.abandoned) {
            error = "limit: " + std::to_string(abandoned) + " plugin calls are stuck, no new instance is started";
            noteRefusal(pluginId, error);
            return nullptr;
        }
        if (liveRunnersOf(pluginId) >= limits.instancesPerPlugin) {
            error = "limit: this plugin already has " + std::to_string(limits.instancesPerPlugin) + " running instances";
            noteRefusal(pluginId, error);
            return nullptr;
        }
        if (liveRunnersTotal() >= limits.instancesTotal) {
            error = "limit: " + std::to_string(limits.instancesTotal) + " plugin instances are running in total";
            noteRefusal(pluginId, error);
            return nullptr;
        }
        std::shared_ptr<Runner> runner = std::make_shared<Runner>();
        runner->api = apis[pluginId];
        runner->pluginId = pluginId;
        runner->key = key;
        runner->sourceName = info->entry.manifest.name + ": " + desc->name;
        runner->sourceIndex = index;
        runner->kind = kind;
        runner->context.pluginId = pluginId;
        runner->context.snapshot = snapshot;
        runner->context.params = paramBlock(key);
        runner->table = makeHostTable(&runner->context);
        runner->maxDuty = std::clamp(limits.maxDuty, 0.05, 1.0);
        runner->context.maxInstances = limits.instancesPerPlugin;
        runner->lastUse.store(steadySeconds());
        runner->markerSlot = crashMarkerAcquire(pluginId);
        if (runner->markerSlot < 0) {
            error = "too many plugin instances";
            return nullptr;
        }
        Tracked tracked;
        tracked.runner = runner;
        runners.push_back(tracked);
        if (!launchThread(runnerMain, runner)) {
            runners.pop_back();
            crashMarkerRelease(runner->markerSlot);
            error = "limit: the system could not start another thread";
            noteRefusal(pluginId, error);
            return nullptr;
        }
        return runner;
    }

    void watchdog(double now) {
        for (Tracked& tracked : runners) {
            Runner& r = *tracked.runner;
            if (r.finished.load()) {
                continue;
            }
            if (r.inCall.load() && !r.failed.load()) {
                const double elapsed = now - r.callStart.load();
                const double budget = r.budget.load();
                if (elapsed > budget) {
                    failRunner(r, PluginErrorKind::Timeout, "a call took longer than the time budget of " + std::to_string(static_cast<int>(budget * 1000.0)) + " ms");
                }
            }
            if (r.failed.load() && !tracked.handled) {
                tracked.handled = true;
                if (r.inCall.load()) {
                    ++abandoned;
                }
                failPlugin(r.pluginId, static_cast<PluginErrorKind>(r.errorKind.load()), r.errorText());
            }
            if (r.kind == kPluginSourceKindModulation && !r.stop.load() && now - r.lastUse.load() > kPluginIdleSeconds) {
                r.requestStop();
            }
        }
        runners.erase(std::remove_if(runners.begin(), runners.end(), [](const Tracked& tracked) { return tracked.runner->finished.load() && tracked.handled == tracked.runner->failed.load(); }), runners.end());
        for (Tracked& tracked : runners) {
            if (tracked.runner->finished.load() && !tracked.handled && tracked.runner->failed.load()) {
                tracked.handled = true;
                failPlugin(tracked.runner->pluginId, static_cast<PluginErrorKind>(tracked.runner->errorKind.load()), tracked.runner->errorText());
            }
        }
        if (now - logRefresh >= 1.0) {
            logRefresh = now;
            for (Tracked& tracked : runners) {
                tracked.runner->context.logBudget.store(kPluginLogsPerSecond);
            }
        }
        for (ModSlot& slot : modSlots) {
            if (slot.runner && (slot.runner->stop.load() || slot.runner->finished.load())) {
                slot.runner.reset();
            }
        }
    }
};

PluginHost::PluginHost(fs::path dataDir, bool allowLoading, PluginLimits limits) : impl_(std::make_unique<Impl>()) {
    impl_->dataDir = std::move(dataDir);
    impl_->allowLoading = allowLoading;
    impl_->limits = limits;
    impl_->config = readPluginConfig(impl_->dataDir);
    rescan();
}

PluginHost::~PluginHost() {
    stopAll();
}

void PluginHost::rescan() {
    Impl& s = *impl_;
    s.safeList = readPluginSafeList(s.dataDir);
    std::vector<PluginInfo> previous = std::move(s.plugins);
    s.plugins.clear();
    const std::vector<PluginEntry> entries = scanPlugins(pluginsFolder(s.dataDir));
    for (const PluginEntry& entry : entries) {
        PluginInfo info;
        info.entry = entry;
        if (!entry.usable()) {
            info.state = PluginState::Incompatible;
            info.errorKind = PluginErrorKind::Incompatible;
            info.message = pluginProblemName(entry.problem);
            if (!entry.detail.empty()) {
                info.message += " (" + entry.detail + ")";
            }
            s.plugins.push_back(std::move(info));
            continue;
        }
        const std::string& id = entry.manifest.id;
        const auto before = std::find_if(previous.begin(), previous.end(), [&](const PluginInfo& other) { return other.entry.manifest.id == id; });
        if (before != previous.end() && (before->state == PluginState::Loaded || before->state == PluginState::Failed || before->state == PluginState::Loading)) {
            info.state = before->state;
            info.errorKind = before->errorKind;
            info.message = before->message;
            info.sources = before->sources;
            s.plugins.push_back(std::move(info));
            continue;
        }
        const auto safe = std::find_if(s.safeList.begin(), s.safeList.end(), [&](const PluginSafeEntry& item) { return item.id == id; });
        if (safe != s.safeList.end()) {
            info.state = PluginState::SafeMode;
            info.safeReason = safe->reason;
            s.plugins.push_back(std::move(info));
            continue;
        }
        s.plugins.push_back(std::move(info));
        if (s.allowLoading && s.config.isEnabled(id)) {
            s.activate(s.plugins.back());
        }
    }
}

void PluginHost::markCrashed(const std::vector<std::string>& ids, long long time) {
    addToPluginSafeList(impl_->dataDir, ids, "crashed in the last session", time);
    for (const std::string& id : ids) {
        PluginInfo* info = impl_->record(id);
        if (info != nullptr && info->state != PluginState::Incompatible) {
            impl_->stopRunnersOf(id);
            info->state = PluginState::SafeMode;
            info->safeReason = "crashed in the last session";
        }
    }
    impl_->safeList = readPluginSafeList(impl_->dataDir);
}

void PluginHost::setEnabled(const std::string& id, bool on) {
    Impl& s = *impl_;
    PluginInfo* info = s.record(id);
    if (info == nullptr) {
        return;
    }
    if (on && (info->state == PluginState::SafeMode || info->state == PluginState::Incompatible)) {
        return;
    }
    s.config.setEnabled(id, on);
    saveConfig();
    if (!on) {
        s.pending.erase(id);
        s.stopRunnersOf(id);
        if (info->state == PluginState::Loaded || info->state == PluginState::Loading) {
            info->state = PluginState::NotEnabled;
            info->message.clear();
            info->errorKind = PluginErrorKind::None;
        }
        return;
    }
    if (!s.allowLoading) {
        return;
    }
    if (info->state == PluginState::NotEnabled) {
        s.activate(*info);
    }
}

void PluginHost::clearSafeMode(const std::string& id) {
    Impl& s = *impl_;
    removeFromPluginSafeList(s.dataDir, id);
    s.safeList = readPluginSafeList(s.dataDir);
    PluginInfo* info = s.record(id);
    if (info != nullptr && info->state == PluginState::SafeMode) {
        info->state = PluginState::NotEnabled;
        info->safeReason.clear();
        s.config.setEnabled(id, false);
        saveConfig();
    }
}

const std::vector<PluginInfo>& PluginHost::plugins() const {
    return impl_->plugins;
}

const PluginInfo* PluginHost::find(const std::string& id) const {
    for (const PluginInfo& info : impl_->plugins) {
        if (info.entry.manifest.id == id) {
            return &info;
        }
    }
    return nullptr;
}

const PluginSourceDesc* PluginHost::findSource(const std::string& key) const {
    std::string pluginId;
    std::string sourceId;
    if (!splitPluginSourceKey(key, pluginId, sourceId)) {
        return nullptr;
    }
    const PluginInfo* info = find(pluginId);
    if (info == nullptr) {
        return nullptr;
    }
    for (const PluginSourceDesc& source : info->sources) {
        if (source.id == sourceId) {
            return &source;
        }
    }
    return nullptr;
}

bool PluginHost::allowLoading() const {
    return impl_->allowLoading;
}

const PluginLimits& PluginHost::limits() const {
    return impl_->limits;
}

bool PluginHost::loading() const {
    return !impl_->jobs.empty();
}

PluginConfig& PluginHost::config() {
    return impl_->config;
}

const PluginConfig& PluginHost::config() const {
    return impl_->config;
}

bool PluginHost::saveConfig() {
    return writePluginConfig(impl_->dataDir, impl_->config);
}

void PluginHost::assignModulation(int slot, const std::string& key) {
    if (slot < 0 || slot >= kPluginModulationSlots) {
        return;
    }
    impl_->config.modulation[static_cast<size_t>(slot)] = key;
    Impl::dropSlot(impl_->modSlots[static_cast<size_t>(slot)]);
    impl_->modSlots[static_cast<size_t>(slot)].key.clear();
    saveConfig();
}

void PluginHost::assignFrame(int slot, const std::string& key) {
    if (slot < 0 || slot >= kPluginFrameSlots) {
        return;
    }
    impl_->config.frames[static_cast<size_t>(slot)] = key;
    saveConfig();
}

void PluginHost::setParameter(const std::string& key, int index, float value) {
    if (index < 0 || index >= kPluginParameters || !std::isfinite(value)) {
        return;
    }
    const float clamped = std::clamp(value, 0.0f, 1.0f);
    std::array<float, kPluginParameters> values = impl_->config.parametersFor(key);
    values[static_cast<size_t>(index)] = clamped;
    impl_->config.parameters[key] = values;
    impl_->paramBlock(key)->values[static_cast<size_t>(index)].store(clamped);
}

void PluginHost::publish(const PluginHostSnapshot& snapshot) {
    std::lock_guard<std::mutex> lock(impl_->snapshot->mutex);
    impl_->snapshot->data = snapshot;
}

void PluginHost::update() {
    const double now = steadySeconds();
    impl_->harvestLoads(now);
    impl_->watchdog(now);
    impl_->settlePending();
}

float PluginHost::modulation(int slot) {
    if (slot < 0 || slot >= kPluginModulationSlots) {
        return 0.0f;
    }
    Impl& s = *impl_;
    Impl::ModSlot& entry = s.modSlots[static_cast<size_t>(slot)];
    const std::string& key = s.config.modulation[static_cast<size_t>(slot)];
    if (key.empty()) {
        return 0.0f;
    }
    if (entry.key != key) {
        Impl::dropSlot(entry);
        entry.key = key;
    }
    if (!entry.runner) {
        std::string pluginId;
        std::string sourceId;
        const PluginSourceDesc* desc = findSource(key);
        PluginInfo* info = splitPluginSourceKey(key, pluginId, sourceId) ? s.record(pluginId) : nullptr;
        if (desc == nullptr || info == nullptr || info->state != PluginState::Loaded || desc->kind != kPluginSourceKindModulation) {
            return 0.0f;
        }
        std::string error;
        entry.runner = s.startRunner(key, kPluginSourceKindModulation, error);
        entry.refused = entry.runner ? std::string() : error;
        if (!entry.runner) {
            return 0.0f;
        }
    }
    entry.runner->lastUse.store(steadySeconds());
    if (entry.runner->failed.load()) {
        return 0.0f;
    }
    return entry.runner->value.load();
}

namespace {

PluginRunStats statsOf(const Runner& runner, Runner& mutableRunner) {
    PluginRunStats stats;
    stats.running = !runner.stop.load() && !runner.finished.load() && !runner.failed.load();
    stats.failed = runner.failed.load();
    stats.errorKind = static_cast<PluginErrorKind>(runner.errorKind.load());
    stats.error = mutableRunner.errorText();
    stats.calls = runner.calls.load();
    stats.frames = runner.frames.load();
    stats.lastCallMs = runner.lastCallMs.load();
    stats.maxCallMs = runner.maxCallMs.load();
    stats.fps = runner.fps.load();
    stats.value = runner.value.load();
    stats.duty = runner.duty.load();
    const double slowedAt = runner.throttledAt.load();
    stats.throttled = slowedAt > 0.0 && steadySeconds() - slowedAt < 2.0;
    return stats;
}

}

PluginRunStats PluginHost::modulationStats(int slot) const {
    PluginRunStats stats;
    if (slot < 0 || slot >= kPluginModulationSlots) {
        return stats;
    }
    const Impl::ModSlot& entry = impl_->modSlots[static_cast<size_t>(slot)];
    if (!entry.runner) {
        if (!entry.refused.empty()) {
            stats.error = entry.refused;
            stats.errorKind = PluginErrorKind::Limit;
        }
        return stats;
    }
    return statsOf(*entry.runner, *entry.runner);
}

PluginRunStats PluginHost::runnerStats(const std::string& key) const {
    PluginRunStats stats;
    for (auto it = impl_->runners.rbegin(); it != impl_->runners.rend(); ++it) {
        if (it->runner->key == key) {
            return statsOf(*it->runner, *it->runner);
        }
    }
    return stats;
}

int PluginHost::liveInstancesOf(const std::string& id) const {
    return impl_->liveRunnersOf(id);
}

std::unique_ptr<LiveSource> PluginHost::openFrameSource(const std::string& key, std::string& error) {
    std::shared_ptr<Runner> runner = impl_->startRunner(key, kPluginSourceKindFrame, error);
    if (!runner) {
        return nullptr;
    }
    return std::make_unique<PluginFrameSource>(runner);
}

int PluginHost::runningInstances() const {
    int count = 0;
    for (const Tracked& tracked : impl_->runners) {
        if (!tracked.runner->finished.load() && !tracked.runner->failed.load() && !tracked.runner->stop.load()) {
            ++count;
        }
    }
    return count;
}

int PluginHost::abandonedInstances() const {
    return impl_->abandoned;
}

int PluginHost::waitForThreads(double seconds) {
    std::unique_lock<std::mutex> lock(threadMutex());
    threadEnded().wait_for(lock, std::chrono::duration<double>(seconds), []() { return gThreads.load() == 0; });
    return gThreads.load();
}

void PluginHost::stopAll() {
    Impl& s = *impl_;
    for (Tracked& tracked : s.runners) {
        tracked.runner->requestStop();
    }
    for (Impl::ModSlot& slot : s.modSlots) {
        slot.runner.reset();
    }
}

const char* pluginErrorKindName(PluginErrorKind kind) {
    switch (kind) {
    case PluginErrorKind::None:
        return "none";
    case PluginErrorKind::Load:
        return "load error";
    case PluginErrorKind::Incompatible:
        return "not compatible";
    case PluginErrorKind::CallFailed:
        return "call failed";
    case PluginErrorKind::Exception:
        return "exception";
    case PluginErrorKind::Timeout:
        return "time budget";
    case PluginErrorKind::BadOutput:
        return "invalid result";
    case PluginErrorKind::Guard:
        return "memory guard";
    case PluginErrorKind::Limit:
        return "limit";
    }
    return "none";
}

int pluginThreadsAlive() {
    return gThreads.load();
}

std::vector<std::string> recordPluginCrashes(const fs::path& dataDir, const std::string& report, long long time) {
    const std::vector<std::string> ids = pluginsInCrashReport(report);
    if (!ids.empty()) {
        addToPluginSafeList(dataDir, ids, "crashed in the last session", time);
        for (const std::string& id : ids) {
            logLine("plugin " + id + " was running when the last session crashed and is now in safe mode");
        }
    }
    return ids;
}

}
