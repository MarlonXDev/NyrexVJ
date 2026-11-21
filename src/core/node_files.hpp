#pragma once

#include "core/node_graph.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace nvj {

struct GraphFileInfo {
    std::string stem;
    std::filesystem::path path;
    unsigned long long bytes = 0;
    long long stamp = 0;
};

std::filesystem::path graphsFolder();
std::string sanitizeGraphName(const std::string& name);
std::vector<GraphFileInfo> scanGraphFiles(const std::filesystem::path& folder);
std::filesystem::path graphPath(const std::filesystem::path& folder, const std::string& name);
bool graphExists(const std::filesystem::path& folder, const std::string& name);
std::string uniqueGraphName(const std::filesystem::path& folder, const std::string& wanted);
bool readGraphFile(const std::filesystem::path& file, GraphDocument& graph, std::string& error);
bool saveGraph(const std::filesystem::path& folder, const std::string& name, const GraphDocument& graph, std::string& error);
bool deleteGraph(const std::filesystem::path& folder, const std::string& name, std::string& error);
bool exportGraph(const std::filesystem::path& folder, const std::string& name, const std::filesystem::path& destination, std::string& error);
bool importGraph(const std::filesystem::path& folder, const std::filesystem::path& source, std::string& importedName, std::string& error);
int writeExampleGraphs(const std::filesystem::path& folder);

}
