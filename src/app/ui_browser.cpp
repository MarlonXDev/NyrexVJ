#include "app/app.hpp"

#include "app/ui_common.hpp"

#include "core/node_graph.hpp"
#include "core/log.hpp"
#include "core/novelty.hpp"
#include "core/preset.hpp"
#include "media/media_image.hpp"
#include "media/video_decoder.hpp"
#include "platform/platform.hpp"
#include "render/clip_library.hpp"
#include "render/effect_library.hpp"
#include "scene3d/builtin_docs.hpp"
#include "scene3d/scene_pack.hpp"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>
#include <system_error>

namespace nvj {

void App::drawPresetsTab() {
    ImGui::TextDisabled("%s", tr("Factory looks"));
    ImGui::SetItemTooltip("%s", tr("A click loads the look into the current deck. Right click for more."));
    const std::vector<Composition>& factory = factoryPresets();
    for (int i = 0; i < static_cast<int>(factory.size()); ++i) {
        const Composition& preset = factory[static_cast<size_t>(i)];
        ImGui::PushID(i);
        const ImVec2 rowStart = ImGui::GetCursorScreenPos();
        if (ImGui::Selectable(preset.name.c_str(), engine_.comp.deckNames[static_cast<size_t>(engine_.comp.activeDeck)] == preset.name)) {
            fillDeckFromLook(engine_.comp.activeDeck, preset);
            toast(std::string(tr("Loaded ")) + preset.name);
        }
        if (isNewPreset(preset.name)) {
            ui::newTag(rowStart, preset.name.c_str(), ImGui::GetTextLineHeight());
        }
        if (ImGui::BeginPopupContextItem("factorymenu")) {
            if (ImGui::MenuItem(tr("Load into current deck"))) {
                fillDeckFromLook(engine_.comp.activeDeck, preset);
                toast(std::string(tr("Loaded ")) + preset.name);
            }
            if (ImGui::BeginMenu(tr("Load into deck"))) {
                for (int d = 0; d < kDeckCount; ++d) {
                    const std::string label = std::string(tr("Deck")) + " " + std::to_string(d + 1);
                    if (ImGui::MenuItem(label.c_str(), nullptr, d == engine_.comp.activeDeck)) {
                        fillDeckFromLook(d, preset);
                        toast(std::string(tr("Loaded ")) + preset.name);
                    }
                }
                ImGui::EndMenu();
            }
            ImGui::EndPopup();
        }
        ImGui::PopID();
    }
    if (ImGui::Button(trId("Fill the decks with factory looks", "presetBank"))) {
        loadFactoryBank();
    }
    ImGui::SetItemTooltip("%s", tr("Puts 16 factory looks into the 16 decks."));

    ImGui::Spacing();
    ImGui::TextDisabled("%s", tr("My presets"));
    if (userPresets_.empty()) {
        ImGui::TextDisabled("%s", tr("Nothing saved yet"));
    }
    std::string toDelete;
    for (int i = 0; i < static_cast<int>(userPresets_.size()); ++i) {
        const std::string& name = userPresets_[static_cast<size_t>(i)];
        ImGui::PushID(1000 + i);
        if (ImGui::Selectable(name.c_str(), selectedUserPreset_ == name)) {
            Composition loaded;
            std::string error;
            if (loadUserPreset(name, loaded, error)) {
                selectedUserPreset_ = name;
                loadComposition(loaded, std::string(tr("Loaded ")) + name);
            } else {
                toast(trMessage(error));
            }
        }
        if (ImGui::BeginPopupContextItem("presetmenu")) {
            if (ImGui::MenuItem(tr("Overwrite with current"))) {
                std::snprintf(presetName_, sizeof(presetName_), "%s", name.c_str());
                saveCurrentPreset();
            }
            if (ImGui::MenuItem(tr("Delete"))) {
                toDelete = name;
            }
            ImGui::EndPopup();
        }
        ImGui::PopID();
    }
    if (!toDelete.empty()) {
        deleteUserPreset(toDelete);
        if (selectedUserPreset_ == toDelete) {
            selectedUserPreset_.clear();
        }
        refreshUserPresets();
        toast(std::string(tr("Deleted ")) + toDelete);
    }

    ImGui::SeparatorText(tr("Save current look"));
    const char* saveLabel = tr("Save");
    const float saveWidth = ImGui::CalcTextSize(saveLabel).x + ImGui::GetStyle().FramePadding.x * 2.0f + ImGui::GetStyle().ItemSpacing.x + 4.0f;
    ImGui::SetNextItemWidth(-saveWidth);
    ImGui::InputText("##presetname", presetName_, sizeof(presetName_));
    ImGui::SameLine();
    if (ImGui::Button(trId("Save", "presetSave"))) {
        saveCurrentPreset();
    }
}

namespace {

std::string lowerText(const std::string& text) {
    std::string result = text;
    std::transform(result.begin(), result.end(), result.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return result;
}

bool matchesFilter(const std::string& name, const char* filter) {
    if (!filter || !*filter) {
        return true;
    }
    return lowerText(name).find(lowerText(filter)) != std::string::npos;
}

bool canEditInCreator(const ClipDef& clip) {
    if (clip.kind == ClipKind::ScenePack) {
        return true;
    }
    if (clip.kind == ClipKind::BuiltinScene) {
        for (const BuiltinDocInfo& info : builtinDocs()) {
            if (clip.name == info.name) {
                return true;
            }
        }
    }
    return false;
}

std::string formatBytes(unsigned long long bytes) {
    char buffer[48];
    if (bytes >= 1024ull * 1024ull) {
        std::snprintf(buffer, sizeof(buffer), "%.1f MiB", static_cast<double>(bytes) / (1024.0 * 1024.0));
    } else if (bytes >= 1024ull) {
        std::snprintf(buffer, sizeof(buffer), "%.0f KiB", static_cast<double>(bytes) / 1024.0);
    } else {
        std::snprintf(buffer, sizeof(buffer), "%llu B", bytes);
    }
    return buffer;
}

bool matchesKind(const ClipDef& clip, int kind) {
    switch (kind) {
    case 1:
        return !clip.is3d;
    case 2:
        return clip.is3d;
    case 3:
        return clip.kind == ClipKind::ModelFile;
    case 4:
        return clip.kind == ClipKind::ScenePack;
    case 5:
        return clip.kind == ClipKind::SpoutSource;
    case 6:
        return clip.isMedia();
    case 7:
        return clip.isShaderFile();
    case 8:
        return clip.isLive();
    case 9:
        return clip.isGenerator();
    default:
        return true;
    }
}

}

void App::drawSceneDetails(const ScenePlayer& player) {
    switch (player.state()) {
    case SceneState::Idle:
        ImGui::TextDisabled("%s", tr("Not loaded yet"));
        break;
    case SceneState::Loading:
        ImGui::TextDisabled("%s", tr("Loading..."));
        break;
    case SceneState::Failed:
        ImGui::Text("%s: %s", tr("Load failed"), player.error().c_str());
        break;
    case SceneState::Ready:
        ImGui::Text("%s: %zu", tr("Vertices"), player.stats().vertices);
        ImGui::Text("%s: %zu", tr("Triangles"), player.stats().triangles);
        if (player.stats().skins > 0) {
            ImGui::Text("%s: %zu (%zu %s)", tr("Skins"), player.stats().skins, player.stats().joints, tr("Joints"));
        }
        if (player.stats().morphTargets > 0) {
            ImGui::Text("%s: %zu", tr("Morph targets"), player.stats().morphTargets);
        }
        if (player.stats().blendMaterials > 0) {
            ImGui::Text("%s: %zu", tr("Blended materials"), player.stats().blendMaterials);
        }
        if (!player.scene().fileLights.empty()) {
            ImGui::TextDisabled("%s: %zu", tr("Lights in the file (not used)"), player.scene().fileLights.size());
        }
        if (!player.scene().fileCameras.empty()) {
            ImGui::TextDisabled("%s: %zu", tr("Cameras in the file (not used)"), player.scene().fileCameras.size());
        }
        ImGui::Text("%s: %.2f MB", tr("GPU memory"), static_cast<double>(player.stats().gpuBytes) / (1024.0 * 1024.0));
        ImGui::Text("%s: %.0f ms", tr("Load time"), player.loadMilliseconds());
        break;
    }
}

void App::drawMediaDetails(const MediaPlayer& player, bool tooltip) {
    switch (player.state()) {
    case MediaState::Idle:
        ImGui::TextDisabled("%s", tr("Not loaded yet"));
        break;
    case MediaState::Loading:
        ImGui::TextDisabled("%s", tr("Loading..."));
        break;
    case MediaState::Failed:
        ImGui::PushTextWrapPos(tooltip ? ImGui::GetFontSize() * 26.0f : 0.0f);
        ImGui::Text("%s: %s", tr("Load failed"), player.error().c_str());
        ImGui::PopTextWrapPos();
        break;
    case MediaState::Ready: {
        ImGui::Text("%s: %d x %d", tr("Size"), player.width(), player.height());
        if (player.isVideo()) {
            const VideoInfo& info = player.videoInfo();
            if (info.width != info.decodedWidth || info.height != info.decodedHeight) {
                ImGui::Text("%s: %d x %d", tr("Original size"), info.width, info.height);
            }
            ImGui::Text("%s: %.2f", tr("Frame rate"), info.fps);
            ImGui::Text("%s: %.1f s", tr("Duration"), info.duration);
            ImGui::Text("%s: %s", tr("Codec"), info.codec.c_str());
            if (info.hasAlpha) {
                ImGui::Text("%s", tr("Has transparency"));
            }
            const VideoStatus status = player.status();
            if (status.path != DecodePath::None) {
                const bool hap = status.path == DecodePath::HapGpu || status.path == DecodePath::HapCpu;
                std::string decoder = status.decoder;
                if (status.path == DecodePath::Hardware && !status.backend.empty()) {
                    decoder += "  (" + hardwareBackendLabel(status.backend) + ")";
                }
                if (hap && !status.variant.empty()) {
                    decoder = status.variant + "  -  " + decoder;
                }
                ImGui::Text("%s: %s", tr("Decoder"), decoder.c_str());
                const char* path = tr("Software (processor)");
                if (status.path == DecodePath::Hardware) {
                    path = tr("Hardware (graphics card)");
                } else if (status.path == DecodePath::HapGpu) {
                    path = tr("HAP, compressed upload");
                } else if (status.path == DecodePath::HapCpu) {
                    path = tr("HAP, processor fallback");
                }
                ImGui::Text("%s: %s", tr("Decoding"), path);
                if (!status.transfer.empty()) {
                    ImGui::PushTextWrapPos(tooltip ? ImGui::GetFontSize() * 26.0f : 0.0f);
                    ImGui::TextDisabled("%s (%s): %s", tr("HDR video"), status.transfer.c_str(), tr("Shown without tone mapping, so colours look flat."));
                    ImGui::PopTextWrapPos();
                }
                if (!status.fallbackReason.empty()) {
                    ImGui::PushTextWrapPos(tooltip ? ImGui::GetFontSize() * 26.0f : 0.0f);
                    ImGui::TextDisabled("%s: %s", tr("Why"), trMessage(status.fallbackReason).c_str());
                    ImGui::PopTextWrapPos();
                    if (!tooltip && !status.fallbackDetail.empty() && ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("%s", status.fallbackDetail.c_str());
                    }
                }
            }
        }
        ImGui::Text("%s: %.2f MB", tr("GPU memory"), static_cast<double>(player.gpuBytes()) / (1024.0 * 1024.0));
        break;
    }
    }
}

void App::drawMediaControls(MediaPlayer& player) {
    ImGui::SeparatorText(player.isVideo() ? tr("Video") : tr("Image"));
    drawMediaDetails(player, false);
    if (player.state() == MediaState::Failed) {
        if (player.isVideo()) {
            const VideoSupport support = videoSupport();
            if (!support.compiled) {
                ImGui::PushTextWrapPos(0.0f);
                ImGui::TextDisabled("%s", tr("This build of NyrexVJ was made without video support."));
                ImGui::PopTextWrapPos();
            } else if (!support.loaded) {
                ImGui::Spacing();
                ImGui::PushTextWrapPos(0.0f);
                ImGui::TextColored(ui::kAccent, "%s", tr("Video clips need the FFmpeg libraries"));
                ImGui::TextDisabled("%s", tr("Get a shared FFmpeg 8.1 build (LGPL) and copy these files into the folder below:"));
                for (const std::string& file : support.files) {
                    ImGui::BulletText("%s", file.c_str());
                }
                ImGui::TextDisabled("%s", support.folder.c_str());
                ImGui::PopTextWrapPos();
                if (ui::accentButton(trId("Set up FFmpeg", "mediaSetupFfmpeg"), true)) {
                    openSettings(SettingsPerformance);
                }
                ImGui::SameLine();
                if (ImGui::SmallButton(tr("Open FFmpeg folder"))) {
                    std::error_code ec;
                    std::filesystem::create_directories(pathFromUtf8(support.folder), ec);
                    platform::openFolder(support.folder);
                }
                ImGui::SameLine();
                if (ImGui::SmallButton(tr("Check again"))) {
                    retryVideoSupport();
                    player.retry();
                }
                return;
            }
        }
        if (ImGui::SmallButton(trId("Try again", "mediaRetry"))) {
            player.retry();
        }
    }
}

namespace {

std::string timecodeText(double seconds) {
    const double clamped = std::max(seconds, 0.0);
    const int minutes = static_cast<int>(clamped / 60.0);
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%02d:%05.2f", minutes, clamped - minutes * 60.0);
    return buffer;
}

}

void App::drawPlaybackControls(MediaPlayer& player, ClipPlayback& playback) {
    if (!player.controllable()) {
        return;
    }
    const double duration = player.videoInfo().duration;
    const double period = player.framePeriod();
    const double position = player.position();
    ImGui::SeparatorText(tr("Playback"));
    ImGui::Text("%s / %s", timecodeText(position).c_str(), timecodeText(duration).c_str());
    ImGui::SameLine();
    ImGui::TextDisabled(tr("Frame %d"), static_cast<int>(std::lround(position / period)));

    float scrub = static_cast<float>(position);
    ImGui::SetNextItemWidth(-1.0f);
    if (ui::sliderFloat("##scrub", &scrub, 0.0f, static_cast<float>(duration), "")) {
        player.scrubTo(static_cast<double>(scrub));
    }
    if (ImGui::ArrowButton("##playStepBack", ImGuiDir_Left)) {
        player.stepFrames(-1);
    }
    ImGui::SetItemTooltip("%s", tr("Step back one frame"));
    ImGui::SameLine();
    if (ImGui::ArrowButton("##playStepForward", ImGuiDir_Right)) {
        player.stepFrames(1);
    }
    ImGui::SetItemTooltip("%s", tr("Step forward one frame"));
    ImGui::SameLine();
    if (ImGui::SmallButton(playback.speed == 0.0f ? trId("Play", "playPlay") : trId("Pause", "playPause"))) {
        playback.speed = playback.speed == 0.0f ? 1.0f : 0.0f;
    }

    const double zero = 0.0;
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.55f);
    ImGui::DragScalar(trId("In point", "playIn"), ImGuiDataType_Double, &playback.in, 0.01f, &zero, &duration, "%.2f s");
    ImGui::SameLine();
    if (ImGui::SmallButton(trId("Set here", "playSetIn"))) {
        playback.in = position;
    }
    ImGui::SetItemTooltip("%s", tr("Set from position"));
    double outShown = playback.out > 0.0 ? std::min(playback.out, duration) : duration;
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.55f);
    if (ImGui::DragScalar(trId("Out point", "playOut"), ImGuiDataType_Double, &outShown, 0.01f, &zero, &duration, "%.2f s")) {
        playback.out = outShown >= duration - period * 0.01 ? 0.0 : outShown;
    }
    ImGui::SameLine();
    if (ImGui::SmallButton(trId("Set here", "playSetOut"))) {
        playback.out = position >= duration - period * 0.01 ? 0.0 : position;
    }
    ImGui::SetItemTooltip("%s", tr("Set from position"));
    playback.in = std::clamp(playback.in, 0.0, std::max(duration - period, 0.0));
    if (playback.out > 0.0 && playback.out < playback.in + period) {
        playback.out = std::min(playback.in + period, duration);
    }

