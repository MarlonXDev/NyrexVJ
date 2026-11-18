# Modulation sources

Everything that moves with the music or with the tempo is a **source**: a number between 0 and 1 that is looked up the same way everywhere. A layer slot, the master slot and the reaction of an effect all pick one source from the same list and multiply it by an amount.

## The list of sources

The order is part of the file format, because presets store the name of the source and old presets must keep meaning the same thing.

| # | Source | Value |
|---|--------|-------|
| 0 | Off | always 0 |
| 1 to 5 | Bass, Mid, High, Level, Kick | analysis of the audio input, or the virtual beat when there is no input |
| 6 to 9 | LFO 1 to 4 | tempo synced oscillator, see below |
| 10 to 17 | Macro 1 to 8 | the position of the knob |
| 18 | Beat | rises from 0 to 1 over one beat, then starts again |
| 19 | Bar | rises from 0 to 1 over one bar (2 to 8 beats), then starts again |

New sources are always appended. Presets store the name of a source (for example `"LFO 2"`), never the number, so reordering would not break them either, but renaming a source would.

## LFOs

Each LFO has a shape, a cycle length and a phase.

- Shapes: Sine, Triangle, Saw (rises, then jumps back), Square (high for the first half of the cycle), Random (a new random value every cycle, held until the next) and Smooth random (glides between those random values). Random is seeded by the LFO number, so LFO 1 and LFO 2 never produce the same sequence, and it is the same sequence every time the show is played.
- Cycle length: 1/4, 1/2, 1, 2, 4, 8, 16 or 32 beats.
- Phase: 0 to 1 of a cycle, where the LFO starts at beat 0.

An LFO is a pure function of the beat position, so it follows Tap, Resync, grid shifting and the BPM, and it stands still while the tempo clock is paused. It has no state of its own that could drift.

## Macros

A macro is a knob that holds a value from 0 to 1. It is saved with the preset but is not part of undo and redo, because it is a performance control like the active column. Every macro is a control target (`macro/1` to `macro/8`), so it can be mapped to a MIDI controller, set over OSC and sent back out as a named OSC parameter.

## Beat grid

The beat count is a running number of beats. The transport shows it as `beat/beats per bar`. Clicking it opens the grid settings:

- Beats per bar, 2 to 8 (default 4). Saved with the preset and covered by undo.
- `<<` and `>>` shift the grid by a sixteenth of a beat, for when the detected or tapped beat is slightly off the music.
- Snap to beat rounds the position to the nearest whole beat without changing the count.
- Bar start here shifts the count so that the nearest beat becomes the first beat of a bar.

## For developers

`src/core/modulation.hpp` holds `modulationSource(source, audio, modulators, beat, beatsPerBar)`, the only place that turns a source number into a value, and `lfoValue`. `Engine::sourceValue` calls it with the engine's audio features and beat position. The unit tests in `tests/tests.cpp` (`testModulation`) cover every shape, the periodicity, the source lookup, the preset round trip, old and tampered files and undo.
