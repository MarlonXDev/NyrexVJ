#include "core/resoport.hpp"

#include "media/media_image.hpp"
#include "scene3d/scene_pack.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <set>
#include <system_error>

namespace nvj::resoport {

namespace fs = std::filesystem;

const std::string* XmlNode::attribute(const std::string& name) const {
    for (const auto& item : attributes) {
        if (item.first == name) {
            return &item.second;
        }
    }
    return nullptr;
}

namespace {

bool isSpace(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

bool isNameChar(char c) {
    const unsigned char u = static_cast<unsigned char>(c);
    return std::isalnum(u) != 0 || c == '_' || c == '-' || c == ':' || c == '.' || u >= 0x80;
}

void appendUtf8(std::string& out, unsigned long code) {
    if (code == 0 || code > 0x10FFFF || (code >= 0xD800 && code <= 0xDFFF)) {
        out.push_back('?');
    } else if (code < 0x80) {
        out.push_back(static_cast<char>(code));
    } else if (code < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (code >> 6)));
        out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
    } else if (code < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (code >> 12)));
        out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (code >> 18)));
        out.push_back(static_cast<char>(0x80 | ((code >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
    }
}

std::string decodeEntities(const char* data, size_t length) {
    std::string out;
    out.reserve(length);
    size_t i = 0;
    while (i < length) {
        if (data[i] != '&') {
            out.push_back(data[i]);
            ++i;
            continue;
        }
        size_t end = i + 1;
        while (end < length && end - i <= 12 && data[end] != ';') {
            ++end;
        }
        if (end >= length || data[end] != ';') {
            out.push_back('&');
            ++i;
            continue;
        }
        const std::string entity(data + i + 1, end - i - 1);
        if (entity == "lt") {
            out.push_back('<');
        } else if (entity == "gt") {
            out.push_back('>');
        } else if (entity == "amp") {
            out.push_back('&');
        } else if (entity == "quot") {
            out.push_back('"');
        } else if (entity == "apos") {
            out.push_back('\'');
        } else if (entity.size() >= 2 && entity[0] == '#') {
            unsigned long code = 0;
            const bool hex = entity[1] == 'x' || entity[1] == 'X';
            const char* first = entity.data() + (hex ? 2 : 1);
            const char* last = entity.data() + entity.size();
            const auto parsed = std::from_chars(first, last, code, hex ? 16 : 10);
            if (parsed.ec == std::errc() && parsed.ptr == last && first != last) {
                appendUtf8(out, code);
            } else {
                out.append(data + i, end - i + 1);
            }
        } else {
            out.append(data + i, end - i + 1);
        }
        i = end + 1;
    }
    return out;
}

class Reader {
public:
    Reader(const std::string& text, XmlDocument& out) : text_(text), out_(out) {}

    bool run(std::string& error) {
        out_.nodes.clear();
        pos_ = 0;
        const size_t size = text_.size();
        if (size >= 3 && static_cast<unsigned char>(text_[0]) == 0xEF && static_cast<unsigned char>(text_[1]) == 0xBB && static_cast<unsigned char>(text_[2]) == 0xBF) {
            pos_ = 3;
        } else if (size >= 2 && ((static_cast<unsigned char>(text_[0]) == 0xFF && static_cast<unsigned char>(text_[1]) == 0xFE) || (static_cast<unsigned char>(text_[0]) == 0xFE && static_cast<unsigned char>(text_[1]) == 0xFF))) {
            error = "The file is encoded as UTF-16, only UTF-8 is supported";
            return false;
        }
        bool rootClosed = false;
        while (pos_ < size) {
            if (text_[pos_] != '<') {
                const size_t next = text_.find('<', pos_);
                pos_ = next == std::string::npos ? size : next;
                continue;
            }
            if (startsWith("<?")) {
                if (!skipPast("?>", 2, error)) {
                    return false;
                }
            } else if (startsWith("<!--")) {
                if (!skipPast("-->", 4, error)) {
                    return false;
                }
            } else if (startsWith("<![CDATA[")) {
                if (!skipPast("]]>", 9, error)) {
                    return false;
                }
            } else if (startsWith("<!DOCTYPE")) {
                const size_t close = text_.find('>', pos_);
                if (close == std::string::npos) {
                    error = "Unterminated document type declaration";
                    return false;
                }
                if (text_.substr(pos_, close - pos_).find('[') != std::string::npos) {
                    error = "Document type declarations with definitions are not supported";
                    return false;
                }
                pos_ = close + 1;
            } else if (startsWith("<!")) {
                error = "Unsupported markup declaration";
                return false;
            } else if (startsWith("</")) {
                if (!endTag(error)) {
                    return false;
                }
                if (stack_.empty()) {
                    rootClosed = true;
                }
            } else {
                if (rootClosed) {
                    error = "More than one root element";
                    return false;
                }
                bool closedRoot = false;
                if (!startTag(error, closedRoot)) {
                    return false;
                }
                if (closedRoot) {
                    rootClosed = true;
                }
            }
        }
        if (!stack_.empty()) {
            error = "The file ends inside an element";
            return false;
        }
        if (out_.nodes.empty()) {
            error = "The file contains no element";
            return false;
        }
        return true;
    }

private:
    bool startsWith(const char* prefix) const {
        return text_.compare(pos_, std::char_traits<char>::length(prefix), prefix) == 0;
    }

    bool skipPast(const char* terminator, size_t from, std::string& error) {
        const size_t found = text_.find(terminator, pos_ + from);
        if (found == std::string::npos) {
            error = "Unterminated markup";
            return false;
        }
        pos_ = found + std::char_traits<char>::length(terminator);
        return true;
    }

    void skipSpace() {
        while (pos_ < text_.size() && isSpace(text_[pos_])) {
            ++pos_;
        }
    }

    bool readName(std::string& name) {
        const size_t begin = pos_;
        if (pos_ < text_.size() && (std::isdigit(static_cast<unsigned char>(text_[pos_])) != 0 || text_[pos_] == '-' || text_[pos_] == '.')) {
            return false;
        }
        while (pos_ < text_.size() && isNameChar(text_[pos_]) && pos_ - begin <= 256) {
            ++pos_;
        }
        if (pos_ == begin || pos_ - begin > 256) {
            return false;
        }
        name.assign(text_, begin, pos_ - begin);
        return true;
    }

    bool endTag(std::string& error) {
        pos_ += 2;
        std::string name;
        if (!readName(name)) {
            error = "Malformed closing tag";
            return false;
        }
        skipSpace();
        if (pos_ >= text_.size() || text_[pos_] != '>') {
            error = "Malformed closing tag";
            return false;
        }
        ++pos_;
        if (stack_.empty() || out_.nodes[static_cast<size_t>(stack_.back())].tag != name) {
            error = "Closing tag does not match its opening tag";
            return false;
        }
        stack_.pop_back();
        return true;
    }

    bool startTag(std::string& error, bool& closedRoot) {
        ++pos_;
        if (out_.nodes.size() >= kMaxNodes) {
            error = "The file has too many elements";
            return false;
        }
        if (stack_.size() >= static_cast<size_t>(kMaxDepth)) {
            error = "The elements are nested too deeply";
            return false;
        }
        XmlNode node;
        if (!readName(node.tag)) {
            error = "Malformed opening tag";
            return false;
        }
        node.parent = stack_.empty() ? -1 : stack_.back();
        while (true) {
            skipSpace();
            if (pos_ >= text_.size()) {
                error = "The file ends inside a tag";
                return false;
            }
            if (text_[pos_] == '>') {
                ++pos_;
                const int index = static_cast<int>(out_.nodes.size());
                attach(node);
                out_.nodes.push_back(std::move(node));
                stack_.push_back(index);
                return true;
            }
            if (text_[pos_] == '/') {
                if (pos_ + 1 >= text_.size() || text_[pos_ + 1] != '>') {
                    error = "Malformed tag";
                    return false;
                }
                pos_ += 2;
                closedRoot = stack_.empty();
                attach(node);
                out_.nodes.push_back(std::move(node));
                return true;
            }
            std::string key;
            if (!readName(key)) {
                error = "Malformed attribute";
                return false;
            }
            skipSpace();
            if (pos_ >= text_.size() || text_[pos_] != '=') {
                error = "Attribute without a value";
                return false;
            }
            ++pos_;
            skipSpace();
            if (pos_ >= text_.size() || (text_[pos_] != '"' && text_[pos_] != '\'')) {
                error = "Attribute value without quotes";
                return false;
            }
            const char quote = text_[pos_];
            const size_t close = text_.find(quote, pos_ + 1);
            if (close == std::string::npos) {
                error = "Unterminated attribute value";
                return false;
            }
            if (node.attributes.size() >= kMaxAttributes) {
                error = "An element has too many attributes";
                return false;
            }
            const size_t length = close - pos_ - 1;
            std::string value;
            if (length <= kMaxValueLength) {
                value = decodeEntities(text_.data() + pos_ + 1, length);
            }
            bool duplicate = false;
            for (const auto& item : node.attributes) {
                if (item.first == key) {
                    duplicate = true;
                    break;
                }
            }
            if (!duplicate) {
                node.attributes.emplace_back(std::move(key), std::move(value));
            }
            pos_ = close + 1;
        }
    }

    void attach(const XmlNode& node) {
        if (node.parent >= 0) {
            out_.nodes[static_cast<size_t>(node.parent)].children.push_back(static_cast<int>(out_.nodes.size()));
        }
    }

    const std::string& text_;
    XmlDocument& out_;
    size_t pos_ = 0;
    std::vector<int> stack_;
};

const std::string kEmpty;

const std::string& attr(const XmlDocument& xml, int node, const char* name) {
    if (node < 0) {
        return kEmpty;
    }
    const std::string* value = xml.nodes[static_cast<size_t>(node)].attribute(name);
    return value ? *value : kEmpty;
}

int childWithTag(const XmlDocument& xml, int node, const char* tag) {
    if (node < 0) {
        return -1;
    }
    for (int child : xml.nodes[static_cast<size_t>(node)].children) {
        if (xml.nodes[static_cast<size_t>(child)].tag == tag) {
            return child;
        }
    }
    return -1;
}

int firstDescendant(const XmlDocument& xml, int node, const char* tag) {
    if (node < 0) {
        return -1;
    }
    std::vector<int> pending{node};
    while (!pending.empty()) {
        const int current = pending.back();
        pending.pop_back();
        const XmlNode& item = xml.nodes[static_cast<size_t>(current)];
        if (current != node && item.tag == tag) {
            return current;
        }
        for (auto it = item.children.rbegin(); it != item.children.rend(); ++it) {
            pending.push_back(*it);
        }
    }
    return -1;
}

int namedParameter(const XmlDocument& xml, int node, const char* name) {
    if (node < 0) {
        return -1;
    }
    std::vector<int> pending{node};
    while (!pending.empty()) {
        const int current = pending.back();
        pending.pop_back();
        const XmlNode& item = xml.nodes[static_cast<size_t>(current)];
        if ((item.tag == "Param" || item.tag == "ParamRange") && attr(xml, current, "name") == name) {
            return current;
        }
        for (auto it = item.children.rbegin(); it != item.children.rend(); ++it) {
            pending.push_back(*it);
        }
    }
    return -1;
}

bool toInt(const std::string& text, int& value) {
    if (text.empty()) {
        return false;
    }
    int parsed = 0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), parsed);
    if (result.ec != std::errc() || result.ptr != text.data() + text.size()) {
        return false;
    }
    value = parsed;
    return true;
}

