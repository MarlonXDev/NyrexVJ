# User manual

NyrexVJ has one user manual, written in English and in German. It exists in three places that all come from the same two files:

| Where | What the reader gets |
| --- | --- |
| In the program | A window with a contents list, a search field, links between chapters and a Back button. Menu **Help**, **User manual**, or `F1`. Works without internet. |
| On the website | One page per chapter, a contents page and the whole manual on one page, with pictures. `site/manual/` (English) and `site/de/handbuch/` (German). |
| In the repository | `docs/manual/manual.en.md` and `docs/manual/manual.de.md`, plain Markdown. |

The source is the only place that is edited by hand. The program embeds it at build time and the website pages are generated from it.

## Files

| File | Purpose |
| --- | --- |
| `docs/manual/manual.en.md`, `docs/manual/manual.de.md` | The manual. Same chapters, same section ids, same blocks in both languages. |
| `src/core/manual.hpp`, `src/core/manual.cpp` | Parser, search and links. No dependency on the window or OpenGL, so it is tested on its own. |
| `src/app/manual_view.hpp`, `src/app/ui_manual.cpp` | The window. |
| `CMakeLists.txt` | `nyrexvj_embed_text(nvjManualEn ...)` and `nvjManualDe` embed the two files. The program holds no manual file at run time. |
| `tests/test_manual.cpp`, `tests/data/manual_fixture.md`, `tests/data/manual_fixture.outline.txt` | `nvj_manual_tests`. |
| `scripts/build_manual_site.py` | Generates the website pages from the manual and checks the website. |
| `scripts/make_manual_shots.py`, `scripts/manual_shots/seed/` | Makes the pictures of the website with the program itself. |
| `site/manual/`, `site/de/handbuch/`, `site/assets/manual/` | Generated pages and the pictures. Do not edit the pages by hand. |

## In the program

- **Open and close:** menu Help, User manual, or `F1` (it toggles). The start option `--manual` opens it at start, `--manual outputs` at the chapter with that id and `--manual outputs/section-id` at a section. An id that does not exist opens the window at the page it was left on (the first chapter at first). The Welcome window and the list of shortcuts in Settings name `F1`.
- **Language:** with the interface in German the German manual is shown, with every other language the English one. For languages other than English and German the window adds one line under the contents that says so.
- **Contents:** all chapters on the left; the open chapter shows its sections (level 2 headings) under it. At the end of each chapter are the buttons to the previous and the next chapter.
- **Search:** the field above the contents. `Ctrl+F` focuses it while the manual window has focus. A chapter is a hit when every word you typed occurs somewhere in it (title, headings or text). Hits are ordered by where the words are: in the title first, then in headings, then in the text; chapters with the same score keep the order of the manual. Under each hit the window shows a snippet around the first word. Search ignores case, including `Ä`, `Ö`, `Ü`, but it does not treat `ä` as `a`, so type the umlaut.
- **Links:** a link to another chapter or section jumps there, and **Back** returns to where you were (64 steps). A link to GitHub goes to the browser. Nothing else can be linked.
- **Pictures:** the window skips the pictures of the website. The text says everything that matters without them.
- **Tables:** each column is as wide as its text needs and at least as wide as its longest word; the cells wrap if the window is narrow.

Nothing is loaded from the network, and the program carries no manual file next to it.

## Format of the source

The files are a small subset of Markdown. The parser in `src/core/manual.cpp` and the one in `scripts/build_manual_site.py` read exactly the same format, and both are checked against `tests/data/manual_fixture.md` (see Tests).

| Write | Result |
| --- | --- |
| `# Title {#id}` | A chapter. Without `{#id}` the id is made from the title (`ä` becomes `ae`, `ö` `oe`, `ü` `ue`, `ß` `ss`, spaces and punctuation become single dashes). The id is the file name of its web page and the target of links. |
| `## Title {#id}`, `### Title {#id}` | A section, and a sub-section. Same rule for ids. The real manuals give every id explicitly and the tests require them to be unique inside a chapter. Deeper levels are not headings. |
| a line of text | A paragraph. Lines that follow each other join into one paragraph. A blank line ends it. |
| `- item` or `* item` | A bullet list. A numbered list is `1. item` (one to three digits). The marker starts at the beginning of the line. A line that is indented continues the item before it. No nested lists. |
| `\| a \| b \|` and a separator row | A table. The first row is the header. The separator row (`---`) is dropped, short rows are filled up with empty cells. A `\|` inside a cell is a bar in the text. |
| a fenced block (three backticks) | A code block. Nothing inside it is read. |
| `> Tip: text`, `> Warning: text`, `> Note: text` | A note. German: `Tipp:`, `Warnung:`, `Hinweis:`. Lines that start with `>` one after the other are one note. |
| `![Description](file.webp)` | A picture, website only. See below. |
| `**bold**`, `` `code` `` | Bold and code, also inside table cells and notes. A backslash writes the next character as it is. |
| `[text](#chapter)`, `[text](#chapter/section)` | A link inside the manual. `#` is required. |
| `[text](https://github.com/...)` | A link to GitHub. The program refuses every other address. |
| `<!-- text -->` | A comment, ignored (also over several lines). |

