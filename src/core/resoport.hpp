#pragma once

#include "core/composition.hpp"

#include <functional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace nvj::resoport {

constexpr unsigned long long kMaxFileBytes = 192ull * 1024ull * 1024ull;
constexpr int kMaxDepth = 96;
constexpr size_t kMaxNodes = 3000000;
constexpr size_t kMaxAttributes = 256;
constexpr size_t kMaxValueLength = 8192;
constexpr size_t kMaxSearchEntries = 60000;
constexpr int kMaxSearchDepth = 6;
constexpr size_t kMaxListed = 12;

struct XmlNode {
    std::string tag;
    std::vector<std::pair<std::string, std::string>> attributes;
    std::vector<int> children;
    int parent = -1;

    const std::string* attribute(const std::string& name) const;
};

struct XmlDocument {
    std::vector<XmlNode> nodes;
};

bool parseXml(const std::string& text, XmlDocument& out, std::string& error);

enum class SourceKind { Media, Generator, Router, Other };

struct ClipInfo {
    int deck = 0;
    int layer = 0;
    int column = 0;
    std::string name;
    std::string path;
    SourceKind source = SourceKind::Other;
};

struct LayerInfo {
    int index = 0;
    std::string blend;
    float opacity = 1.0f;
};

struct DeckInfo {
    std::string name;
};

struct Document {
    std::string name;
    std::string application;
    int versionMajor = 0;
    int versionMinor = 0;
    int width = 0;
    int height = 0;
    float bpm = 0.0f;
    int declaredDecks = 0;
    int declaredLayers = 0;
    int declaredColumns = 0;
    int effectPasses = 0;
    std::vector<DeckInfo> decks;
    std::vector<LayerInfo> layers;
    std::vector<ClipInfo> clips;
};

bool parseDocument(const std::string& text, Document& out, std::string& error);
bool loadDocument(const std::string& fileUtf8, Document& out, std::string& error);

int blendModeFor(const std::string& resolumeName, bool& known);

class MediaFinder {
public:
    MediaFinder(const std::string& compositionFileUtf8, const std::vector<std::string>& extraFoldersUtf8);

    std::string find(const std::string& recordedPath);

private:
    void buildIndex();

    std::vector<std::string> roots_;
    std::unordered_map<std::string, std::string> byName_;
    bool indexed_ = false;
};

struct Report {
    int decksImported = 0;
    int decksSkipped = 0;
    int layersSkipped = 0;
    int clipsImported = 0;
    int clipsSkippedPosition = 0;
    int clipsMissing = 0;
    int clipsUnsupported = 0;
    int clipsGenerator = 0;
    int clipsRouter = 0;
    int clipsOther = 0;
    int effectPasses = 0;
    int layerBlendsMapped = 0;
    bool resolutionMatched = false;
    bool tempoTaken = false;
    std::vector<std::string> missing;
    std::vector<std::string> unsupported;
    std::vector<std::string> unknownBlends;
};

struct ResolvedClip {
    std::string file;
    bool missing = false;
    bool unsupported = false;
};

std::vector<ResolvedClip> resolveClips(const Document& doc, MediaFinder& finder);

nvj::Composition buildComposition(const Document& doc, const std::vector<ResolvedClip>& resolved, const std::function<int(const std::string& file)>& clipIndexFor, Report& report);

}
