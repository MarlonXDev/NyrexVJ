# Show packages: move a whole show to another computer

Added in 0.41.0 ([ADR 0034](adr/0034-show-packages.md)). A show package is one file with the ending `.nyxshow` that holds the current show and every file it uses. File, Export show package makes one, File, Import show package (or dropping the file on the window) opens one. Nothing changes until you use it, and nothing is changed on import before you press Import.

## What a package holds

| Kind | Where it comes from | Where it goes on import | Allowed endings | Largest file |
| --- | --- | --- | --- | --- |
| `composition` | the current show (all 16 decks), as saved by File, Save as preset | opened as the current show, nothing is written to `presets/` | none, one entry | 8 MiB |
| `preset` | `presets/` | `presets/` | `.json` | 8 MiB |
| `media` | `media/` | `media/` | `.png .jpg .jpeg .bmp .tga .gif .mp4 .mov .m4v .mkv .avi .webm` | 4 GiB |
| `model` | `models/` (a glTF also brings its `.bin` and pictures) | `models/` | `.glb .gltf .bin .png .jpg .jpeg` | 512 MiB |
| `scenepack` | `clips/` | `clips/` | `.nyxclip` | 256 MiB |
| `shader` | `shaders/` | `shaders/` | `.glsl .frag .fs` | 256 KiB |
| `node` | `nodes/` | `nodes/` | `.nyxfx` | 256 KiB |
| `venue` | `venues/` | `venues/` | `.json` | 2 MiB |
| `mask` | `masks/` | `masks/` | `.png .jpg .jpeg .bmp .tga` | 16 MiB |
| `cues` | `cues.json` | only if you tick Also replace my cue list | none, one entry | 8 MiB |
| `timeline` | `timeline.json` | only if you tick Also replace my timeline | none, one entry | 4 MiB |

A package holds at most 4096 entries and 16 GiB. Built-in clips and effects are not packed, because every NyrexVJ has them. Files that Live sources, plugin clips and plugin effects supply are not packed either; they are listed (`requires`) so the person who opens the package sees what the other computer must provide.

**Plugins are never in a package.** There is no kind for them. A show that uses a plugin lists the plugin id and the other computer must have the plugin installed itself, so opening a package can never run code that came from the package.

`.nyxclip` (the scene pack of the Clip Creator) is a different file and is not changed. It can be inside a show package as one file of the kind `scenepack`.

## The file format

All numbers are little endian.

| Part | Content |
| --- | --- |
| Header, 16 bytes | `NYXSHOW` and a zero byte, the format version (u32, 1) and flags (u32, 0) |
| File data | every included file one after the other, **stored as it is, not compressed** |
| Manifest | JSON: `format` (`nyrexvj-show`), `version`, `app`, `appVersion`, `name`, `created`, `notes`, `files`, `requires` (`plugins` and `live`) |
| Footer, 32 bytes | offset of the manifest (u64), size of the manifest (u32), CRC-32 of the manifest (u32), format version (u32), 0 (u32), `NYXSHEND` |

A file entry has `kind`, `name`, `size`, `offset`, `sha256`, `included`, `missing` and `license`. `included: false` lists a file that is not in the package (it is only named, with its checksum if one is known); `missing: true` means the file could not be found when the package was made.

The package is read from the end: the footer, then the manifest, then single files by offset. A package that is cut off therefore fails at the footer with "The package is cut off", and a package that was changed fails at the manifest checksum or at the checksum of a file.

Files are stored without compression on purpose. Video is already compressed, and a package with no compressed data cannot be made to unpack into something much larger than the file you opened (no zip bomb).

## Making a package

Export builds a plan before it writes anything (`planExport`):

1. The show is saved to JSON in memory and every clip and effect name in it is collected (`layers[].cells[]`, the deck slots and every `"effect"` key). That is the same text a preset contains, so there is no second way to describe a show.
2. Each name is looked up in the library: a media, model, scene or shader file, a node effect or an ISF filter becomes a file of the package; a built-in is skipped; a live source or plugin entry goes to the `requires` list; a name the library does not know is shown as a warning.
3. A shader file that names a video or picture is searched for those names, a glTF for its `.bin` and pictures and a venue setup for its mask pictures. Dependencies inside an ISF `IMPORTED` block are not followed.
4. Every file is checked: missing, larger than its kind may be, or a name that cannot be used in a package are shown and left out. Nothing is replaced silently.

The window shows the plan as a table: the file, its kind, its size, which parts of the show need it, and a column for a note about where it comes from or its licence. **NyrexVJ cannot know whether you may pass a file on.** The note is yours, it is stored in the package and shown to whoever imports it, and the window counts the media files that have none.

The package is written in one pass to `name.nyxshow.part`, the hash of each file is computed on the way, a file that changes while it is read is refused, and the result is moved to its final name only when everything went well. Cancelling or an error removes the part file. An existing package is only replaced if you ticked Replace the file if it exists.

## Opening a package

Import is a plan first and a change second.

