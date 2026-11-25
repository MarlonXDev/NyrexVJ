#include "app/app.hpp"

#include "app/ui_common.hpp"
#include "core/manual.hpp"
#include "platform/platform.hpp"

#include <algorithm>
#include <cfloat>
#include <string>
#include <utility>
#include <vector>

namespace nvj {

namespace {

constexpr float kCodePad = 4.0f;
constexpr float kBoldExtra = 0.6f;
constexpr float kLineGap = 4.0f;

const ImVec4 kLinkColor(0.45f, 0.72f, 1.0f, 1.0f);
const ImVec4 kLinkHover(0.70f, 0.86f, 1.0f, 1.0f);
const ImVec4 kCodeText(1.0f, 0.78f, 0.52f, 1.0f);
const ImVec4 kCodeBack(0.0f, 0.0f, 0.0f, 0.38f);
const ImVec4 kNoteTint(0.35f, 0.55f, 0.95f, 1.0f);
const ImVec4 kTipTint(0.35f, 0.80f, 0.45f, 1.0f);
const ImVec4 kWarnTint(1.0f, 0.45f, 0.35f, 1.0f);

struct Unit {
    std::string text;
    SpanStyle style = SpanStyle::Normal;
    const std::string* target = nullptr;
    ImVec2 pos;
    ImVec2 size;
};

struct Flow {
    std::vector<Unit> units;
    float height = 0.0f;
};

size_t fitPrefix(const std::string& text, float width) {
    size_t low = 1;
    size_t high = text.size();
    size_t best = 1;
    while (low <= high) {
        const size_t mid = low + (high - low) / 2;
        size_t cut = mid;
        while (cut > 1 && cut < text.size() && (static_cast<unsigned char>(text[cut]) & 0xC0) == 0x80) {
            --cut;
        }
        if (ImGui::CalcTextSize(text.c_str(), text.c_str() + cut).x <= width) {
            best = cut;
            low = mid + 1;
        } else {
            if (mid == 0) {
                break;
            }
            high = mid - 1;
        }
    }
    return std::max<size_t>(best, 1);
}

Flow layoutFlow(const ManualText& text, float width, bool allBold) {
    Flow flow;
    const float lineHeight = ImGui::GetTextLineHeight();
    const float lineStep = lineHeight + kLineGap;
    const float space = ImGui::CalcTextSize(" ").x;
    float x = 0.0f;
    float y = 0.0f;
    bool spaceBefore = false;
    width = std::max(width, 40.0f);

    auto place = [&](const std::string& word, SpanStyle style, const std::string* target, float extra) {
        const float w = ImGui::CalcTextSize(word.c_str()).x + extra;
        float gap = (x > 0.0f && spaceBefore) ? space : 0.0f;
        if (x > 0.0f && x + gap + w > width) {
            x = 0.0f;
            y += lineStep;
            gap = 0.0f;
        }
        Unit unit;
        unit.text = word;
        unit.style = style;
        unit.target = target;
        unit.pos = ImVec2(x + gap, y);
        unit.size = ImVec2(w, lineHeight);
        flow.units.push_back(std::move(unit));
        x += gap + w;
        spaceBefore = false;
    };

    for (const ManualSpan& span : text) {
        SpanStyle style = span.style;
        if (allBold && style == SpanStyle::Normal) {
            style = SpanStyle::Bold;
        }
        if (style == SpanStyle::Code) {
            const float natural = ImGui::CalcTextSize(span.text.c_str()).x + kCodePad * 2.0f;
            if (natural <= width) {
                place(span.text, style, nullptr, kCodePad * 2.0f);
            } else {
                std::string rest = span.text;
                bool firstChunk = true;
                while (!rest.empty()) {
                    const size_t cut = fitPrefix(rest, width - kCodePad * 2.0f);
                    if (!firstChunk && x > 0.0f) {
                        x = width;
                    }
                    place(rest.substr(0, cut), style, nullptr, kCodePad * 2.0f);
                    rest.erase(0, cut);
                    firstChunk = false;
                }
            }
            continue;
        }
        const float extra = style == SpanStyle::Bold ? kBoldExtra : 0.0f;
        const std::string* target = style == SpanStyle::Link ? &span.target : nullptr;
        std::string token;
        for (size_t i = 0; i <= span.text.size(); ++i) {
            const bool end = i == span.text.size();
            if (!end && span.text[i] != ' ') {
                token.push_back(span.text[i]);
                continue;
            }
            if (!token.empty()) {
                place(token, style, target, extra);
                token.clear();
            }
            if (!end) {
                spaceBefore = true;
            }
        }
    }
    flow.height = flow.units.empty() ? 0.0f : flow.units.back().pos.y + lineHeight;
    return flow;
}

void drawFlow(const Flow& flow, const ImVec2& origin, const ImVec4& baseColor, std::string& clicked) {
    ImDrawList* list = ImGui::GetWindowDrawList();
    ImFont* font = ImGui::GetFont();
    const float size = ImGui::GetFontSize();
    const ImU32 base = ImGui::GetColorU32(baseColor);
    const bool windowHovered = ImGui::IsWindowHovered();
    for (const Unit& unit : flow.units) {
        const ImVec2 at(origin.x + unit.pos.x, origin.y + unit.pos.y);
        switch (unit.style) {
        case SpanStyle::Bold:
            list->AddText(font, size, at, base, unit.text.c_str());
            list->AddText(font, size, ImVec2(at.x + kBoldExtra, at.y), base, unit.text.c_str());
            break;
        case SpanStyle::Code:
            list->AddRectFilled(ImVec2(at.x, at.y - 1.0f), ImVec2(at.x + unit.size.x, at.y + unit.size.y + 1.0f), ImGui::GetColorU32(kCodeBack), 3.0f);
            list->AddText(font, size, ImVec2(at.x + kCodePad, at.y), ImGui::GetColorU32(kCodeText), unit.text.c_str());
            break;
        case SpanStyle::Link: {
            const ImVec2 max(at.x + unit.size.x, at.y + unit.size.y);
            const bool hovered = windowHovered && ImGui::IsMouseHoveringRect(at, max);
            const ImU32 color = ImGui::GetColorU32(hovered ? kLinkHover : kLinkColor);
            list->AddText(font, size, at, color, unit.text.c_str());
            list->AddLine(ImVec2(at.x, max.y), ImVec2(max.x, max.y), color, 1.0f);
            if (hovered) {
                ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && unit.target) {
                    clicked = *unit.target;
                }
            }
            break;
        }
        default:
            list->AddText(font, size, at, base, unit.text.c_str());
            break;
        }
    }
}

float paragraph(const ManualText& text, float width, std::string& clicked, bool allBold = false) {
    const Flow flow = layoutFlow(text, width, allBold);
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    drawFlow(flow, origin, ImGui::GetStyleColorVec4(ImGuiCol_Text), clicked);
    ImGui::Dummy(ImVec2(width, std::max(flow.height, ImGui::GetTextLineHeight())));
    return flow.height;
}

void listItems(const ManualBlock& block, float width, std::string& clicked) {
    const float indent = 22.0f;
    const bool numbered = block.kind == BlockKind::Numbered;
    for (size_t i = 0; i < block.items.size(); ++i) {
        const Flow flow = layoutFlow(block.items[i], width - indent, false);
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        ImDrawList* list = ImGui::GetWindowDrawList();
        const float lineHeight = ImGui::GetTextLineHeight();
        if (numbered) {
            const std::string label = std::to_string(i + 1) + ".";
            list->AddText(ImVec2(origin.x + 2.0f, origin.y), ImGui::GetColorU32(ui::kAccent), label.c_str());
        } else {
            list->AddCircleFilled(ImVec2(origin.x + 8.0f, origin.y + lineHeight * 0.5f), 2.6f, ImGui::GetColorU32(ui::kAccent));
        }
        drawFlow(flow, ImVec2(origin.x + indent, origin.y), ImGui::GetStyleColorVec4(ImGuiCol_Text), clicked);
        ImGui::Dummy(ImVec2(width, std::max(flow.height, lineHeight)));
        ImGui::Dummy(ImVec2(0.0f, 2.0f));
    }
}

void noteBox(const ManualBlock& block, float width, std::string& clicked) {
    const float pad = 9.0f;
    const float bar = 4.0f;
    ImVec4 tint = kNoteTint;
    const char* label = tr("Note");
    if (block.note == NoteKind::Tip) {
        tint = kTipTint;
        label = tr("Tip");
    } else if (block.note == NoteKind::Warning) {
        tint = kWarnTint;
        label = tr("Warning");
    }
    ManualText text;
    ManualSpan head;
    head.text = std::string(label) + ":";
    head.style = SpanStyle::Bold;
    text.push_back(head);
    if (!block.items.empty()) {
        text.insert(text.end(), block.items.front().begin(), block.items.front().end());
        if (text.size() > 1 && text[1].style == SpanStyle::Normal && !text[1].text.empty() && text[1].text.front() != ' ') {
            text[1].text.insert(text[1].text.begin(), ' ');
        }
    }
    const float inner = width - pad * 2.0f - bar;
    const Flow flow = layoutFlow(text, inner, false);
    const float boxHeight = flow.height + pad * 2.0f;
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImDrawList* list = ImGui::GetWindowDrawList();
    list->AddRectFilled(origin, ImVec2(origin.x + width, origin.y + boxHeight), ImGui::GetColorU32(ImVec4(tint.x, tint.y, tint.z, 0.12f)), 4.0f);
    list->AddRectFilled(origin, ImVec2(origin.x + bar, origin.y + boxHeight), ImGui::GetColorU32(tint), 4.0f, ImDrawFlags_RoundCornersLeft);
    drawFlow(flow, ImVec2(origin.x + bar + pad, origin.y + pad), ImGui::GetStyleColorVec4(ImGuiCol_Text), clicked);
    ImGui::Dummy(ImVec2(width, boxHeight));
}

void codeBox(const ManualBlock& block, float width, size_t index) {
    int lines = 1;
    for (char c : block.text) {
        if (c == '\n') {
            ++lines;
        }
    }
    const float height = std::min(static_cast<float>(lines) * ImGui::GetTextLineHeight() + 16.0f, 340.0f);
    std::string copy = block.text;
    ImGui::PushID(static_cast<int>(index));
    ImGui::PushStyleColor(ImGuiCol_Text, kCodeText);
    ImGui::InputTextMultiline("##code", &copy[0], copy.size() + 1, ImVec2(width, height), ImGuiInputTextFlags_ReadOnly);
    ImGui::PopStyleColor();
    ImGui::PopID();
}

void tableBlock(const ManualBlock& block, float width, size_t index, std::string& clicked) {
    const int columns = std::max(block.columns, 1);
    const size_t rows = block.items.size() / static_cast<size_t>(columns);
    const float padding = ImGui::GetStyle().CellPadding.x * 2.0f + 8.0f;
    std::vector<float> minimum(static_cast<size_t>(columns), 0.0f);
    std::vector<float> natural(static_cast<size_t>(columns), 0.0f);
    for (size_t r = 0; r < rows; ++r) {
        for (int c = 0; c < columns; ++c) {
            const std::string plain = plainText(block.items[r * static_cast<size_t>(columns) + static_cast<size_t>(c)]);
            float longest = 0.0f;
            size_t from = 0;
            while (from <= plain.size()) {
                size_t to = plain.find(' ', from);
                if (to == std::string::npos) {
                    to = plain.size();
                }
                if (to > from) {
                    longest = std::max(longest, ImGui::CalcTextSize(plain.c_str() + from, plain.c_str() + to).x);
                }
                from = to + 1;
            }
            const float whole = ImGui::CalcTextSize(plain.c_str()).x;
            const size_t column = static_cast<size_t>(c);
            minimum[column] = std::max(minimum[column], longest + padding);
            natural[column] = std::max(natural[column], whole + padding);
        }
    }
    const float available = std::max(width - static_cast<float>(columns + 1), 60.0f);
    std::vector<float> widths(minimum);
    float minimumSum = 0.0f;
    float naturalSum = 0.0f;
    for (size_t c = 0; c < minimum.size(); ++c) {
        minimumSum += minimum[c];
        naturalSum += std::max(natural[c], minimum[c]);
    }
    if (naturalSum <= available) {
        for (size_t c = 0; c < widths.size(); ++c) {
            widths[c] = std::max(natural[c], minimum[c]) + (available - naturalSum) * std::max(natural[c], minimum[c]) / std::max(naturalSum, 1.0f);
        }
    } else if (minimumSum < available) {
        float want = 0.0f;
        for (size_t c = 0; c < widths.size(); ++c) {
            want += std::max(natural[c] - minimum[c], 0.0f);
        }
        const float spare = available - minimumSum;
        for (size_t c = 0; c < widths.size(); ++c) {
            widths[c] = minimum[c] + spare * std::max(natural[c] - minimum[c], 0.0f) / std::max(want, 1.0f);
        }
    } else {
        for (size_t c = 0; c < widths.size(); ++c) {
            widths[c] = available * minimum[c] / std::max(minimumSum, 1.0f);
        }
    }
    ImGui::PushID(static_cast<int>(index));
    if (ImGui::BeginTable("##table", columns, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoSavedSettings)) {
        for (int c = 0; c < columns; ++c) {
            ImGui::TableSetupColumn(nullptr, ImGuiTableColumnFlags_WidthFixed, widths[static_cast<size_t>(c)]);
        }
        for (size_t r = 0; r < rows; ++r) {
            ImGui::TableNextRow();
            for (int c = 0; c < columns; ++c) {
                ImGui::TableSetColumnIndex(c);
                const float cell = widths[static_cast<size_t>(c)] - ImGui::GetStyle().CellPadding.x * 2.0f - 2.0f;
                paragraph(block.items[r * static_cast<size_t>(columns) + static_cast<size_t>(c)], cell, clicked, r == 0);
            }
        }
        ImGui::EndTable();
    }
    ImGui::PopID();
}

}