Limits: the program reads at most 1 MiB of each file. Text before the first chapter is ignored. A reference to a file such as `docs/PLUGINS.md` in plain text becomes a link to that file on GitHub on the website (the build fails if the file does not exist).

### Pictures

`![Description](file.webp)` puts `site/assets/manual/<language>/file.webp` into the page, with the description as the alt text and the caption. The file name is lower case letters, digits and dashes, ending in `.webp`. The path in the line must be the same in both languages (the description is translated, and so is the picture, because the interface language shows in it).

The window in the program skips pictures. Never put information into a picture that the text does not carry.

## Writing rules

- Every chapter starts, after its first heading, with a paragraph. It is the description of the web page and of the card on the contents page.
- Both files have the same structure. The test fails if the chapters, the section ids, the kind and number of blocks, the columns of tables or the picture files differ. Text is translated, not copied.
- Use the names the interface uses. The German manual uses the German texts of the interface (`Einstellungen`, `Steuerung`, `Bühne`), the English one the English texts.
- Say only what is true of the program. If something was never tried (real hardware, VRChat, projectors), the chapter says so in a note, as the Stage and Sync chapters do.
- Write the way you would explain it to a friend next to you: short sentences, no marketing words.

## Adding a chapter

1. Add `# Title {#id}` with its sections to both files, in the same place.
2. Add the id to the list `PARTS` in `scripts/build_manual_site.py`. The script stops with a message if a chapter is missing from it or listed twice.
3. Run `python3 scripts/build_manual_site.py`, then `nvj_manual_tests` (`ctest -R manual`).
4. Add the new interface texts, if any, to all seven tables (`python3 scripts/check_i18n.py`).

Chapter titles that are used in text links of other chapters change the link text only, never the id. Do not rename an id that was published: web addresses and links from other documents use it.

## Tests

- `nvj_manual_tests` (24,177 checks): the parser (every block kind, inline styles, escapes, an unclosed code block, ragged tables, the 1 MiB limit and 4,000 random hostile texts), links and targets, search (several words, upper and lower case including umlauts, snippets, sections, ranking), the outline of `tests/data/manual_fixture.md` against `tests/data/manual_fixture.outline.txt`, and the two real manuals: at least 20 chapters and 3,000 words, text in every chapter, unique section ids, tables whose cells divide by their columns, no leftover `**` or backticks, every link resolves, English and German have the same structure, and the search finds `timeline`, `Zeitleiste` and `blackout`.
- `python3 scripts/build_manual_site.py --check` (CI, Windows and Linux job, and before every deployment of the website): the generated pages are current, the Python parser gives the outline of the fixture that the C++ parser gives, the structure of both languages agrees, every chapter is in a part, every link and picture exists, every anchor exists, no `<script>`, no inline event handler, no address except `github.com` and `docs.github.com`, no picture without `alt`, `width` and `height`, no unused picture.
- `ctest` runs the manual tests with the others.

## Making the pictures

`scripts/make_manual_shots.py --binary <NyrexVJ>` starts the program 16 times per language with `--screenshot`, cuts the regions listed in the script and writes WebP files to `site/assets/manual/en/` and `de/`. It needs a display with a screen larger than 1600 x 900 (for example `Xvfb :98 -screen 0 1920x1080x24`), because the window is 1600 x 900 and the mouse pointer is moved to the screen corner outside it, so that no tooltip is open. The node editor and the timeline start from `scripts/manual_shots/seed/` (four example effects and a small timeline with three lanes and three cues). Use `--only name` to remake single pictures and `--langs en` for one language.

The pictures show the Mesa software renderer's output of the clips, so frame rates in them (4 or 5 FPS) are not what a graphics card gives. Make new pictures after a change of the interface and run the website script again.

## Not checked

- The window was looked at only on Linux with a software renderer, in English and German. It was not opened on Windows beyond the build.
- Nobody who speaks German as a first language has read the German manual yet, apart from the author of the text. The statements about hardware (MIDI controllers, Spout in OBS, NDI, LTC, VRChat, projectors) come from the code and from the other documents and were not tried with the hardware.
- The pages were looked at in one browser (Chrome, desktop and phone width), not in Firefox, Safari or a screen reader.
