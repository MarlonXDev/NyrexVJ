#include "app/app.hpp"

#include "app/ui_common.hpp"
#include "core/log.hpp"
#include "platform/platform.hpp"
#include "scene3d/scene_pack.hpp"

#include <imgui.h>

#include <algorithm>
#include <cfloat>
#include <cstdarg>
#include <cstdio>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

namespace nvj {

namespace {

namespace fs = std::filesystem;

const ImVec4 kGood(0.45f, 0.85f, 0.45f, 1.0f);
const ImVec4 kWarn(1.0f, 0.65f, 0.2f, 1.0f);
const ImVec4 kBad(1.0f, 0.4f, 0.35f, 1.0f);

void copyInto(char* target, size_t size, const std::string& text) {
    std::snprintf(target, size, "%s", text.c_str());
}

std::string megabytes(unsigned long long bytes) {
    char buffer[48];
    std::snprintf(buffer, sizeof(buffer), "%.2f MB", static_cast<double>(bytes) / (1024.0 * 1024.0));
    return buffer;
}

std::string joinNames(const std::vector<std::string>& names) {
    std::string result;
    for (const std::string& name : names) {
        if (!result.empty()) {
            result += ", ";
        }
        result += name;
    }
    return result;
}

void advanceClock(double& clock, double& lastTime, bool playing) {
    const double now = ImGui::GetTime();
    if (lastTime < 0.0) {
        lastTime = now;
    }
    const double delta = std::clamp(now - lastTime, 0.0, 0.25);
    lastTime = now;
    if (playing) {
        clock += delta;
    }
}

void drawPreviewImage(unsigned int texture, float maxHeight) {
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    float width = avail.x;
    float height = width * 9.0f / 16.0f;
    if (height > maxHeight) {
        height = maxHeight;
        width = height * 16.0f / 9.0f;
    }
    if (texture != 0 && width > 8.0f && height > 8.0f) {
        ImGui::Image(ui::texId(texture), ImVec2(width, height), ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f));
    } else {
        ImGui::Dummy(ImVec2(std::max(width, 8.0f), std::max(height, 8.0f)));
    }
}

const char* animationLabel(text::Animation animation) {
    switch (animation) {
        case text::Animation::Typewriter: return "Typewriter";
        case text::Animation::Fade: return "Fade in";
        case text::Animation::SlideUp: return "Slide up";
        case text::Animation::Wave: return "Wave";
        case text::Animation::Pulse: return "Pulse";
        case text::Animation::Audio: return "Follow the audio level";
        case text::Animation::Bounce: return "Bounce";
        case text::Animation::None: break;
    }
    return "No animation";
}

const char* hAlignLabel(text::HAlign align) {
    switch (align) {
        case text::HAlign::Left: return "Left";
        case text::HAlign::Right: return "Right";
        case text::HAlign::Center: break;
    }
    return "Center";
}

const char* vAlignLabel(text::VAlign align) {
    switch (align) {
        case text::VAlign::Top: return "Top";
        case text::VAlign::Bottom: return "Bottom";
        case text::VAlign::Middle: break;
    }
    return "Middle";
}

const char* textColorLabel(text::ColorMode mode) {
    switch (mode) {
        case text::ColorMode::White: return "White";
        case text::ColorMode::Rainbow: return "Rainbow";
        case text::ColorMode::Layer: break;
    }
    return "Layer colour";
}

const char* shapeLabel(particles::Shape shape) {
    switch (shape) {
        case particles::Shape::Line: return "Line";
        case particles::Shape::Ring: return "Ring";
        case particles::Shape::Box: return "Box";
        case particles::Shape::Point: break;
    }
    return "Point";
}

const char* particleColorLabel(particles::ColorMode mode) {
    switch (mode) {
        case particles::ColorMode::Rainbow: return "Rainbow";
        case particles::ColorMode::White: return "White";
        case particles::ColorMode::Gradient: break;
    }
    return "Layer colours";
}

template <typename Enum, typename Label>
bool enumCombo(const char* label, Enum& value, int count, Label labelOf) {
    bool changed = false;
    if (ImGui::BeginCombo(label, tr(labelOf(value)))) {
        for (int i = 0; i < count; ++i) {
            const Enum candidate = static_cast<Enum>(i);
            if (ImGui::Selectable(tr(labelOf(candidate)), candidate == value)) {
                value = candidate;
                changed = true;
            }
        }
        ImGui::EndCombo();
    }
    return changed;
}

void hint(const char* format, ...) {
    char buffer[1024];
    va_list args;
    va_start(args, format);
    std::vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("%s", buffer);
    ImGui::PopTextWrapPos();
}

void textLine(const ImVec4& color, const std::string& text) {
    ImGui::PushStyleColor(ImGuiCol_Text, color);
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextUnformatted(text.c_str());
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
}

}

