# Live workflow: launch on the beat, favorites and tags, replace everywhere, controller feedback and profiles

Added in 0.40.0 ([ADR 0033](adr/0033-live-workflow.md)). Everything here is part of the normal program and changes nothing until you use it: the quantize menu starts on Off, feedback is off, and favorites, tags and profiles are empty.

## Launch on the beat

The Quantize menu in the transport bar sets the grid: Off, Beat, Bar, 2 bars or 4 bars. It is saved as `launchQuantize` in `settings.json` (`off`, `beat`, `bar`, `2bars`, `4bars`).

- **What waits.** Everything that goes through the launch functions of the engine: a click on a cell or column header, the keys `1` to `8`, a deck button, and everything that reaches the program through the control host (MIDI, OSC, the REST API and web page, cues and the timeline). **What does not wait:** Play now in the Preview window, putting a clip into a cell from the Library, and a clip started with `--clip` on the command line.
- **The grid.** `LaunchQuantizer` (`src/core/launch_quantizer.*`) works on the beat position of the beat clock and the number of beats in a bar. It holds a launch until the next grid line and then runs it through the same function an immediate launch would have used, so there is no second way to start a clip.
- **Same target twice.** A new request for the same layer (a clip), the same column or a deck replaces the waiting one, so a double press does not queue two starts. Counted as `replaced`.
- **No clock, no waiting.** The clock counts as usable when the show is not paused and the lock state is Locked or Free. In every other case (Searching, Acquiring, Lost, paused) a launch runs at once and is counted as `executedAfterClockLoss`. A launch also never waits longer than 30 seconds (`executedAfterTimeout`), and the queue holds at most 32 launches (a further one runs at once).
- **Cost.** An update with launches waiting took 0.025 microseconds on the test machine; with nothing waiting it is a check of an empty list.

## Favorites and tags

Stored in `library_meta.json` (see [INTERFACES.md](INTERFACES.md)). A clip is identified by its name, as in presets. A clip has a favorite flag and at most 8 tags of up to 24 characters (turned to lower case, spaces become `-`, quotes, backslashes and control characters are dropped, a leading `#` is dropped). The file holds at most 4096 clips and 512 KiB. It is written through a temporary file and a rename, and a file that cannot be read is kept as `library_meta.json.damaged`.

The search field of the Library uses `parseLibraryQuery`: words match the name or a tag, `tag:name` matches a tag and `is:fav` keeps only favorites. All parts must match. The Favorites filter comes after Live.

## Replace a clip everywhere

`replaceClipEverywhere(comp, from, to, keepPlayback)` (`src/core/composition.*`) changes the library index in every cell that points to the old clip, in the working deck and in all other decks, and returns the number of cells changed. With `keepPlayback` the playback settings of each cell stay, otherwise the cell starts as if the new clip had been assigned. The edit history records the change like any other, so one undo takes it back.

## Feedback to the controller

`MidiFeedback` (`src/control/midi_feedback.*`) is a pure function from mappings and control state to MIDI messages. For each mapping it asks the router what to show:

| Target | Lit (127) while | Dark (0) |
| --- | --- | --- |
| `layer/L/clip/C` | layer L plays column C and the cell has a clip | otherwise |
| `column/C` | any layer plays column C with a clip | otherwise |
| `deck/N` | N is the working deck | otherwise |
| Switches (`master/blackout`, bypass, solo, effect on or off) | the switch is on | off |
| A value mapped with a CC | not lit: the value inside the low to high range is sent as 0 to 127 | |

Rules: only changes are sent, one entry per note or CC number and channel (the first mapping wins), at most 64 messages per frame (the rest go out in the next frames), and the values for faders and knobs are held back until 0.3 seconds after the last CC the controller itself sent (pads are not held back). A send that fails closes the output and shows the message; the show carries on.

The output is a `MidiOutput` (`src/control/midi_output.hpp`). The Windows backend uses `midiOutShortMsg`. Other platforms get a backend that reports that feedback is not available. The Settings, Control page shows the message instead of a device list there. A Linux backend over the ALSA sequencer is not built (see the ADR).

Settings keys: `midiFeedback` (bool, default false) and `midiOutDevice` (the name).

## Controller profiles

A profile is the list of mappings under a name, saved as `midi_profiles/<name>.json` in the data folder:

```json
{
  "format": "nyrexvj-midi-profile",
  "version": 1,
  "name": "Launchpad",
  "bindings": [ { "type": "note", "channel": 1, "number": 36, "target": "layer/1/clip/1", "low": 0, "high": 1 } ]
}
```

- Names are cleaned: letters, numbers, spaces, `.`, `-`, `_`, at most 48 bytes, no leading or trailing dots or spaces, and the Windows device names (`CON`, `NUL`, `COM1` and the like) are refused. A path never gets into a file name.
- Loading refuses a file over 256 KiB, a file that is not JSON, one with another `format`, a missing or lower than 1 `version`, a `version` above 1 ("written by a newer version"), a missing `bindings` list, and more than 1024 mappings. Single entries with an unknown target, number or channel out of range are skipped. In every refused case the current mappings are unchanged.
- Saving goes through a temporary file and a rename.

## What was tested

Automatic: `nvj_workflow_tests`, 346 checks (tags and favorites, the query language, the library file with damaged and oversized input, replacing in every deck, the quantizer at every grid and with clock loss, timeouts and a full queue, the settings, which targets light up, feedback computation including range, channels, duplicates and the 64 message limit, profile names with hostile input, profile files, saving and loading through the control system, and the control system with a recording output that fails). Run on Linux with the Mesa software renderer. **Not tested:** a real controller (the Windows `midiOut` code was built by the CI and never run), launches on a real show clock, and a library of thousands of real clips. The manual tests M-LW-1 to M-LW-3 in [TESTING.md](TESTING.md) are open.