bool toFloat(const std::string& text, float& value) {
    if (text.empty()) {
        return false;
    }
    double parsed = 0.0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), parsed);
    if (result.ec != std::errc() || !std::isfinite(parsed)) {
        return false;
    }
    value = static_cast<float>(parsed);
    return true;
}

std::string lowerCase(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

std::string fileNameOf(const std::string& path) {
    const size_t cut = path.find_last_of("/\\");
    return cut == std::string::npos ? path : path.substr(cut + 1);
}

bool mediaName(const std::string& name) {
    return isImageFileName(name) || isVideoFileName(name);
}

bool hasParentSteps(const std::string& path) {
    size_t start = 0;
    while (start <= path.size()) {
        size_t end = path.find_first_of("/\\", start);
        if (end == std::string::npos) {
            end = path.size();
        }
        if (path.compare(start, end - start, "..") == 0) {
            return true;
        }
        start = end + 1;
    }
    return false;
}

std::string deckName(const XmlDocument& xml, int deck) {
    const int params = childWithTag(xml, deck, "Params");
    const int name = namedParameter(xml, params, "Name");
    return attr(xml, name, "value");
}

std::string tidyName(const std::string& text) {
    std::string out;
    for (unsigned char c : text) {
        if (c >= 0x20 && c != 0x7F) {
            out.push_back(static_cast<char>(c));
        }
        if (out.size() >= 64) {
            break;
        }
    }
    while (!out.empty() && isSpace(out.back())) {
        out.pop_back();
    }
    return out;
}

}

