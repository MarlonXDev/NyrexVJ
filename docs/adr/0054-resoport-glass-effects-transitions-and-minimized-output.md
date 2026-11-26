# ADR 0054: ResoPort (Resolume import), Prism Glass and Frosted Glass, eight more transitions, translucent 3D meshes and output while minimized

- **Status:** Accepted for the fixes, the effects, the transitions and the importer, requested by MarlonXDev with a model file (XmasTree.glb) and a list of wishes. Issue [#138](https://github.com/MarlonXDev/NyrexVJ/issues/138). **The Resolume importer was checked against four sample files of a third-party project and against files made for the tests, never against a real show composition. The Windows part of the minimized fix was only compiled. Nothing here was checked on real graphics hardware.**
- **Date:** 2026-11-26
- **Owners:** Maintainers (pending assignment)
- **Related:** [ADR 0053](0053-window-layout-transport-bar-and-docking-timeline.md), [INTERFACES.md](../INTERFACES.md), [TESTING.md](../TESTING.md)

## Context
Two faults and four wishes:
- A Christmas tree model (glTF, one mesh with a material in blend mode) was drawn wrongly.
- With the main window minimized the output stopped, and it also stopped while the window was being moved.
- A cleaner interface in the manner of Resolume.
- More transitions between clips.
- Import of a Resolume composition (named ResoPort by the requester).
- The effects Prism Glass and Frosted Glass.

## Findings
- **Tree.** Blend materials were drawn in one pass, sorted back to front, with no depth write. A model whose solid parts carry a blend material then shows the branches through each other. The glTF format allows this on purpose and viewers draw such a mesh in two parts.
- **Minimized.** The frame loop skipped the whole frame, engine and outputs included, when the window was iconified. On Windows the system runs its own loop while a window is moved, sized or a menu is open, and the program's loop does not run at all in that time.
- **Transitions.** The transition shader had seven types and one place that mixes two clips; a new type is a new branch in that shader plus a name.
- **Resolume files.** A composition (`.avc`) is XML. Real files of Resolume Arena 7.22 were available as samples from a third-party project: the root `Composition`, one `Layer` per layer with its blend (`ChoosableMixer` named Blend) and opacity, one `Deck` per deck, one `Clip` per cell with a `VideoSource` that names a file, and effects as render passes of base type Effect.
- **Interface.** The bottom strip had five small windows next to each other, so that at 1600 x 900 each had a title and almost no room, the Audio level labels overlapped, and the Blend field in the Layer panel did not use the label-left form of every other field. The maintainer then sent a screenshot of Resolume Arena 7.3.3 and asked that the look, the sliders and the arrangement be the same, one to one. In that picture the clip grid runs across the whole width at the top with the deck tabs below it, a thin transport row follows, and under it come the monitors (Output above Preview), the Composition and Layer tabs, the Clip panel and the browser. Sliders are flat bars with the name right-aligned in its own cell, the value in a cell next to it and a thin marker; the groups of controls sit under dark bands with a small triangle; the selected layer name and the playing column are mint with dark text.

## Decision
1. **Blend materials are split in two parts.** Fragments with an alpha of 0.98 or more are drawn with depth write like an opaque mesh; the rest is drawn after them, sorted and blended as before. One flag in the mesh shader (`uSplit`) selects the part.
2. **A minimized main window keeps the engine and every output running** (output windows, Spout, NDI, recording, network, MIDI and OSC); only the interface is not drawn. `App::frame(dt, drawUi)` is the one frame function for both cases. On Windows a thread timer ticks the engine while the system's move, size and menu loop runs (`setModalLoopTick`); other platforms have no such loop and use the normal loop.
3. **Eight new transitions:** Pixelate, Glitch, Blinds, Ripple, Flash, Blur, Checker and Frost, as types 7 to 14 of the same shader. Existing compositions keep their type numbers. `--shader-selftest` runs every type and checks that it runs, starts on the old clip and ends on the new clip; the middle of each type was looked at by eye in a picture, not measured.
4. **Prism Glass** (Facets, Refraction, Dispersion, Shine) and **Frosted Glass** (Frost, Grain, Milk, Beat Pulse) are two ordinary effects in `src/render/effects_extra.cpp`, four parameters from 0 to 1 each, listed as new in 0.60.
5. **ResoPort is a reader and a builder, in the core and without OpenGL** (`src/core/resoport.*`): a small bounded XML reader (file at most 192 MiB, depth 96, 3,000,000 nodes, 256 attributes per node, values to 8,192 characters; the five standard entities and numeric ones; a document type declaration is skipped, and one with definitions is refused), a document model, a media finder and a builder that returns a normal `Composition`. The dialog (`src/app/ui_resoport.cpp`) first checks the file and shows a report, and nothing changes until Import is pressed. File menu, **Import Resolume composition (ResoPort)...**, and a dropped `.avc` file open it.
6. **What is taken over:** media clips (video and image files) at their layer and column, decks and layers up to the 16 decks and four layers of NyrexVJ (more are counted and reported as skipped), layer opacity, layer blend mode (as far as the name is known, otherwise Normal and the name is listed), deck names, the composition name, the tempo when it lies between 40 and 240 BPM, and the picture size when NyrexVJ has the same size (otherwise the default size is used and the report says so). Media are found next to the file, in the recorded path and in an extra folder that can be entered, and are copied into the media library like any imported file.
7. **What is not taken over, and is counted and shown:** effects (counted as render passes), generators and sources of other kinds, router clips, clips whose file type NyrexVJ cannot play, clips whose media are missing (the cell stays empty), and everything else in the file. Nothing is guessed to fill a gap.
8. **Import replaces the open composition,** like loading a preset, and has no undo.
9. **Interface, the look.** The theme **Arena** is the new default (`settings.theme` is `arena`, `settings.design` is 2; a setup with `design` below 2 and the theme `studio` is moved to `arena` once, as ADR 0020 did for Orange). In `applyTheme` it sets the measured greys of the picture (window `#0A0909`, panels `#161514`, bars `#2A2A2A`, buttons `#3A3A39`, slider well `#080808`), a teal accent `#4D8C75` and a mint highlight `#80FED2` with a dark text colour, flat tabs, 2 pixel corners and tighter padding; every theme keeps passing the contrast tests (`nvj_theme_tests`, 519 checks). A flag `ui::kFlatLook` (set for Arena in standard contrast only, so high contrast keeps its outlines) switches the slider helper to the flat bar, `ui::sectionHeader` (which replaced every `ImGui::SeparatorText`) to the dark band with a triangle, and the deck to the layer block with the buttons X, B and S, the blend mode, the opacity and a name bar, grey name bands under the clip thumbnails and a mint outline and column header for what is playing. In Arena the system's sans-serif font is added next to the built-in font and used as the default font (`useSansFont`: Segoe UI, Tahoma or Arial on Windows, Arial or Helvetica on macOS, DejaVu Sans, Liberation Sans or Noto Sans on Linux; without one the old font stays; a merged Japanese font is merged into both).
10. **Interface, the arrangement.** The default layout follows the picture: the Deck window across the top holds the grid, the deck buttons and the transport bar (the old bar under the menu is gone), below it Output above Preview, Master and Layer as tabs, Clip and Trigger as tabs, and the Browser with Audio, Master level, Audio input, Network, MIDI and OSC as tabs. All windows exist as before and keep their names and settings; only their place changed. The layout file is `layout_v8.ini` (new in this release, so nobody has to lose a layout that was released).

## Not done
- Resolume effects, generators, the Resolume clip transport and clip speed, layer groups, the output setup and the advanced output (Resolume's screens and slices), and Resolume's own blend names beyond the ones listed in the importer. They have no counterpart or no verified file layout.
- Writing a Resolume file (export).
- A pixel-exact copy: Resolume's own font, its icons, the Dashboard knobs, the M, A and V buttons and the T and Alph buttons of its layers, its crossfader row and its third layer (NyrexVJ has four) have no counterpart or no function behind them here.
- Any own handling of DXV: imported media are copied like every import and fall under the existing DXV-to-HAP conversion scan. This was not tried with a real DXV file.

## Alternatives considered
- **Draw blend materials without depth at all, as before.** That is the fault. Rejected.
- **Sort single triangles for blend materials.** Costly and still wrong for crossing triangles. Rejected; two parts are enough for solid models with a blend material.
- **Keep drawing the interface while minimized.** Nothing is visible and the work is wasted. Rejected.
- **Run the engine on its own thread on Windows.** A larger change of the threading of OpenGL and of every output. Rejected; a timer that ticks the same frame function is smaller, and it needs a Windows test to be trusted.
- **A third-party XML library for the importer.** A new dependency for one file format, and a file from outside is exactly the kind of input that needs hard limits. Rejected; the bounded reader is small and has a test that cuts and mutates a file thousands of times.
- **Import Resolume effects by name.** Without real compositions that show the parameter layout there is nothing to map to. Rejected for now.

## Consequences
- Everybody gets a fresh window layout once (`layout_v8.ini`) and, when the theme was Studio, the new default look once; Settings, Appearance brings Studio back.
- The Audio window, Master level, Audio input, Network, MIDI and OSC are tabs of the right column and are no longer visible all at once; drag a tab out to put it where it is needed.
- 28 new texts for ResoPort and 8 for the new transitions and effect parameters and 1 for the theme name in all seven language tables.
- A composition that uses one of the new transition types shows it only in 0.60.0 or later. The older code clamps the type number to its last known type (Slide); this was read from the code and not run with an older build.
- The importer reads files from outside, so it has the limits above and an own test file (`nvj_resoport_tests`, 173 checks).

## Open decisions for the maintainer
1. Should ResoPort take over Resolume effects? That needs real compositions with effects so that the parameter layout is known, and a mapping table from Resolume effect names to NyrexVJ effects.
2. Should an import ask before it replaces the open composition when it has unsaved changes?
3. Should the grid get a third layer row only (as in the picture), or a crossfader row and the M, A and V buttons per layer as well? They need functions that NyrexVJ does not have yet.

## Validation and open questions
- Done: `nvj_resoport_tests` (the reader, a damaged and a hostile file, the blend names, the media finder, the build), the four sample files of the third-party project read once by hand, a UI test that runs the whole import in the real window (`--interface-uitest`, 105 checks), `--shader-selftest` with all 15 transitions and both effects, `--scene-selftest` with a regression test for the translucent mesh, `--check-shaders`, `scripts/check_i18n.py`, `nvj_theme_tests` for all eleven themes, the interface test with Arena and with Studio, screenshots on a virtual screen compared with the picture by eye.
- Not done: a real show composition from Resolume, the blend names other than Alpha and Add (they are expectations, not read from real files), the place of the tempo in a real file, DXV media from Resolume, the Windows move, size and menu loop, real graphics hardware and real screens, the look next to Resolume itself (by eye only) and the system font on Windows and macOS (only Linux font files were loaded).
