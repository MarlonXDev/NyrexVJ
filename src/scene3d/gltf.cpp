#include "scene3d/gltf.hpp"

#include "scene3d/image_decode.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <map>
#include <new>
#include <string_view>
#include <system_error>

namespace nvj {

namespace {

using json = nlohmann::json;

constexpr uint32_t kGlbMagic = 0x46546C67u;
constexpr uint32_t kChunkJson = 0x4E4F534Au;
constexpr uint32_t kChunkBin = 0x004E4942u;
constexpr uint64_t kMaxAccessorCount = 1u << 26;

struct Failure {
    std::string message;
};

struct Interrupted {
    std::string message;
};

using Clock = std::chrono::steady_clock;

struct Budget {
    const GltfLimits* limits = nullptr;
    Clock::time_point begin = Clock::now();

    void check() const {
        if (!limits) {
            return;
        }
        if (limits->cancel && limits->cancel->load(std::memory_order_relaxed)) {
            throw Interrupted{"loading was cancelled"};
        }
        if (limits->maxSeconds > 0.0 && std::chrono::duration<double>(Clock::now() - begin).count() > limits->maxSeconds) {
            throw Interrupted{"loading took longer than the time limit"};
        }
    }
};

[[noreturn]] void fail(const std::string& message) {
    throw Failure{message};
}

uint32_t readU32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

bool jsonDepthOk(const char* text, size_t size, int maxDepth) {
    int depth = 0;
    bool inString = false;
    bool escaped = false;
    for (size_t i = 0; i < size; ++i) {
        const char c = text[i];
        if (inString) {
            if (escaped) {
                escaped = false;
            } else if (c == '\\') {
                escaped = true;
            } else if (c == '"') {
                inString = false;
            }
            continue;
        }
        if (c == '"') {
            inString = true;
        } else if (c == '[' || c == '{') {
            if (++depth > maxDepth) {
                return false;
            }
        } else if (c == ']' || c == '}') {
            --depth;
        }
    }
    return true;
}

int base64Value(char c) {
    if (c >= 'A' && c <= 'Z') {
        return c - 'A';
    }
    if (c >= 'a' && c <= 'z') {
        return c - 'a' + 26;
    }
    if (c >= '0' && c <= '9') {
        return c - '0' + 52;
    }
    if (c == '+') {
        return 62;
    }
    if (c == '/') {
        return 63;
    }
    return -1;
}

std::vector<uint8_t> base64Decode(std::string_view text, size_t maxBytes) {
    std::vector<uint8_t> out;
    out.reserve(std::min(maxBytes, text.size() / 4 * 3 + 3));
    uint32_t accumulator = 0;
    int bits = 0;
    for (const char c : text) {
        if (c == '=' || c == '\n' || c == '\r' || c == ' ') {
            continue;
        }
        const int value = base64Value(c);
        if (value < 0) {
            fail("invalid base64 data");
        }
        accumulator = (accumulator << 6) | static_cast<uint32_t>(value);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            if (out.size() >= maxBytes) {
                fail("embedded data exceeds the size limit");
            }
            out.push_back(static_cast<uint8_t>((accumulator >> bits) & 0xFFu));
        }
    }
    return out;
}

std::string percentDecode(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '%' && i + 2 < text.size() + 0 && std::isxdigit(static_cast<unsigned char>(text[i + 1])) && std::isxdigit(static_cast<unsigned char>(text[i + 2]))) {
            out.push_back(static_cast<char>(std::stoi(text.substr(i + 1, 2), nullptr, 16)));
            i += 2;
        } else {
            out.push_back(text[i]);
        }
    }
    return out;
}

std::vector<uint8_t> readFileLimited(const std::filesystem::path& path, size_t limit, const Budget& budget) {
    std::error_code ec;
    if (!std::filesystem::is_regular_file(path, ec) || ec) {
        fail("file not found: " + path.filename().string());
    }
    const auto size = std::filesystem::file_size(path, ec);
    if (ec) {
        fail("file size unavailable: " + path.filename().string());
    }
    if (size > limit) {
        fail("file exceeds the size limit: " + path.filename().string());
    }
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        fail("file could not be opened: " + path.filename().string());
    }
    std::vector<uint8_t> data(static_cast<size_t>(size));
    constexpr size_t kChunk = 4u * 1024u * 1024u;
    size_t done = 0;
    while (done < data.size()) {
        budget.check();
        const size_t step = std::min(kChunk, data.size() - done);
        in.read(reinterpret_cast<char*>(data.data() + done), static_cast<std::streamsize>(step));
        if (static_cast<size_t>(in.gcount()) != step) {
            fail("file could not be read completely: " + path.filename().string());
        }
        done += step;
    }
    return data;
}

