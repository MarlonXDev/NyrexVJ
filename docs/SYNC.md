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

### Audio analysis 2

Settings, Audio, Beat tracking chooses how the audio source finds the beat. **Classic** is the detector that was there before (a kick found from the low band, the tempo from the median of the last intervals) and is the default. **Improved** adds a tracker next to it, see [ADR 0058](adr/0058-audio-analysis-beat-tracker-latency-bands-and-channels.md):

- **What it reports.** Tempo from all pairs of onsets of the last 10 s (so a missing beat does not break it), the phase inside the beat, a tempo confidence, a beat confidence over the last four beats and a locked state (on at 0.4, off below 0.25). While it is locked the beats come from a steady grid, one period apart, and the grid goes on through a short break; after 1.5 s of silence it stops and keeps the tempo for 10 s. While it is not locked the classic detection gives the beats as before.
- **How the clock uses it.** Nothing changes in the clock: the audio source still reports a tempo (blended in only while the confidence is above 0.55) and a nudge on every beat, so the clock follows the grid exactly as it followed the detections, with the priority and the fallback of the table above. OSC `/nyrex/out/onset` follows the beats of whichever method is active. MIDI clock, LTC, MTC and Pro DJ Link are not touched.
- **Audio latency** (-250 to +250 ms, only with Improved) moves the beats earlier (positive) or later (negative). It is separate from the Sync latency above, which shifts the whole clock. Use it for the delay of the audio device or of a loopback; the detector itself is 1 to 3 ms late at 60 frames per second and 7 to 15 ms at 30, which the setting can cover.
- **Band split.** Bass and mid split (60 to 400 Hz, default 150) and Mid and high split (800 to 6000 Hz, default 2000) move the borders of the three bands for both methods. The defaults are the old values.
- **Channel.** Mix (default), Left or Right. With Left or Right the capture of a device reads two channels and keeps one; the demo signal and inputs that cannot do this show the box greyed out.
- **Cost.** The extra work is one onset curve and a small histogram per frame: 41 us on average and 162 us at worst per frame in the test, against 38 us and 218 us for Classic on the same machine; the audio thread is not touched.
- **Limits.** Tested on synthetic tracks only (see the numbers in the ADR). Not run with a real DJ mix, a real audio interface or a loopback; a track without a steady beat gets low confidence and no grid beats.

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

The cue list (Settings, Sync, Cue list, saved in `cues.json` in the user data folder) holds up to 512 cues of a timecode time, a target and a value. A target is any control target of the Control page (a clip, a column, a layer or master level, an effect parameter, a macro, blackout and so on); a trigger needs no value, a toggle on or off, a value 0 to 1. A cue fires once when the timecode passes its time. After a jump or a gap of more than 0.5 s the cues in between are skipped and counted, so jumping in the timeline does not fire a burst of cues. Times are written like `00:01:12:05` and use the frame rate of the timecode that was seen last (25 frames a second before any timecode). Test fires a cue by hand. Cues work the same from LTC and MTC and do nothing while no timecode runs. Since 0.38.0 the optional show timeline ([TIMELINE.md](TIMELINE.md)) draws the same list on a ruler next to keyframe lanes and fires it while its own transport runs; there is still only one list and one cue runner at a time.

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

What works today for Denon gear: send MIDI clock from the mixer or a DAW, or use the audio analysis.

**Analysis at 0.56.0.** The evaluation above was checked again and extended with sources in [STAGELINQ.md](STAGELINQ.md) (risks, what the public descriptions say and do not say, a proposal in steps, the tests and the hardware checks each step would need) and [ADR 0050](adr/0050-stagelinq-analysis-first.md). The conclusion is the same: StageLinq is still not built. Nothing is implemented until the maintainer approves it, and nothing is sent to DJ equipment before a Denon device was available to check it. If a small, safe, passive subset becomes possible (for example from a published protocol description or captured traffic from a real device), it can be added as a new source behind the existing beat clock without changing anything else: a source only has to report `SyncObservation`s.

## Output frame lock

Settings, Output, Frame lock. Several output windows are separate windows with their own graphics context; with one picture per frame they can still change a frame apart. NyrexVJ does three things:

