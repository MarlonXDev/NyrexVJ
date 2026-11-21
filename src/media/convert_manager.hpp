#pragma once

#include "media/convert_cache.hpp"
#include "media/hap_convert.hpp"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace nvj::hapjobs {

enum class ImportMode { Off, Ask, Automatic };

ImportMode importModeFromName(const std::string& name);
const char* importModeName(ImportMode mode);

enum class JobState { Queued, Probing, Waiting, Converting, Done, Cached, Failed, Cancelled };

const char* jobStateName(JobState state);
bool jobIsFinished(JobState state);

struct Config {
    ImportMode mode = ImportMode::Off;
    hapconv::TargetFormat format = hapconv::TargetFormat::Auto;
    bool snappy = true;
    unsigned long long cacheLimitBytes = 4096ull * 1024ull * 1024ull;
};

struct JobInfo {
    int id = 0;
    std::string source;
    std::string name;
    JobState state = JobState::Queued;
    int framesDone = 0;
    int framesTotal = 0;
    std::string error;
    std::string format;
    hapconv::Variant variant = hapconv::Variant::Unknown;
    bool alphaKept = false;
    unsigned long long bytes = 0;
    unsigned long long sourceBytes = 0;
    double seconds = 0.0;
    int width = 0;
    int height = 0;
    double fps = 0.0;
};

struct CacheStats {
    int entries = 0;
    unsigned long long bytes = 0;
    unsigned long long limitBytes = 0;
};

struct Backend {
    std::function<hapconv::SourceInfo(const std::string&)> inspect;
    std::function<hapconv::Result(const std::string&, const std::string&, const hapconv::Options&, const hapconv::ProgressFn&)> convert;
};

bool isCandidatePath(const std::string& pathUtf8);

class Manager {
public:
    explicit Manager(std::filesystem::path cacheDirectory);
    ~Manager();
    Manager(const Manager&) = delete;
    Manager& operator=(const Manager&) = delete;

    void configure(const Config& config);
    Config config() const;
    void setBackend(const Backend& backend);
    void setReady(bool ready);
    bool ready() const;

    void offer(const std::string& sourceUtf8, bool manual = false);
    int request(const std::string& sourceUtf8, bool force = false);
    void approve(int id);
    void dismiss(int id);
    void approveAll();
    void dismissAll();
    void cancel(int id);
    void cancelAll();
    void clearFinished();

    std::vector<JobInfo> jobs() const;
    int waitingCount() const;
    int activeCount() const;
    bool busy() const;
    bool waitIdle(int timeoutMs) const;

    std::string convertedPathFor(const std::string& sourceUtf8);
    void markBad(const std::string& sourceUtf8, const std::string& reason);
    std::uint64_t generation() const;

    CacheStats cacheStats() const;
    int clearCache();
    int trimCache();
    const std::filesystem::path& cacheDirectory() const { return cache_.directory(); }

private:
    struct Job {
        int id = 0;
        std::string source;
        bool consented = false;
        bool explicitRequest = false;
        bool force = false;
        std::atomic<bool> cancelRequested{false};
        std::atomic<int> framesDone{0};
        std::atomic<int> framesTotal{0};
        JobState state = JobState::Queued;
        std::string error;
        std::string format;
        hapconv::Variant variant = hapconv::Variant::Unknown;
        bool alphaKept = false;
        unsigned long long bytes = 0;
        unsigned long long sourceBytes = 0;
        double seconds = 0.0;
        int width = 0;
        int height = 0;
        double fps = 0.0;
        std::string key;
    };

    struct IndexEntry {
        std::string key;
        std::string file;
        std::string tag;
        unsigned long long sourceBytes = 0;
        std::int64_t sourceMtime = 0;
    };

    void run();
    void process(const std::shared_ptr<Job>& job);
    void finish(const std::shared_ptr<Job>& job, JobState state, const std::string& error);
    std::shared_ptr<Job> nextJob();
    bool duplicateActive(const std::string& source) const;
    std::string seenKey(const std::string& source, const hapcache::SourceStamp& stamp) const;
    void indexEntry(const std::string& source, const hapcache::Entry& entry);
    void loadDismissed();
    void saveDismissedLocked() const;
    std::set<std::string> protectedKeysLocked() const;
    std::string tagFor(const Config& config) const;
    int trim(const std::set<std::string>& extraKeep);

    hapcache::Cache cache_;
    mutable std::mutex mutex_;
    mutable std::mutex cacheMutex_;
    mutable std::condition_variable condition_;
    mutable std::condition_variable idle_;
    std::vector<std::shared_ptr<Job>> jobs_;
    std::map<std::string, IndexEntry> index_;
    std::set<std::string> seen_;
    std::set<std::string> dismissed_;
    std::set<std::string> usedKeys_;
    Config config_;
    Backend backend_;
    bool ready_ = false;
    bool stop_ = false;
    bool working_ = false;
    int nextId_ = 1;
    std::atomic<std::uint64_t> generation_{1};
    std::thread worker_;
};

}