std::filesystem::path resolveExternal(const std::filesystem::path& baseDirectory, const std::string& uri) {
    if (baseDirectory.empty()) {
        fail("external resources are not allowed for in-memory data");
    }
    const std::string decoded = percentDecode(uri);
    if (decoded.empty() || decoded.find(':') != std::string::npos || decoded.find('\0') != std::string::npos || decoded.front() == '/' || decoded.front() == '\\') {
        fail("unsafe resource path");
    }
    const std::filesystem::path relative{std::u8string(decoded.begin(), decoded.end())};
    if (relative.has_root_name() || relative.has_root_directory()) {
        fail("unsafe resource path");
    }
    for (const auto& part : relative) {
        if (part == "..") {
            fail("resource path leaves the model folder");
        }
    }
    std::error_code ec;
    const std::filesystem::path base = std::filesystem::weakly_canonical(baseDirectory, ec);
    if (ec) {
        fail("model folder is not accessible");
    }
    const std::filesystem::path full = std::filesystem::weakly_canonical(base / relative, ec);
    if (ec) {
        fail("resource path is not accessible");
    }
    auto baseIt = base.begin();
    auto fullIt = full.begin();
    for (; baseIt != base.end(); ++baseIt, ++fullIt) {
        if (fullIt == full.end() || *fullIt != *baseIt) {
            fail("resource path leaves the model folder");
        }
    }
    return full;
}

int64_t integerOf(const json& object, const char* key, int64_t fallback) {
    const auto it = object.find(key);
    if (it == object.end()) {
        return fallback;
    }
    if (!it->is_number_integer()) {
        fail(std::string("field '") + key + "' must be an integer");
    }
    return it->get<int64_t>();
}

int indexOf(const json& object, const char* key, size_t limit, const char* what) {
    const int64_t value = integerOf(object, key, -1);
    if (value < 0 || static_cast<uint64_t>(value) >= limit) {
        fail(std::string(what) + " index out of range");
    }
    return static_cast<int>(value);
}

const json& arrayOf(const json& doc, const char* key) {
    static const json empty = json::array();
    const auto it = doc.find(key);
    if (it == doc.end()) {
        return empty;
    }
    if (!it->is_array()) {
        fail(std::string("'") + key + "' must be an array");
    }
    return *it;
}

int componentSize(int type) {
    switch (type) {
    case 5120:
    case 5121:
        return 1;
    case 5122:
    case 5123:
        return 2;
    case 5125:
    case 5126:
        return 4;
    default:
        return 0;
    }
}

int typeComponents(const std::string& type) {
    if (type == "SCALAR") {
        return 1;
    }
    if (type == "VEC2") {
        return 2;
    }
    if (type == "VEC3") {
        return 3;
    }
    if (type == "VEC4") {
        return 4;
    }
    return 0;
}

float convertComponent(const uint8_t* p, int type, bool normalized) {
    switch (type) {
    case 5126: {
        float v;
        std::memcpy(&v, p, 4);
        return v;
    }
    case 5120: {
        const int8_t v = static_cast<int8_t>(*p);
        return normalized ? std::max(static_cast<float>(v) / 127.0f, -1.0f) : static_cast<float>(v);
    }
    case 5121:
        return normalized ? static_cast<float>(*p) / 255.0f : static_cast<float>(*p);
    case 5122: {
        int16_t v;
        std::memcpy(&v, p, 2);
        return normalized ? std::max(static_cast<float>(v) / 32767.0f, -1.0f) : static_cast<float>(v);
    }
    case 5123: {
        uint16_t v;
        std::memcpy(&v, p, 2);
        return normalized ? static_cast<float>(v) / 65535.0f : static_cast<float>(v);
    }
    case 5125: {
        uint32_t v;
        std::memcpy(&v, p, 4);
        return static_cast<float>(v);
    }
    default:
        fail("unsupported component type");
    }
}

struct AccessorView {
    const uint8_t* base = nullptr;
    size_t count = 0;
    int components = 0;
    int componentType = 0;
    bool normalized = false;
    size_t stride = 0;
    size_t elementSize = 0;
};

class Loader {
public:
    Loader(const GltfLimits& limits, const std::filesystem::path& baseDirectory, GltfResult& result)
        : limits_(limits), baseDirectory_(baseDirectory), result_(result) {
        budget_.limits = &limits;
    }


    void run(const uint8_t* data, size_t size) {
        if (size > limits_.maxFileBytes) {
            fail("file exceeds the size limit");
        }
        std::vector<uint8_t> glbBinary;
        const char* jsonText = nullptr;
        size_t jsonSize = 0;
        if (size >= 12 && readU32(data) == kGlbMagic) {
            parseGlb(data, size, jsonText, jsonSize, glbBinary);
        } else {
            jsonText = reinterpret_cast<const char*>(data);
            jsonSize = size;
        }
        if (jsonSize == 0 || jsonSize > limits_.maxJsonBytes) {
            fail("JSON part is empty or exceeds the size limit");
        }
        if (!jsonDepthOk(jsonText, jsonSize, limits_.maxJsonDepth)) {
            fail("JSON nesting is too deep");
        }
        doc_ = json::parse(jsonText, jsonText + jsonSize, nullptr, true, false);
        if (!doc_.is_object()) {
            fail("document is not a glTF object");
        }
        budget_.check();
        checkAsset();
        loadBuffers(std::move(glbBinary));
        budget_.check();
        loadMaterials();
        budget_.check();
        loadMeshes();
        budget_.check();
        loadNodes();
        loadAnimations();
        budget_.check();
        std::string error;
        if (!validateScene(scene(), error)) {
            fail(error);
        }
    }

private:
    Scene& scene() { return result_.scene; }

    void warn(const std::string& message) {
        if (result_.warnings.size() < 32) {
            result_.warnings.push_back(message);
        }
    }

