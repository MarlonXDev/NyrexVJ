#include "media/convert_manager.hpp"

#include "core/log.hpp"
#include "platform/platform.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <system_error>

namespace nvj::hapjobs {

namespace fs = std::filesystem;

namespace {

fs::path pathOf(const std::string& utf8) {
    return fs::path(std::u8string(utf8.begin(), utf8.end()));
}

std::string utf8Of(const fs::path& path) {
    const std::u8string text = path.u8string();
    return std::string(text.begin(), text.end());
}

std::string nameOf(const std::string& source) {
    return utf8Of(pathOf(source).filename());
}

std::string lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

}

ImportMode importModeFromName(const std::string& name) {
    if (name == "ask") {
        return ImportMode::Ask;
    }
    if (name == "automatic") {
        return ImportMode::Automatic;
    }
    return ImportMode::Off;
}

const char* importModeName(ImportMode mode) {
    switch (mode) {
    case ImportMode::Ask:
        return "ask";
    case ImportMode::Automatic:
        return "automatic";
    default:
        return "off";
    }
}

const char* jobStateName(JobState state) {
    switch (state) {
    case JobState::Queued:
        return "queued";
    case JobState::Probing:
        return "checking";
    case JobState::Waiting:
        return "waiting";
    case JobState::Converting:
        return "converting";
    case JobState::Done:
        return "done";
    case JobState::Cached:
        return "cached";
    case JobState::Failed:
        return "failed";
    case JobState::Cancelled:
        return "cancelled";
    }
    return "queued";
}

bool jobIsFinished(JobState state) {
    return state == JobState::Done || state == JobState::Cached || state == JobState::Failed || state == JobState::Cancelled;
}

bool isCandidatePath(const std::string& pathUtf8) {
    return lower(utf8Of(pathOf(pathUtf8).extension())) == ".mov";
}

Manager::Manager(fs::path cacheDirectory) : cache_(std::move(cacheDirectory)) {
    cache_.cleanStale(24 * 3600, {});
    std::map<std::string, hapcache::Entry> best;
    std::map<std::string, bool> bestMatches;
    for (const hapcache::Entry& entry : cache_.list()) {
        if (entry.bad || entry.source.empty()) {
            continue;
        }
        const hapcache::SourceStamp stamp = hapcache::stampOf(entry.source);
        const bool matches = stamp.ok && stamp.bytes == entry.sourceBytes && stamp.mtime == entry.sourceMtime;
        const auto known = best.find(entry.source);
        if (known != best.end()) {
            const bool knownMatches = bestMatches[entry.source];
            if (knownMatches && !matches) {
                continue;
            }
            if (knownMatches == matches && known->second.lastUsed > entry.lastUsed) {
                continue;
            }
        }
        best[entry.source] = entry;
        bestMatches[entry.source] = matches;
    }
    for (const auto& item : best) {
        indexEntry(item.first, item.second);
    }
    loadDismissed();
    worker_ = std::thread([this] { run(); });
}

Manager::~Manager() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stop_ = true;
        for (const std::shared_ptr<Job>& job : jobs_) {
            job->cancelRequested = true;
        }
    }
    condition_.notify_all();
    if (worker_.joinable()) {
        worker_.join();
    }
}

std::string Manager::tagFor(const Config& config) const {
    return hapcache::settingsTag(hapconv::targetKey(config.format), config.snappy);
}

void Manager::configure(const Config& config) {
    bool fromAsk = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        fromAsk = config.mode != ImportMode::Ask && config_.mode == ImportMode::Ask;
        config_ = config;
        if (config_.mode == ImportMode::Off) {
            for (const std::shared_ptr<Job>& job : jobs_) {
                if (!job->explicitRequest && (job->state == JobState::Queued || job->state == JobState::Waiting)) {
                    job->cancelRequested = true;
                    job->state = JobState::Cancelled;
                }
            }
        } else if (fromAsk) {
            for (const std::shared_ptr<Job>& job : jobs_) {
                if (job->state == JobState::Waiting) {
                    job->consented = true;
                    job->state = JobState::Queued;
                }
            }
        }
    }
    condition_.notify_all();
    idle_.notify_all();
}

