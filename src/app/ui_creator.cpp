#include "app/app.hpp"
#include "app/creator.hpp"
#include "app/ui_common.hpp"

#include "audio/audio_features.hpp"
#include "core/log.hpp"
#include "platform/platform.hpp"
#include "scene3d/builtin_docs.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <cstring>

namespace nvj {

namespace {

constexpr size_t kUndoLimit = 64;
constexpr int kPreviewWidth = 960;
constexpr int kPreviewHeight = 540;

bool inputString(const char* label, std::string& value, size_t capacity) {
    char buffer[160];
    std::strncpy(buffer, value.c_str(), sizeof(buffer) - 1);
    buffer[sizeof(buffer) - 1] = 0;
    if (capacity > sizeof(buffer) - 1) {
        capacity = sizeof(buffer) - 1;
    }
    if (ImGui::InputText(label, buffer, capacity + 1)) {
        value = buffer;
        return true;
    }
    return false;
}

bool dragVec3(const char* label, Vec3& v, float speed, float lo, float hi, const char* format = "%.2f") {
    float values[3] = {v.x, v.y, v.z};
    if (ImGui::DragFloat3(label, values, speed, lo, hi, format)) {
        v = {values[0], values[1], values[2]};
        return true;
    }
    return false;
}

bool colorEdit(const char* label, std::array<float, 3>& color) {
    return ImGui::ColorEdit3(label, color.data(), ImGuiColorEditFlags_NoInputs);
}

bool comboList(const char* label, int& index, const char* const* items, int count) {
    bool changed = false;
    if (ImGui::BeginCombo(label, items[std::clamp(index, 0, count - 1)])) {
        for (int i = 0; i < count; ++i) {
            if (ImGui::Selectable(items[i], i == index)) {
                index = i;
                changed = true;
            }
        }
        ImGui::EndCombo();
    }
    return changed;
}

bool assetCombo(const char* label, std::string& value, const std::vector<AssetFile>& assets, bool modelsOnly, const ScenePack* pack) {
    bool changed = false;
    const std::string preview = value.empty() ? std::string(tr("(none)")) : value;
    if (ImGui::BeginCombo(label, preview.c_str())) {
        if (ImGui::Selectable(tr("(none)"), value.empty())) {
            value.clear();
            changed = true;
        }
        for (const AssetFile& asset : assets) {
            const bool usable = modelsOnly ? isModelAssetName(asset.name) : isTextureAssetName(asset.name);
            ImGui::BeginDisabled(!usable);
            if (ImGui::Selectable(asset.name.c_str(), asset.name == value)) {
                value = asset.name;
                changed = true;
            }
            ImGui::EndDisabled();
            if (!usable && modelsOnly) {
                ImGui::SetItemTooltip("%s", tr("Only .glb files can be stored in a pack"));
            }
        }
        if (pack != nullptr) {
            const auto& packed = modelsOnly ? pack->models : pack->textures;
            for (const auto& entry : packed) {
                const bool inFolder = std::any_of(assets.begin(), assets.end(), [&](const AssetFile& a) { return a.name == entry.first; });
                if (!inFolder && ImGui::Selectable((entry.first + "  (" + tr("stored in this scene") + ")").c_str(), entry.first == value)) {
                    value = entry.first;
                    changed = true;
                }
            }
        }
        ImGui::EndCombo();
    }
    return changed;
}

void setStatus(CreatorState& cs, const std::string& text) {
    cs.status = text;
    cs.statusUntil = ImGui::GetTime() + 6.0;
}

void pushUndo(CreatorState& cs, const SceneDoc& before) {
    cs.undo.push_back(before);
    while (cs.undo.size() > kUndoLimit) {
        cs.undo.pop_front();
    }
    cs.redo.clear();
}

void markChanged(CreatorState& cs) {
    cs.dirty = true;
    cs.rebuild = true;
}

void clampSelection(CreatorState& cs) {
    if (cs.selectKind == CreatorSelect::Object) {
        if (cs.doc.objects.empty()) {
            cs.selectKind = CreatorSelect::Scene;
            cs.selectIndex = 0;
        } else {
            cs.selectIndex = std::clamp(cs.selectIndex, 0, static_cast<int>(cs.doc.objects.size()) - 1);
        }
    } else if (cs.selectKind == CreatorSelect::Light) {
        if (cs.doc.lights.empty()) {
            cs.selectKind = CreatorSelect::Scene;
            cs.selectIndex = 0;
        } else {
            cs.selectIndex = std::clamp(cs.selectIndex, 0, static_cast<int>(cs.doc.lights.size()) - 1);
        }
    }
}

AssetFolders assetFolders() {
    return AssetFolders{pathFromUtf8(modelDirectory()), pathFromUtf8(texturesDirectory())};
}

void loadInto(CreatorState& cs, const SceneDoc& doc, std::shared_ptr<ScenePack> pack, const std::filesystem::path& file) {
    cs.doc = doc;
    cs.cameraSelect = std::clamp(doc.activeCamera, 0, docCameraCount(doc) - 1);
    cs.pack = pack;
    cs.file = file;
    cs.source = pack ? AssetSource(assetFolders(), pack) : AssetSource(assetFolders());
    cs.undo.clear();
    cs.redo.clear();
    cs.dirty = false;
    cs.editing = false;
    cs.selectKind = CreatorSelect::Object;
    cs.selectIndex = 0;
    cs.rebuild = true;
}

void startRebuild(CreatorState& cs) {
    const SceneDoc copy = cs.doc;
    const AssetSource source = cs.source;
    cs.pending = std::make_unique<ScenePlayer>(copy.name, [copy, source](const GltfLimits& limits) { return buildSceneFromDoc(copy, source.assets(), limits); });
    cs.pending->start();
    cs.rebuild = false;
}

void pollPreview(CreatorState& cs) {
    if (!cs.pending) {
        return;
    }
    const SceneState state = cs.pending->poll();
    if (state == SceneState::Ready) {
        cs.warnings = cs.pending->warnings();
        cs.buildError.clear();
        cs.shown = std::move(cs.pending);
    } else if (state == SceneState::Failed) {
        cs.buildError = cs.pending->error();
        cs.warnings.clear();
        cs.pending.reset();
        if (cs.doc.objects.empty()) {
            cs.shown.reset();
        }
    }
}

std::string safeStem(const std::string& name) {
    std::string stem;
    for (const char c : name) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (std::isalnum(u) || c == ' ' || c == '-' || c == '_' || c == '(' || c == ')') {
            stem.push_back(c);
        } else {
            stem.push_back('_');
        }
    }
    while (!stem.empty() && (stem.back() == ' ' || stem.back() == '.')) {
        stem.pop_back();
    }
    const auto first = stem.find_first_not_of(' ');
    stem = first == std::string::npos ? std::string() : stem.substr(first);
    if (stem.size() > 64) {
        stem.resize(64);
    }
    return stem.empty() ? std::string("Scene") : stem;
}

const char* bindTargetLabel(BindTarget target) {
    switch (target) {
    case BindTarget::Scale:
        return tr("Size");
    case BindTarget::Emissive:
        return tr("Glow");
    case BindTarget::Twist:
        return tr("Twist");
    case BindTarget::Lift:
        return tr("Lift");
    case BindTarget::Light:
        return tr("Light intensity");
    case BindTarget::Zoom:
        return tr("Zoom");
    }
    return tr("Size");
}

bool bindingEditor(std::array<DocBinding, 2>& bindings, bool light, bool camera) {
    const char* sources[] = {tr("Bass"), tr("Mid"), tr("High"), tr("Level"), tr("Kick")};
    const char* targets[] = {tr("Size"), tr("Glow"), tr("Twist"), tr("Lift")};
    bool changed = false;
    for (size_t i = 0; i < bindings.size(); ++i) {
        DocBinding& b = bindings[i];
        ImGui::PushID(static_cast<int>(i));
        changed |= ImGui::Checkbox("##on", &b.enabled);
        ImGui::SameLine();
        ImGui::BeginDisabled(!b.enabled);
        ImGui::SetNextItemWidth(70.0f);
        int source = static_cast<int>(b.source);
        if (comboList("##source", source, sources, 5)) {
            b.source = static_cast<BindSource>(source);
            changed = true;
        }
        ImGui::SameLine();
        if (light || camera) {
            ImGui::TextUnformatted(bindTargetLabel(light ? BindTarget::Light : BindTarget::Zoom));
        } else {
            ImGui::SetNextItemWidth(70.0f);
            int target = std::clamp(static_cast<int>(b.target), 0, 3);
            if (comboList("##target", target, targets, 4)) {
                b.target = static_cast<BindTarget>(target);
                changed = true;
            }
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-FLT_MIN);
        changed |= ImGui::DragFloat("##amount", &b.amount, 0.01f, -20.0f, 20.0f, "%.2f");
        ImGui::EndDisabled();
        ImGui::PopID();
    }
    return changed;
}

bool animationEditor(std::array<DocAnimation, 2>& animations) {
    const char* kinds[] = {tr("Spin"), tr("Bob"), tr("Orbit"), tr("Pulse"), tr("Sway")};
    const char* axes[] = {"X", "Y", "Z"};
    bool changed = false;
    for (size_t i = 0; i < animations.size(); ++i) {
        DocAnimation& a = animations[i];
        ImGui::PushID(static_cast<int>(i));
        changed |= ImGui::Checkbox("##on", &a.enabled);
        ImGui::SameLine();
        ImGui::BeginDisabled(!a.enabled);
        ImGui::SetNextItemWidth(-FLT_MIN);
        int kind = static_cast<int>(a.kind);
        if (comboList("##kind", kind, kinds, 5)) {
            a.kind = static_cast<MotionKind>(kind);
            if (a.kind == MotionKind::Bob) {
                a.amount = 0.25f;
            } else if (a.kind == MotionKind::Orbit) {
                a.amount = 1.0f;
            } else if (a.kind == MotionKind::Pulse) {
                a.amount = 0.2f;
            } else if (a.kind == MotionKind::Sway) {
                a.amount = 20.0f;
            }
            changed = true;
        }
        if (a.enabled) {
            ImGui::Indent(24.0f);
            ImGui::SetNextItemWidth(-FLT_MIN);
            changed |= ImGui::DragFloat("##speed", &a.speed, 0.005f, -8.0f, 8.0f, tr("Speed %.2f turns/s"));
            if (a.kind != MotionKind::Spin) {
                const char* format = a.kind == MotionKind::Bob ? tr("Height %.2f") : (a.kind == MotionKind::Orbit ? tr("Radius %.2f") : (a.kind == MotionKind::Pulse ? tr("Strength %.2f") : tr("Angle %.0f deg")));
                ImGui::SetNextItemWidth(-FLT_MIN);
                changed |= ImGui::DragFloat("##amount", &a.amount, 0.01f, -50.0f, 50.0f, format);
            }
            if (a.kind == MotionKind::Spin || a.kind == MotionKind::Sway) {
                ImGui::SetNextItemWidth(-FLT_MIN);
                changed |= comboList("##axis", a.axis, axes, 3);
            }
            ImGui::SetNextItemWidth(-FLT_MIN);
            changed |= ImGui::DragFloat("##phase", &a.phase, 0.005f, -4.0f, 4.0f, tr("Offset per copy %.3f"));
            ImGui::Unindent(24.0f);
        }
        ImGui::EndDisabled();
        ImGui::PopID();
    }
    return changed;
}

bool objectInspector(CreatorState& cs, DocObject& o) {
    bool changed = false;
    const char* repeatKinds[] = {tr("Off"), tr("Ring"), tr("Line")};
    changed |= inputString(tr("Name"), o.name, kDocMaxNameBytes);
    if (o.kind == PrimitiveKind::Model) {
        changed |= assetCombo(tr("Model"), o.model, cs.models, true, cs.pack.get());
    } else {
        ImGui::TextDisabled("%s", tr(primitiveLabel(o.kind)));
    }
    changed |= ImGui::Checkbox(tr("Visible"), &o.visible);
    ImGui::SameLine();
    changed |= ImGui::Checkbox(tr("Counts for camera framing"), &o.framing);

    if (ImGui::CollapsingHeader(tr("Transform"), ImGuiTreeNodeFlags_DefaultOpen)) {
        changed |= dragVec3(tr("Position"), o.position, 0.02f, -1000.0f, 1000.0f);
        changed |= dragVec3(tr("Rotation (degrees)"), o.rotation, 0.5f, -100000.0f, 100000.0f, "%.1f");
        changed |= dragVec3(tr("Scale"), o.scale, 0.01f, -1000.0f, 1000.0f);
        int repeat = static_cast<int>(o.repeat.kind);
        if (comboList(tr("Repeat"), repeat, repeatKinds, 3)) {
            o.repeat.kind = static_cast<RepeatKind>(repeat);
            if (o.repeat.kind != RepeatKind::None && o.repeat.count < 2) {
                o.repeat.count = 6;
            }
            changed = true;
        }
        if (o.repeat.kind != RepeatKind::None) {
            changed |= ui::sliderInt(tr("Copies"), &o.repeat.count, 2, kSceneMaxRepeat);
            if (o.repeat.kind == RepeatKind::Ring) {
                changed |= ImGui::DragFloat(tr("Ring radius"), &o.repeat.radius, 0.02f, 0.0f, 500.0f, "%.2f");
            } else {
                changed |= ImGui::DragFloat(tr("Spacing"), &o.repeat.spacing, 0.02f, -500.0f, 500.0f, "%.2f");
            }
        }
    }
    if (o.kind != PrimitiveKind::Model && ImGui::CollapsingHeader(tr("Material"), ImGuiTreeNodeFlags_DefaultOpen)) {
        DocMaterial& m = o.material;
        changed |= colorEdit(tr("Color"), m.color);
        changed |= ui::sliderFloat(tr("Metallic"), &m.metallic, 0.0f, 1.0f, "%.2f");
        changed |= ui::sliderFloat(tr("Roughness"), &m.roughness, 0.04f, 1.0f, "%.2f");
        changed |= colorEdit(tr("Glow color"), m.emissive);
        changed |= ui::sliderFloat(tr("Glow strength"), &m.emissiveStrength, 0.0f, 6.0f, "%.2f");
        changed |= assetCombo(tr("Texture"), m.texture, cs.textures, false, cs.pack.get());
        changed |= assetCombo(tr("Normal map"), m.normalTexture, cs.textures, false, cs.pack.get());
        changed |= assetCombo(tr("Glow map"), m.emissiveTexture, cs.textures, false, cs.pack.get());
        changed |= ImGui::Checkbox(tr("Follow the layer colors"), &m.followPalette);
        ImGui::SetItemTooltip("%s", tr("The object takes the colors of the layer it plays on"));
        changed |= ImGui::Checkbox(tr("Double sided"), &m.doubleSided);
    }
    if (ImGui::CollapsingHeader(tr("Animation"), ImGuiTreeNodeFlags_DefaultOpen)) {
        changed |= animationEditor(o.animations);
    }
    if (ImGui::CollapsingHeader(tr("Audio"), ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled("%s", tr("A sound band moves this object: bigger, glowing, twisting or lifting"));
        ImGui::PopTextWrapPos();
        changed |= bindingEditor(o.bindings, false, false);
    }
    return changed;
}

bool lightInspector(DocLight& l) {
    bool changed = false;
    const char* types[] = {tr("Directional"), tr("Point")};
    changed |= inputString(tr("Name"), l.name, kDocMaxNameBytes);
    int type = l.point ? 1 : 0;
    if (comboList(tr("Type"), type, types, 2)) {
        l.point = type == 1;
        changed = true;
    }
    changed |= dragVec3(l.point ? tr("Position") : tr("Direction (towards the light)"), l.position, 0.02f, -500.0f, 500.0f);
    changed |= colorEdit(tr("Color"), l.color);
    changed |= ImGui::DragFloat(tr("Intensity"), &l.intensity, 0.05f, 0.0f, 100.0f, "%.2f");
    if (ImGui::CollapsingHeader(tr("Audio"), ImGuiTreeNodeFlags_DefaultOpen)) {
        changed |= bindingEditor(l.bindings, true, false);
    }
    return changed;
}

bool sceneInspector(SceneDoc& doc, int& cameraSelect) {
    bool changed = false;
    changed |= inputString(tr("Scene name"), doc.name, kDocMaxNameBytes);
    if (ImGui::CollapsingHeader(tr("Cameras"), ImGuiTreeNodeFlags_DefaultOpen)) {
        const int count = docCameraCount(doc);
        cameraSelect = std::clamp(cameraSelect, 0, count - 1);
        for (int i = 0; i < count; ++i) {
            ImGui::PushID(i);
            const bool playing = doc.cutBeats <= 0.0f && doc.activeCamera == i;
            if (ImGui::Selectable(docCameraAt(doc, i).name.c_str(), cameraSelect == i)) {
                cameraSelect = i;
                if (doc.activeCamera != i) {
                    doc.activeCamera = i;
                    changed = true;
                }
            }
            if (playing) {
                ImGui::SameLine();
                ImGui::TextDisabled("%s", tr("playing"));
            }
            ImGui::PopID();
        }
        if (static_cast<size_t>(count) < kDocMaxCameras) {
            if (ImGui::Button(tr("New"))) {
                cameraSelect = docAddCamera(doc, DocCamera{});
                doc.activeCamera = cameraSelect;
                changed = true;
            }
            ImGui::SameLine();
            if (ImGui::Button(tr("Duplicate"))) {
                cameraSelect = docAddCamera(doc, docCameraAt(doc, cameraSelect));
                doc.activeCamera = cameraSelect;
                changed = true;
            }
            ImGui::SameLine();
        }
        ImGui::BeginDisabled(count <= 1);
        if (ImGui::Button(tr("Delete"))) {
            if (docRemoveCamera(doc, cameraSelect)) {
                cameraSelect = doc.activeCamera;
                changed = true;
            }
        }
        ImGui::EndDisabled();
        cameraSelect = std::clamp(cameraSelect, 0, docCameraCount(doc) - 1);
        ImGui::BeginDisabled(docCameraCount(doc) <= 1);
        changed |= ui::sliderFloat(tr("Cut (beats)"), &doc.cutBeats, 0.0f, 32.0f, "%.1f");
        ImGui::SetItemTooltip("%s", tr("Switches between all cameras of the scene after this many beats. 0 keeps the selected camera."));
        ImGui::EndDisabled();
        ImGui::Separator();
        DocCamera& c = docCameraAt(doc, cameraSelect);
        changed |= inputString(tr("Camera name"), c.name, kDocMaxNameBytes);
        changed |= ui::sliderFloat(tr("Distance"), &c.distance, 0.2f, 4.0f, "%.2f");
        changed |= ui::sliderFloat(tr("Height angle"), &c.pitchDegrees, -80.0f, 80.0f, "%.0f");
        changed |= ImGui::DragFloat(tr("Start angle"), &c.yawDegrees, 0.5f, -720.0f, 720.0f, "%.0f");
        changed |= ui::sliderFloat(tr("Field of view"), &c.fovDegrees, 15.0f, 110.0f, "%.0f");
        changed |= ui::sliderFloat(tr("Orbit speed"), &c.orbitSpeed, -2.0f, 2.0f, "%.2f");
        changed |= ui::sliderFloat(tr("Height sway"), &c.pitchSway, 0.0f, 30.0f, "%.0f");
        ImGui::TextDisabled("%s", tr("Audio"));
        changed |= bindingEditor(c.bindings, false, true);
    }
    if (ImGui::CollapsingHeader(tr("Sky and exposure"), ImGuiTreeNodeFlags_DefaultOpen)) {
        changed |= colorEdit(tr("Sky top"), doc.sky.top);
        changed |= colorEdit(tr("Horizon"), doc.sky.horizon);
        changed |= colorEdit(tr("Ground"), doc.sky.ground);
        changed |= ui::sliderFloat(tr("Sky brightness"), &doc.sky.intensity, 0.0f, 3.0f, "%.2f");
        changed |= ui::sliderFloat(tr("Exposure"), &doc.exposure, 0.2f, 3.0f, "%.2f");
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled("%s", tr("The sky lights the scene and is reflected by shiny objects. The background stays black."));
        ImGui::PopTextWrapPos();
    }
    return changed;
}

}

CreatorState::CreatorState() : source(AssetFolders{pathFromUtf8(modelDirectory()), pathFromUtf8(texturesDirectory())}) {
    doc = makeDefaultDoc();
}

void App::openCreator() {
    if (!creator_) {
        creator_ = std::make_unique<CreatorState>();
    }
    creatorOpen_ = true;
    creatorFocus_ = true;
    creator_->models = listModelAssets();
    creator_->textures = listTextureAssets();
    creator_->packs = listPackAssets();
}

void App::openCreatorWith(const std::string& name) {
    openCreator();
    CreatorState& cs = *creator_;
    if (name.empty()) {
        loadInto(cs, makeDefaultDoc(), nullptr, std::filesystem::path());
        return;
    }
    for (const BuiltinDocInfo& info : builtinDocs()) {
        if (name == info.name) {
            loadInto(cs, info.make(), nullptr, std::filesystem::path());
            return;
        }
    }
    for (const AssetFile& pack : cs.packs) {
        if (packClipName(pathToUtf8(pathFromUtf8(pack.path).stem())) == name) {
            ScenePack loaded;
            std::string error;
            if (readScenePackFile(pathFromUtf8(pack.path), loaded, error)) {
                const SceneDoc doc = loaded.doc;
                loadInto(cs, doc, std::make_shared<ScenePack>(std::move(loaded)), pathFromUtf8(pack.path));
            } else {
                setStatus(cs, std::string(tr("The scene could not be opened: ")) + trMessage(error));
            }
            return;
        }
    }
}

bool App::creatorSave(bool copy, bool useAsClip) {
    CreatorState& cs = *creator_;
    sanitizeDoc(cs.doc);
    std::filesystem::path path = cs.file;
    if (copy || path.empty()) {
        const std::filesystem::path folder = pathFromUtf8(clipsDirectory());
        const std::string stem = safeStem(cs.doc.name);
        path = folder / pathFromUtf8(stem + ".nyxclip");
        std::error_code ec;
        for (int n = 2; std::filesystem::exists(path, ec) && n < 1000; ++n) {
            path = folder / pathFromUtf8(stem + " " + std::to_string(n) + ".nyxclip");
        }
    }
    ScenePack pack;
    std::vector<std::string> warnings;
    cs.source.collect(cs.doc, pack, warnings);
    std::string error;
    if (!writeScenePackFile(path, pack, error)) {
        setStatus(cs, std::string(tr("Saving failed: ")) + trMessage(error));
        toast(tr("The scene could not be saved"));
        return false;
    }
    cs.file = path;
    cs.dirty = false;
    cs.pack = std::make_shared<ScenePack>(pack);
    cs.source = AssetSource(assetFolders(), cs.pack);
    rescanLibrary();
    engine_.syncLibrary();
    cs.packs = listPackAssets();
    const int clip = clipIndexByName(packClipName(pathToUtf8(path.stem())));
    if (clip >= 0) {
        engine_.reloadScene(clip);
        if (useAsClip) {
            assignClipToSelectedCell(clip);
        }
    }
    std::string message = std::string(tr("Saved: ")) + pathToUtf8(path.filename());
    if (!warnings.empty()) {
        message += "  (" + std::to_string(warnings.size()) + " " + tr("warnings") + ")";
    }
    setStatus(cs, message);
    toast(message);
    return true;
}

void App::drawCreator() {
    if (!creator_) {
        return;
    }
    CreatorState& cs = *creator_;
    if (!creatorOpen_) {
        if (cs.shown || cs.pending || cs.target.fbo) {
            cs.pending.reset();
            cs.shown.reset();
            cs.target.destroy();
            cs.rebuild = true;
        }
        return;
    }
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowSize(ImVec2(std::min(1320.0f, viewport->WorkSize.x * 0.96f), std::min(780.0f, viewport->WorkSize.y * 0.92f)), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + viewport->WorkSize.x * 0.5f, viewport->WorkPos.y + viewport->WorkSize.y * 0.5f), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
    if (creatorFocus_) {
        ImGui::SetNextWindowFocus();
        creatorFocus_ = false;
    }
    const std::string title = std::string(tr("Clip Creator")) + (cs.dirty ? " *" : "") + "###creator";
    if (!ImGui::Begin(title.c_str(), &creatorOpen_, ImGuiWindowFlags_NoDocking)) {
        ImGui::End();
        return;
    }

    if (!cs.target.fbo) {
        cs.target.create(kPreviewWidth, kPreviewHeight);
    }
    if (cs.playing) {
        cs.time += static_cast<double>(ImGui::GetIO().DeltaTime);
    }
    pollPreview(cs);
    if (cs.rebuild && !cs.pending) {
        startRebuild(cs);
    }
    clampSelection(cs);

    const auto discardAndRun = [&]() {
        const CreatorAction action = cs.action;
        const int index = cs.actionIndex;
        cs.action = CreatorAction::None;
        if (action == CreatorAction::New) {
            openCreatorWith(std::string());
        } else if (action == CreatorAction::Template && index >= 0 && index < static_cast<int>(builtinDocs().size())) {
            openCreatorWith(builtinDocs()[static_cast<size_t>(index)].name);
        } else if (action == CreatorAction::Pack && index >= 0 && index < static_cast<int>(cs.packs.size())) {
            openCreatorWith(packClipName(pathToUtf8(pathFromUtf8(cs.packs[static_cast<size_t>(index)].path).stem())));
        }
    };
    const auto request = [&](CreatorAction action, int index) {
        cs.action = action;
        cs.actionIndex = index;
        if (cs.dirty) {
            cs.confirmOpen = true;
        } else {
            discardAndRun();
        }
    };

    if (ImGui::Button(tr("New"))) {
        request(CreatorAction::New, -1);
    }
    ImGui::SameLine();
    if (ImGui::Button(tr("Open"))) {
        cs.packs = listPackAssets();
        cs.models = listModelAssets();
        cs.textures = listTextureAssets();
        ImGui::OpenPopup("creatorOpen");
    }
    if (ImGui::BeginPopup("creatorOpen")) {
        ImGui::SeparatorText(tr("Templates"));
        for (int i = 0; i < static_cast<int>(builtinDocs().size()); ++i) {
            if (ImGui::MenuItem(builtinDocs()[static_cast<size_t>(i)].name)) {
                request(CreatorAction::Template, i);
            }
        }
        ImGui::SeparatorText(tr("Your scenes"));
        if (cs.packs.empty()) {
            ImGui::TextDisabled("%s", tr("No saved scenes yet"));
        }
        for (int i = 0; i < static_cast<int>(cs.packs.size()); ++i) {
            if (ImGui::MenuItem(pathToUtf8(pathFromUtf8(cs.packs[static_cast<size_t>(i)].path).stem()).c_str())) {
                request(CreatorAction::Pack, i);
            }
        }
        ImGui::EndPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button(tr("Save"))) {
        creatorSave(false, false);
    }
    ImGui::SetItemTooltip("%s", tr("Saves the scene as a .nyxclip file in the clips folder"));
    ImGui::SameLine();
    if (ImGui::Button(tr("Save copy"))) {
        creatorSave(true, false);
    }
    ImGui::SameLine();
    if (ui::accentButton(tr("Use as clip"), true)) {
        creatorSave(false, true);
    }
    ImGui::SetItemTooltip("%s", tr("Saves the scene and puts it into the selected cell of the deck"));
    ImGui::SameLine();
    ImGui::BeginDisabled(cs.undo.empty());
    if (ImGui::Button(tr("Undo"))) {
        cs.redo.push_back(cs.doc);
        cs.doc = cs.undo.back();
        cs.undo.pop_back();
        markChanged(cs);
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(cs.redo.empty());
    if (ImGui::Button(tr("Redo"))) {
        cs.undo.push_back(cs.doc);
        cs.doc = cs.redo.back();
        cs.redo.pop_back();
        markChanged(cs);
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::TextDisabled("%s", cs.file.empty() ? tr("not saved yet") : pathToUtf8(cs.file.filename()).c_str());
    if (!cs.status.empty() && ImGui::GetTime() < cs.statusUntil) {
        ImGui::SameLine();
        ImGui::TextColored(ui::kAccent, "%s", cs.status.c_str());
    }
    if (cs.confirmOpen) {
        ImGui::OpenPopup("creatorConfirm");
        cs.confirmOpen = false;
    }
    if (ImGui::BeginPopupModal("creatorConfirm", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted(tr("The scene has unsaved changes. Discard them?"));
        if (ImGui::Button(tr("Discard"))) {
            discardAndRun();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button(tr("Cancel"))) {
            cs.action = CreatorAction::None;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    ImGui::Separator();

    const SceneDoc before = cs.doc;
    bool changed = false;
    const auto snapshot = [&]() { pushUndo(cs, cs.doc); };

    if (ImGui::BeginTable("creatorLayout", 3, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_NoSavedSettings)) {
        ImGui::TableSetupColumn("list", ImGuiTableColumnFlags_WidthFixed, 230.0f);
        ImGui::TableSetupColumn("view", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("inspector", ImGuiTableColumnFlags_WidthFixed, 340.0f);
        ImGui::TableNextRow();

        ImGui::TableSetColumnIndex(0);
        ImGui::BeginChild("creatorList", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None);
        if (ImGui::Selectable(tr("Scene, camera and sky"), cs.selectKind == CreatorSelect::Scene)) {
            cs.selectKind = CreatorSelect::Scene;
        }
        ImGui::SeparatorText(tr("Objects"));
        for (int i = 0; i < static_cast<int>(cs.doc.objects.size()); ++i) {
            const DocObject& o = cs.doc.objects[static_cast<size_t>(i)];
            ImGui::PushID(i);
            const std::string label = o.name + (o.visible ? "" : std::string("  (") + tr("hidden") + ")");
            if (ImGui::Selectable(label.c_str(), cs.selectKind == CreatorSelect::Object && cs.selectIndex == i)) {
                cs.selectKind = CreatorSelect::Object;
                cs.selectIndex = i;
            }
            ImGui::PopID();
        }
        ImGui::SeparatorText(tr("Lights"));
        for (int i = 0; i < static_cast<int>(cs.doc.lights.size()); ++i) {
            ImGui::PushID(1000 + i);
            if (ImGui::Selectable(cs.doc.lights[static_cast<size_t>(i)].name.c_str(), cs.selectKind == CreatorSelect::Light && cs.selectIndex == i)) {
                cs.selectKind = CreatorSelect::Light;
                cs.selectIndex = i;
            }
            ImGui::PopID();
        }
        ImGui::Spacing();
        ImGui::Separator();
        if (ImGui::Button(tr("Add object"), ImVec2(-FLT_MIN, 0.0f))) {
            ImGui::OpenPopup("creatorAddObject");
        }
        if (ImGui::BeginPopup("creatorAddObject")) {
            const char* labels[] = {tr("Sphere"), tr("Cube"), tr("Cylinder"), tr("Cone"), tr("Plane"), tr("Torus"), tr("Torus Knot")};
            for (int k = 0; k < 7; ++k) {
                if (ImGui::MenuItem(labels[k]) && cs.doc.objects.size() < kDocMaxObjects) {
                    snapshot();
                    DocObject object = makeDefaultObject(static_cast<PrimitiveKind>(k));
                    object.name = labels[k];
                    cs.doc.objects.push_back(object);
                    cs.selectKind = CreatorSelect::Object;
                    cs.selectIndex = static_cast<int>(cs.doc.objects.size()) - 1;
                    markChanged(cs);
                }
            }
            if (ImGui::BeginMenu(tr("Model from the library"))) {
                bool any = false;
                for (const AssetFile& asset : cs.models) {
                    if (!isModelAssetName(asset.name)) {
                        continue;
                    }
                    any = true;
                    if (ImGui::MenuItem(asset.name.c_str()) && cs.doc.objects.size() < kDocMaxObjects) {
                        snapshot();
                        DocObject object = makeDefaultObject(PrimitiveKind::Model);
                        object.model = asset.name;
                        object.name = pathToUtf8(pathFromUtf8(asset.name).stem());
                        cs.doc.objects.push_back(object);
                        cs.selectKind = CreatorSelect::Object;
                        cs.selectIndex = static_cast<int>(cs.doc.objects.size()) - 1;
                        markChanged(cs);
                    }
                }
                if (!any) {
                    ImGui::TextDisabled("%s", tr("No .glb models in the models folder"));
                }
                ImGui::Separator();
                if (ImGui::MenuItem(tr("Open models folder"))) {
                    platform::openFolder(modelDirectory());
                }
                if (ImGui::MenuItem(tr("Refresh"))) {
                    cs.models = listModelAssets();
                    cs.source.clearCache();
                }
                ImGui::EndMenu();
            }
            ImGui::EndPopup();
        }
        if (ImGui::Button(tr("Add light"), ImVec2(-FLT_MIN, 0.0f))) {
            ImGui::OpenPopup("creatorAddLight");
        }
        if (ImGui::BeginPopup("creatorAddLight")) {
            const bool full = cs.doc.lights.size() >= kDocMaxLights;
            if (ImGui::MenuItem(tr("Directional light"), nullptr, false, !full)) {
                snapshot();
                cs.doc.lights.push_back(makeDefaultLight(false));
                cs.selectKind = CreatorSelect::Light;
                cs.selectIndex = static_cast<int>(cs.doc.lights.size()) - 1;
                markChanged(cs);
            }
            if (ImGui::MenuItem(tr("Point light"), nullptr, false, !full)) {
                snapshot();
                cs.doc.lights.push_back(makeDefaultLight(true));
                cs.selectKind = CreatorSelect::Light;
                cs.selectIndex = static_cast<int>(cs.doc.lights.size()) - 1;
                markChanged(cs);
            }
            if (full) {
                ImGui::TextDisabled("%s", tr("At most 4 lights"));
            }
            ImGui::EndPopup();
        }
        const bool haveSelection = cs.selectKind != CreatorSelect::Scene;
        ImGui::BeginDisabled(!haveSelection);
        const float half = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
        if (ImGui::Button(tr("Duplicate"), ImVec2(half, 0.0f)) && haveSelection) {
            if (cs.selectKind == CreatorSelect::Object && cs.doc.objects.size() < kDocMaxObjects) {
                snapshot();
                DocObject copy = cs.doc.objects[static_cast<size_t>(cs.selectIndex)];
                copy.name += " 2";
                cs.doc.objects.insert(cs.doc.objects.begin() + cs.selectIndex + 1, copy);
                ++cs.selectIndex;
                markChanged(cs);
            } else if (cs.selectKind == CreatorSelect::Light && cs.doc.lights.size() < kDocMaxLights) {
                snapshot();
                DocLight copy = cs.doc.lights[static_cast<size_t>(cs.selectIndex)];
                copy.name += " 2";
                cs.doc.lights.insert(cs.doc.lights.begin() + cs.selectIndex + 1, copy);
                ++cs.selectIndex;
                markChanged(cs);
            }
        }
        ImGui::SameLine();
        if (ImGui::Button(tr("Delete"), ImVec2(half, 0.0f)) && haveSelection) {
            snapshot();
            if (cs.selectKind == CreatorSelect::Object) {
                cs.doc.objects.erase(cs.doc.objects.begin() + cs.selectIndex);
            } else {
                cs.doc.lights.erase(cs.doc.lights.begin() + cs.selectIndex);
            }
            markChanged(cs);
        }
        if (ImGui::Button(tr("Move up"), ImVec2(half, 0.0f)) && cs.selectKind == CreatorSelect::Object && cs.selectIndex > 0) {
            snapshot();
            std::swap(cs.doc.objects[static_cast<size_t>(cs.selectIndex)], cs.doc.objects[static_cast<size_t>(cs.selectIndex - 1)]);
            --cs.selectIndex;
            markChanged(cs);
        }
        ImGui::SameLine();
        if (ImGui::Button(tr("Move down"), ImVec2(half, 0.0f)) && cs.selectKind == CreatorSelect::Object && cs.selectIndex + 1 < static_cast<int>(cs.doc.objects.size())) {
            snapshot();
            std::swap(cs.doc.objects[static_cast<size_t>(cs.selectIndex)], cs.doc.objects[static_cast<size_t>(cs.selectIndex + 1)]);
            ++cs.selectIndex;
            markChanged(cs);
        }
        ImGui::EndDisabled();
        ImGui::EndChild();

        ImGui::TableSetColumnIndex(1);
        ImGui::BeginChild("creatorView", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None);
        const float availW = ImGui::GetContentRegionAvail().x;
        const float imageW = std::max(availW, 64.0f);
        const ImVec2 imageSize(imageW, imageW * 9.0f / 16.0f);
        ScenePlayer* shown = cs.shown.get();
        if (shown != nullptr && shown->state() == SceneState::Ready && engine_.sceneRenderer().ready()) {
            AudioFeatures audio = cs.simulateAudio ? virtualAudio(cs.time * 2.0, cs.time) : engine_.audio();
            SceneFrame frame;
            frame.time = static_cast<float>(cs.time);
            frame.beat = cs.simulateAudio ? cs.time * 2.0 : engine_.beat();
            frame.bass = audio.bass;
            frame.mid = audio.mid;
            frame.high = audio.high;
            frame.level = audio.level;
            frame.kick = audio.kick;
            frame.editor = true;
            frame.editorCamera = cs.freeCamera;
            frame.editYaw = cs.yaw;
            frame.editPitch = cs.pitch;
            frame.editDistance = cs.distance;
            const Layer& layer = engine_.comp.layers[static_cast<size_t>(std::clamp(selectedLayer_, 0, kLayerCount - 1))];
            frame.colorA = layer.colorA;
            frame.colorB = layer.colorB;
            frame.rainbow = layer.rainbow;
            engine_.sceneRenderer().draw(*shown, frame, cs.target);
        }
        const ImVec2 imagePos = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("creatorPreview", imageSize);
        ImGui::GetWindowDrawList()->AddRectFilled(imagePos, ImVec2(imagePos.x + imageSize.x, imagePos.y + imageSize.y), IM_COL32(0, 0, 0, 255));
        if (shown != nullptr && shown->state() == SceneState::Ready) {
            ImGui::GetWindowDrawList()->AddImage(ui::texId(cs.target.texture), imagePos, ImVec2(imagePos.x + imageSize.x, imagePos.y + imageSize.y), ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f), IM_COL32_WHITE);
        }
        if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f) && cs.freeCamera) {
            const ImVec2 delta = ImGui::GetIO().MouseDelta;
            cs.yaw -= delta.x * 0.01f;
            cs.pitch = std::clamp(cs.pitch + delta.y * 0.01f, -1.45f, 1.45f);
        }
        if (ImGui::IsItemHovered() && cs.freeCamera) {
            const float wheel = ImGui::GetIO().MouseWheel;
            if (wheel != 0.0f) {
                cs.distance = std::clamp(cs.distance * std::exp(-wheel * 0.1f), 0.05f, 20.0f);
            }
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", cs.freeCamera ? tr("Drag: orbit, mouse wheel: zoom") : tr("Playing with the camera of the scene. Switch on Free camera to look around."));
        }
        if (cs.shown == nullptr || cs.shown->state() != SceneState::Ready) {
            const char* text = cs.buildError.empty() ? tr("Building the preview...") : tr("The scene cannot be shown");
            const ImVec2 size = ImGui::CalcTextSize(text);
            ImGui::GetWindowDrawList()->AddText(ImVec2(imagePos.x + (imageSize.x - size.x) * 0.5f, imagePos.y + (imageSize.y - size.y) * 0.5f), IM_COL32(190, 190, 195, 255), text);
        }
        if (ImGui::Button(cs.playing ? tr("Pause") : tr("Play"))) {
            cs.playing = !cs.playing;
        }
        ImGui::SameLine();
        if (ImGui::Button(tr("Restart"))) {
            cs.time = 0.0;
        }
        ImGui::SameLine();
        ImGui::Checkbox(tr("Free camera"), &cs.freeCamera);
        ImGui::SameLine();
        ImGui::Checkbox(tr("Simulate audio"), &cs.simulateAudio);
        ImGui::SetItemTooltip("%s", tr("Plays a demo beat so you can see the audio reactions. Off: uses the audio source of the program."));
        ImGui::BeginDisabled(!cs.freeCamera);
        if (ImGui::Button(tr("Reset camera"))) {
            cs.yaw = 0.6f;
            cs.pitch = 0.32f;
            cs.distance = 1.0f;
        }
        ImGui::SameLine();
        if (ImGui::Button(tr("Use this view as the clip camera"))) {
            snapshot();
            DocCamera& target = docCameraAt(cs.doc, cs.cameraSelect);
            target.yawDegrees = cs.yaw * 57.2957795f;
            target.pitchDegrees = cs.pitch * 57.2957795f;
            target.distance = cs.distance;
            markChanged(cs);
        }
        ImGui::EndDisabled();
        if (!cs.buildError.empty()) {
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.35f, 1.0f), "%s", trMessage(cs.buildError).c_str());
            ImGui::PopTextWrapPos();
        }
        if (cs.shown) {
            const SceneStats& stats = cs.shown->stats();
            ImGui::TextDisabled("%zu %s, %zu %s, %zu KiB", stats.vertices, tr("vertices"), stats.triangles, tr("triangles"), stats.gpuBytes / 1024);
        }
        for (const std::string& warning : cs.warnings) {
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f), "%s", trMessage(warning).c_str());
            ImGui::PopTextWrapPos();
        }
        ImGui::EndChild();

        ImGui::TableSetColumnIndex(2);
        ImGui::BeginChild("creatorInspector", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None);
        if (cs.selectKind == CreatorSelect::Object && cs.selectIndex >= 0 && cs.selectIndex < static_cast<int>(cs.doc.objects.size())) {
            ImGui::PushID(cs.selectIndex);
            changed |= objectInspector(cs, cs.doc.objects[static_cast<size_t>(cs.selectIndex)]);
            ImGui::PopID();
        } else if (cs.selectKind == CreatorSelect::Light && cs.selectIndex >= 0 && cs.selectIndex < static_cast<int>(cs.doc.lights.size())) {
            ImGui::PushID(1000 + cs.selectIndex);
            changed |= lightInspector(cs.doc.lights[static_cast<size_t>(cs.selectIndex)]);
            ImGui::PopID();
        } else {
            changed |= sceneInspector(cs.doc, cs.cameraSelect);
        }
        ImGui::EndChild();
        ImGui::EndTable();
    }