    void parseGlb(const uint8_t* data, size_t size, const char*& jsonText, size_t& jsonSize, std::vector<uint8_t>& binary) {
        if (readU32(data + 4) != 2) {
            fail("unsupported GLB version");
        }
        const uint64_t declared = readU32(data + 8);
        if (declared > size || declared < 12) {
            fail("GLB length is inconsistent");
        }
        size_t offset = 12;
        bool haveJson = false;
        while (offset + 8 <= declared) {
            const uint64_t chunkLength = readU32(data + offset);
            const uint32_t chunkType = readU32(data + offset + 4);
            offset += 8;
            if (chunkLength > declared - offset) {
                fail("GLB chunk exceeds the file");
            }
            if (chunkType == kChunkJson && !haveJson) {
                jsonText = reinterpret_cast<const char*>(data + offset);
                jsonSize = static_cast<size_t>(chunkLength);
                haveJson = true;
            } else if (chunkType == kChunkBin && binary.empty()) {
                binary.assign(data + offset, data + offset + chunkLength);
            }
            offset += static_cast<size_t>((chunkLength + 3) & ~static_cast<uint64_t>(3));
        }
        if (!haveJson) {
            fail("GLB has no JSON chunk");
        }
    }

    void checkAsset() {
        const auto asset = doc_.find("asset");
        if (asset == doc_.end() || !asset->is_object()) {
            fail("missing asset information");
        }
        const std::string version = asset->value("version", std::string());
        if (version.empty() || version[0] != '2') {
            fail("only glTF 2.x is supported");
        }
        static const char* const supported[] = {"KHR_mesh_quantization", "KHR_materials_unlit", "KHR_texture_transform", "KHR_materials_emissive_strength"};
        for (const json& entry : arrayOf(doc_, "extensionsRequired")) {
            const std::string name = entry.is_string() ? entry.get<std::string>() : std::string();
            if (std::none_of(std::begin(supported), std::end(supported), [&](const char* s) { return name == s; })) {
                fail("required extension not supported: " + name);
            }
        }
    }

    void loadBuffers(std::vector<uint8_t> glbBinary) {
        const json& buffers = arrayOf(doc_, "buffers");
        size_t total = 0;
        for (size_t i = 0; i < buffers.size(); ++i) {
            budget_.check();
            const json& b = buffers[i];
            if (!b.is_object()) {
                fail("buffer entry is invalid");
            }
            const int64_t length = integerOf(b, "byteLength", -1);
            if (length < 0) {
                fail("buffer without byteLength");
            }
            std::vector<uint8_t> data;
            const auto uri = b.find("uri");
            if (uri == b.end()) {
                if (i != 0 || glbBinary.empty()) {
                    fail("buffer has no data");
                }
                data = std::move(glbBinary);
            } else if (uri->is_string()) {
                data = loadUri(uri->get<std::string>(), limits_.maxFileBytes);
            } else {
                fail("buffer uri must be a string");
            }
            if (data.size() < static_cast<uint64_t>(length)) {
                fail("buffer is shorter than declared");
            }
            total += data.size();
            if (total > limits_.maxFileBytes) {
                fail("buffers exceed the size limit");
            }
            buffers_.push_back(std::move(data));
        }
    }

    std::vector<uint8_t> loadUri(const std::string& uri, size_t limit) {
        if (uri.rfind("data:", 0) == 0) {
            const size_t comma = uri.find(',');
            if (comma == std::string::npos || uri.find(";base64") == std::string::npos || uri.find(";base64") > comma) {
                fail("only base64 data URIs are supported");
            }
            return base64Decode(std::string_view(uri).substr(comma + 1), limit);
        }
        return readFileLimited(resolveExternal(baseDirectory_, uri), limit, budget_);
    }

    AccessorView accessor(int index) {
        const json& accessors = arrayOf(doc_, "accessors");
        if (index < 0 || static_cast<size_t>(index) >= accessors.size() || !accessors[static_cast<size_t>(index)].is_object()) {
            fail("accessor index out of range");
        }
        const json& a = accessors[static_cast<size_t>(index)];
        if (a.contains("sparse")) {
            fail("sparse accessors are not supported");
        }
        AccessorView view;
        const int64_t count = integerOf(a, "count", -1);
        if (count <= 0 || static_cast<uint64_t>(count) > kMaxAccessorCount) {
            fail("accessor count is invalid");
        }
        view.count = static_cast<size_t>(count);
        view.componentType = static_cast<int>(integerOf(a, "componentType", 0));
        const int compSize = componentSize(view.componentType);
        view.components = typeComponents(a.value("type", std::string()));
        if (compSize == 0 || view.components == 0) {
            fail("accessor type is not supported");
        }
        view.normalized = a.value("normalized", false);
        view.elementSize = static_cast<size_t>(compSize) * static_cast<size_t>(view.components);
        if (!a.contains("bufferView")) {
            fail("accessor without bufferView is not supported");
        }
        const json& views = arrayOf(doc_, "bufferViews");
        const int viewIndex = indexOf(a, "bufferView", views.size(), "bufferView");
        const json& v = views[static_cast<size_t>(viewIndex)];
        if (!v.is_object()) {
            fail("bufferView entry is invalid");
        }
        const int bufferIndex = indexOf(v, "buffer", buffers_.size(), "buffer");
        const std::vector<uint8_t>& buffer = buffers_[static_cast<size_t>(bufferIndex)];
        const int64_t viewOffset = integerOf(v, "byteOffset", 0);
        const int64_t viewLength = integerOf(v, "byteLength", -1);
        const int64_t viewStride = integerOf(v, "byteStride", 0);
        const int64_t accOffset = integerOf(a, "byteOffset", 0);
        if (viewOffset < 0 || viewLength < 0 || viewStride < 0 || accOffset < 0 || viewStride > 252) {
            fail("bufferView range is invalid");
        }
        if (static_cast<uint64_t>(viewOffset) + static_cast<uint64_t>(viewLength) > buffer.size()) {
            fail("bufferView exceeds its buffer");
        }
        view.stride = viewStride == 0 ? view.elementSize : static_cast<size_t>(viewStride);
        if (view.stride < view.elementSize) {
            fail("byteStride is smaller than the element");
        }
        const uint64_t end = static_cast<uint64_t>(accOffset) + (static_cast<uint64_t>(count) - 1) * view.stride + view.elementSize;
        if (end > static_cast<uint64_t>(viewLength)) {
            fail("accessor exceeds its bufferView");
        }
        view.base = buffer.data() + viewOffset + accOffset;
        return view;
    }