Config Manager::config() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return config_;
}

void Manager::setBackend(const Backend& backend) {
    std::lock_guard<std::mutex> lock(mutex_);
    backend_ = backend;
}

void Manager::setReady(bool ready) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        ready_ = ready;
    }
    condition_.notify_all();
}

bool Manager::ready() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return ready_;
}

std::string Manager::seenKey(const std::string& source, const hapcache::SourceStamp& stamp) const {
    return source + "|" + std::to_string(stamp.bytes) + "|" + std::to_string(stamp.mtime);
}

bool Manager::duplicateActive(const std::string& source) const {
    for (const std::shared_ptr<Job>& job : jobs_) {
        if (job->source == source && !jobIsFinished(job->state)) {
            return true;
        }
    }
    return false;
}

void Manager::indexEntry(const std::string& source, const hapcache::Entry& entry) {
    std::lock_guard<std::mutex> lock(mutex_);
    IndexEntry item;
    item.key = entry.key;
    item.file = utf8Of(cache_.dataPath(entry.key));
    item.tag = entry.tag;
    item.sourceBytes = entry.sourceBytes;
    item.sourceMtime = entry.sourceMtime;
    index_[source] = item;
}

void Manager::offer(const std::string& sourceUtf8, bool manual) {
    if (!isCandidatePath(sourceUtf8)) {
        return;
    }
    const hapcache::SourceStamp stamp = hapcache::stampOf(sourceUtf8);
    if (!stamp.ok) {
        return;
    }
    bool added = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if ((config_.mode == ImportMode::Off && !manual) || !ready_) {
            return;
        }
        const auto known = index_.find(sourceUtf8);
        if (known != index_.end() && known->second.sourceBytes == stamp.bytes && known->second.sourceMtime == stamp.mtime && known->second.tag == tagFor(config_)) {
            return;
        }
        const std::string seen = seenKey(sourceUtf8, stamp) + "|" + tagFor(config_);
        if (!manual && seen_.count(seen) != 0) {
            return;
        }
        if (!manual && config_.mode == ImportMode::Ask && dismissed_.count(seenKey(sourceUtf8, stamp)) != 0) {
            return;
        }
        seen_.insert(seen);
        if (duplicateActive(sourceUtf8)) {
            return;
        }
        auto job = std::make_shared<Job>();
        job->id = nextId_++;
        job->source = sourceUtf8;
        job->consented = manual || config_.mode == ImportMode::Automatic;
        job->sourceBytes = stamp.bytes;
        jobs_.push_back(job);
        added = true;
    }
    if (added) {
        condition_.notify_all();
    }
}

int Manager::request(const std::string& sourceUtf8, bool force) {
    const hapcache::SourceStamp stamp = hapcache::stampOf(sourceUtf8);
    int id = 0;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!force) {
            for (const std::shared_ptr<Job>& job : jobs_) {
                if (job->source == sourceUtf8 && !jobIsFinished(job->state)) {
                    job->consented = true;
                    job->explicitRequest = true;
                    if (job->state == JobState::Waiting) {
                        job->state = JobState::Queued;
                    }
                    id = job->id;
                    break;
                }
            }
            if (id != 0) {
                condition_.notify_all();
                return id;
            }
        }
        auto job = std::make_shared<Job>();
        job->id = nextId_++;
        job->source = sourceUtf8;
        job->consented = true;
        job->explicitRequest = true;
        job->force = force;
        job->sourceBytes = stamp.bytes;
        if (!ready_) {
            job->state = JobState::Failed;
            job->error = "FFmpeg is not set up, so videos cannot be converted yet";
        }
        id = job->id;
        jobs_.push_back(job);
    }
    condition_.notify_all();
    idle_.notify_all();
    return id;
}

void Manager::approve(int id) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const std::shared_ptr<Job>& job : jobs_) {
            if (job->id == id && job->state == JobState::Waiting) {
                job->consented = true;
                job->state = JobState::Queued;
            }
        }
    }
    condition_.notify_all();
}