    if (changed) {
        if (!cs.editing) {
            pushUndo(cs, before);
            cs.editing = true;
        }
        markChanged(cs);
    }
    if (cs.editing && !ImGui::IsAnyItemActive()) {
        cs.editing = false;
    }
    creatorHasFocus_ = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    if (creatorHasFocus_ && ImGui::GetIO().KeyCtrl && !ImGui::IsAnyItemActive()) {
        if (ImGui::IsKeyPressed(ImGuiKey_Z) && !cs.undo.empty()) {
            cs.redo.push_back(cs.doc);
            cs.doc = cs.undo.back();
            cs.undo.pop_back();
            markChanged(cs);
        } else if (ImGui::IsKeyPressed(ImGuiKey_Y) && !cs.redo.empty()) {
            cs.undo.push_back(cs.doc);
            cs.doc = cs.redo.back();
            cs.redo.pop_back();
            markChanged(cs);
        } else if (ImGui::IsKeyPressed(ImGuiKey_S)) {
            creatorSave(false, false);
        }
    }
    ImGui::End();
}

bool App::creatorUnsaved() const {
    return creator_ != nullptr && creator_->dirty;
}

std::string App::creatorDocJson() const {
    return creator_ ? docToJson(creator_->doc) : std::string();
}

void App::restoreCreatorDoc(const SceneDoc& doc) {
    openCreator();
    loadInto(*creator_, doc, nullptr, std::filesystem::path());
    creator_->dirty = true;
}

void App::shutdownCreator() {
    if (creator_) {
        creator_->pending.reset();
        creator_->shown.reset();
        creator_->target.destroy();
        creator_.reset();
    }
}

}