void App::openGeneratorStudio(int tab, const std::string& name) {
    GeneratorDialog& d = generatorDialog_;
    d.open = true;
    d.focus = true;
    d.requestedTab = tab;
    d.textFilesDirty = true;
    d.particleFilesDirty = true;
    d.fontsDirty = true;
    if (name.empty()) {
        return;
    }
    std::string error;
    if (tab == 0) {
        text::TextDoc doc;
        if (text::loadTextDoc(name, doc, error)) {
            d.textDoc = doc;
            copyInto(d.textBody, sizeof(d.textBody), doc.text);
            copyInto(d.textName, sizeof(d.textName), name);
            d.textReady = true;
            d.textStatus.clear();
        } else {
            d.textStatus = trMessage(error);
            d.textStatusBad = true;
        }
    } else {
        particles::Params params;
        if (particles::loadParticleDoc(name, params, error) || particles::findBuiltin(name, params)) {
            d.params = params;
            copyInto(d.particleName, sizeof(d.particleName), name);
            d.particleStatus.clear();
        } else {
            d.particleStatus = trMessage(error);
            d.particleStatusBad = true;
        }
    }
    d.clock = 0.0;
}

void App::drawGeneratorClipInfo(const ClipDef& def, int clip) {
    const std::string error = engine_.clipError(clip);
    if (!error.empty()) {
        textLine(kBad, trMessage(error));
    }
    GeneratorPlayer* player = engine_.generatorPlayer(clip);
    if (player != nullptr && !player->note().empty()) {
        textLine(kWarn, trMessage(player->note()));
    }
    if (auto* textPlayer = dynamic_cast<TextPlayer*>(player)) {
        if (textPlayer->state() == GeneratorState::Ready) {
            hint(tr("%d letters, drawn from a %d px atlas, %s"), textPlayer->glyphCount(), textPlayer->atlasSide(), megabytes(textPlayer->gpuBytes()).c_str());
        }
    } else if (auto* particlePlayer = dynamic_cast<ParticlePlayer*>(player)) {
        if (particlePlayer->params().gpu && engine_.gpuParticlesEnabled()) {
            hint(tr("Up to %d particles on the graphics card, %s"), particlePlayer->params().gpuCount, megabytes(gpuparticles::bytesFor(particlePlayer->params().gpuCount)).c_str());
        } else {
            hint(tr("Up to %d particles per layer"), engine_.particleLimit());
        }
    }
    ImGui::PushID("generatorInfo");
    if (ImGui::SmallButton(tr("Edit..."))) {
        openGeneratorStudio(def.kind == ClipKind::TextFile ? 0 : 1, generatorStem(def));
    }
    ImGui::SameLine();
    if (ImGui::SmallButton(tr("Open folder"))) {
        platform::openFolder(def.kind == ClipKind::TextFile ? text::textsDirectory() : particles::particlesDirectory());
    }
    ImGui::PopID();
}

