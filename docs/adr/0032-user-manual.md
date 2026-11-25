# ADR 0032: User manual: one source in English and German, shown in a program window and on the website, with pictures made by the program itself

- **Status:** Accepted (requested by MarlonXDev). Issue [#90](https://github.com/MarlonXDev/NyrexVJ/issues/90). A manual in the other six interface languages, a manual picture in a window of the program, context help from a control to its chapter, a printable PDF of the manual and a search across the whole website are **not built** and stay Proposed.
- **Date:** 2026-11-25
- **Owners:** Maintainers (pending assignment)
- **Related:** [MANUAL.md](../MANUAL.md), [WEBSITE.md](../WEBSITE.md), [INTERFACES.md](../INTERFACES.md), [TESTING.md](../TESTING.md), [LANGUAGES.md](../LANGUAGES.md), [ADR 0020](0020-studio-design-and-more-languages.md)

## Context
The request: make the website "much better and more detailed", and write a manual that is also part of the program, with a window that can be opened and that explains everything: how things work and where to find them, in detail.

## Findings (what existed before)
- **No manual.** The knowledge was spread over about 25 documents in `docs/` for contributors and advanced users, the README, and short texts in the Welcome window and in tooltips.
- **A small website.** Four pages in each language (start, download, documents, privacy), plain HTML and CSS, no scripts, links only to GitHub. Its rules are in [WEBSITE.md](../WEBSITE.md) and make the privacy note short.
- **A way to embed text.** The changelogs are embedded into the program at build time (`nyrexvj_embed_text`) and shown in a window, so the program needs no file at run time.
- **Eight interface languages** (English and seven translation tables), with the English text as the key. Text that has no translation stays English.
- **A screenshot option** (`--screenshot`, `--frames`) and a set of start options that open single windows (`--settings`, `--timeline`, `--node-editor`).

## Decision
1. **One source, two files.** `docs/manual/manual.en.md` and `docs/manual/manual.de.md`, 35 chapters in seven parts, about 13,700 words in English and 12,700 in German. They are the only place that is edited by hand. The program embeds them with the same mechanism as the changelogs.
2. **A small Markdown subset with a hand-written parser** (`src/core/manual.cpp`, no new dependency, no OpenGL, so it can be tested alone). Chapters and sections have explicit ids that are never changed once published, because web addresses and links use them. Links inside the manual are `#chapter` or `#chapter/section`; the only links that leave it point to `github.com`.
3. **A manual window in the program.** Menu Help, User manual, `F1`, and the start option `--manual [chapter]`. Contents, search (every word must occur in the chapter; title before headings before text), links with a Back button, tables that size their columns to the text. With the interface in German the German manual is shown, with every other language the English one, and the window says so.
4. **The website shows the same text.** `scripts/build_manual_site.py` (Python 3, standard library only, no comments by project rule) generates one page for each chapter, a contents page and a one-page version in each language, and replaces the header and footer of the hand-written pages so the menu is the same everywhere. The pages have no script and need no other host. The start page was rewritten and the stylesheet redone.
5. **Two parsers, one definition.** The generator parses the manuals itself. `tests/data/manual_fixture.md` with its expected outline `manual_fixture.outline.txt` is the contract: the C++ test and the website check both compare their outline with that file.
6. **The two languages must have the same shape.** The test fails if the chapter ids, the section ids, the kind and number of blocks, the table columns or the picture files differ between English and German. Text is translated, never copied.
7. **Pictures are made by the program.** `scripts/make_manual_shots.py` runs the program 16 times for each language with `--screenshot`, cuts the regions and writes WebP files. Seed data (`scripts/manual_shots/seed/`) gives the node editor and the timeline something to show. Pictures are for the website only; the window in the program skips them, and the text must be complete without them.
8. **The website is checked in CI.** `python3 scripts/build_manual_site.py --check` runs in the Windows and Linux jobs of `build.yml` and before the publication in `pages.yml`. It fails on an outdated generated page, a broken link or anchor, a script, an inline handler, a foreign host, a picture without `alt`, `width` and `height`, an unused picture, or a chapter that is not in a part. `pages.yml` also runs on a change of `docs/manual/**` and the generator.
9. **13 new texts in all seven translation tables** for the window and the F1 hints.

## Alternatives considered
- **A Markdown library inside the program.** More code to keep in step with the Dear ImGui version and a dependency for a subset this small. The parser and the search are about 700 lines and are tested with 24,177 checks.
- **A web view or the system browser for the manual.** Needs the network or a file next to the program, which can be missing or from another version. An embedded text is always the manual of that exact build.
- **Writing the website pages by hand.** The manual would exist twice and drift apart. Generation gives one source.
- **A site generator such as Jekyll or Hugo on GitHub Pages.** It adds a build tool and its themes, and the privacy rules need full control of every page. The repository already uses Python scripts for its checks.
- **JavaScript on the website for search and navigation.** It would break the promise of no scripts that the privacy note is built on. The website has the contents page, the one-page version and the browser's own find.
- **Pictures made by hand.** They are out of date at the next interface change. A script can be run again.
- **A manual in all seven languages now.** The text could be machine translated, but the interface texts in the other six languages are machine assisted (see [LANGUAGES.md](../LANGUAGES.md)) and nobody has reviewed a manual text in them. A wrong manual is worse than an English one, so the other languages show the English version until someone who speaks them offers a translation.

## Consequences
- The program grows by the two embedded files (about 175 KB). The program starts no file read for the manual; the text is parsed on first use.
- `site/` grows to about 2 MB, most of it the 2 x 25 pictures of the manual.
- Every change of the interface needs a change of both manual files and, if a picture shows it, new pictures. The parity test catches a change of only one language; nothing catches a manual that has become wrong.
- The ids of chapters and sections are now an interface (web addresses, `--manual <id>`). They are listed in [INTERFACES.md](../INTERFACES.md).
- A new chapter must also be added to the list of parts in the generator, or the check fails.
- New files: `docs/MANUAL.md`, `docs/manual/`, `src/core/manual.*`, `src/app/manual_view.hpp`, `src/app/ui_manual.cpp`, `tests/test_manual.cpp`, `tests/data/manual_fixture.*`, two scripts and `scripts/manual_shots/`.

## Validation / open questions
- Done here: `nvj_manual_tests` (24,177 checks), the full test run, the website check, and the manual window looked at in English and German on a Linux machine with a software renderer. Details and the commands are in the pull request and in [TESTING.md](../TESTING.md).
- **Not done:** the window on Windows beyond the CI build, any graphics card, a review of the German text by a native speaker, a review of the English text by a native speaker, trying the statements about hardware (MIDI controllers, Spout, NDI, LTC, VRChat, projectors) with the hardware, and any browser except Chrome (headless).
- Open for the maintainer: translations of the manual into the other interface languages, context help (a button next to a control that opens its chapter), a printable PDF built in CI, and whether the seven older unused pictures in `site/assets/` should be deleted.