    std::vector<float> readFloats(int index, int expectedComponents) {
        const AccessorView view = accessor(index);
        if (view.components != expectedComponents) {
            fail("accessor has an unexpected element type");
        }
        std::vector<float> out(view.count * static_cast<size_t>(view.components));
        const int compSize = componentSize(view.componentType);
        for (size_t i = 0; i < view.count; ++i) {
            const uint8_t* element = view.base + i * view.stride;
            for (int c = 0; c < view.components; ++c) {
                out[i * static_cast<size_t>(view.components) + static_cast<size_t>(c)] = convertComponent(element + c * compSize, view.componentType, view.normalized);
            }
        }
        return out;
    }

    std::vector<uint32_t> readIndices(int index) {
        const AccessorView view = accessor(index);
        if (view.components != 1 || (view.componentType != 5121 && view.componentType != 5123 && view.componentType != 5125)) {
            fail("index accessor has an unsupported type");
        }
        std::vector<uint32_t> out(view.count);
        for (size_t i = 0; i < view.count; ++i) {
            const uint8_t* p = view.base + i * view.stride;
            if (view.componentType == 5121) {
                out[i] = *p;
            } else if (view.componentType == 5123) {
                uint16_t v;
                std::memcpy(&v, p, 2);
                out[i] = v;
            } else {
                out[i] = readU32(p);
            }
        }
        return out;
    }

    int loadImage(int textureIndex) {
        const json& textures = arrayOf(doc_, "textures");
        if (textureIndex < 0 || static_cast<size_t>(textureIndex) >= textures.size() || !textures[static_cast<size_t>(textureIndex)].is_object()) {
            warn("texture index out of range");
            return -1;
        }
        const json& images = arrayOf(doc_, "images");
        const int64_t source = integerOf(textures[static_cast<size_t>(textureIndex)], "source", -1);
        if (source < 0 || static_cast<size_t>(source) >= images.size() || !images[static_cast<size_t>(source)].is_object()) {
            warn("texture without a readable image source");
            return -1;
        }
        const auto cached = imageMap_.find(static_cast<int>(source));
        if (cached != imageMap_.end()) {
            return cached->second;
        }
        int result = -1;
        budget_.check();
        try {
            const json& image = images[static_cast<size_t>(source)];
            std::vector<uint8_t> bytes;
            if (image.contains("bufferView")) {
                const json& views = arrayOf(doc_, "bufferViews");
                const int viewIndex = indexOf(image, "bufferView", views.size(), "bufferView");
                const json& v = views[static_cast<size_t>(viewIndex)];
                const int bufferIndex = indexOf(v, "buffer", buffers_.size(), "buffer");
                const std::vector<uint8_t>& buffer = buffers_[static_cast<size_t>(bufferIndex)];
                const int64_t offset = integerOf(v, "byteOffset", 0);
                const int64_t length = integerOf(v, "byteLength", -1);
                if (offset < 0 || length <= 0 || static_cast<uint64_t>(offset) + static_cast<uint64_t>(length) > buffer.size()) {
                    fail("image bufferView exceeds its buffer");
                }
                bytes.assign(buffer.begin() + offset, buffer.begin() + offset + length);
            } else if (image.contains("uri") && image["uri"].is_string()) {
                bytes = loadUri(image["uri"].get<std::string>(), limits_.maxImageBytes);
            } else {
                fail("image has no data");
            }
            const size_t remaining = limits_.maxImageBytes > imageBytes_ ? limits_.maxImageBytes - imageBytes_ : 0;
            SceneImage decoded;
            std::string error;
            if (!decodeSceneImage(bytes.data(), bytes.size(), limits_.maxImageDimension, remaining, decoded, error)) {
                fail(error);
            }
            imageBytes_ += decoded.rgba.size();
            scene().images.push_back(std::move(decoded));
            result = static_cast<int>(scene().images.size()) - 1;
        } catch (const Failure& failure) {
            warn("texture skipped: " + failure.message);
        }
        imageMap_[static_cast<int>(source)] = result;
        return result;
    }