bool parseXml(const std::string& text, XmlDocument& out, std::string& error) {
    Reader reader(text, out);
    if (!reader.run(error)) {
        out.nodes.clear();
        return false;
    }
    return true;
}

bool parseDocument(const std::string& text, Document& out, std::string& error) {
    XmlDocument xml;
    if (!parseXml(text, xml, error)) {
        return false;
    }
    if (xml.nodes[0].tag != "Composition") {
        error = "This is not a Resolume composition";
        return false;
    }
    out = Document{};
    out.name = attr(xml, 0, "name");
    toInt(attr(xml, 0, "numDecks"), out.declaredDecks);
    toInt(attr(xml, 0, "numLayers"), out.declaredLayers);
    toInt(attr(xml, 0, "numColumns"), out.declaredColumns);

    const int version = childWithTag(xml, 0, "versionInfo");
    out.application = attr(xml, version, "name");
    toInt(attr(xml, version, "majorVersion"), out.versionMajor);
    toInt(attr(xml, version, "minorVersion"), out.versionMinor);

    const int info = childWithTag(xml, 0, "CompositionInfo");
    toInt(attr(xml, info, "width"), out.width);
    toInt(attr(xml, info, "height"), out.height);

    int layerCount = 0;
    int deckCount = 0;
    for (int child : xml.nodes[0].children) {
        const std::string& tag = xml.nodes[static_cast<size_t>(child)].tag;
        if (tag == "Layer") {
            LayerInfo layer;
            layer.index = layerCount;
            toInt(attr(xml, child, "layerIndex"), layer.index);
            ++layerCount;
            const int track = childWithTag(xml, child, "VideoTrack");
            if (track >= 0) {
                for (int item : xml.nodes[static_cast<size_t>(track)].children) {
                    if (xml.nodes[static_cast<size_t>(item)].tag == "ChoosableMixer" && attr(xml, item, "name") == "Blend") {
                        const int pass = childWithTag(xml, item, "RenderPass");
                        layer.blend = attr(xml, pass, "type");
                        float opacity = 1.0f;
                        const int param = namedParameter(xml, pass, "Opacity");
                        if (param >= 0 && toFloat(attr(xml, param, "value"), opacity)) {
                            layer.opacity = std::clamp(opacity, 0.0f, 1.0f);
                        }
                        break;
                    }
                }
            }
            out.layers.push_back(std::move(layer));
        } else if (tag == "Deck") {
            DeckInfo deck;
            deck.name = tidyName(deckName(xml, child));
            const int index = deckCount;
            ++deckCount;
            out.decks.push_back(std::move(deck));
            for (int item : xml.nodes[static_cast<size_t>(child)].children) {
                if (xml.nodes[static_cast<size_t>(item)].tag != "Clip") {
                    continue;
                }
                ClipInfo clip;
                clip.deck = index;
                if (!toInt(attr(xml, item, "layerIndex"), clip.layer) || !toInt(attr(xml, item, "columnIndex"), clip.column)) {
                    continue;
                }
                const int source = firstDescendant(xml, item, "VideoSource");
                if (source < 0) {
                    continue;
                }
                const std::string& type = attr(xml, source, "type");
                if (type == "VideoFormatReaderSource") {
                    clip.source = SourceKind::Media;
                    clip.path = attr(xml, source, "fileName");
                    if (clip.path.empty()) {
                        clip.path = attr(xml, firstDescendant(xml, item, "VideoFile"), "value");
                    }
                } else if (type == "GeneratorVideoSource") {
                    clip.source = SourceKind::Generator;
                } else if (type == "CompositionRouterVideoSource") {
                    clip.source = SourceKind::Router;
                }
                const int params = childWithTag(xml, item, "Params");
                clip.name = tidyName(attr(xml, namedParameter(xml, params, "Name"), "value"));
                out.clips.push_back(std::move(clip));
            }
        }
    }

    for (size_t i = 0; i < xml.nodes.size(); ++i) {
        const XmlNode& node = xml.nodes[i];
        if (node.tag == "RenderPass" && attr(xml, static_cast<int>(i), "baseType") == "Effect" && attr(xml, static_cast<int>(i), "type") != "TransformEffect") {
            ++out.effectPasses;
        }
    }

    const int tempo = childWithTag(xml, 0, "TempoController");
    if (tempo >= 0) {
        for (const char* name : {"Tempo", "BPM"}) {
            const int param = namedParameter(xml, tempo, name);
            float value = 0.0f;
            if (param >= 0 && toFloat(attr(xml, param, "value"), value) && value >= 40.0f && value <= 240.0f) {
                out.bpm = value;
                break;
            }
        }
    }
    return true;
}