    const char* loops[kLoopModeCount] = {tr("Loop"), tr("Ping-pong"), tr("Once, hold last frame"), tr("Once, then disappear")};
    int loop = static_cast<int>(playback.loop);
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.55f);
    if (ImGui::Combo(trId("Loop mode", "playLoop"), &loop, loops, kLoopModeCount)) {
        playback.loop = static_cast<LoopMode>(std::clamp(loop, 0, kLoopModeCount - 1));
    }

    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.55f);
    ui::sliderFloat(trId("Clip speed", "playSpeed"), &playback.speed, kMinPlaySpeed, kMaxPlaySpeed, "%.2fx");
    ImGui::SetItemTooltip("%s", tr("Negative values play the clip backwards. Backwards playback decodes short pieces in advance, videos with long keyframe intervals need more processor time and memory for it. A clip that runs in several layers shares one decoder."));
    ImGui::SameLine();
    if (ImGui::SmallButton(trId("Reverse", "playReverse"))) {
        playback.speed = playback.speed == 0.0f ? -1.0f : -playback.speed;
    }

    std::array<std::string, kSyncBeatChoices> beatLabels;
    for (int i = 0; i < kSyncBeatChoices; ++i) {
        const float beats = syncBeatsChoice(i);
        char text[24];
        std::snprintf(text, sizeof(text), "%g", beats);
        beatLabels[static_cast<size_t>(i)] = i == 0 ? std::string(tr("Off")) : std::string(text);
    }
    const int beatIndex = syncBeatsIndex(playback.syncBeats);
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.55f);
    if (ImGui::BeginCombo(trId("Clip length in beats", "playBeats"), beatLabels[static_cast<size_t>(beatIndex)].c_str())) {
        for (int i = 0; i < kSyncBeatChoices; ++i) {
            if (ImGui::Selectable(beatLabels[static_cast<size_t>(i)].c_str(), i == beatIndex)) {
                playback.syncBeats = syncBeatsChoice(i);
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SetItemTooltip("%s", tr("Plays the range from In to Out in exactly this many beats and follows the tempo of the deck. Off plays the clip at its own speed."));

    if (!playback.isDefault() && ImGui::SmallButton(trId("Reset playback", "playReset"))) {
        playback = ClipPlayback{};
    }
}

void App::assignClipToSelectedCell(int clip) {
    const std::vector<ClipDef>& library = clipLibrary();
    if (clip < 0 || clip >= static_cast<int>(library.size())) {
        return;
    }
    selectedLayer_ = std::clamp(selectedLayer_, 0, kLayerCount - 1);
    selectedColumn_ = std::clamp(selectedColumn_, 0, kColumnCount - 1);
    engine_.comp.layers[static_cast<size_t>(selectedLayer_)].setCell(static_cast<size_t>(selectedColumn_), clip);
    engine_.trigger(selectedLayer_, selectedColumn_);
    toast(library[static_cast<size_t>(clip)].name + "  >  " + ui::cellLabel(selectedLayer_, selectedColumn_));
}

void App::assignClipToFreeCell(int clip, int layer) {
    const std::vector<ClipDef>& library = clipLibrary();
    if (clip < 0 || clip >= static_cast<int>(library.size()) || layer < 0 || layer >= kLayerCount) {
        return;
    }
    Layer& target = engine_.comp.layers[static_cast<size_t>(layer)];
    for (int c = 0; c < kColumnCount; ++c) {
        if (target.cells[static_cast<size_t>(c)] < 0) {
            target.setCell(static_cast<size_t>(c), clip);
            selectedLayer_ = layer;
            selectedColumn_ = c;
            engine_.trigger(layer, c);
            toast(library[static_cast<size_t>(clip)].name + "  >  " + ui::cellLabel(layer, c));
            return;
        }
    }
    toast(tr("This layer has no free cell"));
}

void App::addEffectToLayer(int effect, int layer) {
    const std::vector<EffectDef>& library = effectLibrary();
    if (effect < 0 || effect >= static_cast<int>(library.size()) || layer < 0 || layer >= kLayerCount) {
        return;
    }
    Layer& target = engine_.comp.layers[static_cast<size_t>(layer)];
    for (EffectSlot& slot : target.fx) {
        if (slot.effect < 0) {
            setEffect(slot, effect);
            selectedLayer_ = layer;
            toast(library[static_cast<size_t>(effect)].name + "  >  " + tr("Layer ") + std::to_string(layer + 1));
            return;
        }
    }
    toast(tr("All effect slots are used"));
}

void App::addEffectToMaster(int effect) {
    const std::vector<EffectDef>& library = effectLibrary();
    if (effect < 0 || effect >= static_cast<int>(library.size())) {
        return;
    }
    for (EffectSlot& slot : engine_.comp.master.fx) {
        if (slot.effect < 0) {
            setEffect(slot, effect);
            toast(library[static_cast<size_t>(effect)].name + "  >  " + tr("Master"));
            return;
        }
    }
    toast(tr("All effect slots are used"));
}

void App::drawClipTile(int index, const ImVec2& size) {
    const std::vector<ClipDef>& library = clipLibrary();
    const ClipDef& clip = library[static_cast<size_t>(index)];
    ImGui::PushID(index);
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const bool clicked = ImGui::InvisibleButton("clip", size);
    const bool hovered = ImGui::IsItemHovered();
    const bool selected = selectedLibraryClip_ == index;

    ImDrawList* draw = ImGui::GetWindowDrawList();
    const float labelHeight = ImGui::GetTextLineHeight() + 6.0f;
    const ImVec2 imageMax(pos.x + size.x, pos.y + size.y - labelHeight);
    const ImVec2 max(pos.x + size.x, pos.y + size.y);
    draw->AddRectFilled(pos, max, ImGui::GetColorU32(hovered ? ImGuiCol_FrameBgHovered : ImGuiCol_FrameBg), 3.0f);
    draw->AddImage(ui::texId(engine_.clipThumbnail(index)), pos, imageMax, ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f), IM_COL32_WHITE);
    draw->PushClipRect(ImVec2(pos.x, imageMax.y), max, true);
    draw->AddText(ImVec2(pos.x + 6.0f, imageMax.y + 3.0f), IM_COL32(230, 230, 235, 255), clip.name.c_str());
    draw->PopClipRect();
    if (isNewClip(clip.name)) {
        const float tagSize = ImGui::GetFontSize() * 0.66f;
        const char* tagText = tr("new");
        const float tagWidth = ImGui::CalcTextSize(tagText).x * 0.66f;
        const float nameEnd = pos.x + 6.0f + ImGui::CalcTextSize(clip.name.c_str()).x + 7.0f;
        if (nameEnd + tagWidth <= max.x - 3.0f) {
            draw->AddText(ImGui::GetFont(), tagSize, ImVec2(nameEnd, imageMax.y + 3.0f + (ImGui::GetTextLineHeight() - tagSize) * 0.5f), ImGui::GetColorU32(ui::kAccent), tagText);
        } else {
            const ImVec2 pillMin(pos.x + 4.0f, pos.y + 4.0f);
            const ImVec2 pillMax(pillMin.x + tagWidth + 8.0f, pillMin.y + tagSize + 4.0f);
            draw->AddRectFilled(pillMin, pillMax, ImGui::GetColorU32(ui::kAccent), 3.0f);
            draw->AddText(ImGui::GetFont(), tagSize, ImVec2(pillMin.x + 4.0f, pillMin.y + 2.0f), ImGui::GetColorU32(ui::kBlack), tagText);
        }
    }
    const char* badge = nullptr;
    if (clip.kind == ClipKind::SpoutSource) {
        badge = tr("Spout");
    } else if (clip.kind == ClipKind::NdiSource) {
        badge = tr("NDI");
    } else if (clip.kind == ClipKind::CameraSource) {
        badge = tr("Camera");
    } else if (clip.kind == ClipKind::PluginSource) {
        badge = tr("Plugin");
    } else if (clip.kind == ClipKind::VideoFile) {
        badge = tr("Video");
    } else if (clip.kind == ClipKind::ImageFile) {
        badge = tr("Image");
    } else if (clip.kind == ClipKind::TextFile) {
        badge = tr("Text");
    } else if (clip.kind == ClipKind::ParticleClip) {
        badge = tr("Particles");
    }
    if (badge != nullptr) {
        const char* label = badge;
        const ImVec2 badgeSize = ImGui::CalcTextSize(label);
        const ImVec2 badgeMin(imageMax.x - badgeSize.x - 10.0f, pos.y + 4.0f);
        const ImVec2 badgeMax(imageMax.x - 4.0f, pos.y + 8.0f + badgeSize.y);
        draw->AddRectFilled(badgeMin, badgeMax, IM_COL32(0, 0, 0, 170), 3.0f);
        draw->AddText(ImVec2(badgeMin.x + 3.0f, badgeMin.y + 2.0f), ImGui::GetColorU32(ui::kAccent), label);
    } else if (clip.is3d) {
        const ImVec2 badgeSize = ImGui::CalcTextSize("3D");
        const ImVec2 badgeMin(imageMax.x - badgeSize.x - 10.0f, pos.y + 4.0f);
        const ImVec2 badgeMax(imageMax.x - 4.0f, pos.y + 8.0f + badgeSize.y);
        draw->AddRectFilled(badgeMin, badgeMax, IM_COL32(0, 0, 0, 170), 3.0f);
        draw->AddText(ImVec2(badgeMin.x + 3.0f, badgeMin.y + 2.0f), ImGui::GetColorU32(ui::kAccent), "3D");
    }
    if (libraryMeta_.favorite(clip.name)) {
        const ImVec2 center(pos.x + 11.0f, pos.y + 11.0f);
        draw->AddQuadFilled(ImVec2(center.x, center.y - 7.0f), ImVec2(center.x + 5.0f, center.y), ImVec2(center.x, center.y + 7.0f), ImVec2(center.x - 5.0f, center.y), ImGui::GetColorU32(ui::kAccent));
        draw->AddQuad(ImVec2(center.x, center.y - 7.0f), ImVec2(center.x + 5.0f, center.y), ImVec2(center.x, center.y + 7.0f), ImVec2(center.x - 5.0f, center.y), IM_COL32(0, 0, 0, 200), 1.0f);
    }
    if (selected) {
        draw->AddRect(pos, max, ImGui::GetColorU32(ui::kAccent), 3.0f, 0, 2.5f);
    } else if (hovered) {
        draw->AddRect(pos, max, ImGui::GetColorU32(ui::kAccentHover), 3.0f, 0, 1.5f);
    } else {
        draw->AddRect(pos, max, ImGui::GetColorU32(ImGuiCol_Border), 3.0f);
    }

    if (clicked) {
        selectedLibraryClip_ = index;
        assignClipToSelectedCell(index);
    }
    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
        ImGui::SetDragDropPayload("NVJ_CLIP", &index, sizeof(int));
        ImGui::Image(ui::texId(engine_.clipThumbnail(index)), ImVec2(96.0f, 54.0f), ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f));
        ImGui::TextUnformatted(clip.name.c_str());
        ImGui::EndDragDropSource();
    } else if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
        ImGui::BeginTooltip();
        ImGui::TextUnformatted(clip.name.c_str());
        const std::vector<std::string>& clipTags = libraryMeta_.tags(clip.name);
        if (!clipTags.empty()) {
            std::string line;
            for (const std::string& tag : clipTags) {
                line += (line.empty() ? "" : "  ") + std::string("#") + tag;
            }
            ImGui::TextDisabled("%s", line.c_str());
        }
        if (engine_.isSceneClip(index)) {
            ImGui::TextDisabled("%s", tr("3D scene"));
            drawSceneDetails(*engine_.scenePlayer(index));
        }
        if (MediaPlayer* player = engine_.mediaPlayer(index)) {
            drawMediaDetails(*player, true);
        }
        ImGui::Separator();
        ImGui::TextDisabled("%s", tr("Click: put it into the selected cell and play it"));
        ImGui::TextDisabled("%s", tr("Drag it onto any cell or column"));
        ImGui::TextDisabled("%s", tr("Right click for more"));
        ImGui::EndTooltip();
    }
    if (ImGui::BeginPopupContextItem("clipmenu")) {
        ImGui::TextDisabled("%s", clip.name.c_str());
        ImGui::Separator();
        const std::string selectedLabel = std::string(tr("Put into the selected cell")) + "  (" + ui::cellLabel(selectedLayer_, selectedColumn_) + ")";
        if (ImGui::MenuItem(selectedLabel.c_str())) {
            selectedLibraryClip_ = index;
            assignClipToSelectedCell(index);
        }
        if (ImGui::BeginMenu(tr("Put into the next free cell of"))) {
            for (int l = kLayerCount - 1; l >= 0; --l) {
                const std::string label = std::string(tr("Layer ")) + std::to_string(l + 1);
                if (ImGui::MenuItem(label.c_str())) {
                    selectedLibraryClip_ = index;
                    assignClipToFreeCell(index, l);
                }
            }
            ImGui::EndMenu();
        }
        ImGui::Separator();
        const bool isFavorite = libraryMeta_.favorite(clip.name);
        if (ImGui::MenuItem(isFavorite ? tr("Remove from favorites") : tr("Add to favorites"))) {
            libraryMeta_.setFavorite(clip.name, !isFavorite);
            saveLibraryMeta();
        }
        if (ImGui::MenuItem(tr("Edit tags"))) {
            libraryPopup_ = 1;
            libraryPopupClip_ = index;
            tagInput_[0] = '\0';
        }
        if (ImGui::MenuItem(tr("Replace this clip everywhere"))) {
            libraryPopup_ = 2;
            libraryPopupClip_ = index;
            replaceFilter_[0] = '\0';
        }
        if (canEditInCreator(clip)) {
            ImGui::Separator();
            if (ImGui::MenuItem(tr("Edit in the Clip Creator"))) {
                openCreatorWith(clip.name);
            }
        }
        ImGui::EndPopup();
    }
    ImGui::PopID();
}

