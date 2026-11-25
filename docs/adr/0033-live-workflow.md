# ADR 0033: Live workflow: launch quantize, favorites and tags, replace a clip everywhere, controller feedback and profiles

- **Status:** Accepted (requested by MarlonXDev as prompt 14). Issue [#94](https://github.com/MarlonXDev/NyrexVJ/issues/94). A Linux backend for the feedback output, colour or RGB feedback for named controllers, a clip colour label, a Preview that waits for the Live output, and OSC feedback are **not built** and stay Proposed.
- **Date:** 2026-11-25
- **Owners:** Maintainers (pending assignment)
- **Related:** [WORKFLOW.md](../WORKFLOW.md), [SYNC.md](../SYNC.md), [INTERFACES.md](../INTERFACES.md), [TESTING.md](../TESTING.md), [ADR 0016](0016-show-sync.md), [ADR 0013](0013-media-control.md)

## Context
Playing live needs four things the program did not have: a clip that starts on the beat when the press was a little off, a way to find clips fast in a library that keeps growing, a way to swap a media file without visiting every cell, and a controller that shows what is playing. The task also asked for named controller profiles and a check that the Live and Preview display already exists.

## Findings (what existed before)
- **Preview and Live: present.** The deck window shows Live and Preview, Ctrl+click only previews, and Play now is immediate. Nothing to build.
- **Launching:** every start went straight into the composition, from the UI, from `EngineControlHost` (MIDI, OSC, REST, cues, timeline) and from the keyboard. One host interface, so one place to add a wait.
- **Beat clock:** `BeatClock` with a lock state and a source, owned by `Engine`. No launch function used it.
- **MIDI:** input only, on Windows and Linux (ALSA through `dlopen`); mappings in `controls.json`. No output.
- **Library:** search by name and a filter row. No favorites, no tags. Presets store clips by name.
- **Replace:** none. A clip was swapped cell by cell.

## Decision
1. **Quantize in the engine, not in the UI.** `LaunchQuantizer` is a small core class with no OpenGL. `Engine::requestTrigger`, `requestTriggerColumn` and `requestSwitchDeck` take the request, ask the quantizer, and either run it at once or keep it. Every UI path and `EngineControlHost` call these, so MIDI, OSC, the REST API, cues and the timeline are quantized by the same code without knowing about it. Play now, assigning from the Library and the command line call the old functions directly and stay immediate.
2. **Fail towards playing.** If the beat clock cannot be trusted (not Locked or Free, or the show is paused) the launch runs at once. A queue of 32 and a wait of 30 seconds are hard limits that also run the launch. A show must never be stuck waiting for a beat that is not coming.
3. **A repeated request replaces the waiting one** for the same layer, column or deck, so nothing piles up from a double press.
4. **Favorites and tags in their own file** `library_meta.json`, keyed by clip name, written atomically, damaged files set aside. Not in presets (a look should not change because you starred a clip) and not in `settings.json` (it is data, not a setting). Search syntax `tag:name` and `is:fav` in the same field, plus a Favorites filter.
5. **Replace everywhere as one composition edit** (`replaceClipEverywhere`), so the existing edit history gives undo in one step without new code.
6. **Feedback as a pure function plus a small output interface.** `MidiFeedback::compute` turns mappings and router state into messages and remembers what it sent. `MidiOutput` hides the platform. The router got one read-only function, `lit`, that answers what a pad should show. Feedback is off by default and needs a chosen output device.
7. **Plain messages only.** Notes and control changes with the values 0 and 127 (or the scaled value for a fader), on the same note or CC number and channel as the mapping. No colour tables and no controller specific start messages, because nothing could be tried with real hardware.
8. **Profiles are the mappings in a named file** with a `format` and a `version`, bounded reads (256 KiB, 1024 mappings), a strict name filter and atomic writes. A refused file changes nothing.
9. **Windows backend only for the output.** The Windows code is a thin wrapper around `midiOutShortMsg`. Other platforms get a backend that says feedback is not available there, and the page shows that message.

## Alternatives considered
- **Quantizing in the UI and in each input.** Four places to change and the REST API, cues and timeline would have been missed. The host already is the single door.
- **Holding the launch inside `Layer`.** It would have mixed time into the data that is saved in presets.
- **Tags inside presets or the clip library.** The library list is built from files and built-in clips at run time, so there is nothing to store them in, and presets would then differ for no reason.
- **A Linux ALSA output now.** It is about 150 lines on top of the existing `dlopen` code, but the fake ALSA of the tests would need an output side and nothing could be tried with a device. A backend that is built and never exercised is worse than one that says it is missing. It stays Proposed.
- **Colour feedback tables for known controllers (Launchpad, APC and others).** Each table is a promise about a device. Without the device it would be a guess written as a fact.

## Consequences
- Every launch goes through one more function. With nothing waiting it costs a check; with the grid on it is 0.025 microseconds per frame for four waiting launches.
- With the grid on, a launch is no longer instant, and the Preview does not wait either. Users who want instant starts keep the menu on Off, which is the default.
- Feedback adds 12 to 14 microseconds per frame for 160 mappings when it is on, and nothing when it is off.
- New files: `docs/WORKFLOW.md`, `src/core/launch_quantizer.*`, `src/core/library_meta.*`, `src/control/midi_feedback.*`, `src/control/midi_output*.cpp`, `tests/test_workflow.cpp`. New user data: `library_meta.json`, `midi_profiles/*.json`. New settings: `launchQuantize`, `midiFeedback`, `midiOutDevice`. 45 new texts in all seven translation tables, and two sections each in the manual chapters for the clip grid, the Library and MIDI.
- A moved or renamed clip loses its star and tags (the name is the key).

## Validation / open questions
- Done here: `nvj_workflow_tests` (346 checks), the whole test run (36 groups), the translation check, `--check-shaders` and the website check, on Linux with a software renderer.
- **Not done:** any real MIDI controller, the Windows output code beyond the CI build, launches against a real beat in a real show, a library of thousands of clips.
- Open for the maintainer: a Linux feedback backend, which controllers deserve colour tables (and who owns one to test them), whether the Preview should also wait for the grid, and clip colour labels.