bool loadDocument(const std::string& fileUtf8, Document& out, std::string& error) {
    const fs::path path = pathFromUtf8(fileUtf8);
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
    if (size > kMaxFileBytes) {
        error = "The file is larger than 192 MiB";
        return false;
    }
    std::vector<uint8_t> bytes;
    std::string readError;
    if (!readFileBytes(path, static_cast<size_t>(kMaxFileBytes), bytes, readError)) {
        error = readError.empty() ? "The file cannot be read" : readError;
        return false;
    }
    const std::string text(bytes.begin(), bytes.end());
    return parseDocument(text, out, error);
}

int blendModeFor(const std::string& resolumeName, bool& known) {
    std::string key;
    for (unsigned char c : resolumeName) {
        if (std::isalnum(c) != 0) {
            key.push_back(static_cast<char>(std::tolower(c)));
        }
    }
    known = true;
    if (key.empty() || key == "alpha" || key == "normal" || key == "over") {
        return BlendNormal;
    }
    if (key == "add") {
        return BlendAdd;
    }
    if (key == "screen") {
        return BlendScreen;
    }
    if (key == "multiply") {
        return BlendMultiply;
    }
    if (key == "subtract") {
        return BlendSubtract;
    }
    if (key == "difference") {
        return BlendDifference;
    }
    if (key == "exclusion") {
        return BlendExclusion;
    }
    if (key == "overlay") {
        return BlendOverlay;
    }
    if (key == "softlight") {
        return BlendSoftLight;
    }
    if (key == "hardlight") {
        return BlendHardLight;
    }
    if (key == "lighten") {
        return BlendLighten;
    }
    if (key == "darken") {
        return BlendDarken;
    }
    if (key == "colordodge") {
        return BlendColorDodge;
    }
    if (key == "linearburn") {
        return BlendLinearBurn;
    }
    known = false;
    return BlendNormal;
}