void App::drawGeneratorTextTab() {
    GeneratorDialog& d = generatorDialog_;
    if (d.textFilesDirty) {
        d.textFiles = text::listTextDocs();
        d.textFilesDirty = false;
    }
    if (d.fontsDirty) {
        d.fonts = text::availableFonts();
        d.fontsDirty = false;
    }
    if (!d.textReady) {
        copyInto(d.textBody, sizeof(d.textBody), d.textDoc.text);
        d.textReady = true;
    }

    if (ImGui::BeginTable("##textLayout", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("##controls", ImGuiTableColumnFlags_WidthStretch, 1.15f);
        ImGui::TableSetupColumn("##preview", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);

        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::BeginCombo("##savedTexts", tr("Open a saved text"))) {
            for (const std::string& name : d.textFiles) {
                if (ImGui::Selectable(name.c_str(), name == d.textName)) {
                    openGeneratorStudio(0, name);
                }
            }
            if (d.textFiles.empty()) {
                hint("%s", tr("No saved texts yet"));
            }
            ImGui::EndCombo();
        }
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::InputTextMultiline("##textBody", d.textBody, sizeof(d.textBody), ImVec2(-FLT_MIN, ImGui::GetTextLineHeight() * 5.0f));
        d.textDoc.text = d.textBody;
        const text::DecodeResult decoded = text::decodeUtf8(d.textDoc.text);
        if (decoded.hadInvalid) {
            textLine(kWarn, tr("Some characters cannot be shown and are skipped."));
        }
        if (decoded.truncated) {
            textLine(kWarn, tr("The text is too long. Only the first part is used."));
        }

        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::BeginCombo("##font", d.textDoc.font.c_str())) {
            for (const text::FontEntry& font : d.fonts) {
                const std::string label = font.user ? font.name + "  (" + tr("your font") + ")" : font.name;
                if (ImGui::Selectable(label.c_str(), font.name == d.textDoc.font)) {
                    d.textDoc.font = font.name;
                }
            }
            ImGui::EndCombo();
        }
        ImGui::SetItemTooltip("%s", tr("Font. Put .ttf, .otf or .ttc files into the fonts folder to use your own fonts."));

        ui::sliderFloat(tr("Size"), &d.textDoc.size, 0.02f, 0.9f, "%.2f");
        ui::sliderFloat(tr("Line spacing"), &d.textDoc.lineSpacing, 0.5f, 3.0f, "%.2f");
        ui::sliderFloat(tr("Letter spacing"), &d.textDoc.tracking, -0.5f, 2.0f, "%.2f");
        ui::sliderFloat(tr("Wrap width"), &d.textDoc.wrap, 0.0f, 1.0f, "%.2f");
        ImGui::SetItemTooltip("%s", tr("Lines break at this width. At 0 the text is not wrapped."));
        ui::sliderFloat(tr("Position X"), &d.textDoc.posX, -1.0f, 1.0f, "%.2f");
        ui::sliderFloat(tr("Position Y"), &d.textDoc.posY, -1.0f, 1.0f, "%.2f");
        ui::sliderFloat(tr("Shadow"), &d.textDoc.shadow, 0.0f, 1.0f, "%.2f");
        ImGui::Checkbox(tr("Pair kerning"), &d.textDoc.kerning);
        enumCombo(tr("Horizontal alignment"), d.textDoc.hAlign, 3, hAlignLabel);
        enumCombo(tr("Vertical alignment"), d.textDoc.vAlign, 3, vAlignLabel);
        enumCombo(tr("Colour"), d.textDoc.color, 3, textColorLabel);
        enumCombo(tr("Animation"), d.textDoc.animation, text::kAnimationCount, animationLabel);
        ImGui::BeginDisabled(d.textDoc.animation == text::Animation::None);
        ui::sliderFloat(tr("Animation speed"), &d.textDoc.animSpeed, 0.1f, 4.0f, "%.2f");
        ui::sliderFloat(tr("Animation strength"), &d.textDoc.animAmount, 0.0f, 1.0f, "%.2f");
        ImGui::EndDisabled();

        ui::sectionHeader(tr("Save"));
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 180.0f);
        ImGui::InputTextWithHint("##textName", tr("Name"), d.textName, sizeof(d.textName));
        ImGui::SameLine();
        ImGui::BeginDisabled(d.textName[0] == '\0');
        if (ImGui::Button(tr("Save as clip"))) {
            std::string error;
            if (text::saveTextDoc(d.textName, d.textDoc, error)) {
                d.textStatus = std::string(tr("Saved. The clip appears in the library as: ")) + textClipName(text::textDocPath(d.textName).stem().string());
                d.textStatusBad = false;
                d.textFilesDirty = true;
                shaderPoll_ = 1.0;
            } else {
                d.textStatus = trMessage(error);
                d.textStatusBad = true;
            }
        }
        ImGui::EndDisabled();
        ImGui::BeginDisabled(d.textName[0] == '\0' || std::find(d.textFiles.begin(), d.textFiles.end(), std::string(d.textName)) == d.textFiles.end());
        if (ImGui::Button(tr("Delete"))) {
            ImGui::OpenPopup("##deleteText");
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button(tr("New"))) {
            d.textDoc = text::exampleTextDoc();
            copyInto(d.textBody, sizeof(d.textBody), d.textDoc.text);
            d.textName[0] = '\0';
            d.textStatus.clear();
            d.clock = 0.0;
        }
        ImGui::SameLine();
        if (ImGui::Button(tr("Open fonts folder"))) {
            std::error_code ec;
            fs::create_directories(pathFromUtf8(text::fontsDirectory()), ec);
            platform::openFolder(text::fontsDirectory());
            d.fontsDirty = true;
        }
        if (ImGui::BeginPopupModal("##deleteText", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextUnformatted(tr("Delete this text file? This cannot be undone."));
            if (ImGui::Button(tr("Delete"))) {
                text::deleteTextDoc(d.textName);
                d.textFilesDirty = true;
                d.textStatus = tr("The text file was deleted.");
                d.textStatusBad = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button(tr("Cancel"))) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        if (!d.textStatus.empty()) {
            textLine(d.textStatusBad ? kBad : kGood, d.textStatus);
        }

        ImGui::TableSetColumnIndex(1);
        advanceClock(d.clock, d.lastTime, d.playing);
        engine_.requestTextPreview(d.textDoc, d.clock);
        drawPreviewImage(engine_.generatorPreviewTexture(), 330.0f);
        ImGui::Checkbox(tr("Play"), &d.playing);
        ImGui::SameLine();
        if (ImGui::SmallButton(tr("Restart"))) {
            d.clock = 0.0;
        }
        if (TextPlayer* player = engine_.textEditorPlayer()) {
            if (player->state() == GeneratorState::Failed) {
                textLine(kBad, trMessage(player->error()));
            } else if (player->state() == GeneratorState::Ready) {
                hint(tr("%d letters, drawn at %d px from a %d px atlas, %s"), player->glyphCount(), player->bakedHeight(), player->atlasSide(), megabytes(player->gpuBytes()).c_str());
            } else {
                hint("%s", tr("Preparing the letters..."));
            }
            if (!player->note().empty()) {
                textLine(kWarn, trMessage(player->note()));
            }
        }
        hint("%s", tr("The preview moves with the music that is playing."));
        ImGui::EndTable();
    }
}