void App::loadLibraryMeta() {
    std::string error;
    if (!loadLibraryMetaFile(libraryMeta_, std::filesystem::path(userDataDir()) / "library_meta.json", error)) {
        logLine("Library tags and favorites could not be read, the file was set aside: " + error);
    }
}

void App::saveLibraryMeta() {
    std::string error;
    if (!saveLibraryMetaFile(libraryMeta_, std::filesystem::path(userDataDir()) / "library_meta.json", error)) {
        toast(tr("Library tags and favorites could not be saved"));
        logLine("Library tags and favorites could not be saved: " + error);
        return;
    }
    libraryMeta_.markClean();
}

void App::drawLibraryPopups() {
    const std::vector<ClipDef>& library = clipLibrary();
    if (libraryPopup_ != 0 && libraryPopupClip_ >= 0 && libraryPopupClip_ < static_cast<int>(library.size())) {
        ImGui::OpenPopup(libraryPopup_ == 1 ? "##libraryTags" : "##libraryReplace");
        libraryPopup_ = 0;
    }
    const bool valid = libraryPopupClip_ >= 0 && libraryPopupClip_ < static_cast<int>(library.size());
    const std::string clipName = valid ? library[static_cast<size_t>(libraryPopupClip_)].name : std::string();
    if (ImGui::BeginPopup("##libraryTags")) {
        ImGui::TextColored(ui::kAccent, "%s", tr("Tags"));
        ImGui::SameLine();
        ImGui::TextDisabled("%s", clipName.c_str());
        const std::vector<std::string> own = libraryMeta_.tags(clipName);
        if (own.empty()) {
            ImGui::TextDisabled("%s", tr("No tags yet"));
        }
        for (const std::string& tag : own) {
            const std::string label = "#" + tag + "  x";
            if (ImGui::SmallButton(label.c_str())) {
                libraryMeta_.removeTag(clipName, tag);
                saveLibraryMeta();
            }
            ImGui::SameLine();
        }
        ImGui::NewLine();
        ImGui::SetNextItemWidth(180.0f);
        const bool entered = ImGui::InputTextWithHint("##newTag", tr("New tag"), tagInput_, sizeof(tagInput_), ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::SameLine();
        if ((ImGui::Button(trId("Add", "addTag")) || entered) && tagInput_[0] != '\0') {
            if (libraryMeta_.addTag(clipName, tagInput_)) {
                saveLibraryMeta();
            } else {
                toast(tr("This tag was not added. A clip can have 8 tags of up to 24 characters."));
            }
            tagInput_[0] = '\0';
        }
        const std::vector<std::string> known = libraryMeta_.allTags();
        if (!known.empty()) {
            ImGui::Spacing();
            ImGui::TextDisabled("%s", tr("Tags in your library"));
            int shownKnown = 0;
            for (const std::string& tag : known) {
                if (std::find(own.begin(), own.end(), tag) != own.end() || shownKnown >= 16) {
                    continue;
                }
                const std::string label = "#" + tag;
                if (ImGui::SmallButton(label.c_str())) {
                    if (libraryMeta_.addTag(clipName, tag)) {
                        saveLibraryMeta();
                    }
                }
                ImGui::SameLine();
                ++shownKnown;
            }
            ImGui::NewLine();
        }
        ImGui::EndPopup();
    }
    if (ImGui::BeginPopup("##libraryReplace")) {
        ImGui::TextColored(ui::kAccent, "%s", tr("Replace this clip everywhere"));
        ImGui::TextDisabled("%s", clipName.c_str());
        ImGui::TextWrapped("%s", tr("Every cell of every deck that plays this clip plays the clip you pick instead. Layer, effect and mixing settings stay."));
        ImGui::Checkbox(trId("Keep the playback settings of the cells", "replaceKeep"), &replaceKeepPlayback_);
        ImGui::SetNextItemWidth(260.0f);
        ImGui::InputTextWithHint("##replaceFilter", tr("Search"), replaceFilter_, sizeof(replaceFilter_));
        const LibraryQuery query = parseLibraryQuery(replaceFilter_);
        ImGui::BeginChild("##replaceList", ImVec2(300.0f, 220.0f), ImGuiChildFlags_Borders);
        for (int i = 0; i < static_cast<int>(library.size()); ++i) {
            const ClipDef& candidate = library[static_cast<size_t>(i)];
            if (i == libraryPopupClip_ || !libraryMeta_.matches(candidate.name, query)) {
                continue;
            }
            if (ImGui::Selectable(candidate.name.c_str())) {
                const int changed = replaceClipEverywhere(engine_.comp, libraryPopupClip_, i, replaceKeepPlayback_);
                toast(std::to_string(changed) + " " + tr("cells now play") + " " + candidate.name);
                ImGui::CloseCurrentPopup();
                break;
            }
        }
        ImGui::EndChild();
        ImGui::EndPopup();
    }
}

void App::drawLibraryClips() {
    const std::vector<ClipDef>& library = clipLibrary();
    const char* kinds[] = {tr("All"), tr("2D"), tr("3D"), tr("Models"), tr("Scenes"), tr("Spout"), tr("Media"), tr("Shaders"), tr("Live"), tr("Generators"), tr("Favorites")};
    const float rightEdge = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
    for (int k = 0; k < 11; ++k) {
        if (k > 0) {
            const float buttonWidth = ImGui::CalcTextSize(kinds[k]).x + ImGui::GetStyle().FramePadding.x * 2.0f;
            if (ImGui::GetItemRectMax().x + 4.0f + buttonWidth < rightEdge) {
                ImGui::SameLine(0.0f, 4.0f);
            }
        }
        ImGui::PushID(k);
        if (ui::accentButton(kinds[k], libraryKind_ == k)) {
            libraryKind_ = k;
        }
        ImGui::PopID();
    }

    const ImGuiStyle& style = ImGui::GetStyle();
    const float spacing = style.ItemSpacing.x;
    const float avail = ImGui::GetContentRegionAvail().x - style.ScrollbarSize * 0.5f;
    const int columns = std::max(1, static_cast<int>((avail + spacing) / (132.0f + spacing)));
    const float tileWidth = (avail - spacing * static_cast<float>(columns - 1)) / static_cast<float>(columns);
    const ImVec2 tileSize(tileWidth, tileWidth * 9.0f / 16.0f + ImGui::GetTextLineHeight() + 6.0f);

    int shown = 0;
    int total = 0;
    const LibraryQuery query = parseLibraryQuery(libraryFilter_);
    const auto inKind = [this](const ClipDef& clip) {
        return libraryKind_ == 10 ? libraryMeta_.favorite(clip.name) : matchesKind(clip, libraryKind_);
    };
    for (const ClipDef& clip : library) {
        if (inKind(clip)) {
            ++total;
        }
    }
    ImGui::BeginChild("clipTiles", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None);
    for (int i = 0; i < static_cast<int>(library.size()); ++i) {
        const ClipDef& clip = library[static_cast<size_t>(i)];
        if (!inKind(clip) || !libraryMeta_.matches(clip.name, query)) {
            continue;
        }
        if (shown % columns != 0) {
            ImGui::SameLine();
        }
        drawClipTile(i, tileSize);
        ++shown;
    }
    if (shown == 0) {
        ImGui::TextDisabled("%s", tr("Nothing found"));
    }
    ImGui::Spacing();
    ImGui::TextDisabled("%d / %d %s", shown, total, tr("clips"));
    ImGui::Spacing();
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("%s", tr("3D models: put .glb or .gltf files into this folder, then press Scan again"));
    ImGui::TextDisabled("%s", modelDirectory().c_str());
    ImGui::TextDisabled("%s", tr("Images and videos: drag files onto this window or put them into this folder"));
    ImGui::TextDisabled("%s", mediaDirectory().c_str());
    ImGui::TextDisabled("%s", tr("Your own shaders: put .glsl, .frag or .fs files into this folder, changes are picked up when you save"));
    ImGui::TextDisabled("%s", shaderDirectory().c_str());
    ImGui::PopTextWrapPos();
    if (ImGui::SmallButton(tr("Open models folder"))) {
        platform::openFolder(modelDirectory());
    }
    ImGui::SameLine();
    if (ImGui::SmallButton(tr("Open media folder"))) {
        platform::openFolder(mediaDirectory());
    }
    ImGui::SameLine();
    if (ImGui::SmallButton(tr("Open shaders folder"))) {
        platform::openFolder(shaderDirectory());
    }
    ImGui::SameLine();
    if (ImGui::SmallButton(tr("Create example shader"))) {
        const std::string created = createExampleShader();
        if (created.empty()) {
            toast(tr("The example shader could not be created"));
        } else {
            rescanModels_ = true;
            toast(std::string(tr("Created: ")) + created);
        }
    }
    ImGui::SetItemTooltip("%s", tr("Writes a small shader into the shaders folder that you can open in any text editor"));
    ImGui::SameLine();
    if (ImGui::SmallButton(trId("Scan again", "scanModels"))) {
        rescanModels_ = true;
    }
    ImGui::SetItemTooltip("%s", tr("Looks for new model files without restarting. Removed files stay in the list until the next start."));
    ImGui::SameLine();
    if (ui::accentButton(tr("Open Clip Creator"), false)) {
        openCreator();
    }
    ImGui::SetItemTooltip("%s", tr("Build your own 3D scenes from shapes, models, lights and audio reactions"));
    drawLibraryPopups();
    ImGui::EndChild();
}

void App::drawLibraryAssets() {
    if (assetsDirty_) {
        assetModels_ = listModelAssets();
        assetTextures_ = listTextureAssets();
        assetPacks_ = listPackAssets();
        assetMedia_ = listMediaAssets();
        assetsDirty_ = false;
    }
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("%s", tr("Everything your scenes can use. Put files into these folders and press Scan again."));
    ImGui::PopTextWrapPos();
    if (ImGui::SmallButton(tr("Models folder"))) {
        platform::openFolder(modelDirectory());
    }
    ImGui::SameLine();
    if (ImGui::SmallButton(tr("Textures folder"))) {
        platform::openFolder(texturesDirectory());
    }
    ImGui::SameLine();
    if (ImGui::SmallButton(tr("Scenes folder"))) {
        platform::openFolder(clipsDirectory());
    }
    ImGui::SameLine();
    if (ImGui::SmallButton(tr("Media folder"))) {
        platform::openFolder(mediaDirectory());
    }
    if (ImGui::SmallButton(trId("Scan again", "scanModels"))) {
        rescanModels_ = true;
        assetsDirty_ = true;
    }
    ImGui::SameLine();
    if (ImGui::SmallButton(tr("Open Clip Creator"))) {
        openCreator();
    }
    ImGui::BeginChild("assetRows", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None);
    const auto section = [&](const char* title, const std::vector<AssetFile>& files, bool models) {
        ImGui::SeparatorText(title);
        if (files.empty()) {
            ImGui::TextDisabled("%s", tr("Nothing found"));
        }
        for (const AssetFile& file : files) {
            const bool usable = models ? isModelAssetName(file.name) : true;
            ImGui::PushID(file.name.c_str());
            ImGui::BeginDisabled(!usable);
            ImGui::Selectable(file.name.c_str(), false);
            ImGui::EndDisabled();
            const std::string size = formatBytes(file.bytes);
            ImGui::SameLine(std::max(ImGui::GetContentRegionMax().x - ImGui::CalcTextSize(size.c_str()).x - 6.0f, 0.0f));
            ImGui::TextDisabled("%s", size.c_str());
            if (!usable) {
                ImGui::SetItemTooltip("%s", tr("Only .glb files can be used in your own scenes"));
            }
            ImGui::PopID();
        }
    };
    section(tr("Models"), assetModels_, true);
    section(tr("Textures"), assetTextures_, false);
    ImGui::SeparatorText(tr("Images and videos"));
    if (assetMedia_.empty()) {
        ImGui::TextDisabled("%s", tr("Nothing found"));
    }
    for (const AssetFile& file : assetMedia_) {
        ImGui::PushID(file.path.c_str());
        const std::string stem = pathToUtf8(pathFromUtf8(file.name).stem());
        const bool video = isVideoFileName(file.name);
        if (ImGui::Selectable(file.name.c_str(), false)) {
            const int clip = clipIndexByName(video ? videoClipName(stem) : imageClipName(stem));
            if (clip >= 0) {
                selectedLibraryClip_ = clip;
                assignClipToSelectedCell(clip);
            } else {
                toast(tr("Press Scan again first"));
            }
        }
        const std::string size = formatBytes(file.bytes);
        ImGui::SameLine(std::max(ImGui::GetContentRegionMax().x - ImGui::CalcTextSize(size.c_str()).x - 6.0f, 0.0f));
        ImGui::TextDisabled("%s", size.c_str());
        ImGui::SetItemTooltip("%s", tr("Click to put it into the selected cell"));
        ImGui::PopID();
    }
    ImGui::SeparatorText(tr("Your scenes"));
    if (assetPacks_.empty()) {
        ImGui::TextDisabled("%s", tr("No saved scenes yet"));
    }
    for (const AssetFile& file : assetPacks_) {
        ImGui::PushID(file.path.c_str());
        const std::string stem = pathToUtf8(pathFromUtf8(file.name).stem());
        if (ImGui::Selectable(stem.c_str(), false)) {
            openCreatorWith(packClipName(stem));
        }
        const std::string size = formatBytes(file.bytes);
        ImGui::SameLine(std::max(ImGui::GetContentRegionMax().x - ImGui::CalcTextSize(size.c_str()).x - 6.0f, 0.0f));
        ImGui::TextDisabled("%s", size.c_str());
        ImGui::SetItemTooltip("%s", tr("Click to edit this scene in the Clip Creator"));
        ImGui::PopID();
    }
    ImGui::EndChild();
}

void App::drawLibraryEffects() {
    const std::vector<EffectDef>& effects = effectLibrary();
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("%s", tr("Click: add to the first free slot of the selected layer"));
    ImGui::PopTextWrapPos();
    ImGui::BeginChild("effectRows", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None);
    int shown = 0;
    for (int i = 0; i < static_cast<int>(effects.size()); ++i) {
        const EffectDef& effect = effects[static_cast<size_t>(i)];
        if (!matchesFilter(effect.name, libraryFilter_)) {
            continue;
        }
        ImGui::PushID(i);
        const ImVec2 rowStart = ImGui::GetCursorScreenPos();
        const bool broken = !effect.error.empty();
        if (broken) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.45f, 0.45f, 1.0f));
        }
        if (ImGui::Selectable(effect.name.c_str(), selectedLibraryEffect_ == i)) {
            selectedLibraryEffect_ = i;
            addEffectToLayer(i, selectedLayer_);
        }
        if (broken) {
            ImGui::PopStyleColor();
            ImGui::SetItemTooltip("%s", effect.error.c_str());
        }
        if (isNewEffect(effect.name)) {
            ui::newTag(rowStart, effect.name.c_str(), ImGui::GetTextLineHeight());
        }
        if (effect.is3d) {
            const ImVec2 badgeSize = ImGui::CalcTextSize("3D");
            ImGui::SameLine(ImGui::GetContentRegionMax().x - badgeSize.x - 6.0f);
            ImGui::TextColored(ui::kAccent, "3D");
        }
        if (ImGui::BeginDragDropSource()) {
            ImGui::SetDragDropPayload("NVJ_EFFECT", &i, sizeof(int));
            ImGui::TextUnformatted(effect.name.c_str());
            ImGui::EndDragDropSource();
        }
        if (ImGui::BeginPopupContextItem("effectmenu")) {
            ImGui::TextDisabled("%s", effect.name.c_str());
            ImGui::Separator();
            std::string graphStem;
            if (effect.isGraph && graphStemFromEffectName(effect.name, graphStem)) {
                if (ImGui::MenuItem(tr("Edit in the node editor"))) {
                    openNodeEditor(graphStem);
                }
                ImGui::Separator();
            }
            for (int l = kLayerCount - 1; l >= 0; --l) {
                const std::string label = std::string(tr("Add to layer ")) + std::to_string(l + 1);
                if (ImGui::MenuItem(label.c_str())) {
                    addEffectToLayer(i, l);
                }
            }
            if (ImGui::MenuItem(tr("Add to master"))) {
                addEffectToMaster(i);
            }
            ImGui::EndPopup();
        }
        ImGui::PopID();
        ++shown;
    }
    if (shown == 0) {
        ImGui::TextDisabled("%s", tr("Nothing found"));
    }
    ImGui::Spacing();
    ImGui::TextDisabled("%d / %d %s", shown, static_cast<int>(effects.size()), tr("effects"));
    ImGui::EndChild();
}

