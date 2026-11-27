# ADR 0056: Layout export and import as .nyxlayout files

- **Status:** Accepted, requested by MarlonXDev (2026-11-27 03:28): the layouts should be easy to export and import so that friends can be given the arrangement he has in the program. Issue [#142](https://github.com/MarlonXDev/NyrexVJ/issues/142). **Checked on Linux with software OpenGL and in unit tests only; never tried between two real PCs or with the Windows drop of a file from another program.**
- **Date:** 2026-11-27
- **Owners:** Maintainers (pending assignment)
- **Related:** [ADR 0055](0055-resolume-look-for-every-colour-and-no-window-close-buttons.md), [ADR 0053](0053-window-layout-transport-bar-and-docking-timeline.md), [INTERFACES.md](../INTERFACES.md), [TESTING.md](../TESTING.md)

## Context
- Since 0.51.0 a workspace is a plain text file in `workspaces/<name>.ini` with a header (`; NyrexVJ workspace`, `; format=1`, `; app=`, `; display=WxH`) and the Dear ImGui layout. It can be copied by hand to another PC, but that needs the data folder, the right folder inside it and care with the name.
- The program has no native file dialog. Other file types come in by dropping them on the window (`.nyxshow`, `.nyxpack`, `.nyxfx`, `.avc`) and go out to a folder under the data folder (`packages_out`).
- The checks for a workspace (size, lines, control characters, positions, sizes, windows, format number) already exist and are tested.

## Decision
1. **A layout travels as one file, `<name>.nyxlayout`,** with exactly the content of the workspace file. No new format, no new version number: the header and `format=1` stay, and a newer format is refused as before.
2. **Export** is a button for each valid workspace in Settings, Appearance, Workspaces. It writes `layouts_out/<name>.nyxlayout` in the data folder, or into a folder that the user enters in the field Export folder (optional), creates the folder when needed, overwrites an earlier export of the same name and opens the folder. The file name comes from the workspace name, so the user cannot make it overwrite a file of another type. There is no free file name field.
3. **Import** takes a `.nyxlayout` file that is dropped on the window, or a path entered in Settings with the button Import layout. The file is read with the size limit, checked like a saved workspace and written into `workspaces/`. An import **never replaces** a workspace: a taken name (case does not count) gets ` 2`, ` 3` up to 99, the name is cut at a character boundary so that the number fits in 40 bytes, and an empty or reserved name becomes `Imported layout`. At 32 workspaces the import is refused with the usual message. Nothing is loaded on screen by an import; the user loads the layout from the list or from View, Workspaces.
4. **A refused file changes nothing** and is reported with a reason that is translated like the other workspace messages (not a layout, no format number, newer version, too large or unreadable, too many lines or windows, control characters).
5. The core has three functions (`workspace::exportFolder`, `exportTo`, `importFile`) with unit tests, and the interface test drives export, import, drop and a refused file in the real window.

## Not done
- A native file dialog, a layout gallery or an online exchange, sending by link, and a layout that carries clips, presets or settings (a layout holds only the arrangement, which is why it is safe to send).
- Importing and loading in one step, and asking whether to replace a name that is taken (a number is added instead).
- Exporting the unsaved current layout directly: it has to be saved as a workspace first.
- Dropping a plain `.ini` file: only `.nyxlayout` is recognised on the window, and the path field accepts any file that passes the check.

## Consequences
- A friend only needs the file: drop it on the window, load it from the list.
- The file contains the window size it was saved at; docked panels keep their proportions on another size, floating windows keep their position (as when loading any workspace).
- The folder `layouts_out` can fill up with old exports; the program never deletes from it.
- Not tried: two real PCs, Windows drag and drop from Explorer, very different screen sizes between the PCs; see `M-UI-10`.