MediaFinder::MediaFinder(const std::string& compositionFileUtf8, const std::vector<std::string>& extraFoldersUtf8) {
    std::error_code ec;
    const fs::path parent = pathFromUtf8(compositionFileUtf8).parent_path();
    if (!parent.empty() && fs::is_directory(parent, ec)) {
        roots_.push_back(pathToUtf8(parent));
    }
    for (const std::string& folder : extraFoldersUtf8) {
        if (!folder.empty() && fs::is_directory(pathFromUtf8(folder), ec) && std::find(roots_.begin(), roots_.end(), folder) == roots_.end()) {
            roots_.push_back(folder);
        }
    }
}

void MediaFinder::buildIndex() {
    indexed_ = true;
    size_t visited = 0;
    for (const std::string& root : roots_) {
        std::error_code ec;
        fs::recursive_directory_iterator it(pathFromUtf8(root), fs::directory_options::skip_permission_denied, ec);
        const fs::recursive_directory_iterator end;
        while (!ec && it != end && visited < kMaxSearchEntries) {
            ++visited;
            if (it.depth() >= kMaxSearchDepth) {
                it.disable_recursion_pending();
            }
            std::error_code fileEc;
            if (it->is_regular_file(fileEc) && !fileEc) {
                const std::string name = pathToUtf8(it->path().filename());
                if (mediaName(name)) {
                    byName_.emplace(lowerCase(name), pathToUtf8(it->path()));
                }
            }
            it.increment(ec);
        }
    }
}

