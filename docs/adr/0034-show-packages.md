# ADR 0034: Show packages: one file that moves a whole show to another computer

- **Status:** Accepted (requested by MarlonXDev as prompt 16). Issue [#96](https://github.com/MarlonXDev/NyrexVJ/issues/96). Compression, signed or encrypted packages, packing plugins, following the dependencies inside ISF `IMPORTED` blocks, rewriting shader text after a rename and a command line import are **not built** and stay Proposed.
- **Date:** 2026-11-25
- **Owners:** Maintainers (pending assignment)
- **Related:** [SHOWS.md](../SHOWS.md), [INTERFACES.md](../INTERFACES.md), [TESTING.md](../TESTING.md), [ADR 0004](0004-scene-document-and-clip-pack.md), [ADR 0018](0018-plugin-system.md), [ADR 0015](0015-advanced-output.md)

## Context
A show is spread over many places: a composition that refers to clips by name, presets, media, models, shader and node files, venue setups with mask pictures, a cue list and a timeline. To play the same show on another computer, or after a Windows user change, you had to copy the right files by hand, in the right folders, and the show opened with empty cells when one was forgotten. The task asked for an own versioned format with export, import, path repair and a dry run that shows size and licence hints, with plugins never imported as something executable, no misuse of `.nyxclip`, and protection against path traversal, zip bombs, illegal sizes and overwriting foreign files.

## Findings (what existed before)
- **Presets and the session** are JSON (format 2) that refer to clips by library **name** (`Video: loop`) and to effects by name (`Node: x`, `ISF: y`). They hold no absolute paths. The library builds these names from the files in `media/`, `models/`, `clips/`, `shaders/` and `nodes/`. So a show moves correctly as soon as the same names exist on the other computer; no path ever has to be rewritten.
- **`.nyxclip`** is the scene pack of the Clip Creator (ADR 0004): one scene with its models. It is not a show and has its own reader.
- **Venue setups** refer to mask pictures by file name only (ADR 0015, 0023). **Cues and timeline** are two files in the data folder.
- **Plugins** are native libraries that run in the program (ADR 0018). Loading one is a security decision of its owner.
- Nothing in the program wrote or read an archive of several files. There is a CRC-32 and a SHA-256 in `core`.

## Decision
1. **An own container, `.nyxshow`, version 1.** Header, the file data, a JSON manifest, a fixed 32 byte footer that points at the manifest. It is read from the end, so a damaged or cut-off package fails early and cheaply. The `.nyxclip` format is not touched and a scene pack travels inside a show as one file.
2. **Stored, not compressed.** Video, pictures and models are already compressed. With no compressed data there is nothing to inflate, so a small package can never unpack into a huge amount of data, and every size in the manifest is a size on disk that can be checked against the file. This is the answer to zip bombs.
3. **The importer decides where a file goes.** The manifest has a **kind** for each file and a plain file name, never a path. The folder comes from the kind (`media/`, `models/` and so on) and the allowed endings come from the kind. A name with a folder part, `..`, a drive, a character Windows forbids, a control or invisible character, a reserved device name, a trailing space or dot, more than 120 bytes or an ending that does not fit the kind is **blocked** and never written. A package cannot name a path, so path traversal has no place to happen.
4. **A SHA-256 for every file**, a CRC-32 for the manifest, and sizes that must add up (inside the data area, not overlapping, not more than the package holds). Limits: 4096 entries, 16 GiB, a manifest of 4 MiB, JSON depth 24 and a largest size per kind (media 4 GiB, models 512 MiB, scene packs 256 MiB, shaders and node effects 256 KiB, venue setups 2 MiB, masks 16 MiB).
5. **No kind for plugins.** A show that uses a plugin lists its id in `requires` and the other computer must install the plugin itself. Opening a package can therefore never put code on the computer. Live sources are listed in `requires` too.
6. **The show is described by the text a preset already has.** Export saves the composition to JSON in memory and collects every clip and effect name from it. Nothing new describes a show, and import hands the text back to the existing load path.
7. **A plan before a change, on both sides.** `planExport` and `planImport` return a table (what, kind, size, state, licence note) and do not touch the disk. The window shows the plan and the sums and a button runs it. Import states per file: New, Identical, Different, NotIncluded, Blocked, NotApplied.
8. **A licence or source note per file, written by the user.** The program cannot know whether a file may be passed on. The field is stored in the package, shown on import, and the export window counts the media files without a note. Nothing is claimed from file names or metadata.
9. **Import is two phases with a rollback.** Every file is copied to a temporary name and its SHA-256 is checked. Only when all are good are they moved into place, and the files that were replaced were copied to `show_backups/<time>/<kind>/` first. A failure puts the moved files away and the backups back (`rolledBack`). Policies for a file that differs: Keep mine (default), Keep both (renamed copy, references in the show and presets are rewritten) and Replace (with backup). Cues and timeline replace the user's files only if ticked, and then take effect after the next start.
10. **Repair by checksum.** Find missing files walks a folder (6 levels, 20,000 entries, links not followed) and offers only files whose size and SHA-256 equal what the package knows; without a checksum a same-named file is offered unticked and labelled.
11. **A worker thread and a progress bar.** Writing, checking, planning, applying and scanning run in a job with a progress value and a Cancel button; the window and the rest of the program keep drawing. A cancelled write removes its temporary file.

## Alternatives considered
- **A ZIP file.** Readable by every tool, and that is the problem: zip has paths, a central directory that can disagree with the local headers, compression, extra fields, symbolic links and duplicate names. Each of them is a known source of vulnerabilities. A format of our own has exactly the features we need and one reader to test. The price is that nothing else can open it.
- **Storing paths and rewriting them on import.** The show does not contain paths, so there is nothing to rewrite. Rewriting paths in other formats would be the place where bugs live.
- **Compression.** A saving on a few small text files and a cost in safety and time on gigabytes of video. Not worth it for version 1; a flag in the header is reserved.
- **Packing plugins** so the other computer "just works". It would let a package carry executable code. Refused on principle; the maintainer can reopen it with a signing scheme.
- **Putting the show into the program's own preset folder on import.** The show would then be indistinguishable from the user's own presets, and the user could not tell what came from where. The show opens as the current show and presets are the ones that were ticked.
- **Silent replacement of a missing clip by "something similar".** The task asked for missing things to be shown, not hidden.

## Consequences
- One more format to keep stable. Version 1 is the first; an older reader refuses a newer package with a clear message. A migration is a function from the older manifest, there is none yet.
- A package of N gigabytes needs N gigabytes of free space in the destination and on import (the check is made before anything is copied).
- A moved or renamed library file breaks nothing in the package but is known by its name only.
- New files: `docs/SHOWS.md`, `src/core/show_package.*`, `src/core/show_workflow.*`, `src/app/show_dialog.hpp`, `src/app/ui_show.cpp`, `tests/test_show_package.cpp`, `tests/test_show_workflow.cpp`. New data: `shows/` (the suggested destination) and `show_backups/`. New start option: `--show-packages`. 163 new texts in all seven languages and a new manual section in English and German.

## Validation / open questions
- Done here, on Linux with the Mesa software renderer: `nvj_show_tests` (203 checks) and `nvj_show_workflow_tests` (342 checks), the whole suite (38 groups), the translation check, `--check-shaders` and the website check. Measurements: 512 MiB and 2000 small files written in 3.8 s, a manifest of 2008 entries read in 12 ms.
- **Not done:** Windows beyond the CI build, packages of several gigabytes, USB sticks and network drives, an interactive click through of the window (it was opened with `--show-packages` and looked at in English and German screenshots).
- Open for the maintainer: whether packages should be signed, whether compression is wanted for the small text files, a command line import for unattended setups, and rewriting shader text after Keep both.