void Manager::approveAll() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const std::shared_ptr<Job>& job : jobs_) {
            if (job->state == JobState::Waiting) {
                job->consented = true;
                job->state = JobState::Queued;
            }
        }
    }
    condition_.notify_all();
}

void Manager::dismiss(int id) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto it = jobs_.begin(); it != jobs_.end(); ++it) {
        if ((*it)->id == id && (*it)->state == JobState::Waiting) {
            dismissed_.insert(seenKey((*it)->source, hapcache::stampOf((*it)->source)));
            jobs_.erase(it);
            saveDismissedLocked();
            break;
        }
    }
    idle_.notify_all();
}

void Manager::dismissAll() {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto it = jobs_.begin(); it != jobs_.end();) {
        if ((*it)->state == JobState::Waiting) {
            dismissed_.insert(seenKey((*it)->source, hapcache::stampOf((*it)->source)));
            it = jobs_.erase(it);
        } else {
            ++it;
        }
    }
    saveDismissedLocked();
    idle_.notify_all();
}

void Manager::cancel(int id) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const std::shared_ptr<Job>& job : jobs_) {
            if (job->id != id || jobIsFinished(job->state)) {
                continue;
            }
            job->cancelRequested = true;
            if (job->state == JobState::Queued || job->state == JobState::Waiting) {
                job->state = JobState::Cancelled;
            }
        }
    }
    condition_.notify_all();
    idle_.notify_all();
}

void Manager::cancelAll() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const std::shared_ptr<Job>& job : jobs_) {
            if (jobIsFinished(job->state)) {
                continue;
            }
            job->cancelRequested = true;
            if (job->state == JobState::Queued || job->state == JobState::Waiting) {
                job->state = JobState::Cancelled;
            }
        }
    }
    condition_.notify_all();
    idle_.notify_all();
}

void Manager::clearFinished() {
    std::lock_guard<std::mutex> lock(mutex_);
    jobs_.erase(std::remove_if(jobs_.begin(), jobs_.end(), [](const std::shared_ptr<Job>& job) { return jobIsFinished(job->state); }), jobs_.end());
}

std::vector<JobInfo> Manager::jobs() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<JobInfo> result;
    result.reserve(jobs_.size());
    for (const std::shared_ptr<Job>& job : jobs_) {
        JobInfo info;
        info.id = job->id;
        info.source = job->source;
        info.name = nameOf(job->source);
        info.state = job->state;
        info.framesDone = job->framesDone.load();
        info.framesTotal = job->framesTotal.load();
        info.error = job->error;
        info.format = job->format;
        info.variant = job->variant;
        info.alphaKept = job->alphaKept;
        info.bytes = job->bytes;
        info.sourceBytes = job->sourceBytes;
        info.seconds = job->seconds;
        info.width = job->width;
        info.height = job->height;
        info.fps = job->fps;
        result.push_back(info);
    }
    return result;
}

int Manager::waitingCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    int count = 0;
    for (const std::shared_ptr<Job>& job : jobs_) {
        if (job->state == JobState::Waiting) {
            ++count;
        }
    }
    return count;
}

int Manager::activeCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    int count = 0;
    for (const std::shared_ptr<Job>& job : jobs_) {
        if (job->state == JobState::Queued || job->state == JobState::Probing || job->state == JobState::Converting) {
            ++count;
        }
    }
    return count;
}

bool Manager::busy() const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (working_) {
        return true;
    }
    for (const std::shared_ptr<Job>& job : jobs_) {
        if (job->state == JobState::Queued || job->state == JobState::Probing || job->state == JobState::Converting) {
            return true;
        }
    }
    return false;
}

bool Manager::waitIdle(int timeoutMs) const {
    std::unique_lock<std::mutex> lock(mutex_);
    auto idle = [this] {
        if (working_) {
            return false;
        }
        for (const std::shared_ptr<Job>& job : jobs_) {
            if (job->state == JobState::Queued || job->state == JobState::Probing || job->state == JobState::Converting) {
                return false;
            }
        }
        return true;
    };
    return idle_.wait_for(lock, std::chrono::milliseconds(timeoutMs), idle);
}

