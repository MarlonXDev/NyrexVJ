# Show timeline

The show timeline is an optional window that automates controls along a time line. It is **off by default**. It adds keyframe lanes for sliders and switches and shows the cue list on the same ruler. It does not add a second clock, a second cue engine or a second way to move a control. The decisions are in [ADR 0031](adr/0031-show-timeline.md).

## What it is for

- Fade a layer or the master level over a song without touching a slider.
- Run a prepared show against a timecode (LTC or MTC) or against its own play button.
- See the cues of the cue list ([SYNC.md](SYNC.md)) next to the movements they belong to, and move them with the mouse.

## Turning it on

- Menu **Timeline, Run the timeline** (or the tick **Run the timeline** in the window). Until it is on, nothing is written and no cue is fired by the timeline. You can still open the window and prepare lanes.
- Menu **Timeline, Show timeline window** opens the window. The start option `--timeline` opens it at start (a test and screenshot option, see INTERFACES.md).
- Menu **Timeline** also has Play or Pause, Stop and Release hand moves.

Settings that belong to the timeline are in the **Settings** popup of the window and in `settings.json` (`timelineEnabled`, `timelineSource`, `timelineLost`, `timelineOverride`).

## The window

From top to bottom:

1. **Toolbar.** Run the timeline, Play or Pause, Stop, the position as minutes, seconds and milliseconds, and the state (Stopped, Playing, Paused, Following the timecode, Waiting for timecode, Timecode lost with the chosen behaviour). Below it Undo, Redo, Add lane, Fit (zoom so that every keyframe and cue is visible), Follow (the view scrolls with the playhead) and Settings.
2. **Ruler.** Click or drag to move the playhead. **Shift and drag** marks the loop region, a right click opens a menu to set the loop start or end at that point, clear the loop or move the playhead.
3. **Cue lane.** One diamond per cue of the cue list. Drag a diamond to move the cue, double click an empty place or right click it for Add cue here (a picker asks what the cue does), right click a diamond for Fire this cue now and Delete cue.
4. **Lanes.** One row per automated control. On the left: a tick that mutes the lane, the name of the control, a status (Following, Muted, Moved by hand, Waiting, Not available) and an X that removes the lane. On the right: the curve with one handle per keyframe. A square is a hold, a circle is smooth, a diamond is linear.
5. **Edit strip.** The time, value and shape of the selected keyframe, or the time and action of the selected cue, and a delete button.
6. **Scroll bar and counters.** The counters (Cues fired, Cues skipped, Jumps, Source changes, Hand moves, Writes) are hidden until Show counters is ticked in Settings.

### Mouse and keys

| Action | Result |
| --- | --- |
| Double click on a lane | Adds a keyframe at that time and value |
| Drag a keyframe | Moves time and value (times round to milliseconds). **Shift** keeps the value, **Ctrl** keeps the time |
| Right click a keyframe | Linear, Smooth, Hold, Delete keyframe |
| Right click a lane | Add keyframe here, or Add keyframe at the playhead (takes the value the control has now, only while the timeline is on). The tick on the left mutes the lane and the X removes it |
| Right click an empty place on the cue lane | Add cue here |
| Ctrl and wheel | Zoom around the pointer |
| Shift and wheel, or middle button drag | Scroll |
| Delete | Deletes the selected keyframe or cue |
| Space | Play or pause (only while the timeline window has focus; otherwise Space is Tap tempo as before) |
| Home | Stop and return to the start |
| Ctrl+Z, Ctrl+Y or Ctrl+Shift+Z | Undo and redo of timeline edits (only while the window has focus; elsewhere they undo the composition as before) |

## Lanes

A lane has one control as its target, any control id of the Control page that has a value: layer opacity, master level, effect parameters, macros, the four colour controls and so on, and toggles such as blackout. Rules:

- **One lane per control.** A second lane for the same control is refused with a message.
- **Triggers are cues, not lanes.** Clips, columns, decks and presets are triggers; a trigger has no value to follow. Put them on the cue lane.
- **`master/bpm` cannot be a lane**, because the tempo is set by the beat clock and its sources.
- **Toggles** switch at the keyframe whatever shape it has (on at 0.5 or more).
- **Limits:** 32 lanes, 2048 keyframes per lane, 16,384 keyframes in all, times from 0 to 86,400 s (24 hours), a target name up to 128 characters.
- **Shapes:** *Hold* keeps the value until the next keyframe, *Linear* goes straight, *Smooth* eases in and out. The shape belongs to the keyframe it starts at and applies up to the next one.

The timeline writes through the same function as OSC, MIDI, the cue list and the web interface (`ControlSystem::fireTarget`), and reads the value back through the same router. It has no private way to a slider.

## Time and position

The position comes from one of two places and never from both:

- **A timecode** (LTC or MTC from Settings, Sync): the position is the timecode time. The play and stop buttons are ignored while a timecode is present.
- **Its own transport:** play, pause, stop and seek. The position runs on the beat clock's time and stops when the program is paused. There is no second clock.

