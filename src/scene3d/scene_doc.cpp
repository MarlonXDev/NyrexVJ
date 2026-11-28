#include "scene3d/scene_doc.hpp"

#include "scene3d/geometry_graph_json.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <functional>
#include <set>

namespace nvj {

namespace {

using json = nlohmann::json;

float clampFinite(float v, float lo, float hi, float fallback) {
    if (!std::isfinite(v)) {
        return fallback;
    }
    return std::clamp(v, lo, hi);
}

std::string lowerText(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

bool hasExtension(const std::string& name, std::initializer_list<const char*> extensions) {
    const std::string lower = lowerText(name);
    for (const char* extension : extensions) {
        const std::string e = extension;
        if (lower.size() > e.size() && lower.compare(lower.size() - e.size(), e.size(), e) == 0) {
            return true;
        }
    }
    return false;
}

std::string cleanName(std::string name, const std::string& fallback) {
    std::string out;
    for (const char c : name) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (u < 0x20 || u == 0x7f) {
            continue;
        }
        out.push_back(c);
    }
    while (out.size() > kDocMaxNameBytes) {
        out.pop_back();
        while (!out.empty() && (static_cast<unsigned char>(out.back()) & 0xC0) == 0x80) {
            out.pop_back();
        }
    }
    const auto first = out.find_first_not_of(' ');
    if (first == std::string::npos) {
        return fallback;
    }
    return out.substr(first);
}

void sanitizeColor(std::array<float, 3>& color) {
    for (float& c : color) {
        c = clampFinite(c, 0.0f, 1.0f, 0.5f);
    }
}

void sanitizeVec(Vec3& v, float limit) {
    v.x = clampFinite(v.x, -limit, limit, 0.0f);
    v.y = clampFinite(v.y, -limit, limit, 0.0f);
    v.z = clampFinite(v.z, -limit, limit, 0.0f);
}

float clampScale(float s) {
    if (!std::isfinite(s)) {
        return 1.0f;
    }
    const float magnitude = std::clamp(std::fabs(s), 0.001f, 1000.0f);
    return s < 0.0f ? -magnitude : magnitude;
}

template <size_t N>
void sanitizeBindings(std::array<DocBinding, N>& bindings, bool light, bool camera) {
    for (DocBinding& b : bindings) {
        b.amount = clampFinite(b.amount, -20.0f, 20.0f, 0.0f);
        b.source = static_cast<BindSource>(std::clamp(static_cast<int>(b.source), 0, kBindSourceCount - 1));
        b.target = static_cast<BindTarget>(std::clamp(static_cast<int>(b.target), 0, kBindTargetCount - 1));
        if (light) {
            b.target = BindTarget::Light;
        } else if (camera) {
            if (!bindTargetIsCamera(b.target)) {
                b.target = BindTarget::Zoom;
            }
        } else if (b.target == BindTarget::Light || bindTargetIsCamera(b.target)) {
            b.target = BindTarget::Scale;
        }
    }
}

json arrayOf3(const std::array<float, 3>& v) {
    return json::array({v[0], v[1], v[2]});
}

json vecOf(const Vec3& v) {
    return json::array({v.x, v.y, v.z});
}

json bindingsToJson(const DocBindings& bindings) {
    json out = json::array();
    for (const DocBinding& b : bindings) {
        if (b.enabled) {
            out.push_back({{"source", sourceKey(b.source)}, {"target", targetKey(b.target)}, {"amount", b.amount}});
        }
    }
    return out;
}

json animationsToJson(const std::array<DocAnimation, 2>& animations) {
    json out = json::array();
    for (const DocAnimation& a : animations) {
        if (a.enabled) {
            out.push_back({{"kind", motionKey(a.kind)}, {"speed", a.speed}, {"amount", a.amount}, {"axis", a.axis}, {"phase", a.phase}});
        }
    }
    return out;
}

bool readFloat(const json& object, const char* key, float& out) {
    const auto it = object.find(key);
    if (it == object.end() || !it->is_number()) {
        return false;
    }
    out = it->get<float>();
    return true;
}

bool readBool(const json& object, const char* key, bool& out) {
    const auto it = object.find(key);
    if (it == object.end() || !it->is_boolean()) {
        return false;
    }
    out = it->get<bool>();
    return true;
}

bool readString(const json& object, const char* key, std::string& out) {
    const auto it = object.find(key);
    if (it == object.end() || !it->is_string()) {
        return false;
    }
    out = it->get<std::string>();
    return true;
}

bool readTriple(const json& object, const char* key, float* out) {
    const auto it = object.find(key);
    if (it == object.end() || !it->is_array() || it->size() != 3) {
        return false;
    }
    for (size_t i = 0; i < 3; ++i) {
        if (!(*it)[i].is_number()) {
            return false;
        }
    }
    for (size_t i = 0; i < 3; ++i) {
        out[i] = (*it)[i].get<float>();
    }
    return true;
}

void readColor(const json& object, const char* key, std::array<float, 3>& color) {
    float v[3];
    if (readTriple(object, key, v)) {
        color = {v[0], v[1], v[2]};
    }
}

void readVec(const json& object, const char* key, Vec3& vec) {
    float v[3];
    if (readTriple(object, key, v)) {
        vec = {v[0], v[1], v[2]};
    }
}

bool motionFromKey(const std::string& key, MotionKind& kind) {
    for (int i = 0; i < 5; ++i) {
        if (key == motionKey(static_cast<MotionKind>(i))) {
            kind = static_cast<MotionKind>(i);
            return true;
        }
    }
    return false;
}

bool sourceFromKey(const std::string& key, BindSource& source) {
    for (int i = 0; i < kBindSourceCount; ++i) {
        if (key == sourceKey(static_cast<BindSource>(i))) {
            source = static_cast<BindSource>(i);
            return true;
        }
    }
    return false;
}

bool targetFromKey(const std::string& key, BindTarget& target) {
    for (int i = 0; i < kBindTargetCount; ++i) {
        if (key == targetKey(static_cast<BindTarget>(i))) {
            target = static_cast<BindTarget>(i);
            return true;
        }
    }
    return false;
}

enum class BindingOwner { Object, Light, Camera };

void addNote(std::vector<std::string>& notes, std::string text) {
    if (notes.size() < kDocMaxNotes) {
        if (text.size() > 200) {
            text.resize(200);
        }
        notes.push_back(std::move(text));
    } else if (notes.size() == kDocMaxNotes) {
        notes.push_back("more notes were left out");
    }
}

std::string shownKey(const std::string& text) {
    std::string out;
    for (const char c : text) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (u >= 0x20 && u != 0x7f) {
            out.push_back(c);
        }
        if (out.size() >= 24) {
            break;
        }
    }
    return out;
}

bool targetFitsOwner(BindingOwner owner, BindTarget target) {
    switch (owner) {
    case BindingOwner::Light:
        return target == BindTarget::Light;
    case BindingOwner::Camera:
        return bindTargetIsCamera(target);
    case BindingOwner::Object:
        return target != BindTarget::Light && !bindTargetIsCamera(target);
    }
    return false;
}

template <size_t N>
void readBindings(const json& object, std::array<DocBinding, N>& out, BindingOwner owner, const std::string& label, std::vector<std::string>& notes) {
    for (DocBinding& b : out) {
        b = DocBinding{};
    }
    const auto it = object.find("bindings");
    if (it == object.end() || !it->is_array()) {
        return;
    }
    size_t index = 0;
    size_t seen = 0;
    for (const json& entry : *it) {
        ++seen;
        const std::string where = label + ", binding " + std::to_string(seen);
        if (!entry.is_object()) {
            addNote(notes, where + " is not an object and was ignored");
            continue;
        }
        if (index >= N) {
            addNote(notes, label + " has more than " + std::to_string(N) + " bindings, the rest were ignored");
            break;
        }
        DocBinding b;
        b.enabled = true;
        std::string text;
        if (!readString(entry, "source", text) || !sourceFromKey(text, b.source)) {
            addNote(notes, where + " uses the unknown source '" + shownKey(text) + "' and was ignored");
            continue;
        }
        text.clear();
        if (!readString(entry, "target", text) || !targetFromKey(text, b.target)) {
            addNote(notes, where + " uses the unknown target '" + shownKey(text) + "' and was ignored");
            continue;
        }
        if (!targetFitsOwner(owner, b.target)) {
            addNote(notes, where + " has the target '" + shownKey(text) + "' that does not fit here and was changed to the one that does");
        }
        readFloat(entry, "amount", b.amount);
        out[index++] = b;
    }
}

void readAnimations(const json& object, std::array<DocAnimation, 2>& out) {
    for (DocAnimation& a : out) {
        a = DocAnimation{};
    }
    const auto it = object.find("animations");
    if (it == object.end() || !it->is_array()) {
        return;
    }
    size_t index = 0;
    for (const json& entry : *it) {
        if (index >= out.size()) {
            break;
        }
        if (!entry.is_object()) {
            continue;
        }
        DocAnimation a;
        a.enabled = true;
        std::string text;
        if (readString(entry, "kind", text)) {
            motionFromKey(text, a.kind);
        }
        readFloat(entry, "speed", a.speed);
        readFloat(entry, "amount", a.amount);
        readFloat(entry, "phase", a.phase);
        float axis = 1.0f;
        if (readFloat(entry, "axis", axis)) {
            a.axis = static_cast<int>(axis);
        }
        out[index++] = a;
    }
}

bool depthOk(const std::string& text, int maxDepth) {
    int depth = 0;
    bool inString = false;
    bool escape = false;
    for (const char c : text) {
        if (inString) {
            if (escape) {
                escape = false;
            } else if (c == '\\') {
                escape = true;
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

void sanitizeCamera(DocCamera& cam, const std::string& fallback) {
    cam.name = cleanName(cam.name, fallback);
    cam.distance = clampFinite(cam.distance, 0.2f, 6.0f, 1.0f);
    cam.pitchDegrees = clampFinite(cam.pitchDegrees, -85.0f, 85.0f, 18.0f);
    cam.yawDegrees = clampFinite(cam.yawDegrees, -720.0f, 720.0f, 0.0f);
    cam.fovDegrees = clampFinite(cam.fovDegrees, 15.0f, 110.0f, 46.0f);
    cam.orbitSpeed = clampFinite(cam.orbitSpeed, -4.0f, 4.0f, 0.4f);
    cam.pitchSway = clampFinite(cam.pitchSway, 0.0f, 40.0f, 0.0f);
    sanitizeVec(cam.move, kSceneMaxMove);
    cam.rollDegrees = clampFinite(cam.rollDegrees, -180.0f, 180.0f, 0.0f);
    cam.beats = clampFinite(cam.beats, 0.0f, kDocMaxCutBeats, 0.0f);
    sanitizeBindings(cam.bindings, false, true);
}

bool cameraHasMoves(const DocCamera& cam) {
    if (cam.move.x != 0.0f || cam.move.y != 0.0f || cam.move.z != 0.0f || cam.rollDegrees != 0.0f || cam.beats > 0.0f) {
        return true;
    }
    for (const DocBinding& b : cam.bindings) {
        if (b.enabled && bindTargetIsCameraMove(b.target)) {
            return true;
        }
    }
    return false;
}

const char* glideKey(CameraGlide shape) {
    switch (shape) {
    case CameraGlide::Cut:
        return "cut";
    case CameraGlide::Linear:
        return "linear";
    case CameraGlide::Smooth:
        break;
    }
    return "smooth";
}

bool glideFromKey(const std::string& key, CameraGlide& shape) {
    for (const CameraGlide candidate : {CameraGlide::Cut, CameraGlide::Linear, CameraGlide::Smooth}) {
        if (key == glideKey(candidate)) {
            shape = candidate;
            return true;
        }
    }
    return false;
}

json cameraToJson(const DocCamera& cam) {
    json out = {{"name", cam.name}, {"distance", cam.distance}, {"pitch", cam.pitchDegrees}, {"yaw", cam.yawDegrees}, {"fov", cam.fovDegrees}, {"orbitSpeed", cam.orbitSpeed}, {"pitchSway", cam.pitchSway}, {"bindings", bindingsToJson(cam.bindings)}};
    if (cam.move.x != 0.0f || cam.move.y != 0.0f || cam.move.z != 0.0f) {
        out["move"] = vecOf(cam.move);
    }
    if (cam.rollDegrees != 0.0f) {
        out["roll"] = cam.rollDegrees;
    }
    if (cam.beats > 0.0f) {
        out["beats"] = cam.beats;
    }
    return out;
}

void readCamera(const json& node, DocCamera& out, std::vector<std::string>& notes) {
    readString(node, "name", out.name);
    readFloat(node, "distance", out.distance);
    readFloat(node, "pitch", out.pitchDegrees);
    readFloat(node, "yaw", out.yawDegrees);
    readFloat(node, "fov", out.fovDegrees);
    readFloat(node, "orbitSpeed", out.orbitSpeed);
    readFloat(node, "pitchSway", out.pitchSway);
    readVec(node, "move", out.move);
    readFloat(node, "roll", out.rollDegrees);
    readFloat(node, "beats", out.beats);
    readBindings(node, out.bindings, BindingOwner::Camera, "camera '" + out.name + "'", notes);
}

}

const char* motionKey(MotionKind kind) {
    static const char* const keys[5] = {"spin", "bob", "orbit", "pulse", "sway"};
    const int i = static_cast<int>(kind);
    return i >= 0 && i < 5 ? keys[i] : "spin";
}

const char* sourceKey(BindSource source) {
    static const char* const keys[kBindSourceCount] = {"bass", "mid", "high", "level", "kick", "beat", "band1", "band2", "band3", "band4", "band5", "band6", "band7", "band8"};
    const int i = static_cast<int>(source);
    return i >= 0 && i < kBindSourceCount ? keys[i] : "bass";
}

const char* targetKey(BindTarget target) {
    static const char* const keys[kBindTargetCount] = {"scale", "emissive", "twist", "lift", "light", "zoom", "hue", "roughness", "uvx", "uvy", "inflate", "ripple", "orbit", "tilt", "roll", "fov", "movex", "movey", "movez"};
    const int i = static_cast<int>(target);
    return i >= 0 && i < kBindTargetCount ? keys[i] : "scale";
}

bool isSafeAssetName(const std::string& name) {
    if (name.empty() || name.size() > kDocMaxAssetNameBytes || name.front() == '.' || name.front() == ' ' || name.back() == ' ' || name.back() == '.') {
        return false;
    }
    if (name.find("..") != std::string::npos) {
        return false;
    }
    for (const char c : name) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (u < 0x20 || u == 0x7f || c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|') {
            return false;
        }
    }
    return true;
}

bool isModelAssetName(const std::string& name) {
    return isSafeAssetName(name) && hasExtension(name, {".glb"});
}

bool isTextureAssetName(const std::string& name) {
    return isSafeAssetName(name) && hasExtension(name, {".png", ".jpg", ".jpeg"});
}

DocObject makeDefaultObject(PrimitiveKind kind) {
    DocObject object;
    object.kind = kind;
    object.name = primitiveLabel(kind);
    object.material.color = {0.85f, 0.85f, 0.88f};
    object.material.metallic = 0.0f;
    object.material.roughness = 0.45f;
    if (kind == PrimitiveKind::Plane) {
        object.material.doubleSided = true;
        object.scale = {6.0f, 1.0f, 6.0f};
        object.position = {0.0f, -0.8f, 0.0f};
        object.framing = false;
    }
    return object;
}

DocLight makeDefaultLight(bool point) {
    DocLight light;
    light.point = point;
    light.name = point ? "Point light" : "Directional light";
    if (point) {
        light.position = {1.8f, 1.6f, 1.4f};
        light.intensity = 12.0f;
    }
    return light;
}

SceneDoc makeDefaultDoc() {
    SceneDoc doc;
    doc.name = "New scene";
    DocObject sphere = makeDefaultObject(PrimitiveKind::Sphere);
    sphere.material.color = {0.9f, 0.55f, 0.15f};
    sphere.material.metallic = 1.0f;
    sphere.material.roughness = 0.25f;
    sphere.animations[0].enabled = true;
    sphere.animations[0].kind = MotionKind::Spin;
    sphere.animations[0].speed = 0.12f;
    doc.objects.push_back(sphere);
    doc.lights.push_back(makeDefaultLight(false));
    return doc;
}

int docCameraCount(const SceneDoc& doc) {
    return 1 + static_cast<int>(doc.extraCameras.size());
}

DocCamera& docCameraAt(SceneDoc& doc, int index) {
    index = std::clamp(index, 0, docCameraCount(doc) - 1);
    return index == 0 ? doc.camera : doc.extraCameras[static_cast<size_t>(index - 1)];
}

const DocCamera& docCameraAt(const SceneDoc& doc, int index) {
    index = std::clamp(index, 0, docCameraCount(doc) - 1);
    return index == 0 ? doc.camera : doc.extraCameras[static_cast<size_t>(index - 1)];
}

int docAddCamera(SceneDoc& doc, const DocCamera& source) {
    if (static_cast<size_t>(docCameraCount(doc)) >= kDocMaxCameras) {
        return -1;
    }
    DocCamera added = source;
    added.name = "Camera " + std::to_string(docCameraCount(doc) + 1);
    doc.extraCameras.push_back(added);
    return docCameraCount(doc) - 1;
}

bool docRemoveCamera(SceneDoc& doc, int index) {
    if (docCameraCount(doc) <= 1 || index < 0 || index >= docCameraCount(doc)) {
        return false;
    }
    if (index == 0) {
        doc.camera = doc.extraCameras.front();
        doc.extraCameras.erase(doc.extraCameras.begin());
    } else {
        doc.extraCameras.erase(doc.extraCameras.begin() + (index - 1));
    }
    if (doc.activeCamera > index) {
        --doc.activeCamera;
    } else if (doc.activeCamera == index) {
        doc.activeCamera = std::min(index, docCameraCount(doc) - 1);
    }
    doc.activeCamera = std::clamp(doc.activeCamera, 0, docCameraCount(doc) - 1);
    return true;
}

namespace {

std::string idFor(size_t counter) {
    return "o" + std::to_string(counter);
}

void assignObjectIds(SceneDoc& doc) {
    std::set<std::string> used;
    for (DocObject& o : doc.objects) {
        if (!isSafeObjectId(o.id) || !used.insert(o.id).second) {
            o.id.clear();
        }
    }
    size_t counter = 1;
    for (DocObject& o : doc.objects) {
        if (!o.id.empty()) {
            continue;
        }
        std::string id;
        do {
            id = idFor(counter++);
        } while (used.count(id) != 0);
        used.insert(id);
        o.id = std::move(id);
    }
}

void repairHierarchy(SceneDoc& doc, std::vector<std::string>* notes) {
    for (DocObject& o : doc.objects) {
        if (o.parent.empty()) {
            continue;
        }
        const std::string shown = shownKey(o.parent);
        if (o.parent == o.id) {
            if (notes) {
                addNote(*notes, "object '" + o.name + "' was its own parent and was moved to the top level");
            }
            o.parent.clear();
        } else if (docObjectById(doc, o.parent) < 0) {
            if (notes) {
                addNote(*notes, "object '" + o.name + "' has the parent '" + shown + "' that does not exist and was moved to the top level");
            }
            o.parent.clear();
        }
    }
    for (size_t i = 0; i < doc.objects.size(); ++i) {
        if (doc.objects[i].parent.empty()) {
            continue;
        }
        int depth = 0;
        bool cycle = false;
        int at = static_cast<int>(i);
        while (!doc.objects[static_cast<size_t>(at)].parent.empty()) {
            at = docObjectById(doc, doc.objects[static_cast<size_t>(at)].parent);
            ++depth;
            if (at < 0) {
                break;
            }
            if (at == static_cast<int>(i) || depth > static_cast<int>(doc.objects.size())) {
                cycle = true;
                break;
            }
        }
        if (cycle || depth > kDocMaxDepth) {
            if (notes) {
                addNote(*notes, "object '" + doc.objects[i].name + "' " + (cycle ? "was part of a parent loop" : "was nested deeper than " + std::to_string(kDocMaxDepth) + " levels") + " and was moved to the top level");
            }
            doc.objects[i].parent.clear();
        }
    }
}

}

bool isSafeObjectId(const std::string& id) {
    if (id.empty() || id.size() > kDocMaxIdBytes) {
        return false;
    }
    for (const char c : id) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (!(std::isalnum(u) || c == '_' || c == '-')) {
            return false;
        }
    }
    return true;
}

int docObjectById(const SceneDoc& doc, const std::string& id) {
    if (id.empty()) {
        return -1;
    }
    for (size_t i = 0; i < doc.objects.size(); ++i) {
        if (doc.objects[i].id == id) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

std::string docNewObjectId(const SceneDoc& doc) {
    std::set<std::string> used;
    for (const DocObject& o : doc.objects) {
        used.insert(o.id);
    }
    size_t counter = doc.objects.size() + 1;
    std::string id = idFor(counter);
    while (used.count(id) != 0) {
        id = idFor(++counter);
    }
    return id;
}

int docObjectDepth(const SceneDoc& doc, int index) {
    int depth = 0;
    int at = index;
    while (at >= 0 && at < static_cast<int>(doc.objects.size()) && !doc.objects[static_cast<size_t>(at)].parent.empty() && depth <= static_cast<int>(doc.objects.size())) {
        at = docObjectById(doc, doc.objects[static_cast<size_t>(at)].parent);
        ++depth;
    }
    return depth;
}

bool docCanParent(const SceneDoc& doc, int child, int parent) {
    const int count = static_cast<int>(doc.objects.size());
    if (child < 0 || child >= count) {
        return false;
    }
    if (parent < 0) {
        return true;
    }
    if (parent >= count || parent == child) {
        return false;
    }
    int at = parent;
    int steps = 0;
    while (at >= 0 && steps <= count) {
        if (at == child) {
            return false;
        }
        at = docObjectById(doc, doc.objects[static_cast<size_t>(at)].parent);
        ++steps;
    }
    int deepest = 0;
    for (int i = 0; i < count; ++i) {
        int up = i;
        int levels = 0;
        while (up >= 0 && levels <= count) {
            if (up == child) {
                deepest = std::max(deepest, levels);
                break;
            }
            up = docObjectById(doc, doc.objects[static_cast<size_t>(up)].parent);
            ++levels;
        }
    }
    return docObjectDepth(doc, parent) + 1 + deepest <= kDocMaxDepth;
}

bool docSetParent(SceneDoc& doc, int child, int parent) {
    if (!docCanParent(doc, child, parent)) {
        return false;
    }
    DocObject& object = doc.objects[static_cast<size_t>(child)];
    if (object.id.empty()) {
        object.id = docNewObjectId(doc);
    }
    object.parent = parent < 0 ? std::string() : doc.objects[static_cast<size_t>(parent)].id;
    return true;
}

void docRemoveObject(SceneDoc& doc, int index) {
    if (index < 0 || index >= static_cast<int>(doc.objects.size())) {
        return;
    }
    const std::string id = doc.objects[static_cast<size_t>(index)].id;
    const std::string parent = doc.objects[static_cast<size_t>(index)].parent;
    for (DocObject& o : doc.objects) {
        if (!id.empty() && o.parent == id) {
            o.parent = parent;
        }
    }
    doc.objects.erase(doc.objects.begin() + index);
}

std::vector<int> docObjectOrder(const SceneDoc& doc) {
    std::vector<int> order;
    std::vector<bool> placed(doc.objects.size(), false);
    const std::function<void(int)> visit = [&](int index) {
        if (placed[static_cast<size_t>(index)]) {
            return;
        }
        placed[static_cast<size_t>(index)] = true;
        order.push_back(index);
        const std::string& id = doc.objects[static_cast<size_t>(index)].id;
        for (size_t i = 0; i < doc.objects.size(); ++i) {
            if (!placed[i] && !id.empty() && doc.objects[i].parent == id) {
                visit(static_cast<int>(i));
            }
        }
    };
    for (size_t i = 0; i < doc.objects.size(); ++i) {
        if (doc.objects[i].parent.empty()) {
            visit(static_cast<int>(i));
        }
    }
    for (size_t i = 0; i < doc.objects.size(); ++i) {
        visit(static_cast<int>(i));
    }
    return order;
}

bool docUsesHierarchy(const SceneDoc& doc) {
    for (const DocObject& o : doc.objects) {
        if (!o.parent.empty()) {
            return true;
        }
    }
    return false;
}

bool docUsesGraphs(const SceneDoc& doc) {
    if (!doc.graphs.empty()) {
        return true;
    }
    for (const DocObject& o : doc.objects) {
        if (o.kind == PrimitiveKind::Graph) {
            return true;
        }
    }
    return false;
}

int docGraphIndex(const SceneDoc& doc, const std::string& name) {
    for (size_t i = 0; i < doc.graphs.size(); ++i) {
        if (doc.graphs[i].name == name) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

std::string docNewGraphName(const SceneDoc& doc) {
    for (size_t n = 1; n < 1000; ++n) {
        const std::string name = "Graph " + std::to_string(n);
        if (docGraphIndex(doc, name) < 0) {
            return name;
        }
    }
    return "Graph";
}

int docAddGraph(SceneDoc& doc, GeometryGraph graph) {
    if (doc.graphs.size() >= kGeoMaxGraphs) {
        return -1;
    }
    sanitizeGeometryGraph(graph, nullptr);
    if (docGraphIndex(doc, graph.name) >= 0) {
        graph.name = docNewGraphName(doc);
    }
    doc.graphs.push_back(std::move(graph));
    return static_cast<int>(doc.graphs.size()) - 1;
}

int docDuplicateGraph(SceneDoc& doc, int index) {
    if (index < 0 || index >= static_cast<int>(doc.graphs.size())) {
        return -1;
    }
    GeometryGraph copy = doc.graphs[static_cast<size_t>(index)];
    copy.name = cleanName(copy.name + " copy", "Graph");
    if (docGraphIndex(doc, copy.name) >= 0) {
        copy.name = docNewGraphName(doc);
    }
    return docAddGraph(doc, std::move(copy));
}

bool docRemoveGraph(SceneDoc& doc, int index) {
    if (index < 0 || index >= static_cast<int>(doc.graphs.size())) {
        return false;
    }
    const std::string name = doc.graphs[static_cast<size_t>(index)].name;
    doc.graphs.erase(doc.graphs.begin() + index);
    for (DocObject& o : doc.objects) {
        if (o.kind == PrimitiveKind::Graph && o.graph == name) {
            o.graph.clear();
        }
    }
    return true;
}

bool docRenameGraph(SceneDoc& doc, int index, const std::string& name) {
    if (index < 0 || index >= static_cast<int>(doc.graphs.size())) {
        return false;
    }
    const std::string clean = cleanName(name, "");
    if (clean.empty()) {
        return false;
    }
    const int other = docGraphIndex(doc, clean);
    if (other >= 0 && other != index) {
        return false;
    }
    const std::string before = doc.graphs[static_cast<size_t>(index)].name;
    doc.graphs[static_cast<size_t>(index)].name = clean;
    for (DocObject& o : doc.objects) {
        if (o.kind == PrimitiveKind::Graph && o.graph == before) {
            o.graph = clean;
        }
    }
    return true;
}

void sanitizeDoc(SceneDoc& doc) {
    doc.name = cleanName(doc.name, "Untitled");
    if (doc.objects.size() > kDocMaxObjects) {
        doc.objects.resize(kDocMaxObjects);
    }
    if (doc.lights.size() > kDocMaxLights) {
        doc.lights.resize(kDocMaxLights);
    }
    doc.exposure = clampFinite(doc.exposure, 0.1f, 4.0f, 1.0f);
    sanitizeColor(doc.sky.top);
    sanitizeColor(doc.sky.horizon);
    sanitizeColor(doc.sky.ground);
    doc.sky.intensity = clampFinite(doc.sky.intensity, 0.0f, 4.0f, 1.0f);
    sanitizeCamera(doc.camera, "Camera 1");
    if (doc.extraCameras.size() + 1 > kDocMaxCameras) {
        doc.extraCameras.resize(kDocMaxCameras - 1);
    }
    for (size_t i = 0; i < doc.extraCameras.size(); ++i) {
        sanitizeCamera(doc.extraCameras[i], "Camera " + std::to_string(i + 2));
    }
    doc.activeCamera = std::clamp(doc.activeCamera, 0, docCameraCount(doc) - 1);
    doc.cutBeats = clampFinite(doc.cutBeats, 0.0f, kDocMaxCutBeats, 0.0f);
    doc.glide = clampFinite(doc.glide, 0.0f, 1.0f, 0.0f);
    doc.glideShape = static_cast<CameraGlide>(std::clamp(static_cast<int>(doc.glideShape), 0, 2));
    for (DocObject& o : doc.objects) {
        o.name = cleanName(o.name, "Object");
        sanitizeVec(o.position, 1000.0f);
        sanitizeVec(o.rotation, 100000.0f);
        o.scale = {clampScale(o.scale.x), clampScale(o.scale.y), clampScale(o.scale.z)};
        o.repeat.count = std::clamp(o.repeat.count, 1, kSceneMaxRepeat);
        o.repeat.radius = clampFinite(o.repeat.radius, 0.0f, 500.0f, 2.0f);
        o.repeat.spacing = clampFinite(o.repeat.spacing, -500.0f, 500.0f, 1.5f);
        if (o.repeat.count == 1) {
            o.repeat.kind = RepeatKind::None;
        }
        DocMaterial& m = o.material;
        sanitizeColor(m.color);
        sanitizeColor(m.emissive);
        m.metallic = clampFinite(m.metallic, 0.0f, 1.0f, 0.0f);
        m.roughness = clampFinite(m.roughness, 0.04f, 1.0f, 0.5f);
        m.emissiveStrength = clampFinite(m.emissiveStrength, 0.0f, 20.0f, 0.0f);
        if (!isTextureAssetName(m.texture)) {
            m.texture.clear();
        }
        if (!isTextureAssetName(m.normalTexture)) {
            m.normalTexture.clear();
        }
        if (!isTextureAssetName(m.emissiveTexture)) {
            m.emissiveTexture.clear();
        }
        if (o.kind == PrimitiveKind::Model) {
            if (!isModelAssetName(o.model)) {
                o.model.clear();
            }
        } else {
            o.model.clear();
        }
        if (o.kind == PrimitiveKind::Graph) {
            o.graph = cleanName(o.graph, "");
        } else {
            o.graph.clear();
        }
        for (DocAnimation& a : o.animations) {
            a.speed = clampFinite(a.speed, -8.0f, 8.0f, 0.25f);
            a.amount = clampFinite(a.amount, -50.0f, 50.0f, 1.0f);
            a.axis = std::clamp(a.axis, 0, 2);
            a.phase = clampFinite(a.phase, -4.0f, 4.0f, 0.0f);
        }
        sanitizeBindings(o.bindings, false, false);
    }
    assignObjectIds(doc);
    repairHierarchy(doc, nullptr);
    if (doc.graphs.size() > kGeoMaxGraphs) {
        doc.graphs.resize(kGeoMaxGraphs);
    }
    for (GeometryGraph& graph : doc.graphs) {
        sanitizeGeometryGraph(graph, nullptr);
    }
    for (size_t i = 0; i < doc.graphs.size(); ++i) {
        for (size_t j = 0; j < i; ++j) {
            if (doc.graphs[i].name == doc.graphs[j].name) {
                doc.graphs[i].name = cleanName(doc.graphs[i].name + " " + std::to_string(i + 1), "Graph");
                j = static_cast<size_t>(-1);
            }
        }
    }
    for (DocLight& l : doc.lights) {
        l.name = cleanName(l.name, "Light");
        sanitizeVec(l.position, 500.0f);
        if (!l.point && length(l.position) < 1e-3f) {
            l.position = {0.0f, 1.0f, 0.0f};
        }
        sanitizeColor(l.color);
        l.intensity = clampFinite(l.intensity, 0.0f, 100.0f, 3.0f);
        sanitizeBindings(l.bindings, true, false);
    }
}

namespace {

bool usesNewBindings(const DocBindings& bindings, BindingOwner owner) {
    size_t used = 0;
    for (const DocBinding& b : bindings) {
        if (!b.enabled) {
            continue;
        }
        ++used;
        if (static_cast<int>(b.source) >= kBindOldSourceCount) {
            return true;
        }
        if (owner == BindingOwner::Object && static_cast<int>(b.target) >= kBindOldTargetCount) {
            return true;
        }
    }
    return used > 2;
}

}

bool docUsesCameraMoves(const SceneDoc& source) {
    SceneDoc doc = source;
    sanitizeDoc(doc);
    bool used = doc.glide > 0.0f || cameraHasMoves(doc.camera);
    for (const DocCamera& cam : doc.extraCameras) {
        used = used || cameraHasMoves(cam);
    }
    return used;
}

DocCamera docCameraFromScene(const SceneCamera& camera, const std::string& name) {
    DocCamera out;
    out.name = name;
    out.distance = camera.distance;
    out.pitchDegrees = camera.pitchDegrees;
    out.yawDegrees = camera.yawDegrees;
    out.fovDegrees = camera.fovDegrees;
    out.orbitSpeed = camera.orbitSpeed;
    out.pitchSway = camera.pitchSway;
    out.move = camera.move;
    out.rollDegrees = camera.rollDegrees;
    out.beats = camera.beats;
    sanitizeCamera(out, "Camera");
    return out;
}

int docRequiredVersion(const SceneDoc& source) {
    SceneDoc doc = source;
    sanitizeDoc(doc);
    if (docUsesGraphs(doc)) {
        return kDocGraphVersion;
    }
    if (docUsesHierarchy(doc)) {
        return kDocHierarchyVersion;
    }
    if (docUsesCameraMoves(doc)) {
        return kDocCameraVersion;
    }
    bool newer = usesNewBindings(doc.camera.bindings, BindingOwner::Camera);
    for (const DocCamera& cam : doc.extraCameras) {
        newer = newer || usesNewBindings(cam.bindings, BindingOwner::Camera);
    }
    for (const DocObject& o : doc.objects) {
        newer = newer || usesNewBindings(o.bindings, BindingOwner::Object);
    }
    for (const DocLight& l : doc.lights) {
        newer = newer || usesNewBindings(l.bindings, BindingOwner::Light);
    }
    return newer ? kDocBindingsVersion : kDocBaseVersion;
}

std::string docToJson(const SceneDoc& source) {
    SceneDoc doc = source;
    sanitizeDoc(doc);
    json root;
    root["format"] = "nyxscene";
    root["version"] = docRequiredVersion(doc);
    root["name"] = doc.name;
    root["exposure"] = doc.exposure;
    root["sky"] = {{"top", arrayOf3(doc.sky.top)}, {"horizon", arrayOf3(doc.sky.horizon)}, {"ground", arrayOf3(doc.sky.ground)}, {"intensity", doc.sky.intensity}};
    root["camera"] = cameraToJson(doc.camera);
    if (!doc.extraCameras.empty()) {
        json cameras = json::array();
        for (const DocCamera& extra : doc.extraCameras) {
            cameras.push_back(cameraToJson(extra));
        }
        root["cameras"] = std::move(cameras);
        root["activeCamera"] = doc.activeCamera;
    }
    if (doc.cutBeats > 0.0f) {
        root["cutBeats"] = doc.cutBeats;
    }
    if (doc.glide > 0.0f) {
        root["glide"] = doc.glide;
        root["glideShape"] = glideKey(doc.glideShape);
    }
    json objects = json::array();
    const bool hierarchy = docUsesHierarchy(doc);
    for (const DocObject& o : doc.objects) {
        json entry;
        if (hierarchy) {
            entry["id"] = o.id;
            if (!o.parent.empty()) {
                entry["parent"] = o.parent;
            }
        }
        entry["name"] = o.name;
        entry["kind"] = primitiveKey(o.kind);
        if (o.kind == PrimitiveKind::Model) {
            entry["model"] = o.model;
        }
        if (o.kind == PrimitiveKind::Graph) {
            entry["graph"] = o.graph;
        }
        entry["visible"] = o.visible;
        entry["framing"] = o.framing;
        entry["position"] = vecOf(o.position);
        entry["rotation"] = vecOf(o.rotation);
        entry["scale"] = vecOf(o.scale);
        entry["repeat"] = {{"kind", o.repeat.kind == RepeatKind::Ring ? "ring" : (o.repeat.kind == RepeatKind::Line ? "line" : "none")}, {"count", o.repeat.count}, {"radius", o.repeat.radius}, {"spacing", o.repeat.spacing}};
        json material;
        material["color"] = arrayOf3(o.material.color);
        material["metallic"] = o.material.metallic;
        material["roughness"] = o.material.roughness;
        material["emissive"] = arrayOf3(o.material.emissive);
        material["emissiveStrength"] = o.material.emissiveStrength;
        material["followPalette"] = o.material.followPalette;
        material["doubleSided"] = o.material.doubleSided;
        if (!o.material.texture.empty()) {
            material["texture"] = o.material.texture;
        }
        if (!o.material.normalTexture.empty()) {
            material["normalTexture"] = o.material.normalTexture;
        }
        if (!o.material.emissiveTexture.empty()) {
            material["emissiveTexture"] = o.material.emissiveTexture;
        }
        entry["material"] = std::move(material);
        entry["animations"] = animationsToJson(o.animations);
        entry["bindings"] = bindingsToJson(o.bindings);
        objects.push_back(std::move(entry));
    }
    root["objects"] = std::move(objects);
    json lights = json::array();
    for (const DocLight& l : doc.lights) {
        lights.push_back({{"name", l.name}, {"type", l.point ? "point" : "directional"}, {"position", vecOf(l.position)}, {"color", arrayOf3(l.color)}, {"intensity", l.intensity}, {"bindings", bindingsToJson(l.bindings)}});
    }
    root["lights"] = std::move(lights);
    if (!doc.graphs.empty()) {
        json graphs = json::array();
        for (const GeometryGraph& graph : doc.graphs) {
            json entry;
            geometryGraphToJson(graph, entry);
            graphs.push_back(std::move(entry));
        }
        root["graphs"] = std::move(graphs);
    }
    return root.dump(2);
}

bool docFromJson(const std::string& text, SceneDoc& doc, std::string& error) {
    json root;
    if (!depthOk(text, 24)) {
        error = "the scene file is nested too deeply";
        return false;
    }
    try {
        root = json::parse(text, nullptr, true, false);
    } catch (const std::exception& e) {
        error = std::string("the scene file is not valid JSON: ") + e.what();
        return false;
    }
    if (!root.is_object()) {
        error = "the scene file is not a JSON object";
        return false;
    }
    std::string format;
    if (!readString(root, "format", format) || format != "nyxscene") {
        error = "the scene file has no nyxscene marker";
        return false;
    }
    float version = 0.0f;
    if (!readFloat(root, "version", version) || version < 1.0f) {
        error = "the scene file has no version";
        return false;
    }
    if (version > static_cast<float>(kDocVersion)) {
        error = "the scene was made with a newer NyrexVJ (scene version " + std::to_string(static_cast<int>(version)) + ")";
        return false;
    }
    SceneDoc out;
    readString(root, "name", out.name);
    readFloat(root, "exposure", out.exposure);
    const auto sky = root.find("sky");
    if (sky != root.end() && sky->is_object()) {
        readColor(*sky, "top", out.sky.top);
        readColor(*sky, "horizon", out.sky.horizon);
        readColor(*sky, "ground", out.sky.ground);
        readFloat(*sky, "intensity", out.sky.intensity);
    }
    const auto camera = root.find("camera");
    if (camera != root.end() && camera->is_object()) {
        readCamera(*camera, out.camera, out.notes);
    }
    const auto cameras = root.find("cameras");
    if (cameras != root.end() && cameras->is_array()) {
        for (const json& entry : *cameras) {
            if (!entry.is_object() || out.extraCameras.size() + 1 >= kDocMaxCameras) {
                continue;
            }
            DocCamera extra;
            extra.name = "Camera " + std::to_string(out.extraCameras.size() + 2);
            readCamera(entry, extra, out.notes);
            out.extraCameras.push_back(std::move(extra));
        }
        float active = 0.0f;
        if (readFloat(root, "activeCamera", active)) {
            out.activeCamera = static_cast<int>(active);
        }
    }
    readFloat(root, "cutBeats", out.cutBeats);
    readFloat(root, "glide", out.glide);
    std::string glideName;
    if (readString(root, "glideShape", glideName) && !glideFromKey(glideName, out.glideShape)) {
        addNote(out.notes, "the camera glide '" + shownKey(glideName) + "' is not known and was replaced by smooth");
    }
    const auto objects = root.find("objects");
    if (objects != root.end() && objects->is_array()) {
        for (const json& entry : *objects) {
            if (!entry.is_object()) {
                continue;
            }
            if (out.objects.size() >= kDocMaxObjects) {
                break;
            }
            DocObject o;
            readString(entry, "id", o.id);
            readString(entry, "parent", o.parent);
            readString(entry, "name", o.name);
            std::string key;
            if (readString(entry, "kind", key)) {
                primitiveFromKey(key, o.kind);
            }
            readString(entry, "model", o.model);
            readString(entry, "graph", o.graph);
            readBool(entry, "visible", o.visible);
            readBool(entry, "framing", o.framing);
            readVec(entry, "position", o.position);
            readVec(entry, "rotation", o.rotation);
            readVec(entry, "scale", o.scale);
            const auto repeat = entry.find("repeat");
            if (repeat != entry.end() && repeat->is_object()) {
                std::string kind;
                if (readString(*repeat, "kind", kind)) {
                    o.repeat.kind = kind == "ring" ? RepeatKind::Ring : (kind == "line" ? RepeatKind::Line : RepeatKind::None);
                }
                float count = 1.0f;
                if (readFloat(*repeat, "count", count)) {
                    o.repeat.count = static_cast<int>(std::clamp(count, 1.0f, 1000.0f));
                }
                readFloat(*repeat, "radius", o.repeat.radius);
                readFloat(*repeat, "spacing", o.repeat.spacing);
            }
            const auto material = entry.find("material");
            if (material != entry.end() && material->is_object()) {
                readColor(*material, "color", o.material.color);
                readFloat(*material, "metallic", o.material.metallic);
                readFloat(*material, "roughness", o.material.roughness);
                readColor(*material, "emissive", o.material.emissive);
                readFloat(*material, "emissiveStrength", o.material.emissiveStrength);
                readBool(*material, "followPalette", o.material.followPalette);
                readBool(*material, "doubleSided", o.material.doubleSided);
                readString(*material, "texture", o.material.texture);
                readString(*material, "normalTexture", o.material.normalTexture);
                readString(*material, "emissiveTexture", o.material.emissiveTexture);
            }
            readAnimations(entry, o.animations);
            readBindings(entry, o.bindings, BindingOwner::Object, "object '" + o.name + "'", out.notes);
            out.objects.push_back(std::move(o));
        }
    }
    const auto lights = root.find("lights");
    if (lights != root.end() && lights->is_array()) {
        for (const json& entry : *lights) {
            if (!entry.is_object() || out.lights.size() >= kDocMaxLights) {
                continue;
            }
            DocLight l;
            readString(entry, "name", l.name);
            std::string type;
            if (readString(entry, "type", type)) {
                l.point = type == "point";
            }
            readVec(entry, "position", l.position);
            readColor(entry, "color", l.color);
            readFloat(entry, "intensity", l.intensity);
            readBindings(entry, l.bindings, BindingOwner::Light, "light '" + l.name + "'", out.notes);
            out.lights.push_back(std::move(l));
        }
    }
    const auto graphs = root.find("graphs");
    if (graphs != root.end() && graphs->is_array()) {
        for (const json& entry : *graphs) {
            if (out.graphs.size() >= kGeoMaxGraphs) {
                addNote(out.notes, "more than " + std::to_string(kGeoMaxGraphs) + " geometry graphs, the rest were left out");
                break;
            }
            GeometryGraph graph;
            if (geometryGraphFromJson(entry, graph, &out.notes)) {
                out.graphs.push_back(std::move(graph));
            } else {
                addNote(out.notes, "a geometry graph could not be read and was left out");
            }
        }
    }
    for (DocObject& o : out.objects) {
        o.name = cleanName(o.name, "Object");
    }
    assignObjectIds(out);
    repairHierarchy(out, &out.notes);
    sanitizeDoc(out);
    doc = std::move(out);
    return true;
}

std::vector<std::string> docModelNames(const SceneDoc& doc) {
    std::set<std::string> names;
    for (const DocObject& o : doc.objects) {
        if (o.kind == PrimitiveKind::Model && !o.model.empty()) {
            names.insert(o.model);
        }
    }
    return std::vector<std::string>(names.begin(), names.end());
}

std::vector<std::string> docTextureNames(const SceneDoc& doc) {
    std::set<std::string> names;
    for (const DocObject& o : doc.objects) {
        for (const std::string* name : {&o.material.texture, &o.material.normalTexture, &o.material.emissiveTexture}) {
            if (!name->empty()) {
                names.insert(*name);
            }
        }
    }
    return std::vector<std::string>(names.begin(), names.end());
}

}