std::string Manager::convertedPathFor(const std::string& sourceUtf8) {
    const hapcache::SourceStamp stamp = hapcache::stampOf(sourceUtf8);
    if (!stamp.ok) {
        return std::string();
    }
    IndexEntry entry;
    bool first = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto it = index_.find(sourceUtf8);
        if (it == index_.end() || it->second.sourceBytes != stamp.bytes || it->second.sourceMtime != stamp.mtime) {
            return std::string();
        }
        entry = it->second;
        first = usedKeys_.insert(entry.key).second;
    }
    std::error_code ec;
    if (!fs::exists(pathOf(entry.file), ec) || ec) {
        return std::string();
    }
    if (first) {
        std::lock_guard<std::mutex> lock(cacheMutex_);
        cache_.touch(entry.key);
    }
    return entry.file;
}

void Manager::markBad(const std::string& sourceUtf8, const std::string& reason) {
    std::string key;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto it = index_.find(sourceUtf8);
        if (it == index_.end()) {
            return;
        }
        key = it->second.key;
        index_.erase(it);
    }
    {
        std::lock_guard<std::mutex> lock(cacheMutex_);
        cache_.markBad(key, reason);
    }
    logLine("The converted copy of '" + nameOf(sourceUtf8) + "' is not used any more: " + reason);
}

std::uint64_t Manager::generation() const {
    return generation_.load();
}

CacheStats Manager::cacheStats() const {
    CacheStats stats;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stats.limitBytes = config_.cacheLimitBytes;
    }
    std::lock_guard<std::mutex> lock(cacheMutex_);
    for (const hapcache::Entry& entry : cache_.list()) {
        ++stats.entries;
        stats.bytes += entry.bytes;
    }
    return stats;
}

std::set<std::string> Manager::protectedKeysLocked() const {
    std::set<std::string> keep = usedKeys_;
    for (const std::shared_ptr<Job>& job : jobs_) {
        if (!job->key.empty() && !jobIsFinished(job->state)) {
            keep.insert(job->key);
        }
    }
    return keep;
}

int Manager::trimCache() {
    return trim({});
}

int Manager::trim(const std::set<std::string>& extraKeep) {
    std::set<std::string> keep;
    unsigned long long limit = 0;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        keep = protectedKeysLocked();
        keep.insert(extraKeep.begin(), extraKeep.end());
        limit = config_.cacheLimitBytes;
    }
    int removed = 0;
    {
        std::lock_guard<std::mutex> lock(cacheMutex_);
        removed = cache_.evict(limit, keep);
    }
    if (removed > 0) {
        std::vector<hapcache::Entry> entries;
        {
            std::lock_guard<std::mutex> lock(cacheMutex_);
            entries = cache_.list();
        }
        std::set<std::string> alive;
        for (const hapcache::Entry& entry : entries) {
            alive.insert(entry.key);
        }
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto it = index_.begin(); it != index_.end();) {
            if (alive.count(it->second.key) == 0) {
                it = index_.erase(it);
            } else {
                ++it;
            }
        }
    }
    return removed;
}

int Manager::clearCache() {
    std::set<std::string> keep;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const std::shared_ptr<Job>& job : jobs_) {
            if (!job->key.empty() && job->state == JobState::Converting) {
                keep.insert(job->key);
            }
        }
    }
    int removed = 0;
    std::vector<hapcache::Entry> entries;
    {
        std::lock_guard<std::mutex> lock(cacheMutex_);
        removed = cache_.clear(keep);
        entries = cache_.list();
    }
    std::set<std::string> alive;
    for (const hapcache::Entry& entry : entries) {
        alive.insert(entry.key);
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto it = index_.begin(); it != index_.end();) {
            if (alive.count(it->second.key) == 0) {
                it = index_.erase(it);
            } else {
                ++it;
            }
        }
        usedKeys_.clear();
        seen_.clear();
    }
    generation_.fetch_add(1);
    return removed;
}

void Manager::loadDismissed() {
    std::ifstream in(cache_.directory() / "dismissed.txt", std::ios::binary);
    std::string line;
    std::lock_guard<std::mutex> lock(mutex_);
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (!line.empty()) {
            dismissed_.insert(line);
        }
    }
}

