#pragma once

#include "core/output_map.hpp"

#include <nlohmann/json.hpp>

#include <array>
#include <filesystem>
#include <string>
#include <vector>

namespace nvj {

constexpr int kVenueVersion = 1;
constexpr size_t kMaxVenueFileBytes = 2u * 1024u * 1024u;
constexpr const char* kVenueFormat = "nyrexvj-venue";

using OutputSet = std::array<OutputSpec, kMaxOutputs>;

std::string sanitizeVenueName(const std::string& name);

nlohmann::json venueToJson(const std::string& name, const OutputSet& outputs);
bool venueFromJson(const nlohmann::json& doc, std::string& name, OutputSet& outputs, std::string& error);

std::filesystem::path venuesDirectory();
std::vector<std::string> listVenues(const std::filesystem::path& directory);
bool venueExists(const std::filesystem::path& directory, const std::string& name);
bool saveVenue(const std::filesystem::path& directory, const std::string& name, const OutputSet& outputs, std::string& error);
bool loadVenue(const std::filesystem::path& directory, const std::string& name, OutputSet& outputs, std::string& error);
bool loadVenueFile(const std::filesystem::path& file, std::string& name, OutputSet& outputs, std::string& error);
bool deleteVenue(const std::filesystem::path& directory, const std::string& name, std::string& error);
bool duplicateVenue(const std::filesystem::path& directory, const std::string& from, const std::string& to, std::string& error);
bool exportVenue(const std::filesystem::path& directory, const std::string& name, const std::filesystem::path& destination, std::string& error);
bool importVenue(const std::filesystem::path& directory, const std::filesystem::path& source, std::string& importedName, std::string& error);
std::string uniqueVenueName(const std::filesystem::path& directory, const std::string& wanted);

}