std::string MediaFinder::find(const std::string& recordedPath) {
    std::string path = recordedPath;
    if (path.rfind("file://", 0) == 0) {
        path.erase(0, 7);
    }
    if (path.empty() || !mediaName(fileNameOf(path))) {
        return std::string();
    }
    std::error_code ec;
    const fs::path direct = pathFromUtf8(path);
    if (direct.is_absolute()) {
        if (fs::is_regular_file(direct, ec)) {
            return pathToUtf8(direct);
        }
    } else if (!hasParentSteps(path)) {
        for (const std::string& root : roots_) {
            const fs::path candidate = pathFromUtf8(root) / direct;
            if (fs::is_regular_file(candidate, ec)) {
                return pathToUtf8(candidate);
            }
        }
    }
    const std::string name = fileNameOf(path);
    if (name.empty()) {
        return std::string();
    }
    for (const std::string& root : roots_) {
        const fs::path candidate = pathFromUtf8(root) / pathFromUtf8(name);
        if (fs::is_regular_file(candidate, ec)) {
            return pathToUtf8(candidate);
        }
    }
    if (!indexed_) {
        buildIndex();
    }
    const auto found = byName_.find(lowerCase(name));
    return found == byName_.end() ? std::string() : found->second;
}

std::vector<ResolvedClip> resolveClips(const Document& doc, MediaFinder& finder) {
    std::vector<ResolvedClip> out(doc.clips.size());
    std::unordered_map<std::string, ResolvedClip> memo;
    for (size_t i = 0; i < doc.clips.size(); ++i) {
        const ClipInfo& clip = doc.clips[i];
        if (clip.source != SourceKind::Media) {
            continue;
        }
        const auto cached = memo.find(clip.path);
        if (cached != memo.end()) {
            out[i] = cached->second;
            continue;
        }
        ResolvedClip resolved;
        if (!mediaName(fileNameOf(clip.path))) {
            resolved.unsupported = true;
        } else {
            resolved.file = finder.find(clip.path);
            resolved.missing = resolved.file.empty();
        }
        memo.emplace(clip.path, resolved);
        out[i] = resolved;
    }
    return out;
}

