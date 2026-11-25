# ADR 0031: Show timeline: optional keyframe lanes on the existing cue list, one position from the timecode or an own transport, writes through the control system, hand moves win, one undo history

- **Status:** Accepted (requested by MarlonXDev, Prompt 08). Issue [#88](https://github.com/MarlonXDev/NyrexVJ/issues/88). Control ids for the timeline itself, REST endpoints, named show files, import and export, recording hand moves into lanes, a beat grid, automation of shader clip controls and event logic are **Proposed** and wait for the maintainer.
- **Date:** 2026-11-25
- **Owners:** Maintainers (pending assignment)
- **Related:** [TIMELINE.md](../TIMELINE.md), [SYNC.md](../SYNC.md), [INTERFACES.md](../INTERFACES.md), [TESTING.md](../TESTING.md), [ADR 0016](0016-show-sync.md), [ADR 0029](0029-rest-api.md), [ADR 0030](0030-isf-import.md)

## Context
The request: a timeline for shows with keyframes for controls and a cue list on it, that follows LTC or MTC or runs on its own, that can loop, jump and lose its timecode without a mess, that does not fight the hands of the person at the controls, that has undo and redo, saves safely, and does not add a second clock, a second cue engine or a second way to move a control.

## Findings (what existed before)
- **A cue list.** `CueRunner` (`src/core/cue_list.*`) fires cues of a timecode time, a control id and a value when the timecode passes them, skips and counts cues after a jump of more than 0.5 s, and is run by `SyncSystem`. The list is `cues.json` (version 1, 512 cues) and is edited in a table in Settings, Sync.
- **One way to move a control.** Every control that has a value has an id ("control id", `ControlSystem::controlTargets`). OSC, MIDI, the cue list and the web interface all end in `ControlSystem::fireTarget(id, value)`; `ControlRouter::value(index)` reads the value back.
- **One clock.** `BeatClock` owns time and tempo. The timecode (LTC, MTC) is an input of the sync system, not a clock.
- **No automation over time.** There was nothing between a cue (a point in time) and an LFO (a repeating shape).
- **Shader clip controls have no ids** (ADR 0030), so they cannot be a target.

## Decision
1. **Optional and off by default** (`timelineEnabled`). While off nothing is written and the timeline fires no cue; the lanes can still be prepared. The cost per frame while off is a flag check.
2. **Own file.** Lanes and the loop are `timeline.json` (version 1). `cues.json`, presets and sessions are unchanged, and a program without the timeline reads them as before.
3. **No second clock.** The position is the timecode time while a timecode runs. Otherwise it is the timeline's own transport (play, pause, stop, seek), anchored to `BeatClock::time()` and advancing only while the program runs. Position source: Automatic, Timecode only, Own transport only. When a timecode is lost: hold the position (default) or keep running on the transport.
4. **No second cue engine.** The cue lane edits `SyncSystem::cues()`. While a timecode runs the existing `CueRunner` fires them; while the transport runs the timeline's `fireRange` fires them; never both. Jumps (a step below 0 or above 0.5 s: seek, loop wrap, timecode jump) skip the cues in between and count them, as the cue list always did. A cue exactly at the start position fires on play (`armStart_`).
5. **No second way to move a control.** Lanes write with `ControlSystem::fireTarget` and read back with `ControlRouter::value`. A lane target must be a control id with a value: triggers (clips, columns, decks, presets) are refused as lanes and live on the cue lane; `master/bpm` is refused because the beat clock owns the tempo; there is one lane per target. A lane whose target does not exist (a newer file) is kept and shows *Not available*.
6. **Key model.** Keyframes have a time (0 to 86,400 s), a value (0 to 1) and a shape that applies up to the next keyframe: hold, linear or smooth. Toggles are step only (on at 0.5 or more). Limits: 32 lanes, 2048 keyframes a lane, 16,384 in all, target text 128 bytes, file 4 MiB, JSON depth 16. The reader is strict and tolerant in the same way as the cue list: entries of the wrong type are skipped, values are clamped, a file without `lanes` is refused.
7. **Hand moves win.** A lane is written only once the timeline is "engaged". If the value read back differs from the value written by more than 0.004, the person moved it and the lane lets go. *Until the next keyframe segment* (default) or *Until I release it*; a release button per lane and one for all.
8. **Loop and seek.** A loop region of at least 0.05 s repeats on the own transport only, never on a timecode. A seek while playing keeps playing; a seek while paused reports Stopped. Lanes chase after a jump (they take the value of the new position at once); cues are skipped and counted.
9. **One undo history.** `TimelineHistory`: lanes, loop and cues together, 64 steps, 8 MiB. The window has its own Undo and Redo and handles Ctrl+Z, Ctrl+Y and Ctrl+Shift+Z only while it has focus; Space (tap tempo) is left alone while it has focus. A timeline undo restores the cue list as of that step, so it can step back over edits made in the Sync page table (they are not steps of this history). Values the timeline writes are ordinary composition changes.
10. **Safe saving.** Saving is debounced by 0.5 s, written to `timeline.json.tmp` and renamed, and done once more at exit. A file that cannot be read is copied to `timeline.json.damaged`, a line goes to the log, and the timeline starts empty. (The existing cue and settings files are not changed by this decision.)
11. **Window.** A dockable window with a toolbar, a ruler (click to move the playhead, Shift and drag for the loop), a cue lane, a row per lane with its curve, an edit strip, a scroll bar and optional counters. Menu **Timeline** and the start option `--timeline`. 80 new texts in all seven languages.

## Alternatives considered
- **A second cue list inside the timeline.** It would let the two lists drift apart and fire a cue twice. One list with two firing paths that are never active at once is smaller and keeps `cues.json` stable.
- **A separate timeline clock.** Two clocks would drift against each other and against the timecode. The transport is a position anchored to the one clock.
- **Writing values straight into the composition.** It would skip the router and the audio gate, the macro and MIDI paths, and bypass the control id rule. `fireTarget` is already the single door.
- **Storing the timeline in presets or in the session.** A look is a state; a show is a sequence. Loading a look must not erase a prepared show.
- **Keyframes in beats.** It would need tempo changes to be handled in the model. Seconds follow a timecode directly. A beat grid is Proposed.
- **Recording hand moves into lanes.** Useful, but it needs rules for thinning, undo and conflicts with the override policy; not part of this change.
- **Automating shader clip controls.** They have no control ids (ADR 0030); adding ids is a decision of its own.

## Consequences
- With the timeline on and playing, a lane writes only when its wanted value changes by more than 0.0001, so a lane that holds a value costs a read and a compare.
- A new file in the data folder (`timeline.json`, `timeline.json.tmp` while saving, `timeline.json.damaged` after a bad file). Four new keys in `settings.json`: `timelineEnabled`, `timelineSource`, `timelineLost`, `timelineOverride`.
- The timeline has no control ids and no API endpoints, so it cannot be started from OSC, MIDI or the web interface yet. What it writes can be read through the API like any other change.
- The cue table in Settings, Sync still works and shows the same list.

## Validation / open questions
- Done here: `nvj_timeline_tests` (420,671 checks: evaluation of every shape, edits, JSON round trip and 6,000 hostile files, old files and an unchanged cue file, transport, cues on the transport, loop, seeks, timecode following and loss, clock changes, source choice, hand moves in both policies, a disabled timeline, undo and redo with a heavy history, the real control system with simulated sources, persistence including the damaged file and the atomic write, and random operations), also under AddressSanitizer and UndefinedBehaviorSanitizer; the cost of the update with 32 lanes of 512 keyframes (5.6 to 6.2 µs a frame with a fake host); all 34 test groups of the suite; `--shader-selftest`; `--check-shaders`; the translation check; and the window driven with scripted input on a software renderer (see TIMELINE.md for what was and was not driven).
- **Not done:** any graphics card, Windows beyond the CI build and the unit tests, real LTC or MTC sources, a show of hours, VRChat, projectors, and clicking the Settings popup, the release buttons, the counters and panning.
- Open for the maintainer: control ids and REST endpoints for the timeline, named show files and export or import, recording hand moves, a beat grid, ids for shader clip controls, and event logic (a separate task).