    static float finiteOr(const json& value, float fallback) {
        if (!value.is_number()) {
            return fallback;
        }
        const float v = value.get<float>();
        return std::isfinite(v) ? v : fallback;
    }

    int materialTexture(const json& holder, const char* key, float* scale = nullptr) {
        const auto texture = holder.find(key);
        if (texture == holder.end() || !texture->is_object() || !texture->contains("index")) {
            return -1;
        }
        if (scale != nullptr) {
            *scale = std::clamp(finiteOr((*texture).value("scale", json(1.0)), 1.0f), 0.0f, 4.0f);
        }
        return loadImage(static_cast<int>(integerOf(*texture, "index", -1)));
    }

    void loadMaterials() {
        scene().shading = Shading::Pbr;
        for (const json& m : arrayOf(doc_, "materials")) {
            SceneMaterial material;
            material.metallic = 1.0f;
            material.roughness = 1.0f;
            if (m.is_object()) {
                material.doubleSided = m.value("doubleSided", false);
                const std::string alphaMode = m.value("alphaMode", std::string("OPAQUE"));
                if (alphaMode == "MASK") {
                    material.alphaMode = AlphaMode::Mask;
                    material.alphaCutoff = std::clamp(finiteOr(m.value("alphaCutoff", json(0.5)), 0.5f), 0.0f, 1.0f);
                } else if (alphaMode != "OPAQUE") {
                    warn("alpha mode " + alphaMode + " is drawn opaque");
                }
                const auto pbr = m.find("pbrMetallicRoughness");
                if (pbr != m.end() && pbr->is_object()) {
                    const auto factor = pbr->find("baseColorFactor");
                    if (factor != pbr->end() && factor->is_array() && factor->size() == 4) {
                        for (size_t i = 0; i < 4; ++i) {
                            const float v = (*factor)[i].is_number() ? (*factor)[i].get<float>() : 1.0f;
                            material.baseColor[i] = std::isfinite(v) ? std::clamp(v, 0.0f, 1.0f) : 1.0f;
                        }
                    }
                    material.baseTexture = materialTexture(*pbr, "baseColorTexture");
                    material.metallic = std::clamp(finiteOr(pbr->value("metallicFactor", json(1.0)), 1.0f), 0.0f, 1.0f);
                    material.roughness = std::clamp(finiteOr(pbr->value("roughnessFactor", json(1.0)), 1.0f), 0.0f, 1.0f);
                    material.metallicRoughnessTexture = materialTexture(*pbr, "metallicRoughnessTexture");
                }
                material.normalTexture = materialTexture(m, "normalTexture", &material.normalScale);
                material.emissiveTexture = materialTexture(m, "emissiveTexture");
                const auto emissive = m.find("emissiveFactor");
                if (emissive != m.end() && emissive->is_array() && emissive->size() == 3) {
                    for (size_t i = 0; i < 3; ++i) {
                        material.emissive[i] = std::clamp(finiteOr((*emissive)[i], 0.0f), 0.0f, 1.0f);
                    }
                }
                const auto extensions = m.find("extensions");
                if (extensions != m.end() && extensions->is_object()) {
                    const auto strength = extensions->find("KHR_materials_emissive_strength");
                    if (strength != extensions->end() && strength->is_object()) {
                        material.emissiveStrength = std::clamp(finiteOr(strength->value("emissiveStrength", json(1.0)), 1.0f), 0.0f, 100.0f);
                    }
                }
            }
            scene().materials.push_back(material);
        }
    }

