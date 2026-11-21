#include "core/node_files.hpp"

#include "core/log.hpp"
#include "core/recovery.hpp"
#include "core/venue_setup.hpp"

#include <algorithm>
#include <cctype>

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

std::string lower(const std::string& text) {
    std::string out = text;
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

}

fs::path graphsFolder() {
    fs::path folder = utf8Path(graphDirectory());
    std::error_code ec;
    fs::create_directories(folder, ec);
    return folder;
}

std::string sanitizeGraphName(const std::string& name) {
    return sanitizeVenueName(name);
}

fs::path graphPath(const fs::path& folder, const std::string& name) {
    return folder / utf8Path(graphFileName(sanitizeGraphName(name)));
}

std::vector<GraphFileInfo> scanGraphFiles(const fs::path& folder) {
    std::vector<GraphFileInfo> files;
    std::error_code ec;
    for (fs::directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code entryEc;
        if (!it->is_regular_file(entryEc) || entryEc) {
            continue;
        }
        const std::string name = utf8String(it->path().filename());
        if (!isGraphFileName(name)) {
            continue;
        }
        GraphFileInfo info;
        info.stem = utf8String(it->path().stem());
        info.path = it->path();
        info.bytes = static_cast<unsigned long long>(it->file_size(entryEc));
        const auto time = fs::last_write_time(it->path(), entryEc);
        info.stamp = entryEc ? 0 : static_cast<long long>(time.time_since_epoch().count());
        files.push_back(std::move(info));
    }
    std::sort(files.begin(), files.end(), [](const GraphFileInfo& a, const GraphFileInfo& b) {
        const std::string la = lower(a.stem);
        const std::string lb = lower(b.stem);
        return la != lb ? la < lb : a.stem < b.stem;
    });
    if (files.size() > kMaxNodeEffects) {
        files.resize(kMaxNodeEffects);
    }
    return files;
}

bool graphExists(const fs::path& folder, const std::string& name) {
    if (sanitizeGraphName(name).empty()) {
        return false;
    }
    std::error_code ec;
    return fs::is_regular_file(graphPath(folder, name), ec);
}

std::string uniqueGraphName(const fs::path& folder, const std::string& wanted) {
    std::string base = sanitizeGraphName(wanted);
    if (base.empty()) {
        base = "Effect";
    }
    if (!graphExists(folder, base)) {
        return base;
    }
    if (base.size() > 54) {
        base.resize(54);
    }
    for (int n = 2; n < 1000; ++n) {
        const std::string candidate = sanitizeGraphName(base + " " + std::to_string(n));
        if (!graphExists(folder, candidate)) {
            return candidate;
        }
    }
    return base + " copy";
}

bool readGraphFile(const fs::path& file, GraphDocument& graph, std::string& error) {
    std::error_code ec;
    if (!fs::is_regular_file(file, ec)) {
        error = "The effect file was not found";
        return false;
    }
    const std::uintmax_t size = fs::file_size(file, ec);
    if (ec || size > kMaxGraphFileBytes) {
        error = "The effect file is larger than 256 KiB";
        return false;
    }
    std::string text;
    if (!readWholeFile(file, text)) {
        error = "Could not read the effect file";
        return false;
    }
    return graphFromJson(text, graph, error);
}

bool saveGraph(const fs::path& folder, const std::string& name, const GraphDocument& graph, std::string& error) {
    const std::string clean = sanitizeGraphName(name);
    if (clean.empty()) {
        error = "Please enter a name";
        return false;
    }
    std::error_code ec;
    fs::create_directories(folder, ec);
    GraphDocument copy = graph;
    copy.name = clean;
    const std::string text = graphToJson(copy);
    if (text.size() > kMaxGraphFileBytes) {
        error = "The effect is larger than 256 KiB";
        return false;
    }
    if (!writeFileAtomic(graphPath(folder, clean), text)) {
        error = "Could not write the effect file";
        return false;
    }
    return true;
}

bool deleteGraph(const fs::path& folder, const std::string& name, std::string& error) {
    if (!graphExists(folder, name)) {
        error = "The effect file was not found";
        return false;
    }
    std::error_code ec;
    if (!fs::remove(graphPath(folder, name), ec) || ec) {
        error = "Could not delete the effect file";
        return false;
    }
    return true;
}

bool exportGraph(const fs::path& folder, const std::string& name, const fs::path& destination, std::string& error) {
    GraphDocument graph;
    if (!readGraphFile(graphPath(folder, name), graph, error)) {
        return false;
    }
    if (destination.empty()) {
        error = "Please enter a file name";
        return false;
    }
    fs::path target = destination;
    std::error_code ec;
    if (fs::is_directory(target, ec)) {
        target /= utf8Path(graphFileName(sanitizeGraphName(name)));
    }
    if (target.extension().empty()) {
        target += ".nyxfx";
    }
    if (!target.parent_path().empty()) {
        fs::create_directories(target.parent_path(), ec);
    }
    if (!writeFileAtomic(target, graphToJson(graph))) {
        error = "Could not write the file";
        return false;
    }
    return true;
}

bool importGraph(const fs::path& folder, const fs::path& source, std::string& importedName, std::string& error) {
    GraphDocument graph;
    if (!readGraphFile(source, graph, error)) {
        return false;
    }
    std::string wanted = graph.name.empty() ? utf8String(source.stem()) : graph.name;
    importedName = uniqueGraphName(folder, wanted);
    return saveGraph(folder, importedName, graph, error);
}

int writeExampleGraphs(const fs::path& folder) {
    int written = 0;
    for (const ExampleGraph& example : exampleGraphs()) {
        if (graphExists(folder, example.name)) {
            continue;
        }
        std::string error;
        GraphDocument graph = example.build();
        arrangeGraph(graph);
        if (saveGraph(folder, example.name, graph, error)) {
            ++written;
        }
    }
    return written;
}

}