void Manager::saveDismissedLocked() const {
    std::ofstream out(cache_.directory() / "dismissed.txt", std::ios::binary | std::ios::trunc);
    for (const std::string& line : dismissed_) {
        out << line << "\n";
    }
}

std::shared_ptr<Manager::Job> Manager::nextJob() {
    std::unique_lock<std::mutex> lock(mutex_);
    for (;;) {
        if (stop_) {
            return nullptr;
        }
        for (const std::shared_ptr<Job>& job : jobs_) {
            if (job->state == JobState::Queued) {
                job->state = JobState::Probing;
                working_ = true;
                return job;
            }
        }
        condition_.wait(lock);
    }
}

void Manager::finish(const std::shared_ptr<Job>& job, JobState state, const std::string& error) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        job->state = state;
        job->error = error;
        if (state == JobState::Done || state == JobState::Cached) {
            generation_.fetch_add(1);
        }
    }
    idle_.notify_all();
}

void Manager::run() {
    platform::setCurrentThreadBackground(true);
    for (;;) {
        std::shared_ptr<Job> job = nextJob();
        if (!job) {
            break;
        }
        process(job);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            working_ = false;
        }
        idle_.notify_all();
    }
}

void Manager::process(const std::shared_ptr<Job>& job) {
    Config config;
    Backend backend;
    bool ready = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        config = config_;
        backend = backend_;
        ready = ready_;
    }
    auto drop = [&] {
        std::lock_guard<std::mutex> lock(mutex_);
        jobs_.erase(std::remove(jobs_.begin(), jobs_.end(), job), jobs_.end());
    };
    if (job->cancelRequested) {
        finish(job, JobState::Cancelled, std::string());
        return;
    }
    if (!ready) {
        if (job->explicitRequest) {
            finish(job, JobState::Failed, "FFmpeg is not set up, so videos cannot be converted yet");
        } else {
            drop();
        }
        return;
    }
    const std::string source = job->source;
    const hapcache::SourceStamp stamp = hapcache::stampOf(source);
    if (!stamp.ok) {
        if (job->explicitRequest) {
            finish(job, JobState::Failed, "The file cannot be read");
        } else {
            drop();
        }
        return;
    }
    const hapconv::SourceInfo info = backend.inspect ? backend.inspect(source) : hapconv::inspectSource(source);
    if (!info.ok) {
        if (job->explicitRequest) {
            finish(job, JobState::Failed, info.error.empty() ? std::string("The video cannot be opened") : info.error);
        } else {
            drop();
        }
        return;
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        job->variant = info.variant;
        job->width = info.width;
        job->height = info.height;
        job->fps = info.fps;
        job->framesTotal = info.frames;
        job->sourceBytes = stamp.bytes;
    }
    std::string fingerprint;
    std::string error;
    if (!hapcache::fingerprintOf(source, fingerprint, error)) {
        finish(job, JobState::Failed, error);
        return;
    }
    const std::string tag = tagFor(config);
    const std::string key = hapcache::keyFor(fingerprint, tag);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        job->key = key;
    }
    hapcache::Entry hit;
    bool found = false;
    {
        std::lock_guard<std::mutex> lock(cacheMutex_);
        found = cache_.find(key, hit);
        if (found && hit.bad && job->force) {
            cache_.remove(key);
            found = false;
        }
    }
    if (found && hit.bad) {
        if (job->explicitRequest) {
            finish(job, JobState::Failed, "The converted copy failed earlier: " + hit.badReason);
        } else {
            drop();
        }
        return;
    }
    if (found) {
        hit.source = source;
        hit.sourceBytes = stamp.bytes;
        hit.sourceMtime = stamp.mtime;
        hit.lastUsed = hapcache::nowSeconds();
        {
            std::lock_guard<std::mutex> lock(cacheMutex_);
            cache_.commit(hit);
        }
        indexEntry(source, hit);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            job->format = hit.format;
            job->alphaKept = hit.alphaKept;
            job->bytes = hit.bytes;
            job->seconds = 0.0;
        }
        logLine("The converted copy of '" + nameOf(source) + "' was already in the cache");
        finish(job, JobState::Cached, std::string());
        return;
    }
    bool needsConsent = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        needsConsent = config_.mode == ImportMode::Ask && !job->consented && !job->explicitRequest;
        if (needsConsent) {
            job->state = JobState::Waiting;
        }
    }
    if (needsConsent) {
        return;
    }
    std::error_code spaceError;
    const fs::space_info space = fs::space(cache_.directory(), spaceError);
    const unsigned long long needed = stamp.bytes * 3ull + 256ull * 1024ull * 1024ull;
    if (!spaceError && static_cast<unsigned long long>(space.available) < needed) {
        finish(job, JobState::Failed, "There is not enough free disk space for the converted copy");
        return;
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        job->state = JobState::Converting;
    }
    if (job->cancelRequested) {
        finish(job, JobState::Cancelled, std::string());
        return;
    }
    const fs::path part = cache_.partPath(key);
    const fs::path data = cache_.dataPath(key);
    std::error_code removeError;
    fs::remove(part, removeError);
    hapconv::Options options;
    options.preference = config.format;
    options.snappy = config.snappy;
    options.threads = 2;
    logLine("Converting '" + nameOf(source) + "' (" + hapconv::variantName(info.variant) + ") to HAP in the background");
    const hapconv::ProgressFn progress = [&](int done, int total) {
        job->framesDone = done;
        job->framesTotal = total;
        return !job->cancelRequested.load();
    };
    const hapconv::Result result = backend.convert ? backend.convert(source, utf8Of(part), options, progress) : hapconv::convertFile(source, utf8Of(part), options, progress);
    if (result.cancelled || (job->cancelRequested && !result.ok)) {
        fs::remove(part, removeError);
        finish(job, JobState::Cancelled, std::string());
        return;
    }
    if (!result.ok) {
        fs::remove(part, removeError);
        logLine("Converting '" + nameOf(source) + "' failed: " + result.error);
        finish(job, JobState::Failed, result.error);
        return;
    }
    std::error_code renameError;
    fs::rename(part, data, renameError);
    if (renameError) {
        fs::remove(part, removeError);
        finish(job, JobState::Failed, "The converted file cannot be stored in the cache folder");
        return;
    }
    hapcache::Entry entry;
    entry.key = key;
    entry.source = source;
    entry.fingerprint = fingerprint;
    entry.tag = tag;
    entry.sourceBytes = stamp.bytes;
    entry.sourceMtime = stamp.mtime;
    entry.width = result.width;
    entry.height = result.height;
    entry.fps = result.fps;
    entry.frames = result.frames;
    entry.format = hapconv::targetName(result.format);
    entry.alphaKept = result.alphaKept;
    entry.bytes = result.bytes;
    entry.convertSeconds = result.seconds;
    entry.created = hapcache::nowSeconds();
    entry.lastUsed = entry.created;
    std::error_code sizeError;
    const auto stored = fs::file_size(data, sizeError);
    if (!sizeError) {
        entry.bytes = static_cast<unsigned long long>(stored);
    }
    bool committed = false;
    {
        std::lock_guard<std::mutex> lock(cacheMutex_);
        committed = cache_.commit(entry);
    }
    if (!committed) {
        fs::remove(data, removeError);
        finish(job, JobState::Failed, "The cache entry cannot be written");
        return;
    }
    indexEntry(source, entry);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        job->format = entry.format;
        job->alphaKept = entry.alphaKept;
        job->bytes = entry.bytes;
        job->seconds = result.seconds;
        job->variant = result.variant;
        job->width = result.width;
        job->height = result.height;
        job->fps = result.fps;
        job->framesDone = result.frames;
        job->framesTotal = result.frames;
    }
    char seconds[32];
    std::snprintf(seconds, sizeof(seconds), "%.1f", result.seconds);
    logLine("Converted '" + nameOf(source) + "' to " + entry.format + " in " + seconds + " s");
    finish(job, JobState::Done, std::string());
    trim({key});
}

}