void App::drawGeneratorParticleTab() {
    GeneratorDialog& d = generatorDialog_;
    if (d.particleFilesDirty) {
        d.particleFiles = particles::listParticleDocs();
        d.particleFilesDirty = false;
    }
    if (ImGui::BeginTable("##particleLayout", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("##controls", ImGuiTableColumnFlags_WidthStretch, 1.15f);
        ImGui::TableSetupColumn("##preview", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);

        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::BeginCombo("##startFrom", tr("Start from a preset or a saved file"))) {
            hint("%s", tr("Built-in"));
            for (const particles::Preset& preset : particles::builtinPresets()) {
                if (ImGui::Selectable(preset.name.c_str())) {
                    d.params = preset.params;
                    d.particleName[0] = '\0';
                    d.particleStatus.clear();
                    d.clock = 0.0;
                }
            }
            ImGui::Separator();
            hint("%s", tr("Graphics card (experimental)"));
            for (const particles::Preset& preset : particles::gpuPresets()) {
                if (ImGui::Selectable(preset.name.c_str())) {
                    d.params = preset.params;
                    d.particleName[0] = '\0';
                    d.particleStatus.clear();
                    d.clock = 0.0;
                }
            }
            ImGui::Separator();
            hint("%s", tr("Saved"));
            for (const std::string& name : d.particleFiles) {
                if (ImGui::Selectable(name.c_str(), name == d.particleName)) {
                    openGeneratorStudio(1, name);
                }
            }
            if (d.particleFiles.empty()) {
                hint("%s", tr("No saved particle files yet"));
            }
            ImGui::EndCombo();
        }

        particles::Params& p = d.params;
        if (ImGui::CollapsingHeader(tr("Graphics card"), ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Checkbox(tr("Run on the graphics card"), &p.gpu);
            ImGui::SetItemTooltip("%s", tr("The graphics card keeps and moves every particle itself, so far more particles are possible. It needs Experimental GPU particles turned on in Settings, Performance. Otherwise, or when the card cannot do it, the clip runs on the processor with fewer particles."));
            if (p.gpu) {
                ImGui::SetNextItemWidth(ui::px(220.0f));
                ImGui::SliderInt(tr("Particle pool"), &p.gpuCount, particles::kGpuMinCount, particles::kGpuMaxCount, "%d", ImGuiSliderFlags_Logarithmic);
                ImGui::SetItemTooltip("%s", tr("How many particles the graphics card keeps. When the pool is full, the oldest particles are reused for new ones."));
            }
        }
        if (ImGui::CollapsingHeader(tr("Emission"), ImGuiTreeNodeFlags_DefaultOpen)) {
            ui::sliderFloat(tr("Particles per second"), &p.rate, 0.0f, p.gpu ? particles::kGpuMaxRate : particles::kCpuMaxRate, "%.0f", p.gpu ? ImGuiSliderFlags_Logarithmic : 0);
            ui::sliderFloat(tr("Burst on every beat"), &p.burst, 0.0f, static_cast<float>(p.gpu ? particles::kGpuMaxBurst : particles::kMaxBurst), "%.0f", p.gpu ? ImGuiSliderFlags_Logarithmic : 0);
            ui::sliderFloat(tr("Lifetime"), &p.life, 0.2f, 10.0f, "%.1f s");
            ui::sliderFloat(tr("Lifetime variation"), &p.lifeJitter, 0.0f, 1.0f, "%.2f");
        }
        if (ImGui::CollapsingHeader(tr("Motion"), ImGuiTreeNodeFlags_DefaultOpen)) {
            ui::sliderFloat(tr("Speed"), &p.speed, 0.0f, 3.0f, "%.2f");
            ui::sliderFloat(tr("Speed variation"), &p.speedJitter, 0.0f, 1.0f, "%.2f");
            ui::sliderFloat(tr("Direction"), &p.direction, -360.0f, 360.0f, "%.0f deg");
            ui::sliderFloat(tr("Spread"), &p.spread, 0.0f, 360.0f, "%.0f deg");
            ui::sliderFloat(tr("Gravity"), &p.gravity, -3.0f, 3.0f, "%.2f");
            ui::sliderFloat(tr("Drag"), &p.drag, 0.0f, 5.0f, "%.2f");
            ui::sliderFloat(tr("Turbulence"), &p.turbulence, 0.0f, 1.0f, "%.2f");
            ui::sliderFloat(tr("Swirl"), &p.swirl, -4.0f, 4.0f, "%.2f");
        }
        if (ImGui::CollapsingHeader(tr("Look"), ImGuiTreeNodeFlags_DefaultOpen)) {
            ui::sliderFloat(tr("Size"), &p.size, 0.002f, 0.25f, "%.3f");
            ui::sliderFloat(tr("Size at the end"), &p.sizeEnd, 0.0f, 3.0f, "%.2f");
            ui::sliderFloat(tr("Size variation"), &p.sizeJitter, 0.0f, 1.0f, "%.2f");
            ui::sliderFloat(tr("Fade in"), &p.fadeIn, 0.0f, 1.0f, "%.2f");
            ui::sliderFloat(tr("Softness"), &p.softness, 0.0f, 1.0f, "%.2f");
            enumCombo(tr("Colour"), p.color, 3, particleColorLabel);
            ImGui::Checkbox(tr("Add light (glow)"), &p.additive);
        }
        if (ImGui::CollapsingHeader(tr("Emitter"))) {
            enumCombo(tr("Shape"), p.shape, 4, shapeLabel);
            ui::sliderFloat(tr("Origin X"), &p.originX, -1.0f, 1.0f, "%.2f");
            ui::sliderFloat(tr("Origin Y"), &p.originY, -1.0f, 1.0f, "%.2f");
            ui::sliderFloat(tr("Extent"), &p.extent, 0.0f, 1.0f, "%.2f");
            int seed = static_cast<int>(p.seed & 0x7fffffffu);
            ImGui::SetNextItemWidth(ui::px(140.0f));
            if (ImGui::InputInt(tr("Random seed"), &seed)) {
                p.seed = static_cast<std::uint32_t>(std::max(seed, 0));
            }
        }
        if (ImGui::CollapsingHeader(tr("Music"))) {
            ui::sliderFloat(tr("Level adds particles"), &p.levelRate, 0.0f, 4.0f, "%.2f");
            ui::sliderFloat(tr("Level grows particles"), &p.levelSize, 0.0f, 2.0f, "%.2f");
            ui::sliderFloat(tr("Level speeds particles up"), &p.levelSpeed, 0.0f, 2.0f, "%.2f");
            hint("%s", tr("The beat fires the burst. The loudness of the music drives the three sliders above."));
        }
        particles::clampParams(p);

        const int limit = engine_.particleLimit();
        const float expected = p.rate * p.life;
        if (p.gpu) {
            const int pool = gpuparticles::capacityFor(p.gpuCount, static_cast<std::uint64_t>(engine_.gpuParticleBudgetMb()) * 1024ull * 1024ull);
            if (!engine_.gpuParticlesEnabled()) {
                textLine(kWarn, std::string(tr("GPU particles are turned off in Settings, Performance. This clip runs on the processor with fewer particles.")));
            } else if (pool < p.gpuCount) {
                textLine(kWarn, std::string(tr("The graphics memory budget in Settings, Performance is smaller than this pool. The pool is cut down.")));
            }
            if (expected > static_cast<float>(std::max(pool, particles::kGpuMinCount))) {
                textLine(kWarn, std::string(tr("At this rate and lifetime the pool is too small. The oldest particles are reused before they fade out.")));
            }
            hint(tr("Pool: %d particles, %s of graphics memory"), p.gpuCount, megabytes(gpuparticles::bytesFor(p.gpuCount)).c_str());
        } else {
            if (expected > static_cast<float>(limit)) {
                textLine(kWarn, std::string(tr("At this rate and lifetime more than the limit of particles would be alive. Newer particles are dropped. The limit is in Settings, Performance.")));
            }
            hint(tr("Limit: %d particles per layer, %s of graphics memory"), limit, megabytes(particles::gpuBytes(limit)).c_str());
        }

        ui::sectionHeader(tr("Save"));
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 180.0f);
        ImGui::InputTextWithHint("##particleName", tr("Name"), d.particleName, sizeof(d.particleName));
        ImGui::SameLine();
        ImGui::BeginDisabled(d.particleName[0] == '\0');
        if (ImGui::Button(tr("Save as clip"))) {
            std::string error;
            if (particles::saveParticleDoc(d.particleName, p, error)) {
                d.particleStatus = std::string(tr("Saved. The clip appears in the library as: ")) + particleClipName(particles::particleDocPath(d.particleName).stem().string());
                d.particleStatusBad = false;
                d.particleFilesDirty = true;
                shaderPoll_ = 1.0;
            } else {
                d.particleStatus = trMessage(error);
                d.particleStatusBad = true;
            }
        }
        ImGui::EndDisabled();
        ImGui::BeginDisabled(d.particleName[0] == '\0' || std::find(d.particleFiles.begin(), d.particleFiles.end(), std::string(d.particleName)) == d.particleFiles.end());
        if (ImGui::Button(tr("Delete"))) {
            ImGui::OpenPopup("##deleteParticles");
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button(tr("Open folder"))) {
            std::error_code ec;
            fs::create_directories(pathFromUtf8(particles::particlesDirectory()), ec);
            platform::openFolder(particles::particlesDirectory());
        }
        if (ImGui::BeginPopupModal("##deleteParticles", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextUnformatted(tr("Delete this particle file? This cannot be undone."));
            if (ImGui::Button(tr("Delete"))) {
                particles::deleteParticleDoc(d.particleName);
                d.particleFilesDirty = true;
                d.particleStatus = tr("The particle file was deleted.");
                d.particleStatusBad = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button(tr("Cancel"))) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        if (!d.particleStatus.empty()) {
            textLine(d.particleStatusBad ? kBad : kGood, d.particleStatus);
        }

        ImGui::TableSetColumnIndex(1);
        advanceClock(d.clock, d.lastTime, d.playing);
        engine_.requestParticlePreview(d.params, d.clock);
        drawPreviewImage(engine_.generatorPreviewTexture(), 330.0f);
        ImGui::Checkbox(tr("Play"), &d.playing);
        ImGui::SameLine();
        if (ImGui::SmallButton(tr("Restart"))) {
            d.clock = 0.0;
        }
        if (ParticlePlayer* player = engine_.particleEditorPlayer()) {
            const ParticlePlayer::BackendInfo& backend = player->backend();
            if (backend.gpu) {
                hint(tr("On the graphics card: %d of %d slots in use, %lld reused"), backend.used, backend.capacity, static_cast<long long>(backend.recycled));
            } else {
                hint(tr("%d particles alive, %lld dropped because of the limit"), player->alive(0), player->dropped());
            }
            if (!backend.note.empty()) {
                textLine(kWarn, trMessage(backend.note));
            }
        }
        hint("%s", tr("The preview reacts to the beat and the level of the music that is playing."));
        ImGui::EndTable();
    }
}

void App::drawGeneratorStudio() {
    GeneratorDialog& d = generatorDialog_;
    if (!d.open) {
        d.lastTime = -1.0;
        return;
    }
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ui::setNextWindowSize(ImVec2(std::min(980.0f, viewport->Size.x - 60.0f), std::min(720.0f, viewport->Size.y - 60.0f)), ImGuiCond_Appearing);
    ui::setNextWindowSizeConstraints(ImVec2(640.0f, 420.0f), ImVec2(FLT_MAX, FLT_MAX));
    if (d.focus) {
        ImGui::SetNextWindowFocus();
        d.focus = false;
    }
    const std::string title = std::string(trId("Text and particles", "generatorWindow"));
    if (!ui::beginTool(title.c_str(), &d.open, ImGuiWindowFlags_NoDocking)) {
        ImGui::End();
        return;
    }
    if (ImGui::BeginTabBar("##generatorTabs")) {
        const int wanted = d.requestedTab;
        d.requestedTab = -1;
        if (ImGui::BeginTabItem(tr("Text"), nullptr, wanted == 0 ? ImGuiTabItemFlags_SetSelected : 0)) {
            drawGeneratorTextTab();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem(tr("Particles"), nullptr, wanted == 1 ? ImGuiTabItemFlags_SetSelected : 0)) {
            drawGeneratorParticleTab();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::End();
}

void App::openChainLibrary(int target) {
    ChainDialog& d = chainDialog_;
    d.open = true;
    d.focus = true;
    d.target = std::clamp(target, 0, kLayerCount);
    d.dirty = true;
    d.status.clear();
    d.warnings.clear();
}

void App::drawChainButtons(std::array<EffectSlot, kEffectSlots>& slots) {
    int target = -1;
    for (int l = 0; l < kLayerCount; ++l) {
        if (&slots == &engine_.comp.layers[static_cast<size_t>(l)].fx) {
            target = l;
        }
    }
    if (&slots == &engine_.comp.master.fx) {
        target = kLayerCount;
    }
    if (target < 0) {
        return;
    }
    ImGui::PushID("chainButtons");
    if (ImGui::SmallButton(tr("Effect chains..."))) {
        openChainLibrary(target);
    }
    ImGui::SetItemTooltip("%s", tr("Save the four effects as a reusable chain, search saved chains, preview them and share them as a file."));
    ImGui::PopID();
}

void App::drawChainLibrary() {
    ChainDialog& d = chainDialog_;
    if (!d.open) {
        d.lastTime = -1.0;
        return;
    }
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ui::setNextWindowSize(ImVec2(std::min(860.0f, viewport->Size.x - 60.0f), std::min(700.0f, viewport->Size.y - 60.0f)), ImGuiCond_Appearing);
    ui::setNextWindowSizeConstraints(ImVec2(560.0f, 400.0f), ImVec2(FLT_MAX, FLT_MAX));
    if (d.focus) {
        ImGui::SetNextWindowFocus();
        d.focus = false;
    }
    const std::string title = std::string(trId("Effect chains", "chainWindow"));
    if (!ui::beginTool(title.c_str(), &d.open, ImGuiWindowFlags_NoDocking)) {
        ImGui::End();
        return;
    }
    if (d.dirty) {
        d.chains = chain::listChains();
        d.dirty = false;
    }

    std::array<EffectSlot, kEffectSlots>& slots = d.target >= kLayerCount ? engine_.comp.master.fx : engine_.comp.layers[static_cast<size_t>(d.target)].fx;

    char targetLabel[64];
    if (d.target >= kLayerCount) {
        std::snprintf(targetLabel, sizeof(targetLabel), "%s", tr("Master"));
    } else {
        std::snprintf(targetLabel, sizeof(targetLabel), "%s %d", tr("Layer"), d.target + 1);
    }
    ImGui::SetNextItemWidth(ui::px(160.0f));
    if (ImGui::BeginCombo(tr("Applies to"), targetLabel)) {
        for (int t = 0; t <= kLayerCount; ++t) {
            char label[64];
            if (t >= kLayerCount) {
                std::snprintf(label, sizeof(label), "%s", tr("Master"));
            } else {
                std::snprintf(label, sizeof(label), "%s %d", tr("Layer"), t + 1);
            }
            if (ImGui::Selectable(label, t == d.target)) {
                d.target = t;
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputTextWithHint("##chainFilter", tr("Search by name, description or effect"), d.filter, sizeof(d.filter));

    const std::vector<chain::ChainInfo> shown = chain::searchChains(d.chains, d.filter);
    if (ImGui::BeginTable("##chainLayout", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("##list", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("##detail", ImGuiTableColumnFlags_WidthStretch, 1.1f);
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::BeginChild("##chainList", ImVec2(0.0f, ImGui::GetContentRegionAvail().y - 4.0f), ImGuiChildFlags_Borders);
        if (shown.empty()) {
            hint("%s", d.chains.empty() ? tr("No saved chains yet. Set up the effects of a layer and save them below.") : tr("No chain matches the search."));
        }
        for (const chain::ChainInfo& info : shown) {
            ImGui::PushID(info.name.c_str());
            if (ImGui::Selectable(info.name.c_str(), info.name == d.selected)) {
                d.selected = info.name;
                d.status.clear();
                d.warnings.clear();
                d.confirmDelete = false;
                copyInto(d.exportPath, sizeof(d.exportPath), std::string());
            }
            if (!info.readable) {
                ImGui::SameLine();
                ImGui::TextColored(kBad, "%s", tr("(damaged)"));
            } else {
                const std::string effects = joinNames(info.effects);
                hint("%s", effects.empty() ? tr("empty") : effects.c_str());
                if (!info.description.empty()) {
                    hint("%s", info.description.c_str());
                }
            }
            ImGui::Spacing();
            ImGui::PopID();
        }
        ImGui::EndChild();

        ImGui::TableSetColumnIndex(1);
        advanceClock(d.clock, d.lastTime, true);
        chain::ChainDoc selectedDoc;
        bool haveSelected = false;
        std::string loadError;
        std::vector<std::string> loadWarnings;
        if (!d.selected.empty()) {
            haveSelected = chain::loadChain(d.selected, selectedDoc, loadError, &loadWarnings);
        }
        ImGui::Checkbox(tr("Show a preview"), &d.preview);
        if (d.preview) {
            engine_.requestChainPreview(haveSelected ? selectedDoc.slots : slots, d.clock);
            drawPreviewImage(engine_.chainPreviewTexture(), 220.0f);
            hint("%s", haveSelected ? tr("A test picture with the selected chain.") : tr("A test picture with the effects that are set now."));
        }

        if (!d.selected.empty()) {
            ui::sectionHeader(d.selected.c_str());
            if (!haveSelected) {
                textLine(kBad, trMessage(loadError));
            } else {
                if (!selectedDoc.missing.empty()) {
                    textLine(kWarn, std::string(tr("Not installed here, these slots stay empty: ")) + joinNames(selectedDoc.missing));
                }
                for (const std::string& warning : loadWarnings) {
                    textLine(kWarn, trMessage(warning));
                }
                if (ImGui::Button(tr("Apply"))) {
                    slots = selectedDoc.slots;
                    d.status = std::string(tr("Applied to ")) + targetLabel;
                    d.statusBad = false;
                }
                ImGui::SetItemTooltip("%s", tr("Replaces the four effect slots of the chosen layer. You can undo it with Ctrl+Z."));
                ImGui::SameLine();
                if (ImGui::Button(tr("Delete"))) {
                    d.confirmDelete = true;
                }
                if (d.confirmDelete) {
                    ImGui::SameLine();
                    ImGui::TextColored(kWarn, "%s", tr("Delete for good?"));
                    ImGui::SameLine();
                    if (ImGui::SmallButton(tr("Yes, delete"))) {
                        chain::deleteChain(d.selected);
                        d.selected.clear();
                        d.dirty = true;
                        d.confirmDelete = false;
                        d.status = tr("The chain was deleted.");
                        d.statusBad = false;
                    }
                    ImGui::SameLine();
                    if (ImGui::SmallButton(tr("Cancel"))) {
                        d.confirmDelete = false;
                    }
                }
                ImGui::SetNextItemWidth(ui::px(-130.0f));
                ImGui::InputTextWithHint("##chainExport", tr("Folder or file ending in .nyxchain"), d.exportPath, sizeof(d.exportPath));
                ImGui::SameLine();
                ImGui::BeginDisabled(d.exportPath[0] == '\0' || d.selected.empty());
                if (ImGui::Button(tr("Export"))) {
                    std::string error;
                    const fs::path target = pathFromUtf8(d.exportPath);
                    std::error_code ec;
                    if (target.extension().empty()) {
                        fs::create_directories(target, ec);
                    } else if (target.has_parent_path()) {
                        fs::create_directories(target.parent_path(), ec);
                    }
                    if (chain::exportChain(d.selected, target, error)) {
                        d.status = tr("The chain was exported.");
                        d.statusBad = false;
                    } else {
                        d.status = trMessage(error);
                        d.statusBad = true;
                    }
                }
                ImGui::EndDisabled();
            }
        }

        ui::sectionHeader(tr("Save the current effects as a chain"));
        const int used = chain::usedSlots(slots);
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::InputTextWithHint("##chainName", tr("Name"), d.saveName, sizeof(d.saveName));
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::InputTextWithHint("##chainDescription", tr("Short description (optional)"), d.description, sizeof(d.description));
        ImGui::BeginDisabled(d.saveName[0] == '\0' || used == 0);
        if (ImGui::Button(tr("Save chain"))) {
            std::string error;
            if (chain::saveChain(d.saveName, slots, d.description, error)) {
                d.status = tr("The chain was saved.");
                d.statusBad = false;
                d.selected = d.saveName;
                d.dirty = true;
            } else {
                d.status = trMessage(error);
                d.statusBad = true;
            }
        }
        ImGui::EndDisabled();
        if (used == 0) {
            ImGui::SameLine();
            hint("%s", tr("The chosen layer has no effects yet."));
        }

        ui::sectionHeader(tr("Import a chain file"));
        ImGui::SetNextItemWidth(ui::px(-130.0f));
        ImGui::InputTextWithHint("##chainImport", tr("Path of a .nyxchain file"), d.importPath, sizeof(d.importPath));
        ImGui::SameLine();
        ImGui::BeginDisabled(d.importPath[0] == '\0');
        if (ImGui::Button(tr("Import"))) {
            std::string error;
            std::string name;
            std::vector<std::string> warnings;
            if (chain::importChain(pathFromUtf8(d.importPath), name, error, &warnings)) {
                d.status = std::string(tr("Imported as: ")) + name;
                d.statusBad = false;
                d.selected = name;
                d.warnings = warnings;
                d.dirty = true;
                d.importPath[0] = '\0';
            } else {
                d.status = trMessage(error);
                d.statusBad = true;
            }
        }
        ImGui::EndDisabled();
        for (const std::string& warning : d.warnings) {
            textLine(kWarn, trMessage(warning));
        }
        if (!d.status.empty()) {
            textLine(d.statusBad ? kBad : kGood, d.status);
        }
        hint("%s", tr("Chains are small text files. Imported files are checked, nothing in them is ever run."));
        if (ImGui::SmallButton(tr("Open chains folder"))) {
            std::error_code ec;
            fs::create_directories(pathFromUtf8(chain::chainsDirectory()), ec);
            platform::openFolder(chain::chainsDirectory());
        }
        ImGui::EndTable();
    }
    ImGui::End();
}

}