1. **Same frame on all outputs.** All open output windows are drawn first and then swapped one right after the other (before, each window was swapped right after it was drawn, so the swap of window 2 came a whole draw time after window 1). It is on by default and can be turned off to get the old behaviour.
2. **Delay per output.** Every output has a delay of 0 to 8 frames. The picture of a slower projector or a path with more processing delay can be matched by delaying the faster outputs. Delayed outputs read the picture from a ring of copies that is shared by all outputs, filled only while at least one open output has a delay. At 1920 x 1080 with a delay of 2 the ring is 3 pictures, 23.7 MiB of video memory and 0.6 ms a frame for the copy (software OpenGL). With no delay set, nothing is allocated and nothing is copied. The first frames after a change show the oldest picture that exists.
3. **Sync bars.** The Sync bars switch shows on every open output a frame counter (12 bars, binary), a flash that changes every frame, a bar that moves 60 positions and green guide marks, instead of the picture. Film several screens with one camera: equal counters mean the same frame, a different counter shows how many frames apart two outputs are, and the delay of each output is already counted in. With a delay of 2 on one output and a path that is 2 frames slower, the counters match.

**Swap skew.** The program measures the time between the first and the last swap of every frame while two or more outputs are open and shows the last, average and longest value over the last 120 frames. Measured with the self test (two 1280 x 720 windows, software OpenGL, three runs): 0.34 to 0.35 ms with the frame lock, 4.5 to 5.1 ms with one by one swapping.

**What this is not.** It is not a hardware genlock. The swap skew is the CPU time when the swaps are handed to the graphics driver; the driver, the graphics card, the display and the projector add their own time, and the windows are not locked to the vertical blank of each other. Windows on different graphics cards or at different refresh rates still drift. For tear free output on several cards, use a card with a frame lock option (for example NVIDIA Quadro Sync or AMD FirePro S400) and its driver tools; NyrexVJ does not control them. Use the sync bars with a camera to see what you get on your hardware.

**Hardware status (since 0.57.0).** Below the swap skew, Settings, Output shows "Hardware frame lock": not checked, not offered by this graphics driver, or offered by the driver, and the extension names the driver lists. It reads what the driver says and nothing more: no swap group is joined, no barrier is bound and no driver setting is changed, so the software frame lock above is still the only lock that runs. A listed extension does not mean that a sync board is connected or locked. What was checked, what was not and why joining is held back: [GENLOCK.md](GENLOCK.md) and [ADR 0051](adr/0051-hardware-sync-status.md).

## Network sync (several PCs)

Settings, Sync, Network sync. Off by default. One NyrexVJ is the **master** and up to eight others are **followers**. The decision and the alternatives are in [ADR 0041](adr/0041-multi-pc-sync.md).

**What a follower takes over** (three ticks, each can be off):

- *Follow the controls:* every control that is not a trigger and not the tempo (150 values: master, layer, colour, effect and macro controls, blackout). While this is on the timeline and the cue list of the follower are paused so that they do not fight the master.
- *Follow decks and clips:* the active deck and the clip that plays on each layer. A clip started again on the master restarts on the follower.
- *Follow beat, tempo and timecode:* the beat clock (a new source `Network`, first in Automatic), the tempo, play and pause, the timecode (a fifth timecode source, so cues work) and jumps of the beat. A PC set to Manual takes Network while it follows; a PC set to another fixed source keeps it and the page says so.

**What it never does.** It never sends media: images, videos, 3D models and shaders must exist on every PC; a show package ([ADR 0034](adr/0034-show-packages.md)) is the way to get the same files everywhere. A follower never changes the master. It does not send audio, and it does not share the timeline, the presets or the settings.

**Setting it up.** Master: Role Master, tick Allow other PCs in the network if the followers are other computers, press Copy key. Follower: Role Follower, type the IPv4 address of the master (`127.0.0.1` for two programs on one PC), the same port (default 9030), paste the key and press Use this key. Both pages show the same Key check code. Allow the UDP port in the firewall of the master. The follower goes through Connecting and Getting the show state to In sync, and the page shows offset, round trip and jitter of the clock, the tempo and what was applied.

**The protocol** (version 1, details in the ADR). UDP datagrams of at most 1200 bytes with a header of 26 bytes and a 16 byte HMAC-SHA256 tag. Messages: Hello, Welcome, State, Delta, Clock, Ping, Pong, Resync, Manifest, Bye, Reject. The state is a vector of 159 floats (150 controls, the deck, and for each layer the active column and a start counter). The master sends a Delta of what changed at most 30 times a second, a full State as a keyframe every 5 s and when asked, a Clock beacon every 0.1 s and the manifest every 5 s and when it changes. Every packet of a session has a sequence number that only grows; duplicates and old packets are dropped and counted, a gap makes the follower ask for a resync until a State arrives, and a tail check catches a lost last packet. Hello and Welcome repeat until the handshake is confirmed.

