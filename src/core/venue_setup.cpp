#include "core/venue_setup.hpp"

#include "core/log.hpp"
#include "core/output_map_json.hpp"
#include "core/recovery.hpp"

#include <algorithm>
#include <cctype>

namespace nvj {

namespace fs = std::filesystem;

namespace {

bool reservedName(const std::string& name) {
    std::string lower = name;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    const size_t dot = lower.find('.');
    if (dot != std::string::npos) {
        lower.resize(dot);
    }
    static const char* const kReserved[] = {"con", "prn", "aux", "nul", "com1", "com2", "com3", "com4", "com5", "com6", "com7", "com8", "com9", "lpt1", "lpt2", "lpt3", "lpt4", "lpt5", "lpt6", "lpt7", "lpt8", "lpt9"};
    for (const char* reserved : kReserved) {
        if (lower == reserved) {
            return true;
        }
    }
    return false;
}

fs::path utf8Path(const std::string& text) {
    return fs::path(std::u8string(text.begin(), text.end()));
}

std::string utf8String(const fs::path& path) {
    const std::u8string text = path.u8string();
    return std::string(text.begin(), text.end());
}

fs::path venuePath(const fs::path& directory, const std::string& name) {
    return directory / utf8Path(sanitizeVenueName(name) + ".json");
}

}

std::string sanitizeVenueName(const std::string& name) {
    std::string out;
    for (char c : name) {
        if (static_cast<unsigned char>(c) < 32 || std::string("\\/:*?\"<>|").find(c) != std::string::npos) {
            out.push_back('_');
        } else {
            out.push_back(c);
        }
    }
    const size_t start = out.find_first_not_of(" .");
    if (start == std::string::npos) {
        return std::string();
    }
    const size_t end = out.find_last_not_of(" .");
    out = out.substr(start, end - start + 1);
    if (out.size() > 60) {
        out.resize(60);
        const size_t last = out.find_last_not_of(" .");
        out = last == std::string::npos ? std::string() : out.substr(0, last + 1);
    }
    if (reservedName(out)) {
        out += "_";
    }
    return out;
}

nlohmann::json venueToJson(const std::string& name, const OutputSet& outputs) {
    nlohmann::json doc;
    doc["format"] = kVenueFormat;
    doc["version"] = kVenueVersion;
    doc["name"] = name;
    nlohmann::json list = nlohmann::json::array();
    for (const OutputSpec& spec : outputs) {
        list.push_back(outputToJson(spec));
    }
    doc["outputs"] = list;
    return doc;
}

bool venueFromJson(const nlohmann::json& doc, std::string& name, OutputSet& outputs, std::string& error) {
    if (!doc.is_object()) {
        error = "This is not a venue setup file";
        return false;
    }
    auto format = doc.find("format");
    if (format == doc.end() || !format->is_string() || format->get<std::string>() != kVenueFormat) {
        error = "This is not a venue setup file";
        return false;
    }
    auto version = doc.find("version");
    if (version != doc.end()) {
        if (!version->is_number_integer() || version->get<int>() < 1) {
            error = "The version of this venue setup is not valid";
            return false;
        }
        if (version->get<int>() > kVenueVersion) {
            error = "This venue setup was made with a newer version of NyrexVJ";
            return false;
        }
    }
    auto list = doc.find("outputs");
    if (list == doc.end() || !list->is_array() || list->empty()) {
        error = "The venue setup has no outputs";
        return false;
    }
    OutputSet result = defaultOutputs();
    for (size_t i = 0; i < list->size() && i < result.size(); ++i) {
        if (!(*list)[i].is_object()) {
            error = "The venue setup is damaged";
            return false;
        }
        outputFromJson((*list)[i], result[i]);
    }
    result[0].enabled = true;
    auto nameIt = doc.find("name");
    std::string readName;
    if (nameIt != doc.end() && nameIt->is_string()) {
        readName = sanitizeVenueName(nameIt->get<std::string>());
    }
    name = readName;
    outputs = result;
    return true;
}

fs::path venuesDirectory() {
    fs::path directory = utf8Path(userDataDir()) / "venues";
    std::error_code ec;
    fs::create_directories(directory, ec);
    return directory;
}

std::vector<std::string> listVenues(const fs::path& directory) {
    std::vector<std::string> names;
    std::error_code ec;
    for (const fs::directory_entry& entry : fs::directory_iterator(directory, ec)) {
        if (entry.is_regular_file(ec) && entry.path().extension() == ".json") {
            names.push_back(utf8String(entry.path().stem()));
        }
    }
    std::sort(names.begin(), names.end(), [](const std::string& a, const std::string& b) {
        std::string la = a;
        std::string lb = b;
        std::transform(la.begin(), la.end(), la.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        std::transform(lb.begin(), lb.end(), lb.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return la != lb ? la < lb : a < b;
    });
    return names;
}

bool venueExists(const fs::path& directory, const std::string& name) {
    const std::string clean = sanitizeVenueName(name);
    if (clean.empty()) {
        return false;
    }
    std::error_code ec;
    return fs::is_regular_file(venuePath(directory, clean), ec);
}

std::string uniqueVenueName(const fs::path& directory, const std::string& wanted) {
    std::string base = sanitizeVenueName(wanted);
    if (base.empty()) {
        base = "Venue";
    }
    if (!venueExists(directory, base)) {
        return base;
    }
    if (base.size() > 54) {
        base.resize(54);
    }
    for (int n = 2; n < 1000; ++n) {
        const std::string candidate = sanitizeVenueName(base + " " + std::to_string(n));
        if (!venueExists(directory, candidate)) {
            return candidate;
        }
    }
    return base + " copy";
}

bool saveVenue(const fs::path& directory, const std::string& name, const OutputSet& outputs, std::string& error) {
    const std::string clean = sanitizeVenueName(name);
    if (clean.empty()) {
        error = "Please enter a name";
        return false;
    }
    std::error_code ec;
    fs::create_directories(directory, ec);
    if (!writeFileAtomic(venuePath(directory, clean), venueToJson(clean, outputs).dump(2))) {
        error = "Could not write the venue file";
        return false;
    }
    return true;
}

bool loadVenueFile(const fs::path& file, std::string& name, OutputSet& outputs, std::string& error) {
    std::error_code ec;
    if (!fs::is_regular_file(file, ec)) {
        error = "The file was not found";
        return false;
    }
    const std::uintmax_t size = fs::file_size(file, ec);
    if (ec || size > kMaxVenueFileBytes) {
        error = "The file is too large for a venue setup";
        return false;
    }
    std::string text;
    if (!readWholeFile(file, text)) {
        error = "Could not read the file";
        return false;
    }
    nlohmann::json doc = nlohmann::json::parse(text, nullptr, false);
    if (doc.is_discarded()) {
        error = "The file is damaged";
        return false;
    }
    std::string readName;
    if (!venueFromJson(doc, readName, outputs, error)) {
        return false;
    }
    name = readName.empty() ? sanitizeVenueName(utf8String(file.stem())) : readName;
    return true;
}

bool loadVenue(const fs::path& directory, const std::string& name, OutputSet& outputs, std::string& error) {
    const std::string clean = sanitizeVenueName(name);
    if (clean.empty()) {
        error = "Please enter a name";
        return false;
    }
    std::string ignored;
    return loadVenueFile(venuePath(directory, clean), ignored, outputs, error);
}

bool deleteVenue(const fs::path& directory, const std::string& name, std::string& error) {
    const std::string clean = sanitizeVenueName(name);
    if (clean.empty() || !venueExists(directory, clean)) {
        error = "The venue setup was not found";
        return false;
    }
    std::error_code ec;
    if (!fs::remove(venuePath(directory, clean), ec) || ec) {
        error = "Could not delete the venue file";
        return false;
    }
    return true;
}

bool duplicateVenue(const fs::path& directory, const std::string& from, const std::string& to, std::string& error) {
    OutputSet outputs;
    if (!loadVenue(directory, from, outputs, error)) {
        return false;
    }
    const std::string target = uniqueVenueName(directory, to.empty() ? from + " copy" : to);
    return saveVenue(directory, target, outputs, error);
}

bool exportVenue(const fs::path& directory, const std::string& name, const fs::path& destination, std::string& error) {
    OutputSet outputs;
    if (!loadVenue(directory, name, outputs, error)) {
        return false;
    }
    if (destination.empty()) {
        error = "Please enter a file name";
        return false;
    }
    fs::path target = destination;
    std::error_code ec;
    if (fs::is_directory(target, ec)) {
        target /= utf8Path(sanitizeVenueName(name) + ".json");
    }
    if (target.extension().empty()) {
        target += ".json";
    }
    if (!target.parent_path().empty()) {
        fs::create_directories(target.parent_path(), ec);
    }
    if (!writeFileAtomic(target, venueToJson(sanitizeVenueName(name), outputs).dump(2))) {
        error = "Could not write the file";
        return false;
    }
    return true;
}

bool importVenue(const fs::path& directory, const fs::path& source, std::string& importedName, std::string& error) {
    OutputSet outputs;
    std::string name;
    if (!loadVenueFile(source, name, outputs, error)) {
        return false;
    }
    importedName = uniqueVenueName(directory, name);
    return saveVenue(directory, importedName, outputs, error);
}

}
