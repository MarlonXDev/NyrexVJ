#include "scene3d/scene_doc.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
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
        if (light) {
            b.target = BindTarget::Light;
        } else if (camera) {
            b.target = BindTarget::Zoom;
        } else if (b.target == BindTarget::Light || b.target == BindTarget::Zoom) {
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

json bindingsToJson(const std::array<DocBinding, 2>& bindings) {
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
    for (int i = 0; i < 5; ++i) {
        if (key == sourceKey(static_cast<BindSource>(i))) {
            source = static_cast<BindSource>(i);
            return true;
        }
    }
    return false;
}

bool targetFromKey(const std::string& key, BindTarget& target) {
    for (int i = 0; i < 6; ++i) {
        if (key == targetKey(static_cast<BindTarget>(i))) {
            target = static_cast<BindTarget>(i);
            return true;
        }
    }
    return false;
}

template <size_t N>
void readBindings(const json& object, std::array<DocBinding, N>& out) {
    for (DocBinding& b : out) {
        b = DocBinding{};
    }
    const auto it = object.find("bindings");
    if (it == object.end() || !it->is_array()) {
        return;
    }
    size_t index = 0;
    for (const json& entry : *it) {
        if (index >= N) {
            break;
        }
        if (!entry.is_object()) {
            continue;
        }
        DocBinding b;
        b.enabled = true;
        std::string text;
        if (readString(entry, "source", text)) {
            sourceFromKey(text, b.source);
        }
        if (readString(entry, "target", text)) {
            targetFromKey(text, b.target);
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

}

const char* motionKey(MotionKind kind) {
    static const char* const keys[5] = {"spin", "bob", "orbit", "pulse", "sway"};
    const int i = static_cast<int>(kind);
    return i >= 0 && i < 5 ? keys[i] : "spin";
}

const char* sourceKey(BindSource source) {
    static const char* const keys[5] = {"bass", "mid", "high", "level", "kick"};
    const int i = static_cast<int>(source);
    return i >= 0 && i < 5 ? keys[i] : "bass";
}

const char* targetKey(BindTarget target) {
    static const char* const keys[6] = {"scale", "emissive", "twist", "lift", "light", "zoom"};
    const int i = static_cast<int>(target);
    return i >= 0 && i < 6 ? keys[i] : "scale";
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
    DocCamera& cam = doc.camera;
    cam.distance = clampFinite(cam.distance, 0.2f, 6.0f, 1.0f);
    cam.pitchDegrees = clampFinite(cam.pitchDegrees, -85.0f, 85.0f, 18.0f);
    cam.yawDegrees = clampFinite(cam.yawDegrees, -720.0f, 720.0f, 0.0f);
    cam.fovDegrees = clampFinite(cam.fovDegrees, 15.0f, 110.0f, 46.0f);
    cam.orbitSpeed = clampFinite(cam.orbitSpeed, -4.0f, 4.0f, 0.4f);
    cam.pitchSway = clampFinite(cam.pitchSway, 0.0f, 40.0f, 0.0f);
    sanitizeBindings(cam.bindings, false, true);
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
        for (DocAnimation& a : o.animations) {
            a.speed = clampFinite(a.speed, -8.0f, 8.0f, 0.25f);
            a.amount = clampFinite(a.amount, -50.0f, 50.0f, 1.0f);
            a.axis = std::clamp(a.axis, 0, 2);
            a.phase = clampFinite(a.phase, -4.0f, 4.0f, 0.0f);
        }
        sanitizeBindings(o.bindings, false, false);
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

std::string docToJson(const SceneDoc& source) {
    SceneDoc doc = source;
    sanitizeDoc(doc);
    json root;
    root["format"] = "nyxscene";
    root["version"] = kDocVersion;
    root["name"] = doc.name;
    root["exposure"] = doc.exposure;
    root["sky"] = {{"top", arrayOf3(doc.sky.top)}, {"horizon", arrayOf3(doc.sky.horizon)}, {"ground", arrayOf3(doc.sky.ground)}, {"intensity", doc.sky.intensity}};
    root["camera"] = {{"distance", doc.camera.distance}, {"pitch", doc.camera.pitchDegrees}, {"yaw", doc.camera.yawDegrees}, {"fov", doc.camera.fovDegrees}, {"orbitSpeed", doc.camera.orbitSpeed}, {"pitchSway", doc.camera.pitchSway}, {"bindings", bindingsToJson(doc.camera.bindings)}};
    json objects = json::array();
    for (const DocObject& o : doc.objects) {
        json entry;
        entry["name"] = o.name;
        entry["kind"] = primitiveKey(o.kind);
        if (o.kind == PrimitiveKind::Model) {
            entry["model"] = o.model;
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
        readFloat(*camera, "distance", out.camera.distance);
        readFloat(*camera, "pitch", out.camera.pitchDegrees);
        readFloat(*camera, "yaw", out.camera.yawDegrees);
        readFloat(*camera, "fov", out.camera.fovDegrees);
        readFloat(*camera, "orbitSpeed", out.camera.orbitSpeed);
        readFloat(*camera, "pitchSway", out.camera.pitchSway);
        readBindings(*camera, out.camera.bindings);
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
            readString(entry, "name", o.name);
            std::string key;
            if (readString(entry, "kind", key)) {
                primitiveFromKey(key, o.kind);
            }
            readString(entry, "model", o.model);
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
            readBindings(entry, o.bindings);
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
            readBindings(entry, l.bindings);
            out.lights.push_back(std::move(l));
        }
    }
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
