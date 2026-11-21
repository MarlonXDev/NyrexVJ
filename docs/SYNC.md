# Show sync

Since v0.24.0 NyrexVJ has one beat clock that every part of the program reads (effects, clip speed in beats, modulation, OSC output, the deck BPM display). Several sources can feed it: tapping, the audio analysis, MIDI clock, timecode (LTC or MTC) and the beat packets of Pro DJ Link players. Timecode can also start cues. Outputs can be locked to show the same frame. All settings are on one page: Settings, Sync (output settings are in Settings, Output, Frame lock).

Design and reasons: [ADR 0016](adr/0016-show-sync.md). The numbers in this file come from tests with generated signals and a packet simulator. Nothing was tried with real players, mixers, timecode generators or projectors, see [What was not tested](#what-was-not-tested).

## The beat clock

The clock holds a beat position and a tempo. It is advanced by the frame time and corrected by observations that sources report. An observation can carry a phase (a beat position, with the length of the bar it is counted in), a tempo, or only the fact that the source is present.

- **Phase continuity.** The clock never jumps while a source is stable. A phase error is removed gradually: each observation corrects at most a quarter of the error, so a picture that follows the beat does not stutter. Only an error of more than one beat, or a source that says "the position jumped" (a start, a song position, a timecode jump), moves the clock at once.
- **Tempo.** A reported tempo is blended in with a source specific gain, and when the clock is within 0.01 BPM of the reported value it takes the value exactly, so a steady source shows a steady number.
- **Latency offset.** Settings, Sync, Latency shifts the clock by -500 to +500 ms. A positive value moves the beat forward. Use it for the delay in cables, a network, an audio interface or the screens.
- **Lock indicator.** The state is Free running (Automatic or Manual and no source is present, only the manual clock), Searching (a fixed source was chosen and was never seen), Acquiring (a source is present, but the phase error is not yet small), Locked (at least three observations in a row within 0.06 beat of the clock) or Lost (a fixed source was seen before but is gone). In Automatic a lost source simply hands over to the next one, or to Free running. The page shows the active source, the state, the BPM, the phase error in milliseconds and a lock quality bar.
- **Bar alignment.** Sources that know the position inside the bar (Pro DJ Link, MIDI clock after a start, timecode) also set the bar start. Turn off Align to the bar start to follow only tempo and beat.

### Sources and priority

Settings, Sync, Clock source chooses one source or Automatic. Automatic uses the first source that is present in this order:

| Priority | Source | Present while | Provides |
| ---: | --- | --- | --- |
| 1 | Pro DJ Link | a beat packet arrived in the last 2 s | tempo, beat in the bar, phase |
| 2 | Timecode (LTC or MTC) | a timecode fix arrived in the last 0.5 s | position, so phase and bar |
| 3 | MIDI clock | a clock tick arrived in the last 1 s | tempo; phase after a Start |
| 4 | Audio | the audio analysis is running and Sync BPM to audio is on, or Audio is the chosen source (last report within 3 s) | tempo and a nudge towards the beat |
| 5 | Manual | always | tap tempo, BPM field, Resync |

When a chosen source is lost, Automatic falls back to the next one and returns when the better source is back; a fixed choice stays on its source and shows Searching or Lost. The old setting "Sync BPM to audio" still works: with the clock source on Manual it blends the audio tempo in as before, so decks saved by earlier versions behave the same.

## MIDI clock

MIDI clock needs the MIDI device chosen on the Control page. Ticks (24 per quarter note) are time stamped in the input thread, not when the frame is drawn, so the frame rate does not add jitter. The tempo comes from the interval of the ticks of a short window. Start and Continue make the position known (phase), Stop ends the observations, and a song position pointer sets the position. Without a Start only the tempo is used. The page shows whether clock is received, the tempo, running or stopped, whether the position is known, and counts ticks and gaps.

## Timecode

Timecode gives a position in seconds. The clock maps it to beats as `beat = seconds x BPM / 60`, counted from zero. The BPM should stay constant while a timecode runs (a tempo change in the middle of a show would move all later positions). The timecode is also a modulation source (Timecode second and Timecode minute in the modulation bands) and starts cues.

Supported frame rates: 24, 25, 29.97 drop frame and 30. 29.97 non-drop shows as 30 and drifts against the real time by 0.1 %.

### LTC (audio)

LTC is read from an audio input of its own (Settings, Sync, LTC input). It does not use the device of the audio analysis, and it reads a mono mix of the device, so a line input that carries only the timecode is the right choice. The decoder follows the biphase mark code with a zero crossing detector with hysteresis and a bit length estimate that follows the signal, finds the sync word (0011111111111101) and accepts a frame only when it follows the previous one, so one corrupted word does not move the position. The position is computed from the sample at which a frame ended, so it is not limited to whole frames.

- Measured with generated signals: all four frame rates decode, also in 1 to 4096 sample chunks; filtered and noisy signals decode with a position error of 0.2 to 0.7 samples; a jump is accepted after one consecutive frame, damaged words cost 2 to 3 frames; in the system test a generated signal that starts at one hour is read as 3602.017 s where about 3602.000 s is expected after two seconds, so within 17 ms.
- The frame rate is recognised from the measured speed (below 24.5 frames a second is 24, below 27.5 is 25, otherwise 30; the drop frame flag gives 29.97 drop). 25 frames a second played 4 % slow looks like 24 and cannot be told apart. Varispeed of 1.5 % is followed, more is not guaranteed.
- Reverse playback of LTC is not supported. A sample rate of 44.1 kHz or more is recommended.
- A missing or wrong device shows the error from the audio system, and a device with no signal shows No signal. A device that disappears while it runs is not reopened by itself: set the input to Off and back to the device.

### MTC (MIDI)

MTC arrives as quarter frame messages on the MIDI port chosen on the Control page. Eight pieces make one position. The last piece arrives 1.75 frames after the first, so the position is the decoded frame plus 1.75 frames at that moment. Pieces out of order are dropped and counted. If LTC and MTC both run, LTC is used.

## Cues

The cue list (Settings, Sync, Cue list, saved in `cues.json` in the user data folder) holds up to 512 cues of a timecode time, a target and a value. A target is any control target of the Control page (a clip, a column, a layer or master level, an effect parameter, a macro, blackout and so on); a trigger needs no value, a toggle on or off, a value 0 to 1. A cue fires once when the timecode passes its time. After a jump or a gap of more than 0.5 s the cues in between are skipped and counted, so jumping in the timeline does not fire a burst of cues. Times are written like `00:01:12:05` and use the frame rate of the timecode that was seen last (25 frames a second before any timecode). Test fires a cue by hand. Cues work the same from LTC and MTC and do nothing while no timecode runs.

## Pro DJ Link

NyrexVJ can follow the beat packets that Pioneer DJ players send on the network (UDP port 50001). It is a listener only: it does not announce itself as a player, takes no player number, sends nothing and so cannot disturb a show. Because of that it receives the broadcast beat packets, not the unicast status packets, so there is no play state and no master flag. The mixer's own packets are ignored.

- Beat packets give the pitch adjusted tempo, the beat in the bar and the time to the next beat and bar. The clock follows the player chosen in Settings, Sync, Follow player: Automatic takes the first player that is heard and sticks to it, 1 to 6 follows that player only.
- Packets are handled when a frame is drawn, so the position has up to one frame of jitter; the clock smooths it (the simulator test reaches 0.04 beat phase offset).
- Port 50001 is opened only when Listen for Pro DJ Link beats is on. It is opened shared, so other programs that listen on the same port can run at the same time. The page shows whether the listener runs, whether beats arrive, the player, the tempo, the beat in the bar, the players heard and the packet counts (packets, ignored, malformed). A port that cannot be opened shows an error.
- A firewall that blocks incoming UDP on port 50001 stops it, and the players must be on the same network segment (broadcast packets are not routed).

Reference for the packet layout: the public analysis of the protocol by Deep Symmetry (<https://djl-analysis.deepsymmetry.org/>). NyrexVJ contains no code from it.

## StageLinq (evaluation)

StageLinq is the network protocol of Denon DJ players and mixers (Prime series, SC5000 and SC6000, X1800 and X1850). **It is not built into NyrexVJ.** The reasons:

- There is no published specification. Everything known comes from reverse engineering by the community, and the libraries state that they were tested only with a few devices (for example Prime 4, Prime 2, Prime Go, two SC6000 with an X1850), with differences between players and mixers. A parser written from those notes could not be checked here.
- A device only talks to a program that announces itself. A StageLinq client has to send discovery frames to UDP port 51337 (a frame starting with the characters `airD` and a token), accept TCP connections from the devices and offer services (state map, beat info, file transfer). That is the opposite of the passive listener above: NyrexVJ would take part in the network of the show, and a mistake can be seen by the DJ's equipment.
- The beat information needs a long, stateful TCP exchange; the safe subset (listen only, no file transfer, no state writes) is not small, and a wrong implementation could confuse the players of a live set.
- It cannot be tested without a Denon player and mixer.

What works today for Denon gear: send MIDI clock from the mixer or a DAW, or use the audio analysis. If a small, safe, passive subset becomes possible (for example from a published protocol description or captured traffic from a real device), it can be added as a new source behind the existing beat clock without changing anything else: a source only has to report `SyncObservation`s.

## Output frame lock

Settings, Output, Frame lock. Several output windows are separate windows with their own graphics context; with one picture per frame they can still change a frame apart. NyrexVJ does three things:

1. **Same frame on all outputs.** All open output windows are drawn first and then swapped one right after the other (before, each window was swapped right after it was drawn, so the swap of window 2 came a whole draw time after window 1). It is on by default and can be turned off to get the old behaviour.
2. **Delay per output.** Every output has a delay of 0 to 8 frames. The picture of a slower projector or a path with more processing delay can be matched by delaying the faster outputs. Delayed outputs read the picture from a ring of copies that is shared by all outputs, filled only while at least one open output has a delay. At 1920 x 1080 with a delay of 2 the ring is 3 pictures, 23.7 MiB of video memory and 0.6 ms a frame for the copy (software OpenGL). With no delay set, nothing is allocated and nothing is copied. The first frames after a change show the oldest picture that exists.
3. **Sync bars.** The Sync bars switch shows on every open output a frame counter (12 bars, binary), a flash that changes every frame, a bar that moves 60 positions and green guide marks, instead of the picture. Film several screens with one camera: equal counters mean the same frame, a different counter shows how many frames apart two outputs are, and the delay of each output is already counted in. With a delay of 2 on one output and a path that is 2 frames slower, the counters match.

**Swap skew.** The program measures the time between the first and the last swap of every frame while two or more outputs are open and shows the last, average and longest value over the last 120 frames. Measured with the self test (two 1280 x 720 windows, software OpenGL, three runs): 0.34 to 0.35 ms with the frame lock, 4.5 to 5.1 ms with one by one swapping.

**What this is not.** It is not a hardware genlock. The swap skew is the CPU time when the swaps are handed to the graphics driver; the driver, the graphics card, the display and the projector add their own time, and the windows are not locked to the vertical blank of each other. Windows on different graphics cards or at different refresh rates still drift. For tear free output on several cards, use a card with a frame lock option (for example NVIDIA Quadro Sync or AMD FirePro S400) and its driver tools; NyrexVJ does not control them. Use the sync bars with a camera to see what you get on your hardware.

## Files

- `src/core/beat_clock.*` the clock, `timecode.*` frame maths, MTC decoder and tracker, `ltc.*` the LTC decoder, `midi_clock.*`, `djlink.*`, `cue_list.*`, `frame_lock.*` (delay ring logic, swap skew, sync bar numbers).
- `src/control/sync_system.*` ties the sources to the clock, `djlink_input.*` is the UDP listener, `udp_socket*` has a shared open mode.
- `src/render/output_manager.*`, `output_window.*` (draw and swap are separate steps), `texture_history.*` (the delay ring).
- `src/app/ui_sync.cpp` the Sync page.
- Tests: `test_beat_clock`, `test_timecode` (LTC, MTC, cues), `test_djlink` with a packet simulator, `test_midi_clock`, `test_sync_system`, `test_frame_lock`, and the cases of `--output-selftest`.

## What was not tested

- Real Pioneer players or mixers, real Denon gear, a real network with switches, a firewall or a second network segment. The packet layout comes from the public analysis; the simulator was written from the same description, so a shared misunderstanding would not be found.
- Real LTC or MTC sources: timecode generators, DAWs, hardware that sends timecode, audio interfaces and their line levels, cables, drop frame material at 29.97 and any reverse or varispeed playback.
- Real MIDI clock hardware and USB MIDI jitter, and MIDI clock over network MIDI.
- Frame lock and delay on real graphics cards, several cards, real projectors, real displays with different refresh rates, tear free output, and the photographed sync bars. Everything above was measured with software OpenGL (llvmpipe) under a virtual display.
- Windows at run time: the code is built for Windows in CI, but the shared UDP port, the audio input for LTC and the output windows were not run there.
- Next to VRChat: the cost of the sync system (a few small updates a frame, the timecode decoding on the render thread, and a capture thread of the audio input only while a timecode device is chosen) and of the output changes was not measured while VRChat runs.