**Versions.** The Hello carries the protocols the follower speaks and a hash of the list of control ids. Same hash: Full. Different hash with a common protocol: **Clock only**, so only the beat is followed and the page says why. No common protocol: refused with a reason. A packet with a wrong tag gets no answer.

**The clock.** The follower pings once a second and keeps the last 40 round trips. It uses the lowest third by round trip time, takes the mean offset and fits a drift only when the samples span at least 10 s and the slope is more than three standard errors from zero (clamped to plus or minus 200 ppm). The master beat is predicted as the last beacon plus the time since then times the tempo while the master plays, and given to the beat clock as a phase observation once per frame, so the clock's smoothing and lock states apply. A jump of the beat on the master is counted in the beacon and the follower jumps with it.

**Conflict rules.** (1) The master wins: a value that changes on the master is applied. (2) The first connection applies the whole state. (3) After a reconnect only values that changed on the master are applied; a value edited on the follower that did not change on the master stays. (4) A deck switch is applied before the controls that came with it. (5) A clip is not started when the local cell holds a clip with another name than the master's, and a deck is not switched when it is not in use here; both are shown. (6) After 3 s without packets the follower is Lost: it holds the last values, the beat keeps running at the last tempo, the timeline and cues stay paused and it reconnects with a backoff of 0.5 to 5 s.

**Missing media is shown.** The manifest holds the names of the used decks and of the 32 clip cells of the active deck. The page lists up to 10 differences: a clip only the master has (missing here), a cell that holds another clip here, a deck not in use here, a deck with another name.

**Safe defaults.** Off, no socket, no key. The master listens on this PC only until Allow other PCs in the network is ticked. A follower connects only to the address that you typed. No discovery. The key comes from the secure random source, lives in `netsync_key` in the data folder, is hidden on the page until you press Show, and is not part of settings, presets, packages or the log. A new key disconnects every follower. At most 8 followers; each address is limited to 400 packets a second. It is **signed and not encrypted**: someone on the network can read the show state but cannot change it or join without the key. Use it in a network you trust.

**What this is not.** It is not a hardware frame lock. Each PC draws at its own frame time, so two outputs can differ by about one frame, and cards, displays and projectors add their own delay. The beat of a follower is accurate to within the frame time and the network jitter. For tear free output across machines use hardware genlock (prompt 12, not built).

**Measured here** (Linux, Mesa software renderer, loopback network, two programs or one program with a relay; see [PERFORMANCE.md](PERFORMANCE.md)): beat of a follower within 2 to 17 ms mean and 31 ms worst of the master, including 25 percent packet loss, up to 25 ms added delay and a 6 s outage; the update step costs 10 to 18 microseconds a frame on average; one follower takes about 1.1 kB/s idle and 2.9 kB/s with three faders moving.

## Hot standby (a backup PC)