1. **Inspect** (`inspectPackage`): the footer, the manifest and every entry are checked without reading file data: format name and version (a newer one is refused, an older one would be migrated), sizes, offsets inside the data area and not overlapping, names, limits, checksums that are really 64 hex digits, text that is really UTF-8, a JSON depth of at most 24. A kind that this version does not know is ignored and counted.
2. **Plan** (`planImport`): for each file it decides what would happen: `New` (will be copied), `Identical` (already here, the SHA-256 is the same, nothing to do), `Different` (a file of that name exists and is not the same), `NotIncluded` (the package lists it but does not hold it), `Blocked` (the name is not allowed), `NotApplied` (cues or timeline that you did not tick). The free space is checked against what would be copied.
3. **Choose what happens to a different file:** Keep mine (the default), Keep both (the new file is copied as `name (2).ext` and the references in the show and the presets are changed to the new name) or Replace mine, save it first (your file is copied to `show_backups/<time>/<kind>/` before it is replaced). A shader that refers to a renamed clip only gets a warning, its text is not rewritten.
4. **Apply** (`applyImport`): every file is copied to a temporary name next to its place and checked against its SHA-256. Only when all files are good are they moved into place and the files you chose to replace backed up. If anything fails, the moved files are removed, the backed up files are put back and the result says `rolledBack`. A folder that is in the way of a file stops the import before anything is written.

The show itself opens as the current show through the normal load path of the program, with a message that says how many of its clips and effects are missing. A missing clip or effect stays empty; NyrexVJ never puts a different one in its place.

## Names and paths

The package never decides where a file goes: the importer derives the folder from the kind. The name in the package must be a plain file name:

- no folders, no `/` or `\`, no `..`, no drive letters, nothing that starts or ends with a space or ends with a dot
- no characters that Windows does not allow in file names, no control characters, no invisible or text-direction characters
- no names Windows reserves (`CON`, `PRN`, `AUX`, `NUL`, `COM1`, `LPT1` and so on, also with an ending)
- at most 120 bytes, valid UTF-8, and an ending that is allowed for the kind
- a single-entry kind (the show, cues, timeline) has a fixed name

A name that fails any of these is `Blocked`, shown in the window with the reason and never written. The same rule is used when a package is made, so a package you make can be opened by this program.

## Finding missing files

When files of the show are in the package only by name (the maker chose not to include them, or they were missing), Import shows Find missing files. Point it to a folder, for example the original media drive. NyrexVJ walks that folder (at most 6 levels and 20,000 entries, no links followed), compares the file name, size and SHA-256 with what the package knows and lists the files that are **the same**, ticked. A file with the same name that is not the same file is never offered. If the package knows no checksum for a file (it was already missing on the maker's computer), a file with the same name is offered, **not ticked** and marked "same name, content cannot be checked", so you decide.

## Limits and what is not done

- Only format version 1 exists. A package with a higher version is refused with the message that it was made by a newer version of NyrexVJ. The migration hook is the version check in `manifestFromJson`; older show and preset texts are read by the tolerant `compositionFromJson`.
- Plugins are not packed (see above). A package from a computer with other plugins opens with those clips empty.
- A dependency that is only named inside a shader (through an ISF `IMPORTED` block, or a texture name built at run time) is not found. A shader whose clip was renamed by Keep both is not rewritten, only a warning is shown.
- A replaced cue list or timeline is used after the next start of NyrexVJ.
- A moved or renamed clip is found by its name only; the library has no other key.
- The window was built and its layout looked at on Linux with a software renderer; it was not clicked through on Windows.

## Tests and measurements

Run on Linux with the Mesa software renderer, 2 cores:

- `nvj_show_tests` (203 checks): the format on its own: write and read back, every damage to the header, footer and manifest, a manifest that lies about sizes and offsets, overlaps, duplicates, checksums, a file that changes while it is read, names (traversal, drive letters, reserved names, invisible characters, long names), JSON depth, invalid UTF-8, size limits, cancel, and an existing destination.
- `nvj_show_workflow_tests` (342 checks): collecting and renaming references in a show, the export plan (missing, too large, built-in, live, plugin), a round trip into an empty data folder and into one with other data, all three policies, a path that changed after the user or the computer changed, a package with intentionally missing files, a hostile package, the rollback when a folder is in the way, a full disk check, and repairing missing files from a folder.
- Cost, measured with a small program on the same machine: a package of 8 media files of 64 MiB (512 MiB) and 2000 small presets was written in 3.8 s (about 135 MiB/s), its list of 2008 entries was read in 12 ms, checking the 8 files took 2.9 s, planning an import into an empty folder 13 ms, applying it 3.5 s, and planning it again with everything already present 3.0 s (the existing files are hashed to see that they are the same). All of it runs on a worker thread; the window keeps drawing and shows a progress bar and a Cancel button.
- **Not tested:** a package of several gigabytes, a slow USB stick or a network drive, Windows (built by the CI, not run), and the window with a mouse. The window was opened with `--show-packages` and looked at in screenshots in English and German.
