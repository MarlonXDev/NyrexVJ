#include "app/app.hpp"

#include "app/ui_common.hpp"
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
                ImGui::TextDisabled("%s", tr("Get a shared FFmpeg build (LGPL, version 7.1) and copy these files into the folder below:"));
                for (const std::string& file : support.files) {
                    ImGui::BulletText("%s", file.c_str());
                }
                ImGui::TextDisabled("%s", support.folder.c_str());
                ImGui::PopTextWrapPos();
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

void App::assignClipToSelectedCell(int clip) {
    const std::vector<ClipDef>& library = clipLibrary();
    if (clip < 0 || clip >= static_cast<int>(library.size())) {
        return;
    }
    selectedLayer_ = std::clamp(selectedLayer_, 0, kLayerCount - 1);
    selectedColumn_ = std::clamp(selectedColumn_, 0, kColumnCount - 1);
    engine_.comp.layers[static_cast<size_t>(selectedLayer_)].cells[static_cast<size_t>(selectedColumn_)] = clip;
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
            target.cells[static_cast<size_t>(c)] = clip;
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
    } else if (clip.kind == ClipKind::VideoFile) {
        badge = tr("Video");
    } else if (clip.kind == ClipKind::ImageFile) {
        badge = tr("Image");
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

void App::drawLibraryClips() {
    const std::vector<ClipDef>& library = clipLibrary();
    const char* kinds[] = {tr("All"), tr("2D"), tr("3D"), tr("Models"), tr("Scenes"), tr("Spout"), tr("Media")};
    for (int k = 0; k < 7; ++k) {
        if (k > 0) {
            ImGui::SameLine(0.0f, 4.0f);
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
    for (const ClipDef& clip : library) {
        if (matchesKind(clip, libraryKind_)) {
            ++total;
        }
    }
    ImGui::BeginChild("clipTiles", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None);
    for (int i = 0; i < static_cast<int>(library.size()); ++i) {
        const ClipDef& clip = library[static_cast<size_t>(i)];
        if (!matchesKind(clip, libraryKind_) || !matchesFilter(clip.name, libraryFilter_)) {
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
    ImGui::PopTextWrapPos();
    if (ImGui::SmallButton(tr("Open models folder"))) {
        platform::openFolder(modelDirectory());
    }
    ImGui::SameLine();
    if (ImGui::SmallButton(tr("Open media folder"))) {
        platform::openFolder(mediaDirectory());
    }
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
        if (ImGui::Selectable(effect.name.c_str(), selectedLibraryEffect_ == i)) {
            selectedLibraryEffect_ = i;
            addEffectToLayer(i, selectedLayer_);
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
    ImGui::SliderFloat(trId("Gain", "audioGain"), &settings_.audioGain, 0.1f, 8.0f, "%.2f", ImGuiSliderFlags_Logarithmic);
    ImGui::SliderFloat(trId("Smoothing", "audioSmoothing"), &settings_.audioSmoothing, 0.0f, 1.0f, "%.2f");
    ImGui::SliderFloat(trId("Beat sensitivity", "audioSensitivity"), &settings_.audioBeatSensitivity, 0.0f, 1.0f, "%.2f");

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