void App::drawLibraryTab() {
    const char* modes[] = {tr("Clips"), tr("Effects"), tr("Assets")};
    for (int m = 0; m < 3; ++m) {
        if (m > 0) {
            ImGui::SameLine(0.0f, 4.0f);
        }
        ImGui::PushID(m);
        if (ui::accentButton(modes[m], libraryMode_ == m, ImVec2(72.0f, 0.0f))) {
            libraryMode_ = m;
            assetsDirty_ = true;
        }
        ImGui::PopID();
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputTextWithHint("##libraryfilter", tr("Search"), libraryFilter_, sizeof(libraryFilter_));
    ImGui::SetItemTooltip("%s", tr("Type words to search names and tags. tag:name finds a tag, is:fav finds favorites."));
    ImGui::Spacing();
    if (libraryMode_ == 0) {
        drawLibraryClips();
    } else if (libraryMode_ == 1) {
        drawLibraryEffects();
    } else {
        drawLibraryAssets();
    }
}

void App::drawAudioSource() {
    std::string preview = tr("Off");
    if (audio_.selected() == AudioSystem::demoId()) {
        preview = tr("Demo signal");
    } else if (!audio_.selected().empty()) {
        preview = audio_.selected();
        for (const AudioDevice& device : audio_.devices()) {
            if (device.id == audio_.selected()) {
                preview = std::string(device.loopback ? tr("System audio: ") : tr("Input: ")) + device.name;
            }
        }
    }

    ImGui::TextDisabled("%s", tr("Audio source"));
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::BeginCombo("##audiosource", preview.c_str())) {
        if (ImGui::Selectable(tr("Off"), audio_.selected().empty())) {
            audio_.select("");
            settings_.audioDevice.clear();
        }
        if (ImGui::Selectable(tr("Demo signal"), audio_.selected() == AudioSystem::demoId())) {
            audio_.select(AudioSystem::demoId());
        }
        bool loopbackHeader = false;
        bool inputHeader = false;
        for (int pass = 0; pass < 2; ++pass) {
            for (const AudioDevice& device : audio_.devices()) {
                if (device.loopback != (pass == 0)) {
                    continue;
                }
                bool& header = pass == 0 ? loopbackHeader : inputHeader;
                if (!header) {
                    ImGui::SeparatorText(pass == 0 ? tr("System audio (what your PC plays)") : tr("Inputs (microphone, line-in)"));
                    header = true;
                }
                ImGui::PushID(device.id.c_str());
                if (ImGui::Selectable(device.name.c_str(), audio_.selected() == device.id)) {
                    if (audio_.select(device.id)) {
                        settings_.audioDevice = device.id;
                    } else {
                        toast(trMessage(audio_.error()));
                    }
                }
                ImGui::PopID();
            }
        }
        ImGui::EndCombo();
    }
    if (ImGui::SmallButton(trId("Refresh devices", "audioRefresh"))) {
        audio_.refreshDevices();
    }
    if (!audio_.error().empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.3f, 1.0f), "%s", trMessage(audio_.error()).c_str());
    }
    if (audio_.lost()) {
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.3f, 1.0f), "%s", tr("The audio device is no longer available."));
    }
    if (audio_.noSignal()) {
        ImGui::TextColored(ImVec4(1.0f, 0.65f, 0.2f, 1.0f), "%s", tr("No sound is arriving on this device. Play something on this output or pick another source."));
    }
}

