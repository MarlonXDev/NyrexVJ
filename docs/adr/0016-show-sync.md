# ADR 0016: Show sync: one beat clock, timecode, MIDI clock, Pro DJ Link, cues and output frame lock

- **Status:** Accepted (2026-11-21) as milestone 5 of [ADR 0012](0012-open-source-vj-platform-plan.md), implemented in v0.24.0. Issue [#51](https://github.com/MarlonXDev/NyrexVJ/issues/51).
- **Date:** 2026-11-21
- **Owners:** Maintainers (pending assignment)
- **Related:** [SYNC.md](../SYNC.md), [ADR 0015](0015-advanced-output.md), [OUTPUTS.md](../OUTPUTS.md), [PERFORMANCE.md](../PERFORMANCE.md), [INTERFACES.md](../INTERFACES.md)

## Context
Until 0.23 the tempo of a show came from three places that did not know about each other: tapping, a BPM field and an optional blend of the tempo found by the audio analysis. A show that runs on timecode, a DJ set on Pioneer players and a show that is driven by a lighting desk need more: a tempo and a beat position that follow an outside clock without jumps, that tell the operator whether they can be trusted, and that fall back cleanly when the clock goes away. A timeline needs positions, and several outputs should show the same frame.

Constraints that shaped the design:
- the program runs next to VRChat, so every source costs nothing while it is off (no thread, no socket, no audio input) and a running source costs a few small updates a frame,
- no new dependency; the audio input, the MIDI input and the UDP code that already exist are reused, GCC 11 and the Windows toolchain must compile it,
- the logic must be testable without a window, a device or a network, and the first test of every decoder must be a generated signal,
- the clock must never make the picture jump while a source is stable, and a lost source must never freeze the show,
- NyrexVJ must not disturb equipment on the network of a show, and what cannot be tested must be said so.

## Decision
1. **One `BeatClock` in the core** (`src/core/beat_clock.*`). Sources report `SyncObservation`s: a phase (beat and the length of the bar it is counted in), a tempo, or only presence. Each source has a timeout and gains (Pro DJ Link 2 s, timecode 0.5 s, MIDI clock 1 s, audio 3 s). The clock advances by the frame time and removes a phase error by at most a quarter of the error per observation, so it never steps while a source is stable; an error of more than a beat or an explicit jump flag moves it at once. Modes are Automatic (Pro DJ Link, timecode, MIDI clock, audio, manual in that order, returning to a better source when it comes back) or one fixed source. The lock state (free running, searching, acquiring, locked, lost), the phase error in milliseconds and a quality value are exposed for the page. A latency offset of -500 to +500 ms shifts the target phase. The old "Sync BPM to audio" path is unchanged and only used while the audio source is the active one, so existing decks behave as before (the default clock source is Manual).
2. **Timecode as a position** (`timecode.*`). Frame rates 24, 25, 29.97 drop frame and 30 with exact conversions and a parser. A `TimecodeTracker` turns fixes (position, age, rate) into a running position, detects jumps and gaps and runs on the frame time between fixes. The position is the clock position (`beat = seconds x BPM / 60`), a modulation source (Timecode second and minute) and the trigger of cues.
3. **LTC from an audio input of its own** (`ltc.*`). A decoder for biphase mark code: zero crossings with hysteresis and interpolation between samples, an adaptive half bit estimate, the sync word, and a frame is accepted only if it follows the previous one. The position is the sample position of the end of the frame. It uses its own `AudioInput` (not the analysis device), only while a device is chosen.
4. **MTC from the MIDI port** (`MtcDecoder`): quarter frames in order, position at the last piece. The MIDI input now forwards system real time and system common messages (clock, start, continue, stop, song position, quarter frame) with a time stamp taken in the input thread, so the frame rate does not add jitter. LTC wins over MTC when both run.
5. **MIDI clock** (`midi_clock.*`): the tempo from the tick interval over a window, the position after Start, Continue or a song position pointer; without a Start only the tempo is used; Stop ends the observations.
6. **Pro DJ Link as a passive beat listener** (`djlink.*`, `djlink_input.*`). It listens on UDP 50001 (opened shared, only while enabled), parses the beat packets (tempo with pitch, beat in the bar, time to the next beat and bar), lets Automatic pick the first player or the user pick 1 to 6, and ignores the mixer. It never announces itself, takes no player number and sends nothing. The price is that it gets no play state and no master flag, and the beat is processed at frame time (up to one frame of jitter that the clock smooths).
7. **Cues** (`cue_list.*`): a sorted list of up to 512 (time, control target, value) entries stored as JSON in the user data folder, a runner that fires each cue once when the position passes its time and skips cues after a jump or a step above 0.5 s. A cue fires through the same router as MIDI and OSC (`ControlSystem::fireTarget`), so every control target is available.
8. **StageLinq is evaluated and not built.** The protocol is not published; the community libraries are reverse engineered and tested on a few devices, a client must announce itself on UDP 51337 and accept TCP connections to get the beat information, and none of it can be tested without Denon equipment. The passive approach of Pro DJ Link is not possible. [SYNC.md](../SYNC.md) lists the evidence and says how a source could be added later without touching the clock.
9. **Output frame lock** (`frame_lock.*`, `texture_history.*`, `output_manager.*`, `output_window.*`). `OutputWindow::render` is split into `draw` (own context, no swap) and `swap`. With the frame lock on, `OutputManager::render` draws all open windows and then swaps them in one loop (one context switch each, restoring the main context at the end). Per output `delayFrames` 0 to 8 reads from a ring of copies of the output texture made with `glBlitFramebuffer` in the main context; the ring has `max delay + 1` slots, exists only while an open output has a delay and is freed when none has. The swap skew (time between the first and last swap of a frame, CPU side) is kept over 120 frames and shown. The "Sync bars" pattern renders a 12 bit frame counter, a flash and a moving bar from one integer uniform; the counter is `frame - delay`, so equal numbers on two screens mean they show the same frame. Frame lock can be turned off (`outputFrameLock`), which restores one-by-one swapping.
10. **Settings and UI.** One new Sync page; the settings keys are `syncMode`, `syncLatencyMs`, `syncBarAlign`, `djLinkEnabled`, `djLinkPlayer`, `ltcDevice`, `cuesEnabled`, `outputFrameLock` and `delayFrames` per output. Unknown or wrong values fall back to the defaults, so an old or damaged file loads.

## Alternatives
- **Link or a plug-in for Ableton Link.** Not rejected, but Link needs its own library and a licence check, and it does not cover timecode or Pioneer players. It would be one more source behind the same clock.
- **Active Pro DJ Link participation (a virtual player).** It gives play state, master and beat grid, but takes one of the six player numbers and changes what the DJ equipment sees. Rejected for a first version; passive listening cannot disturb a show.
- **A timecode library (libltc) and a MIDI library.** Rejected: both decoders are small, a dependency would need a licence entry and a build on two platforms, and generated signal tests are easier against our own code.
- **A single phase locked loop instead of slewed corrections.** Rejected: the correction of a quarter of the error per observation is easy to reason about, easy to test (phase continuity) and has no tuning interplay between sources.
- **Frame lock with `glFinish` and a barrier between windows.** Rejected: it stalls the main thread for the slowest window and does not make the swaps simultaneous. The two phase draw and swap removes the one delay we control (the draw time of the next window) without blocking.
- **Per output delay through render targets of every output.** Rejected: one shared ring is allocated only when needed and costs one copy per frame instead of one per delayed output.
- **Using `glCopyImageSubData`.** Rejected: it needs OpenGL 4.3; the program targets 3.3. The blit works on 3.3 and keeps the format of the source.

## Consequences
- The beat position of the engine now comes from the beat clock; the BPM field, tap, resync and nudge act on it, and the tempo of the clock is written back to the composition BPM.
- Timecode maps to beats with the current BPM. A tempo change during a timecode run moves all later positions, so the BPM should be constant (or the timeline should be built for it).
- Pro DJ Link has no play state, so a paused player that still sends beats cannot be told apart from a playing one by NyrexVJ.
- The swap skew is a CPU time, not an optical one. Without a hardware frame lock (NVIDIA Quadro Sync and similar) outputs on different cards or at different refresh rates can still drift; the sync bars exist to show it.
- Delay is in whole frames of the composition frame rate and costs video memory in proportion to the delay (23.7 MiB at 1080p with 2 frames).
- A shared UDP port can receive packets of another program on the same port; packets are checked (header, length) and counted when malformed.
- Windows was built but not run for the new inputs and outputs.

## Validation
Tests (all run in CI, the output self test on Linux under a virtual display):
- beat clock: phase continuity, source priority, loss and return, lock states, latency, tap, resync (99 checks),
- timecode, LTC at 24, 25, 29.97 drop frame and 30 frames a second in several chunk sizes, with noise and filtering, corruption, jumps, gaps, MTC with generated quarter frames in streams and with errors, tracker, cue list (364 checks),
- Pro DJ Link with a packet simulator including other players, a mixer, malformed and foreign packets (28,185 checks),
- MIDI clock with timestamps, start, stop, continue and song position (246 checks), MIDI plumbing and the Linux sequencer test updated for clock messages,
- sync system: all sources into one clock, LTC and MTC from signals, cues fired from timecode (48 checks),
- frame lock logic: delay ring, swap skew statistics, sync bar numbers, JSON of the delay (130 checks),
- `--output-selftest` (93 checks, 29 new): sync bars decoded from pixels with and without delay, delay of 0, 1 and 2 frames with the right picture, memory of the ring (3 pictures at 64 x 36), release of the ring, two windows with the longest delay winning, swap skew with two windows,
- address and undefined behaviour sanitizers and a 1 MB stack run for the new tests.

Measured with the self test, software OpenGL under a virtual display (three runs): swap skew with two 1280 x 720 windows 0.34 to 0.35 ms with the frame lock and 4.5 to 5.1 ms swapped one by one; delay copy at 1920 x 1080 0.57 to 0.62 ms a frame and 23.7 MiB for a delay of 2.

Open questions:
- real Pioneer players and Denon gear, real LTC and MTC sources, real MIDI clock hardware,
- the frame lock on real graphics cards, several cards and real projectors; photographs of the sync bars on a real multi-screen set-up,
- the cost next to VRChat,
- whether a passive StageLinq subset exists.