    static void smoothNormals(SceneMesh& mesh) {
        std::vector<Vec3> sums(mesh.positions.size());
        for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
            const Vec3& a = mesh.positions[mesh.indices[i]];
            const Vec3& b = mesh.positions[mesh.indices[i + 1]];
            const Vec3& c = mesh.positions[mesh.indices[i + 2]];
            const Vec3 n = cross(b - a, c - a);
            for (size_t k = 0; k < 3; ++k) {
                sums[mesh.indices[i + k]] = sums[mesh.indices[i + k]] + n;
            }
        }
        mesh.normals.resize(mesh.positions.size());
        for (size_t i = 0; i < sums.size(); ++i) {
            mesh.normals[i] = normalize(sums[i]);
        }
    }

    void loadMeshes() {
        const json& meshes = arrayOf(doc_, "meshes");
        if (meshes.size() > limits_.maxMeshes) {
            fail("too many meshes");
        }
        for (const json& mesh : meshes) {
            std::vector<int> produced;
            if (!mesh.is_object()) {
                fail("mesh entry is invalid");
            }
            for (const json& primitive : arrayOf(mesh, "primitives")) {
                if (!primitive.is_object()) {
                    fail("primitive entry is invalid");
                }
                budget_.check();
                const int built = buildPrimitive(primitive);
                if (built >= 0) {
                    produced.push_back(built);
                }
            }
            meshPrimitives_.push_back(std::move(produced));
        }
        if (scene().meshes.empty()) {
            fail("the file contains no drawable triangle meshes");
        }
    }

    int buildPrimitive(const json& primitive) {
        const int64_t mode = integerOf(primitive, "mode", 4);
        if (mode != 4 && mode != 5 && mode != 6) {
            warn("primitive mode " + std::to_string(mode) + " is skipped");
            return -1;
        }
        const auto attributes = primitive.find("attributes");
        if (attributes == primitive.end() || !attributes->is_object() || !attributes->contains("POSITION")) {
            fail("primitive without POSITION");
        }
        SceneMesh mesh;
        const std::vector<float> positions = readFloats(static_cast<int>(integerOf(*attributes, "POSITION", -1)), 3);
        const size_t vertexCount = positions.size() / 3;
        vertexTotal_ += vertexCount;
        if (vertexTotal_ > limits_.maxVertices) {
            fail("vertex limit exceeded");
        }
        mesh.positions.resize(vertexCount);
        for (size_t i = 0; i < vertexCount; ++i) {
            mesh.positions[i] = {positions[i * 3], positions[i * 3 + 1], positions[i * 3 + 2]};
        }
        bool haveNormals = false;
        if (attributes->contains("NORMAL")) {
            const std::vector<float> normals = readFloats(static_cast<int>(integerOf(*attributes, "NORMAL", -1)), 3);
            if (normals.size() == positions.size()) {
                mesh.normals.resize(vertexCount);
                for (size_t i = 0; i < vertexCount; ++i) {
                    mesh.normals[i] = normalize(Vec3{normals[i * 3], normals[i * 3 + 1], normals[i * 3 + 2]});
                }
                haveNormals = true;
            }
        }
        if (attributes->contains("TEXCOORD_0")) {
            std::vector<float> uvs = readFloats(static_cast<int>(integerOf(*attributes, "TEXCOORD_0", -1)), 2);
            if (uvs.size() == vertexCount * 2) {
                mesh.uvs = std::move(uvs);
            }
        }
        if (attributes->contains("COLOR_0")) {
            const int colorIndex = static_cast<int>(integerOf(*attributes, "COLOR_0", -1));
            const int components = accessor(colorIndex).components;
            if (components == 3 || components == 4) {
                const std::vector<float> colors = readFloats(colorIndex, components);
                if (colors.size() == vertexCount * static_cast<size_t>(components)) {
                    mesh.colors.resize(vertexCount * 4);
                    for (size_t i = 0; i < vertexCount; ++i) {
                        for (int c = 0; c < 3; ++c) {
                            mesh.colors[i * 4 + static_cast<size_t>(c)] = colors[i * static_cast<size_t>(components) + static_cast<size_t>(c)];
                        }
                        mesh.colors[i * 4 + 3] = components == 4 ? colors[i * 4 + 3] : 1.0f;
                    }
                }
            }
        }
        std::vector<uint32_t> source;
        if (primitive.contains("indices")) {
            source = readIndices(static_cast<int>(integerOf(primitive, "indices", -1)));
        } else {
            source.resize(vertexCount);
            for (size_t i = 0; i < vertexCount; ++i) {
                source[i] = static_cast<uint32_t>(i);
            }
        }
        for (const uint32_t index : source) {
            if (index >= vertexCount) {
                fail("index out of range");
            }
        }
        if (mode == 4) {
            if (source.size() % 3 != 0) {
                warn("index count is not a multiple of 3, trailing indices dropped");
                source.resize(source.size() / 3 * 3);
            }
            mesh.indices = std::move(source);
        } else {
            for (size_t i = 2; i < source.size(); ++i) {
                if (mode == 5) {
                    if (i % 2 == 0) {
                        mesh.indices.insert(mesh.indices.end(), {source[i - 2], source[i - 1], source[i]});
                    } else {
                        mesh.indices.insert(mesh.indices.end(), {source[i - 1], source[i - 2], source[i]});
                    }
                } else {
                    mesh.indices.insert(mesh.indices.end(), {source[0], source[i - 1], source[i]});
                }
                if (mesh.indices.size() > limits_.maxIndices) {
                    fail("index limit exceeded");
                }
            }
        }
        indexTotal_ += mesh.indices.size();
        if (indexTotal_ > limits_.maxIndices) {
            fail("index limit exceeded");
        }
        if (mesh.indices.empty()) {
            return -1;
        }
        if (!haveNormals) {
            smoothNormals(mesh);
        }
        const int64_t material = integerOf(primitive, "material", -1);
        if (material >= static_cast<int64_t>(scene().materials.size())) {
            fail("material index out of range");
        }
        mesh.material = static_cast<int>(material);
        scene().meshes.push_back(std::move(mesh));
        return static_cast<int>(scene().meshes.size()) - 1;
    }

    static Quat quatFromMatrix(float r00, float r01, float r02, float r10, float r11, float r12, float r20, float r21, float r22) {
        Quat q;
        const float trace = r00 + r11 + r22;
        if (trace > 0.0f) {
            const float s = std::sqrt(trace + 1.0f) * 2.0f;
            q = {(r21 - r12) / s, (r02 - r20) / s, (r10 - r01) / s, 0.25f * s};
        } else if (r00 > r11 && r00 > r22) {
            const float s = std::sqrt(1.0f + r00 - r11 - r22) * 2.0f;
            q = {0.25f * s, (r01 + r10) / s, (r02 + r20) / s, (r21 - r12) / s};
        } else if (r11 > r22) {
            const float s = std::sqrt(1.0f + r11 - r00 - r22) * 2.0f;
            q = {(r01 + r10) / s, 0.25f * s, (r12 + r21) / s, (r02 - r20) / s};
        } else {
            const float s = std::sqrt(1.0f + r22 - r00 - r11) * 2.0f;
            q = {(r02 + r20) / s, (r12 + r21) / s, 0.25f * s, (r10 - r01) / s};
        }
        return normalize(q);
    }

    void applyMatrix(const json& values, SceneNode& node) {
        if (!values.is_array() || values.size() != 16) {
            fail("node matrix must have 16 numbers");
        }
        float m[16];
        for (size_t i = 0; i < 16; ++i) {
            if (!values[i].is_number()) {
                fail("node matrix must contain numbers");
            }
            m[i] = values[i].get<float>();
            if (!std::isfinite(m[i])) {
                fail("node matrix is not finite");
            }
        }
        node.translation = {m[12], m[13], m[14]};
        Vec3 c0{m[0], m[1], m[2]};
        Vec3 c1{m[4], m[5], m[6]};
        Vec3 c2{m[8], m[9], m[10]};
        float sx = length(c0);
        const float sy = length(c1);
        const float sz = length(c2);
        if (dot(c0, cross(c1, c2)) < 0.0f) {
            sx = -sx;
        }
        if (std::fabs(sx) < 1e-9f || sy < 1e-9f || sz < 1e-9f) {
            node.scale = {sx, sy, sz};
            return;
        }
        node.scale = {sx, sy, sz};
        node.rotation = quatFromMatrix(m[0] / sx, m[4] / sy, m[8] / sz, m[1] / sx, m[5] / sy, m[9] / sz, m[2] / sx, m[6] / sy, m[10] / sz);
    }

    void loadNodes() {
        const json& nodes = arrayOf(doc_, "nodes");
        if (nodes.size() > limits_.maxNodes) {
            fail("too many nodes");
        }
        std::vector<int> parent(nodes.size(), -1);
        bool skinWarned = false;
        for (size_t i = 0; i < nodes.size(); ++i) {
            const json& n = nodes[i];
            if (!n.is_object()) {
                fail("node entry is invalid");
            }
            SceneNode node;
            node.name = n.value("name", std::string());
            if (n.contains("matrix")) {
                applyMatrix(n["matrix"], node);
            } else {
                readVec(n, "translation", 3, &node.translation.x);
                readVec(n, "scale", 3, &node.scale.x);
                float q[4] = {0, 0, 0, 1};
                if (readVec(n, "rotation", 4, q)) {
                    node.rotation = normalize(Quat{q[0], q[1], q[2], q[3]});
                }
            }
            if (n.contains("mesh")) {
                const int meshIndex = indexOf(n, "mesh", meshPrimitives_.size(), "mesh");
                node.meshes = meshPrimitives_[static_cast<size_t>(meshIndex)];
            }
            if (n.contains("skin") && !skinWarned) {
                warn("skinning is not supported, meshes are drawn in bind pose");
                skinWarned = true;
            }
            for (const json& child : arrayOf(n, "children")) {
                if (!child.is_number_integer()) {
                    fail("child index must be an integer");
                }
                const int64_t c = child.get<int64_t>();
                if (c < 0 || static_cast<size_t>(c) >= nodes.size() || static_cast<size_t>(c) == i) {
                    fail("child index out of range");
                }
                if (parent[static_cast<size_t>(c)] != -1) {
                    fail("node has more than one parent");
                }
                parent[static_cast<size_t>(c)] = static_cast<int>(i);
                node.children.push_back(static_cast<int>(c));
            }
            scene().nodes.push_back(std::move(node));
        }
        const json& scenes = arrayOf(doc_, "scenes");
        if (!scenes.empty()) {
            const int64_t chosen = integerOf(doc_, "scene", 0);
            if (chosen < 0 || static_cast<size_t>(chosen) >= scenes.size() || !scenes[static_cast<size_t>(chosen)].is_object()) {
                fail("scene index out of range");
            }
            for (const json& root : arrayOf(scenes[static_cast<size_t>(chosen)], "nodes")) {
                if (!root.is_number_integer() || root.get<int64_t>() < 0 || static_cast<size_t>(root.get<int64_t>()) >= nodes.size()) {
                    fail("scene root out of range");
                }
                const int r = static_cast<int>(root.get<int64_t>());
                if (parent[static_cast<size_t>(r)] != -1) {
                    fail("scene root has a parent");
                }
                scene().roots.push_back(r);
            }
        } else {
            for (size_t i = 0; i < nodes.size(); ++i) {
                if (parent[i] == -1) {
                    scene().roots.push_back(static_cast<int>(i));
                }
            }
        }
        if (scene().roots.empty()) {
            fail("the scene has no root nodes");
        }
    }

    bool readVec(const json& object, const char* key, size_t count, float* out) {
        const auto it = object.find(key);
        if (it == object.end()) {
            return false;
        }
        if (!it->is_array() || it->size() != count) {
            fail(std::string("'") + key + "' has the wrong size");
        }
        for (size_t i = 0; i < count; ++i) {
            if (!(*it)[i].is_number()) {
                fail(std::string("'") + key + "' must contain numbers");
            }
            const float v = (*it)[i].get<float>();
            if (!std::isfinite(v)) {
                fail(std::string("'") + key + "' is not finite");
            }
            out[i] = v;
        }
        return true;
    }

    void loadAnimations() {
        size_t keyTotal = 0;
        for (const json& a : arrayOf(doc_, "animations")) {
            budget_.check();
            if (!a.is_object()) {
                fail("animation entry is invalid");
            }
            SceneAnimation animation;
            animation.name = a.value("name", std::string());
            const json& samplers = arrayOf(a, "samplers");
            for (const json& c : arrayOf(a, "channels")) {
                if (!c.is_object() || !c.contains("target") || !c["target"].is_object()) {
                    fail("animation channel is invalid");
                }
                const json& target = c["target"];
                const std::string path = target.value("path", std::string());
                if (!target.contains("node")) {
                    continue;
                }
                AnimPath animPath;
                if (path == "translation") {
                    animPath = AnimPath::Translation;
                } else if (path == "rotation") {
                    animPath = AnimPath::Rotation;
                } else if (path == "scale") {
                    animPath = AnimPath::Scale;
                } else {
                    warn("animation target '" + path + "' is not supported");
                    continue;
                }
                const int node = indexOf(target, "node", scene().nodes.size(), "animation node");
                const int samplerIndex = indexOf(c, "sampler", samplers.size(), "animation sampler");
                const json& sampler = samplers[static_cast<size_t>(samplerIndex)];
                if (!sampler.is_object()) {
                    fail("animation sampler is invalid");
                }
                SceneChannel channel;
                channel.node = node;
                channel.path = animPath;
                const std::string interpolation = sampler.value("interpolation", std::string("LINEAR"));
                channel.interp = interpolation == "STEP" ? AnimInterp::Step : AnimInterp::Linear;
                channel.times = readFloats(static_cast<int>(integerOf(sampler, "input", -1)), 1);
                const int per = animPath == AnimPath::Rotation ? 4 : 3;
                std::vector<float> values = readFloats(static_cast<int>(integerOf(sampler, "output", -1)), per);
                if (interpolation == "CUBICSPLINE") {
                    warn("cubic spline animation is approximated with linear interpolation");
                    if (values.size() != channel.times.size() * 3 * static_cast<size_t>(per)) {
                        fail("animation output size mismatch");
                    }
                    std::vector<float> reduced;
                    for (size_t k = 0; k < channel.times.size(); ++k) {
                        const size_t base = (k * 3 + 1) * static_cast<size_t>(per);
                        reduced.insert(reduced.end(), values.begin() + static_cast<long>(base), values.begin() + static_cast<long>(base) + per);
                    }
                    values = std::move(reduced);
                } else if (interpolation != "LINEAR" && interpolation != "STEP") {
                    fail("unknown animation interpolation");
                }
                if (values.size() != channel.times.size() * static_cast<size_t>(per)) {
                    fail("animation output size mismatch");
                }
                keyTotal += channel.times.size();
                if (keyTotal > limits_.maxAnimationKeys) {
                    fail("animation key limit exceeded");
                }
                channel.values = std::move(values);
                animation.duration = std::max(animation.duration, channel.times.back());
                animation.channels.push_back(std::move(channel));
            }
            if (!animation.channels.empty()) {
                scene().animations.push_back(std::move(animation));
            }
        }
    }

    const GltfLimits& limits_;
    Budget budget_;
    std::filesystem::path baseDirectory_;
    GltfResult& result_;
    json doc_;
    std::vector<std::vector<uint8_t>> buffers_;
    std::vector<std::vector<int>> meshPrimitives_;
    std::map<int, int> imageMap_;
    size_t imageBytes_ = 0;
    size_t vertexTotal_ = 0;
    size_t indexTotal_ = 0;
};

}