Output, Hot standby... Off by default. The standby is the follower of [Network sync](#network-sync-several-pcs) with one more option, Use hot standby. It follows the show, proves with eleven checks that it could play it, holds its own output black, and can take over when the primary stops. The decision and the alternatives are in [ADR 0042](adr/0042-hot-standby.md).

**What it needs.** Two PCs with NyrexVJ 0.49.0, the same show files in the same folders (a show package, [ADR 0034](adr/0034-show-packages.md), is the way), the output windows already open on the standby (Output page), the same key, and protocol 2 on both sides. Against a v0.48.0 master the standby can still follow, but no fingerprint and no health arrive (Show only warns, Primary cannot see a fault), and a v0.48.0 master does not know the Takeover message and would not step down; that combination was not run. The standby needs its own video path to the audience: one input on a video switcher, a matrix, an NDI router or a capture device per PC.

**Readiness.** The page shows eleven checks, each Ok, Warn or Fail with the number behind it:

| Check | Fails when | Warns when |
| --- | --- | --- |
| Link | no master, or not in sync yet | |
| Version | the control list of the master differs (Clock only) or there is no common protocol | |
| Clock | no usable offset | round trip over 50 ms or jitter over 15 ms |
| State | no state yet, or the newest is older than 1 s | |
| Assets | a clip named in the cells of the master is missing here | another clip in a cell, a deck not in use here |
| Files | a clip file of this PC is missing on its disk | |
| Show | the fingerprint of the deck and clip names differs | the primary does not report one yet |
| Outputs | outputs are wanted and none is open | fewer are open than wanted, or none wanted |
| Decoders | a clip that plays failed to open or to run | |
| Primary | | the primary reports a fault of its own |
| Snapshot | | no session snapshot yet, or older than 150 s |

A single Fail makes the standby Not ready and blocks the automatic takeover; the primary sees the readiness of the standby in its own window. The fingerprint compares **names**: two files with the same name and different content pass. The output check is conservative: a PC that wants its output and has no window open is not a standby.

**Takeover.** Two ways, one gate.

- *By hand:* Take over... asks again; if the primary still answers, the dialog says that two PCs would play and the confirm button is disabled for 0.6 s.
- *Automatic:* only if Take over automatically is ticked, and only when the standby was Ready for at least 10 s before the loss, no clean goodbye arrived (a primary that is closed on purpose is **not** replaced), this PC has no decoder fault and has its output open, and no automatic takeover was used since the standby was armed (one shot; Arm again resets it). The time without packets that counts as lost is a setting, 3 to 60 s, default 5.

**What happens.** The standby becomes the leader with the epoch of the lost master plus 1, opens the leader port (the setting Port as leader, 0 = the port of the old master; use another one when both PCs share one address), releases its output and announces itself with a signed Takeover message to the old master's address until it joins. A master that gets a Takeover with a higher epoch steps down: it sends Bye with the reason Superseded to its own followers and follows the new leader. A Takeover whose epoch is not higher than the master's own, or than one already seen, is ignored. The old master therefore comes back as a follower and the former standby leads; the page and the report show the epoch.

**What it does not do.** It never opens windows, changes monitors or touches a switcher. It does not move OSC, MIDI or the web interface: controllers must be pointed at the new leader (the page shows its address). It does not copy media, the timeline position, presets or settings, and it has no third voter: during a real network split both PCs may think they lead.

**RTO and RPO as criteria.** These are the definitions used by the tests; they are measured and not promised.

- *RTO (the time until the leader serves).* From the last packet of the old master to the moment the leader answers a follower. It has three parts: detection (the lost-after time plus at most one frame), decision (zero for automatic, the time of the person for manual, at least 0.6 s because of the confirm delay) and serving (from the decision to the leader socket answering).
- *RPO (the data the new leader may be behind).* The age of the newest state the standby had. It is bounded by the delta interval (1/30 s), the network time, a frame and lost packets (a missing last packet is noticed after 0.4 s and resynced). Media, timeline position, presets, settings and the cue list state are not replicated at all.

| Criterion | Measured here | Conditions |
| --- | --- | --- |
| Detection | 3.02 s | lost-after 3 s (the minimum), simulated primary, real engine |
| Serving after the decision | 0.035 s (self test), 0.08 to 0.13 s (real programs) | loopback network |
| Frozen primary (SIGSTOP) to a serving leader | 3.2 to 3.6 s | two real programs, polled every 0.5 s, so an upper bound |
| Killed primary (SIGKILL) to a serving leader | 3.3 to 3.5 s | same |
| Standby ready before the loss | 10.5 s | rule: at least 10 s |
| Last control change before the kill | a change 0.15 s before the kill was on the leader | one value, not a statistic |
| Held output of the standby | brightness 0.0, against 169.6 on the primary before and 167.8 as the leader | software rendering |

Not measured: the default lost-after time of 5 s (it follows the same rule, 5 s plus a frame), a pulled cable, a switch that dies, two real PCs, real projectors, 60 frames a second.

**Simulated faults.** `--failover-selftest` and `scripts/test_failover_two_instances.py` run: an app crash (SIGKILL of the primary), a frozen primary (SIGSTOP, then SIGCONT to see the old master step down), a clean close (no automatic takeover, manual takeover works), a decoder failure on the standby (an unreadable image file in a playing cell: no automatic takeover, manual works), a show that differs (the check fails and recovers), and a network cut (the primary is silent while the process lives). A network cut by pulling a real cable was **not** run; a frozen process is the closest thing here and is not the same.

**What you need outside NyrexVJ for a seamless switch.** NyrexVJ switches no video. What the audience sees changes only if something outside selects the other PC:

- a video switcher, matrix or NDI router with an input for each PC (for example a Blackmagic ATEM, an HDMI matrix, an NDI router or a capture based switcher), set up by a person with a rule for who selects what;
- both PCs on the same resolution, refresh rate and output mapping, or the picture jumps at the switch;
- if the switch must not tear, a frame synchronised path (genlock on the switcher and on the cards, see [Output frame lock](#output-frame-lock)); NyrexVJ does not build it;
- a decision about tally, GPIO or a control command from NyrexVJ to the switcher: **not built**. The person or an operator presses the button on the switcher, after the page says that the standby is the leader.

## Files

- `src/core/beat_clock.*` the clock, `timecode.*` frame maths, MTC decoder and tracker, `ltc.*` the LTC decoder, `midi_clock.*`, `djlink.*`, `cue_list.*`, `frame_lock.*` (delay ring logic, swap skew, sync bar numbers).
- `src/control/sync_system.*` ties the sources to the clock, `djlink_input.*` is the UDP listener, `udp_socket*` has a shared open mode.
- `src/render/output_manager.*`, `output_window.*` (draw and swap are separate steps), `texture_history.*` (the delay ring).
- `src/app/ui_sync.cpp` the Sync page.
- Network sync: `src/core/netsync.*` (protocol, state machines, clock estimator), `netsync_link.hpp` (simulated network), `src/control/netsync_key.*`, `netsync_show.*`, `netsync_transport.*`, `src/app/netsync_system.*`, `netsync_selftest.*`, `scripts/test_netsync_two_instances.py`.
- Hot standby: `src/core/failover.*` (readiness checks, state machine, measures), `src/app/failover_system.*`, `ui_failover.cpp`, `failover_selftest.*`, `scripts/test_failover_two_instances.py`.
- Tests: `test_beat_clock`, `test_timecode` (LTC, MTC, cues), `test_djlink` with a packet simulator, `test_midi_clock`, `test_sync_system`, `test_frame_lock`, `test_netsync`, `test_netsync_show`, `test_netsync_transport`, `test_failover`, and the cases of `--output-selftest`, `--netsync-selftest` and `--failover-selftest`.

## What was not tested

- Network sync on a real network: switches, Wi-Fi, a firewall, loss and delay of a real link, a second network segment, three or more real PCs, two different program versions on real PCs, a master that runs for hours, and Windows at run time (the socket code is built in CI; the two program test was run on Linux only). The simulated and the local tests share one author, so a shared misunderstanding of the protocol would not be found. No encryption, no discovery, no IPv6.
- Hot standby on two real PCs: a pulled network cable, a switch that dies, a power loss, a real projector or capture card on the standby, a real video switcher, matrix or NDI router and the person who presses its button, a graphics card, hardware decoders that fail in their own ways, a show of hours, Windows at run time, and a v0.48.0 master against a v0.49.0 follower. A network split in which both PCs lead was reasoned about and not run. The times above are those of one container with software rendering.
- Real Pioneer players or mixers, real Denon gear, a real network with switches, a firewall or a second network segment. The packet layout comes from the public analysis; the simulator was written from the same description, so a shared misunderstanding would not be found.
- The improved beat tracker on real music and real audio devices: real DJ mixes with long tempo changes, live bands, a loopback measurement of the audio latency, the left and right channel of a real device on PulseAudio and WASAPI (the Windows capture was changed and built in CI but not run), other frame rates and other machines for the CPU numbers. The tests use synthetic tracks, see `M-AUD-1`.
- Real LTC or MTC sources: timecode generators, DAWs, hardware that sends timecode, audio interfaces and their line levels, cables, drop frame material at 29.97 and any reverse or varispeed playback.
- Real MIDI clock hardware and USB MIDI jitter, and MIDI clock over network MIDI.
- The hardware frame lock status on any real graphics card (it was only run on a software renderer), and everything about sync boards and swap groups ([GENLOCK.md](GENLOCK.md)). Frame lock and delay on real graphics cards, several cards, real projectors, real displays with different refresh rates, tear free output, and the photographed sync bars. Everything above was measured with software OpenGL (llvmpipe) under a virtual display.
- Windows at run time: the code is built for Windows in CI, but the shared UDP port, the audio input for LTC and the output windows were not run there.
- Next to VRChat: the cost of the sync system (a few small updates a frame, the timecode decoding on the render thread, and a capture thread of the audio input only while a timecode device is chosen) and of the output changes was not measured while VRChat runs.
