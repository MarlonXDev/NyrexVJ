# ADR 0053: Window layout: a fixed transport bar, a trigger and an audio window, a bottom strip, sliders with the label on the left, and a timeline that can dock

- **Status:** Accepted for the layout, the sliders and the docking timeline, requested by MarlonXDev with a mock-up image of the wanted arrangement (the font stays as it is). Issue [#136](https://github.com/MarlonXDev/NyrexVJ/issues/136). **Checked on a virtual screen only; real screens, real display scaling and the Windows build are not checked.**
- **Date:** 2026-11-26
- **Owners:** Maintainers (pending assignment)
- **Related:** [ADR 0044](0044-interface-workspaces-shortcuts-accessibility.md), [INTERFACES.md](../INTERFACES.md), [TESTING.md](../TESTING.md)

## Context
The request was to arrange the program like a mock-up: a menu bar, a transport bar with deck tabs, tempo, Tap, Play, Pause and Stop and the beat, a clip grid on the left, tabbed Clip and Master panels with a Layer panel on the right, a row with Preview, Output, Trigger, Audio and Library, a strip with level, audio input, network, MIDI and OSC at the bottom, and sliders with the name on the left, a thin track and the value on the right. The font must stay. The timeline must be openable and dockable somewhere.

## Findings
- Docking was already on. The timeline could not dock only because its window had the flag `ImGuiWindowFlags_NoDocking`.
- The deck window held the grid, the 16 deck buttons, the tempo controls and the transition settings in one window, so none of these could be moved on their own.
- The Audio tab lived in the Browser. Level, blackout, MIDI, OSC, NDI, Spout and the network sync state existed only inside Settings pages or the Master window.
- All sliders go through `ui::sliderFloat` and `ui::sliderInt` (93 calls), so one change gives every slider the new look.
- No counterpart in the program for two parts of the mock-up: a per-clip MIDI or OSC trigger panel, and the layer Transform controls.

## Decision
1. **The transport bar is a fixed bar under the menu** (a viewport side bar, like the status bar), not a window: it holds the 16 deck buttons with Previous and Next, BPM with minus, plus, /2 and x2, Tap, Resync, Play, Pause, Stop, the beat in the bar, one tick per beat and the phase inside the beat, and a thin line while a deck transition runs. In a narrow window /2 and x2 and the ticks drop out first. Play lets the clock run, Pause is the toggle it was, **Stop pauses and restarts the beat count at 1** (new behaviour, a small addition for the third button of the mock-up).
2. **The deck window holds only the grid.** Quantize, the transition (type, Beat sync, length, Auto cycle), the beat grid (beats per bar, nudge, snap, bar start) and Sync BPM to audio moved into a new Trigger window. The beat-grid pop-up on the beat display is gone; the same controls are in the Trigger window.
3. **The Audio tab became the Audio window** (spectrum, meters, kick, detected tempo, analysis sliders). The source choice is in the strip window Audio input. The Browser keeps Presets and Library. `--tab 2` brings the Audio window forward and `--tab 3` the Master window.
4. **A bottom strip of five small windows:** Master level (level, dB, blackout), Audio input (source, refresh, level), Network (NDI and Spout switches, state of the network sync, a button to its settings), MIDI (device, state, last message), OSC (receive switch, state, last message). They show state and hold the one switch or choice that is needed during a show; every other option stays in Settings.
5. **The default layout** has the grid left and Clip with Master as tabs above Layer on the right, then the row Preview, Output, Trigger, Audio, Browser, then the strip. It is sized from the work area that the menu, transport and status bars leave (`GetBuildWorkRect`). With the full window size the last window of the layout would be squeezed to its minimum when the bars appear one frame later; this was seen in the first screenshot.
6. **The layout file is `layout_v7.ini`.** An old `layout_v6.ini` is not read and not deleted, so everybody gets the new default once. Workspaces saved earlier keep their old arrangement when loaded; windows they do not know float until the layout is reset.
7. **The slider look is in the two helpers.** The name is drawn on the left (36 percent of the width, between 56 and 150 pixels, wider for a long name), then a thin track with a round handle and the accent colour up to the handle, then the value on the right. When the track would be shorter than 56 pixels the old slider is drawn. Call sites, widths given with `SetNextItemWidth` and the font are unchanged.
8. **The timeline can dock.** The flag is gone, so it can be dragged into any dock. Timeline, Place the timeline offers Dock at the bottom and Dock on the right, which split the whole dock area (34 percent), and Float as a window, which sets the old size and place. Docking is saved with the rest of the layout. It opens floating as before.

## Not done
- A per-clip MIDI or OSC trigger panel and the layer Transform controls: nothing in the program is behind them, and adding them would be new features that need their own decision.
- The eye, M and A buttons and the colour strip of the mock-up's layer rows, the Library tree, and a display choice in the Output window.
- A change of font, colours or theme.

## Alternatives considered
- **A dockable transport window.** It could be hidden or lost by accident, and a transport that is not on screen is a risk in a live show. Rejected; the bar is always shown.
- **Keep the old slider and add only a new look for some.** Two looks in one program. Rejected.
- **Pre-build an empty dock for the timeline.** Dear ImGui removes empty dock nodes, so the node would not stay. Rejected; the node is made when the timeline is placed.
- **Migrate the old layout file.** The arrangement changes completely, so there is nothing to carry over.

## Consequences
- Everybody sees the new default once. The old layout stays on the disk.
- 12 new texts in all seven language tables.
- The manual, its figures and the website figures were made again.
- Dear ImGui lets the last (central) panel of the dock area take over any space that is freed. When the timeline docks, the sizes of the other panels are scaled with it, and when it leaves its dock (floated, moved to the other side or closed) they are scaled back, so the default arrangement returns. This only works while the timeline is alone in its dock node. A timeline that was dragged into a tab group by hand is not scaled, and the last panel takes the freed space; **Reset window layout** brings the proportions back.
- In a window narrower than about 1540 pixels (at 100 percent) the 16 deck buttons become one button with the active deck that opens the list; below that /2, x2, Resync and the beat display are dropped before Play, Pause and Stop. Found on a virtual screen of 1024 x 600 and 1280 x 720, where Play, Pause and Stop were pushed out of the bar.
- A request to dock the timeline waits one frame when the timeline window does not exist yet, because a window that is created and docked in the same frame stays floating.

## Open decisions for the maintainer
1. Should the timeline open docked by default?
2. Should the strip windows get more controls (full MIDI and OSC options), or stay at state and one switch?
3. Should the per-clip trigger panel and the layer Transform controls of the mock-up become features?
4. Is Stop pausing and restarting the beat count what you want, or should it only pause?

## Validation and open questions
- Done: `--interface-uitest` with the new windows in the panel list and checks for the docking of the timeline (bottom, right, float), for the sizes of the other panels after the timeline leaves its dock or is closed, for the reset, and for typing a value into a slider with Ctrl+click; `scripts/check_i18n.py`; screenshots at 1600x900 and 1920x1080 on a virtual screen.
- Not done: real screens, display scaling above 100 percent on real hardware, a 4K screen, the Windows build, the font as it looks natively on Windows, and a show with the new layout.
