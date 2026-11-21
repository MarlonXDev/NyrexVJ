# ADR 0020: Studio design as the new default, four more interface languages and a system font for Japanese

- **Status:** Accepted (2026-11-21, requested by MarlonXDev), implemented in v0.27.0. Issue [#66](https://github.com/MarlonXDev/NyrexVJ/issues/66).
- **Date:** 2026-11-21
- **Owners:** Maintainers (pending assignment)
- **Related:** [LANGUAGES.md](../LANGUAGES.md), [DECISIONS.md](../DECISIONS.md)

## Context
The maintainer sent a picture of another VJ program with a dark, calm look (neutral dark greys, a red-orange accent, rounded corners, generous spacing, flat tabs and slim sliders) and asked for the same look in NyrexVJ, with the existing font and all existing texts kept, and for more languages that are translated well. In the answers to the questions asked before the work started the choices were: the look becomes the new default (colours and style, not a new window layout), the old accent colours stay selectable, English, German, Spanish and French stay as they are, and the new languages are Italian, Portuguese, Dutch and Japanese.

## Decision

**Design.**
1. A new accent theme `studio` (red-orange) is the first in the list and the default. `applyTheme()` builds the whole palette from a neutral dark base and the chosen accent: frame, tab, header, popup, border and scrollbar colours are derived from the accent with small mixing factors, so the nine older accents (Orange, Blue, Red, Green, Purple, Pink, Cyan, Gold, Mono) still work and look consistent.
2. The style gets rounded corners (frames 6, popups 8, tabs 6), more padding (window 10, frame 9 x 5) and spacing, and 1 px borders. The font stays the built-in font and no text changed. The window layout (deck on top, panels at the bottom, browser on the right) stays; the deck tabs and transport bar of the picture were not copied.
3. Sliders get two small wrappers, `ui::sliderFloat` and `ui::sliderInt` in `src/app/ui_common.hpp`. They draw a rounded track and a filled part up to the value under the normal ImGui slider, which keeps its value text, dragging, typing and Ctrl+click. The grab is a narrow bar (6 px) so it does not hide the centred value. 54 call sites were switched.
4. Migration: a new setting `design` (0 when the file has none, 1 now). On the first start of this version, `design` becomes 1 and a saved theme `orange` (the old default) becomes `studio`; every other saved theme is kept. The result is saved, so a person who then picks Orange again keeps it.

**Languages.**
5. Italian (`it`), Portuguese (`pt`), Dutch (`nl`) and Japanese (`ja`) are built in, each as a table with the same 1,302 keys as the German table (`src/core/i18n_{it,pt,nl,ja}.cpp`). They are machine assisted and not reviewed by native speakers, which is stated in the README, in LANGUAGES.md and in the note under the language list.
6. One glossary was used for every table so a word means one thing everywhere (layer, column, clip, effect, preset, beat, bar, output, settings, venue, slice, and Level for the audio gain). Clip, effect and preset names and BLACKOUT stay English. Italian, Portuguese and Dutch use only Latin-1 characters and straight quotes, so the built-in font shows them.
7. English, German, Spanish and French are unchanged except for one text: the note under the language list, whose key now names all languages (the key changed, so the three tables were re-keyed in that one line).
8. The built-in language codes now include `it`; the example in LANGUAGES.md and the user-language tests use `sv`, because a file whose code clashes with a built-in code is rejected.

**Japanese and the font.**
9. The built-in font has no Japanese glyphs. When Japanese is selected, `src/app/fonts.cpp` looks for a system font (Windows: Yu Gothic, Meiryo, MS Gothic; Linux: Noto Sans CJK, IPAGothic and similar; macOS: Hiragino) and merges it into the interface font (`MergeMode`), so it supplies only the characters the main font lacks. Fonts are added before the next frame starts (`App::applyPendingFonts`, called from the main loop before `ImGui_ImplOpenGL3_NewFrame`), and Dear ImGui 1.93 loads glyphs on demand, so no glyph ranges are listed and a large atlas is not built.
10. No font is bundled or downloaded. If none is found, the language list shows `Japanese` instead of `日本語` and Settings, General shows an English hint that names Yu Gothic and Noto Sans CJK JP. The native label is only used once a font was merged.
11. Language files that people write themselves stay limited to Latin-1. The system font is used only for the built-in Japanese table.

## Alternatives
- **Bundle a CJK font.** A usable Japanese font is several megabytes (or a subset has to be built and its licence tracked), the repository and every download would grow for a language most users do not select, and the licence text would have to ship. Rejected for now; the system font costs nothing and can be replaced by a bundled subset later.
- **Replace the interface font with one that has more glyphs.** The maintainer likes the current font and asked to keep it.
- **Copy the window layout of the picture.** Not asked for in the answers; it would have changed where every panel lives and the saved window layouts. Colours and style were chosen.
- **A new theme system instead of an accent plus derived palette.** More work and more settings for no clear gain; the derived palette keeps the old accents working with one code path.
- **Native review before shipping the languages.** Would delay them with no reviewers named. Shipping them machine assisted and marked as such, with a clear way to correct lines, follows what was done for Spanish and French in v0.16.0.

## Consequences
- The default look changes for everyone on the first start, and a saved Orange accent is switched to Studio once. Other accents and every setting stay.
- Eight languages must stay in step with the German key list; `scripts/check_i18n.py` and `nvj_i18n_tests` check keys, edge spaces and format specifiers for all of them, and the character check for Latin-1 applies to every table except German and Japanese.
- Longer texts in Italian, Portuguese and Dutch can be wider than the English ones. Two widths were found too narrow when the interface was looked at (the transition list and the audio source lists), and those boxes now widen to their text. Other places were not measured for every text.
- Japanese works only where a system font exists. Without one the text is shown as missing glyphs, which the hint under the language list explains.
- The screenshots in the README and the main picture on the website were replaced with the new look.

## Validation / open questions
- Done: build and all tests on Linux (software OpenGL), the interface was captured as pictures in Italian, Portuguese, Dutch and Japanese (main window, Settings pages, welcome screen) with Noto Sans CJK for Japanese, the migration is covered by a test, and the key, space and specifier checks run for all tables.
- Not done: no native speaker read any of the four new tables; the Japanese merge was not tried on Windows fonts or macOS; the design was only seen in software OpenGL pictures, not on a real graphics card or on a screen with another scale; the text widths of every Italian, Portuguese and Dutch string were not measured.
- Open: whether to bundle a Japanese font subset, and whether more languages (for example Korean, Chinese or Russian) are wanted, which would need the same font step and, for Russian, a Cyrillic font.