void App::openManual(const std::string& target) {
    manualOpen_ = true;
    manualFocus_ = true;
    if (!target.empty()) {
        const Manual& m = manual(currentLanguage() == Language::German);
        const ManualLocation location = resolveTarget(m, target.front() == '#' ? target : "#" + target);
        if (location.valid) {
            manualGo(location.chapter, location.section, false);
        }
    }
}

void App::manualGo(size_t chapter, const std::string& section, bool remember) {
    if (remember) {
        manualView_.history.emplace_back(manualView_.chapter, manualView_.section);
        if (manualView_.history.size() > 64) {
            manualView_.history.erase(manualView_.history.begin());
        }
    }
    manualView_.chapter = chapter;
    manualView_.section = section;
    manualView_.scrollTop = section.empty();
    manualView_.scrollToSection = !section.empty();
}

void App::drawManual() {
    if (!manualOpen_) {
        return;
    }
    const Manual& m = manual(currentLanguage() == Language::German);
    if (m.chapters.empty()) {
        manualOpen_ = false;
        return;
    }
    if (manualView_.chapter >= m.chapters.size()) {
        manualView_.chapter = 0;
    }
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(std::min(1040.0f, viewport->Size.x - 60.0f), std::min(700.0f, viewport->Size.y - 60.0f)), ImGuiCond_Appearing);
    ImGui::SetNextWindowSizeConstraints(ImVec2(520.0f, 340.0f), ImVec2(FLT_MAX, FLT_MAX));
    if (manualFocus_) {
        ImGui::SetNextWindowFocus();
        manualFocus_ = false;
    }
    const std::string title = std::string(trId("User manual", "manualWindow"));
    if (!ImGui::Begin(title.c_str(), &manualOpen_, ImGuiWindowFlags_NoDocking)) {
        ImGui::End();
        return;
    }

    ImGuiIO& io = ImGui::GetIO();
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_F, false)) {
        manualView_.focusSearch = true;
    }

    const float avail = ImGui::GetContentRegionAvail().x;
    const float leftWidth = std::clamp(avail * 0.28f, 190.0f, 290.0f);
    manualView_.clicked.clear();

    ImGui::BeginChild("##manualLeft", ImVec2(leftWidth, 0.0f), ImGuiChildFlags_Borders);
    if (manualView_.focusSearch) {
        ImGui::SetKeyboardFocusHere();
        manualView_.focusSearch = false;
    }
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputTextWithHint("##manualSearch", tr("Search the manual"), manualView_.search, sizeof(manualView_.search));
    ImGui::Spacing();
    const bool searching = manualView_.search[0] != '\0';
    if (searching) {
        const std::vector<ManualHit> hits = searchManual(m, manualView_.search);
        if (hits.empty()) {
            ImGui::TextWrapped("%s", tr("No chapter matches your search."));
        }
        for (size_t h = 0; h < hits.size(); ++h) {
            const ManualHit& hit = hits[h];
            ImGui::PushID(static_cast<int>(h));
            if (ImGui::Selectable(m.chapters[hit.chapter].title.c_str(), false)) {
                manualGo(hit.chapter, hit.section, true);
                manualView_.search[0] = '\0';
            }
            if (!hit.snippet.empty()) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
                ImGui::PushTextWrapPos(0.0f);
                ImGui::TextUnformatted(hit.snippet.c_str());
                ImGui::PopTextWrapPos();
                ImGui::PopStyleColor();
            }
            ImGui::Spacing();
            ImGui::PopID();
        }
    } else {
        ImGui::SeparatorText(tr("Contents"));
        for (size_t c = 0; c < m.chapters.size(); ++c) {
            ImGui::PushID(static_cast<int>(c));
            const bool current = c == manualView_.chapter;
            if (current) {
                ImGui::PushStyleColor(ImGuiCol_Text, ui::kAccent);
            }
            if (ImGui::Selectable(m.chapters[c].title.c_str(), current)) {
                manualGo(c, std::string(), true);
            }
            if (current) {
                ImGui::PopStyleColor();
                ImGui::Indent(14.0f);
                for (const ManualBlock& block : m.chapters[c].blocks) {
                    if (block.kind == BlockKind::Subheading && block.level == 2) {
                        ImGui::PushID(block.id.c_str());
                        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
                        if (ImGui::Selectable(plainText(block.items.front()).c_str(), manualView_.section == block.id)) {
                            manualGo(c, block.id, true);
                        }
                        ImGui::PopStyleColor();
                        ImGui::PopID();
                    }
                }
                ImGui::Unindent(14.0f);
            }
            ImGui::PopID();
        }
    }
    if (currentLanguage() != Language::German && currentLanguage() != Language::English) {
        ImGui::Spacing();
        ImGui::TextDisabled("%s", tr("The manual is written in English and German. You see the English version."));
    }
    ImGui::EndChild();

    ImGui::SameLine();
    if (manualView_.scrollTop) {
        ImGui::SetNextWindowScroll(ImVec2(0.0f, 0.0f));
        manualView_.scrollTop = false;
    }
    ImGui::BeginChild("##manualRight", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders);
    const ManualChapter& chapter = m.chapters[manualView_.chapter];
    const float width = std::max(ImGui::GetContentRegionAvail().x - 6.0f, 120.0f);
    if (!manualView_.history.empty()) {
        if (ImGui::SmallButton(tr("Back"))) {
            const std::pair<size_t, std::string> previous = manualView_.history.back();
            manualView_.history.pop_back();
            manualGo(previous.first, previous.second, false);
        }
        ImGui::SameLine();
    }
    ImGui::PushFont(nullptr, 28.0f);
    ImGui::TextColored(ui::kAccent, "%s", chapter.title.c_str());
    ImGui::PopFont();
    ImGui::Separator();
    ImGui::Spacing();

    for (size_t b = 0; b < chapter.blocks.size(); ++b) {
        const ManualBlock& block = chapter.blocks[b];
        switch (block.kind) {
        case BlockKind::Subheading: {
            ImGui::Spacing();
            if (manualView_.scrollToSection && manualView_.section == block.id) {
                ImGui::SetScrollHereY(0.0f);
                manualView_.scrollToSection = false;
            }
            ImGui::PushFont(nullptr, block.level == 2 ? 21.0f : 17.0f);
            ImGui::PushStyleColor(ImGuiCol_Text, block.level == 2 ? ui::kAccentHover : ImGui::GetStyleColorVec4(ImGuiCol_Text));
            ImGui::TextUnformatted(plainText(block.items.front()).c_str());
            ImGui::PopStyleColor();
            ImGui::PopFont();
            if (block.level == 2) {
                ImGui::Separator();
            }
            ImGui::Spacing();
            break;
        }
        case BlockKind::Paragraph:
            paragraph(block.items.front(), width, manualView_.clicked);
            ImGui::Dummy(ImVec2(0.0f, 6.0f));
            break;
        case BlockKind::Bullets:
        case BlockKind::Numbered:
            listItems(block, width, manualView_.clicked);
            ImGui::Dummy(ImVec2(0.0f, 4.0f));
            break;
        case BlockKind::Table:
            tableBlock(block, width, b, manualView_.clicked);
            ImGui::Dummy(ImVec2(0.0f, 8.0f));
            break;
        case BlockKind::Code:
            codeBox(block, width, b);
            ImGui::Dummy(ImVec2(0.0f, 6.0f));
            break;
        case BlockKind::Note:
            noteBox(block, width, manualView_.clicked);
            ImGui::Dummy(ImVec2(0.0f, 8.0f));
            break;
        case BlockKind::Figure:
            break;
        }
    }

    ImGui::Separator();
    ImGui::Spacing();
    const size_t count = m.chapters.size();
    if (manualView_.chapter > 0) {
        if (ImGui::Button((std::string("< ") + tr("Previous chapter")).c_str())) {
            manualGo(manualView_.chapter - 1, std::string(), true);
        }
        ImGui::SetItemTooltip("%s", m.chapters[manualView_.chapter - 1].title.c_str());
        ImGui::SameLine();
    }
    if (manualView_.chapter + 1 < count) {
        if (ImGui::Button((std::string(tr("Next chapter")) + " >").c_str())) {
            manualGo(manualView_.chapter + 1, std::string(), true);
        }
        ImGui::SetItemTooltip("%s", m.chapters[manualView_.chapter + 1].title.c_str());
    }
    ImGui::Dummy(ImVec2(0.0f, 12.0f));
    ImGui::EndChild();
    ImGui::End();

    if (!manualView_.clicked.empty()) {
        const std::string target = manualView_.clicked;
        manualView_.clicked.clear();
        if (isInternalTarget(target)) {
            const ManualLocation location = resolveTarget(m, target);
            if (location.valid) {
                manualGo(location.chapter, location.section, true);
            }
        } else if (isAllowedManualLink(target)) {
            platform::openUrl(target);
        }
    }
}

}