nvj::Composition buildComposition(const Document& doc, const std::vector<ResolvedClip>& resolved, const std::function<int(const std::string& file)>& clipIndexFor, Report& report) {
    report = Report{};
    nvj::Composition comp;
    comp.name = tidyName(doc.name);
    if (comp.name.empty()) {
        comp.name = "Resolume composition";
    }
    if (doc.bpm >= 40.0f && doc.bpm <= 240.0f) {
        comp.bpm = doc.bpm;
        report.tempoTaken = true;
    }
    if (doc.width > 0 && doc.height > 0) {
        comp.resolution = resolutionIndexFor(doc.width, doc.height);
        const Resolution& chosen = kResolutions[static_cast<size_t>(comp.resolution)];
        report.resolutionMatched = chosen.width == doc.width && chosen.height == doc.height;
    }

    std::array<int, kLayerCount> blends{};
    std::array<float, kLayerCount> opacities{};
    opacities.fill(1.0f);
    std::set<std::string> unknown;
    for (const LayerInfo& layer : doc.layers) {
        if (layer.index < 0 || layer.index >= kLayerCount) {
            continue;
        }
        bool known = true;
        const int mode = blendModeFor(layer.blend, known);
        blends[static_cast<size_t>(layer.index)] = mode;
        opacities[static_cast<size_t>(layer.index)] = layer.opacity;
        if (known) {
            ++report.layerBlendsMapped;
        } else if (unknown.insert(layer.blend).second && report.unknownBlends.size() < kMaxListed) {
            report.unknownBlends.push_back(layer.blend);
        }
    }
    const int layerTotal = std::max(static_cast<int>(doc.layers.size()), doc.declaredLayers);
    report.layersSkipped = std::max(0, layerTotal - kLayerCount);
    const int deckTotal = std::max(static_cast<int>(doc.decks.size()), doc.declaredDecks);
    report.decksSkipped = std::max(0, deckTotal - kDeckCount);
    report.effectPasses = doc.effectPasses;

    std::array<DeckData, kDeckCount> decks;
    std::array<bool, kDeckCount> used{};
    used[0] = true;
    for (DeckData& deck : decks) {
        for (int l = 0; l < kLayerCount; ++l) {
            deck.layers[static_cast<size_t>(l)].blend = blends[static_cast<size_t>(l)];
            deck.layers[static_cast<size_t>(l)].opacity = opacities[static_cast<size_t>(l)];
        }
    }

    std::set<std::string> missingNames;
    std::set<std::string> unsupportedNames;
    for (size_t i = 0; i < doc.clips.size(); ++i) {
        const ClipInfo& clip = doc.clips[i];
        if (clip.deck < 0 || clip.deck >= kDeckCount) {
            continue;
        }
        if (clip.source == SourceKind::Generator) {
            ++report.clipsGenerator;
            continue;
        }
        if (clip.source == SourceKind::Router) {
            ++report.clipsRouter;
            continue;
        }
        if (clip.source != SourceKind::Media) {
            ++report.clipsOther;
            continue;
        }
        const ResolvedClip item = i < resolved.size() ? resolved[i] : ResolvedClip{};
        const std::string shown = fileNameOf(clip.path);
        if (item.unsupported) {
            ++report.clipsUnsupported;
            if (unsupportedNames.insert(shown).second && report.unsupported.size() < kMaxListed) {
                report.unsupported.push_back(shown);
            }
            continue;
        }
        if (item.missing || item.file.empty()) {
            ++report.clipsMissing;
            if (missingNames.insert(shown).second && report.missing.size() < kMaxListed) {
                report.missing.push_back(shown);
            }
            continue;
        }
        if (clip.layer < 0 || clip.layer >= kLayerCount || clip.column < 0 || clip.column >= kColumnCount) {
            ++report.clipsSkippedPosition;
            continue;
        }
        const int index = clipIndexFor(item.file);
        if (index < 0) {
            ++report.clipsMissing;
            if (missingNames.insert(shown).second && report.missing.size() < kMaxListed) {
                report.missing.push_back(shown);
            }
            continue;
        }
        decks[static_cast<size_t>(clip.deck)].layers[static_cast<size_t>(clip.layer)].setCell(static_cast<size_t>(clip.column), index);
        used[static_cast<size_t>(clip.deck)] = true;
        ++report.clipsImported;
    }

    for (int d = 0; d < kDeckCount; ++d) {
        const size_t di = static_cast<size_t>(d);
        comp.deckUsed[di] = used[di];
        if (!used[di]) {
            continue;
        }
        comp.decks[di] = decks[di];
        if (d < static_cast<int>(doc.decks.size())) {
            comp.deckNames[di] = doc.decks[di].name;
        }
        ++report.decksImported;
    }
    restoreDeck(comp, comp.decks[0]);
    comp.activeDeck = 0;
    normalizeDecks(comp);
    return comp;
}

}