void App::drawAudioTab() {
    drawAudioSource();

    const AudioFeatures& features = engine_.audio();
    ImGui::Spacing();
    if (!audio_.active()) {
        ImGui::TextWrapped("%s", tr("Audio is off. Clips and effects follow the BPM clock instead."));
    }

    ImVec2 size(ImGui::GetContentRegionAvail().x, 84.0f);
    ImVec2 origin = ImGui::GetCursorScreenPos();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(origin, ImVec2(origin.x + size.x, origin.y + size.y), IM_COL32(14, 14, 17, 255));
    const float barWidth = size.x / static_cast<float>(kSpectrumBins);
    for (int i = 0; i < kSpectrumBins; ++i) {
        const float value = std::clamp(features.spectrum[static_cast<size_t>(i)], 0.0f, 1.0f);
        const float x0 = origin.x + barWidth * static_cast<float>(i);
        const float t = static_cast<float>(i) / static_cast<float>(kSpectrumBins - 1);
        const ImU32 color = IM_COL32(static_cast<int>(255.0f - 255.0f * t), static_cast<int>(115.0f + 85.0f * t), static_cast<int>(0.0f + 255.0f * t), 255);
        draw->AddRectFilled(ImVec2(x0, origin.y + size.y * (1.0f - value)), ImVec2(x0 + std::max(1.0f, barWidth - 1.0f), origin.y + size.y), color);
    }
    ImGui::Dummy(size);

    const char* names[4] = {tr("Bass"), tr("Mid"), tr("High"), tr("Level")};
    const float values[4] = {features.bass, features.mid, features.high, features.level};
    for (int i = 0; i < 4; ++i) {
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ui::kAccent);
        ImGui::ProgressBar(std::clamp(values[i], 0.0f, 1.0f), ImVec2(-FLT_MIN, 12.0f), "");
        ImGui::PopStyleColor();
        ImGui::SameLine(8.0f);
        ImGui::TextUnformatted(names[i]);
    }

    ImVec2 dot = ImGui::GetCursorScreenPos();
    const float kick = std::clamp(features.kick, 0.0f, 1.0f);
    draw->AddCircleFilled(ImVec2(dot.x + 8.0f, dot.y + 9.0f), 7.0f, ImGui::GetColorU32(ImVec4(ui::kAccent.x, ui::kAccent.y + (1.0f - ui::kAccent.y) * 0.5f * kick, ui::kAccent.z + (1.0f - ui::kAccent.z) * 0.5f * kick, 0.25f + 0.75f * kick)));
    ImGui::Dummy(ImVec2(18.0f, 18.0f));
    ImGui::SameLine();
    if (audio_.active() && features.bpm > 0.0f) {
        ImGui::Text("%s %.1f BPM", tr("Detected tempo"), features.bpm);
        ImGui::SameLine();
        ImGui::TextDisabled("(%d%%)", static_cast<int>(features.bpmConfidence * 100.0f));
        ImGui::SameLine();
        if (ImGui::SmallButton(trId("Use", "audioUseBpm"))) {
            engine_.comp.bpm = std::clamp(features.bpm, 40.0f, 240.0f);
        }
    } else if (audio_.active()) {
        ImGui::TextDisabled("%s", tr("Listening for the beat..."));
    } else {
        ImGui::TextDisabled("%s", tr("Beat"));
    }
    ImGui::Checkbox(trId("Sync BPM to audio", "audioSyncBpm"), &settings_.audioSyncBpm);

    ImGui::SeparatorText(tr("Analysis"));
    ui::sliderFloat(trId("Gain", "audioGain"), &settings_.audioGain, 0.1f, 8.0f, "%.2f", ImGuiSliderFlags_Logarithmic);
    ui::sliderFloat(trId("Smoothing", "audioSmoothing"), &settings_.audioSmoothing, 0.0f, 1.0f, "%.2f");
    ui::sliderFloat(trId("Beat sensitivity", "audioSensitivity"), &settings_.audioBeatSensitivity, 0.0f, 1.0f, "%.2f");

    ImGui::Spacing();
    ImGui::TextWrapped("%s", tr("System audio listens to everything your PC plays, for example a DJ program or a music player. Inputs are microphones, line-in and audio interfaces."));
    ImGui::TextWrapped("%s", tr("Use the Audio reactivity rows on layers, the master and effects to let values follow Bass, Mid, High, Level or Kick."));
#if !defined(_WIN32)
    ImGui::TextWrapped("%s", tr("Capturing from devices is only available on Windows. Use the demo signal to try the audio reaction."));
#endif
}

void App::drawBrowser() {
    ImGui::Begin(trId("Browser", "browser"));
    if (ImGui::BeginTabBar("browser_tabs")) {
        const char* names[] = {trId("Presets", "tabPresets"), trId("Library", "tabLibrary"), trId("Audio", "tabAudio")};
        for (int i = 0; i < 3; ++i) {
            ImGuiTabItemFlags flags = focusTab_ == i ? ImGuiTabItemFlags_SetSelected : 0;
            if (ImGui::BeginTabItem(names[i], nullptr, flags)) {
                if (i == 0) {
                    drawPresetsTab();
                } else if (i == 1) {
                    drawLibraryTab();
                } else {
                    drawAudioTab();
                }
                ImGui::EndTabItem();
            }
        }
        focusTab_ = -1;
        ImGui::EndTabBar();
    }
    ImGui::End();
}

}
