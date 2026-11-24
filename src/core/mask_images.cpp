#include "core/mask_images.hpp"

#include "core/log.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>

namespace nvj {

namespace fs = std::filesystem;

namespace {

fs::path utf8Path(const std::string& text) {
    return fs::path(std::u8string(text.begin(), text.end()));
}

std::string utf8String(const fs::path& path) {
    const std::u8string text = path.u8string();
    return std::string(text.begin(), text.end());
}

bool readFileBytes(const fs::path& path, std::vector<uint8_t>& bytes, std::string& error) {
    std::error_code ec;
    if (!fs::is_regular_file(path, ec)) {
        error = "The file does not exist";
        return false;
    }
    const auto size = fs::file_size(path, ec);
    if (ec) {
        error = "The file cannot be read";
        return false;
    }
    if (size == 0 || size > kMaxMaskFileBytes) {
        error = size == 0 ? "The file is empty" : "The file is larger than 16 MB";
        return false;
    }
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        error = "The file cannot be opened";
        return false;
    }
    bytes.assign(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
    if (bytes.size() != static_cast<size_t>(size)) {
        error = "The file cannot be read completely";
        return false;
    }
    return true;
}

std::string cleanFileName(const std::string& wanted) {
    std::string result;
    for (char c : wanted) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (u < 32 || c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|') {
            result.push_back('_');
        } else {
            result.push_back(c);
        }
    }
    while (!result.empty() && (result.front() == '.' || result.front() == ' ')) {
        result.erase(result.begin());
    }
    if (result.size() > kMaxMaskImageNameLength) {
        const size_t dot = result.rfind('.');
        const std::string extension = dot != std::string::npos ? result.substr(dot) : std::string();
        result = result.substr(0, kMaxMaskImageNameLength - extension.size()) + extension;
    }
    return result;
}

}

bool loadMaskFile(const fs::path& path, MaskBitmap& out, std::string& error) {
    std::vector<uint8_t> bytes;
    if (!readFileBytes(path, bytes, error)) {
        return false;
    }
    return decodeMaskBitmap(bytes.data(), bytes.size(), out, error);
}

MaskImages& MaskImages::instance() {
    static MaskImages images;
    return images;
}

fs::path MaskImages::folder() const {
    return utf8Path(userDataDir()) / "masks";
}

void MaskImages::start(const std::string& name, Entry& entry) {
    entry.running = true;
    entry.error.clear();
    const fs::path path = folder() / utf8Path(name);
    entry.job = std::async(std::launch::async, [this, name, path]() {
        auto bitmap = std::make_shared<MaskBitmap>();
        std::string message;
        const bool ok = loadMaskFile(path, *bitmap, message);
        std::lock_guard<std::mutex> lock(mutex_);
        Entry& target = entries_[name];
        target.running = false;
        if (ok) {
            target.bitmap = bitmap;
            target.error.clear();
        } else {
            target.bitmap.reset();
            target.error = message;
            target.failedAt = std::chrono::steady_clock::now();
        }
    }).share();
}

std::shared_ptr<const MaskBitmap> MaskImages::get(const std::string& name) {
    if (!maskImageNameValid(name)) {
        return nullptr;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    auto found = entries_.find(name);
    if (found == entries_.end()) {
        Entry& entry = entries_[name];
        start(name, entry);
        return nullptr;
    }
    Entry& entry = found->second;
    if (entry.bitmap) {
        return entry.bitmap;
    }
    if (!entry.running && !entry.error.empty() && std::chrono::steady_clock::now() - entry.failedAt > std::chrono::seconds(5)) {
        start(name, entry);
    }
    return nullptr;
}

std::string MaskImages::error(const std::string& name) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto found = entries_.find(name);
    return found == entries_.end() ? std::string() : found->second.error;
}

bool MaskImages::loading(const std::string& name) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto found = entries_.find(name);
    return found != entries_.end() && found->second.running;
}

bool MaskImages::waitFor(const std::string& name, int milliseconds) {
    std::shared_future<void> job;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto found = entries_.find(name);
        if (found == entries_.end()) {
            return false;
        }
        if (found->second.bitmap) {
            return true;
        }
        job = found->second.job;
    }
    if (job.valid()) {
        job.wait_for(std::chrono::milliseconds(milliseconds));
    }
    std::lock_guard<std::mutex> lock(mutex_);
    auto found = entries_.find(name);
    return found != entries_.end() && found->second.bitmap != nullptr;
}

void MaskImages::reload(const std::string& name) {
    if (!maskImageNameValid(name)) {
        return;
    }
    std::shared_future<void> previous;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto found = entries_.find(name);
        if (found != entries_.end()) {
            previous = found->second.job;
        }
    }
    if (previous.valid()) {
        previous.wait();
    }
    std::lock_guard<std::mutex> lock(mutex_);
    Entry& entry = entries_[name];
    entry.bitmap.reset();
    start(name, entry);
}

void MaskImages::clear() {
    std::map<std::string, Entry> jobs;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        jobs.swap(entries_);
    }
    for (auto& item : jobs) {
        if (item.second.job.valid()) {
            item.second.job.wait();
        }
    }
}

bool MaskImages::importFile(const std::string& sourcePath, std::string& nameOut, std::string& error) {
    const fs::path source = utf8Path(sourcePath);
    std::vector<uint8_t> bytes;
    if (!readFileBytes(source, bytes, error)) {
        return false;
    }
    auto bitmap = std::make_shared<MaskBitmap>();
    if (!decodeMaskBitmap(bytes.data(), bytes.size(), *bitmap, error)) {
        return false;
    }
    std::string name = cleanFileName(utf8String(source.filename()));
    if (!maskImageNameValid(name)) {
        const size_t dot = name.rfind('.');
        const std::string stem = dot != std::string::npos ? name.substr(0, dot) : name;
        name = (stem.empty() ? std::string("mask") : stem) + ".png";
        if (!maskImageNameValid(name)) {
            error = "The file name is not usable";
            return false;
        }
    }
    const fs::path directory = folder();
    std::error_code ec;
    fs::create_directories(directory, ec);
    const size_t dot = name.rfind('.');
    const std::string stem = name.substr(0, dot);
    const std::string extension = name.substr(dot);
    std::string candidate = name;
    for (int counter = 2; fs::exists(directory / utf8Path(candidate), ec) && counter < 1000; ++counter) {
        candidate = stem + " " + std::to_string(counter) + extension;
    }
    if (!maskImageNameValid(candidate)) {
        error = "The file name is not usable";
        return false;
    }
    {
        std::ofstream stream(directory / utf8Path(candidate), std::ios::binary);
        if (!stream) {
            error = "The folder for mask images cannot be written";
            return false;
        }
        stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        if (!stream) {
            error = "The folder for mask images cannot be written";
            return false;
        }
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        Entry& entry = entries_[candidate];
        entry.bitmap = bitmap;
        entry.error.clear();
        entry.running = false;
    }
    nameOut = candidate;
    return true;
}

}