GltfResult loadGltfMemory(const uint8_t* data, size_t size, const std::filesystem::path& baseDirectory, const GltfLimits& limits) {
    GltfResult result;
    try {
        if (data == nullptr || size == 0) {
            fail("file is empty");
        }
        Loader loader(limits, baseDirectory, result);
        loader.run(data, size);
        result.ok = true;
    } catch (const Failure& failure) {
        result.error = failure.message;
    } catch (const Interrupted& interrupted) {
        result.error = interrupted.message;
        result.interrupted = true;
    } catch (const nlohmann::json::exception& e) {
        result.error = std::string("invalid glTF JSON: ") + e.what();
    } catch (const std::bad_alloc&) {
        result.error = "out of memory while loading";
    } catch (const std::exception& e) {
        result.error = e.what();
    }
    if (!result.ok) {
        result.scene = Scene{};
    }
    return result;
}

GltfResult loadGltfFile(const std::filesystem::path& path, const GltfLimits& limits) {
    GltfResult result;
    try {
        Budget budget;
        budget.limits = &limits;
        const std::vector<uint8_t> data = readFileLimited(path, limits.maxFileBytes, budget);
        GltfLimits remaining = limits;
        if (limits.maxSeconds > 0.0) {
            const double spent = std::chrono::duration<double>(Clock::now() - budget.begin).count();
            remaining.maxSeconds = std::max(0.001, limits.maxSeconds - spent);
        }
        result = loadGltfMemory(data.data(), data.size(), path.parent_path(), remaining);
        if (result.ok) {
            result.scene.name = path.stem().string();
        }
    } catch (const Failure& failure) {
        result.error = failure.message;
    } catch (const Interrupted& interrupted) {
        result.error = interrupted.message;
        result.interrupted = true;
    } catch (const std::bad_alloc&) {
        result.error = "out of memory while loading";
    }
    return result;
}

}