**Position source** (Settings of the window): *Automatic* follows a timecode when there is one and uses the transport otherwise, *Timecode only* waits for a timecode, *Own transport only* ignores a timecode.

**When the timecode is lost:** *Hold the position* (default) keeps the position where it was and waits, *Keep running on its own transport* carries on from there. When a timecode comes back, the position jumps to it.

A change of more than 0.5 s between two frames, backwards or forwards, is a **jump** (a seek, a loop wrap, a timecode jump). Cues between the old and new position are **not** fired and are counted as skipped, so jumping around the timeline does not fire a burst of cues. Lanes do not skip; they take the value of the new position at once (they "chase").

**Loop.** The loop region (at least 0.05 s long) repeats on the timeline's own transport. A timecode is never looped. Cues inside the region fire on every pass.

**Start rule.** A cue exactly at the position where the play button starts fires once on play. After a stop or a seek the cues at the start position are armed again.

## Cues on the timeline

The cue lane shows and edits the normal cue list (`cues.json`, SYNC.md). There is no second list and no second engine:

- While a timecode runs, the cue runner of the sync system fires the cues, as before.
- While the timeline's own transport runs, the timeline fires them.
- Never both for the same moment. Cues need **Cues** to be on in Settings, Sync; the window says so when it is off.

## Hand moves

If you move a controlled slider yourself while the timeline plays, the timeline notices (the value read back differs from what it wrote by more than 0.004) and lets go of that lane, which shows **Moved by hand**. *When I move it by hand* chooses how long your move wins:

- **Until the next keyframe segment** (default): the lane takes over again when the playhead enters another segment, or after a seek.
- **Until I release it:** the lane stays yours until you press *Release* on the lane or *Release hand moves* in the toolbar or the menu.

## Undo and redo

Edits of lanes, keyframes and cues are one history of 64 steps (8 MiB at most), with its own Undo and Redo buttons. Two honest notes:

- A timeline undo restores the cue list as it was at that step. Cue edits made in the table in Settings, Sync are not steps of this history, but a timeline undo can step back over them.
- Values that the timeline writes into layers and effects are ordinary changes of the composition. From reading the code (not tested), the composition's own undo history can record them as steps once they settle.

## Files and safety

- Lanes and the loop are saved in **`timeline.json`** in the data folder, not in presets and not in `cues.json`. Saving waits 0.5 s after the last change, writes `timeline.json.tmp` and renames it, so a crash leaves the old file whole. The program also saves when it closes.
- A file that cannot be read is copied to `timeline.json.damaged`, the log says so, and the timeline starts empty. Nothing else changes.
- Reading is strict and bounded: at most 4 MiB, nesting up to 16 levels, 32 lanes, 2048 and 16,384 keyframes, times from 0 to 86,400 s, values clamped to 0 to 1, a second lane for the same target and entries of the wrong type are skipped. A file without a `lanes` array is refused. A lane whose target is not a control of this program (for example from a newer version) is kept and shows *Not available*; nothing is written for it.
- Presets do not carry the timeline. Loading a look does not change lanes.

## Cost

With the timeline off the cost per frame is one check of a flag. With the largest allowed timeline (32 lanes of 512 keyframes each, 64 cues, playing) the update of the model and the runner took 5.6 to 6.2 µs per frame on the build machine, with a fake host in `nvj_timeline_tests`. The cost of the real reads and writes through the control system was not measured separately.

## What is not built

These are **Proposed** and wait for the maintainer (see ADR 0031):

- Control ids for the timeline itself (play, pause, stop, seek by OSC, MIDI or the web interface), and REST endpoints for lanes.
- Named show files, export and import of a timeline.
- Recording hand moves into lanes.
- A grid or keyframes in beats instead of seconds.
- Automating the controls of a shader clip. They have **no control id** yet (ADR 0030), so they cannot be a lane. The four controls of an ISF filter are effect controls and can.
- Event logic (conditions, "when this then that"); that is a separate task.

## What was not tested

- Any real graphics card: every run used the Mesa software renderer.
- Windows at run time. The CI builds it and runs the unit tests; the window, the keys and the files were not tried there.
- Real LTC or MTC sources, DAWs, timecode generators, VRChat, projectors.
- A show of hours with the timeline running.
- The window was driven with scripted input on a software renderer (play, pause, stop by button and key, dragging and adding keyframes, undo and redo, deleting, moving a cue, the cue picker, the lane picker, the right click menu, zoom, marking the loop). That used a temporary patch that is not part of the program, and it was not done by hand on a real screen. Not driven at all: the Settings popup, the *Release* buttons, the counters, the scroll bar and panning.

## Files

- `src/core/show_timeline.*` the model, evaluation, edits, JSON, runner and history.
- `src/control/timeline_system.*` ties the runner to the sync system, the beat clock and the control system, and saves the file.
- `src/app/ui_timeline.cpp`, `timeline_view.hpp` the window.
- Tests: `tests/test_timeline.cpp` (`nvj_timeline_tests`).
