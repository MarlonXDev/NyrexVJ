# StageLinq (Denon DJ): risk and protocol analysis

Version 0.56.0, written 2026-11-26. [ADR 0050](adr/0050-stagelinq-analysis-first.md) has the decision that is waiting for the maintainer, [SYNC.md](SYNC.md) the sync sources that exist, [ADR 0016](adr/0016-show-sync.md) the first evaluation, [TESTING.md](TESTING.md) the test matrix. Issue [#128](https://github.com/MarlonXDev/NyrexVJ/issues/128).

**Nothing in this document is built.** NyrexVJ does not contain StageLinq code and this change adds none. The prompt that asked for this analysis says to implement StageLinq only after the maintainer approves it explicitly, to start read only and opt in, and never to send unchecked network packets to DJ equipment. No Denon device was available, so no packet that a real device would see can be checked. This page therefore gives the analysis, a proposal in steps, and the tests and hardware checks each step would need.

## What was decided here

| Question | Answer in this version |
| --- | --- |
| Is StageLinq built? | No. Nothing was added to the program. |
| Is a safe, passive subset possible? | Only a small one, and only on one reading of one source (see "What a passive listener could and could not do"). It gives the names of devices, not beats. |
| Can beats be read without sending anything to the DJ equipment? | Not according to any description found. A client has to announce itself and open connections to the device. |
| What would the next step be? | A passive listener for the discovery announcements (step A in the proposal). It still needs the maintainer's approval, and its first real check needs a Denon device. |
| What does the maintainer have to decide? | See "Approval gate". |

## Inventory: what exists today

Status words: **Existing**, **Partial**, **Not found**, **Not verified**. "Code" is the code of 0.56.0 (the commit before this change is `36d9c5f`).

| Item | Status | Where | Note |
| --- | --- | --- | --- |
| A written evaluation of StageLinq | Existing | [SYNC.md](SYNC.md) section "StageLinq (evaluation)", [ADR 0016](adr/0016-show-sync.md) item 8 | Says: not built, reverse engineered, a client must announce itself on UDP 51337, cannot be tested without a Denon player and mixer. This document checks those statements again and adds sources. |
| StageLinq code (parser, socket, source) | Not found | a search for the name in `src`, `tests` and `CMakeLists.txt` finds nothing | Only documents and changelog lines mention it. |
| A place where a new beat source plugs in | Existing | `SyncSource` in `src/core/beat_clock.hpp:8`, `kAutoOrder` in `src/core/beat_clock.cpp:36` (Network, Pro DJ Link, timecode, MIDI clock, audio), `SyncObservation` | A source only has to report `SyncObservation` values. Adding a source changes `kSyncSourceCount` and the Automatic order, which has to be handled in settings and presets. |
| Pro DJ Link as a passive beat listener | Existing | `src/core/djlink.*` (packet parser and `DjLinkTracker`), `src/control/djlink_input.*` (socket), `src/control/sync_system.cpp:57` (opened only when enabled), settings `djLinkEnabled`, `djLinkPlayer` | Reviewed below. |
| Tests for Pro DJ Link | Existing | `tests/test_djlink.cpp` (`nvj_djlink_tests`, 135 `CHECK` lines in seven tests: parse, fuzz, following, selection, end to end, beat only, a real socket on loopback), `tests/djlink_sim.hpp` (the packet builder) | Computer made packets only. |
| A UDP socket that opens a port shared | Existing | `UdpSocket::openShared` in `src/control/udp_socket_posix.cpp:51` and `src/control/udp_socket_win.cpp:75` | Sets `SO_REUSEADDR` and `SO_BROADCAST`, binds `INADDR_ANY`. |
| A TCP client with framing and timeouts | Partial | The network sync and the stream input have their own code; there is no shared small TCP client for a protocol like this | A StageLinq step would need one. |
| A Denon device, a capture of its traffic, a fixture | Not found | none in the repository | |
| Any claim that StageLinq works | Not found | The README and manual say it does not exist. | Keep it that way until a device was used. |
| Open issues or pull requests about StageLinq | Not found | The open items (25 to 28 and 54) are about the roadmap and ADR 0003; this analysis is issue 128. | |

## Review of the Pro DJ Link integration

The prompt asked for the existing integration to be looked at again. This compares the code with the public analysis of the protocol by Deep Symmetry (page on beat packets, see Sources). It is a reading; no real player was available.

| Point | Code | Public analysis | Result |
| --- | --- | --- | --- |
| Magic bytes and type | `kMagic`, `kBeatType` 0x28 (`src/core/djlink.cpp:10`) | Packets are told apart by the type byte together with the port they arrive on | consistent as far as read |
| Pitch | bytes 84 to 87 (0x54 to 0x57), `kPitchUnit` 1048576, accepted 1 to 0x400000 (`djlink.cpp:13`, `:43`) | The analysis puts the pitch at bytes 0x54 to 0x57 | consistent |
| Track BPM | bytes 90 and 91 (0x5a, 0x5b), value divided by 100, 0xffff refused | The analysis puts the BPM at bytes 0x5a and 0x5b | consistent |
| Timing of the next beat and bar | read from bytes 36 and 44 and divided by the pitch (`djlink.cpp:58`) | The analysis says these times are always given as if the track played at normal speed (pitch 0 percent), so a real time needs the pitch | consistent. The clock does not use these two values yet (`nextBeatMs`, `nextBarMs` are parsed and tested but only the beat in the bar, the tempo and the arrival time reach `SyncObservation`). |
| Who sends beat packets | players 1 to 6 are followed, the mixer's packets are ignored (`onBeat`, `djlink.cpp:108`) | The analysis says a CDJ sends them only while it plays and only for tracks analysed by rekordbox, and the mixer sends them all the time as a backup metronome | consistent. Practical effect: a player that plays a track without a rekordbox analysis gives no beats, and the page shows no beat. |
| Sending | `DjLinkInput` has no send call; it only receives | The protocol allows a program to pose as a player and receive more | **holds**: the integration sends nothing. The price is documented: no play state, no master flag. |
| Opening the port | only while `djLinkEnabled` is on (`sync_system.cpp:57`), shared (`openShared`) | | holds. `SO_REUSEADDR` lets two programs share the port on Windows. On Linux both programs have to set it; a program that does not makes the open fail with the message "Could not open the network port, it may be in use". **Not verified** with a second program. |
| Robustness | 96 bytes minimum, magic, type, constant bytes, the two copies of the player number, ranges, a fuzz test | | tests pass (`nvj_djlink_tests`); no real traffic |
| Newer players and mixers | | The analysis covers the devices of its time | **Not verified**: whether current players (for example CDJ-3000, or newer mixers) send the same beat packet. Nothing in the code or the tests can show it. |

Result: no defect was found in the points that could be checked against the public description. Nothing in the Pro DJ Link code was changed. What remains unknown is real traffic from current hardware (the manual test M-SYNC-1 in [TESTING.md](TESTING.md) is the one that needs a real player and is still open).

## What is known about StageLinq

All of this comes from public pages that were read on 2026-11-26. The manufacturer has published no specification that I could find; the Engine DJ community thread "Stagelinq protocol API availability?" has run over 500 replies in part 1 and continues in part 2, and what I read there was users asking and reverse engineering, not a manufacturer document. This is not an exhaustive search.

- **Protocol description from one device.** The file `StageLinQ_protocol.md` of the PyStageLinQ project says of itself that it is partial and reverse engineered from Wireshark captures of a single Denon DJ Prime Go. It describes the discovery frame and the first steps of a connection and states that BeatInfo is not documented there (no start procedure, no message layout, no units).
- **Libraries.** `chrisle/StageLinq` (TypeScript, licence MIT as stated on its page) lists the services StateMap, BeatInfo, FileTransfer, TimeSync and Broadcast, a Wireshark dissector, and credits another person for BeatInfo, TimeSync and Broadcast. `MarByteBeep/StageLinq` (NodeJS) says it was tested with two SC6000, an X1850, Prime 4, Prime 2 and Prime Go. `icedream/go-stagelinq` calls itself an experimental reverse engineering effort that can behave erratically in untested cases and says it was practically tested only with the Prime 4; a fork of it states the MIT licence. PyStageLinQ says it was tested with a Prime Go.
- **Official integrations exist.** In August 2018 Resolume wrote that Arena 6.1 would include a StageLinQ integration, shown together with Denon DJ. That tells us the manufacturer has worked with partners; it does not tell us what a third party may do, and I found no licence or developer programme text.
- **The protocol moves with the firmware.** Engine DJ 5.0 was released on 2026-05-13 (Engine OS 5.0.1 was the latest hardware release on the download page when it was read). A community post in the thread above says that a firmware update (2.0 at that time) may have broken existing implementations. Whether 4.x or 5.x changed the protocol is **not verified**.
- **Port conflicts are real.** Several posts in that thread show the library failing with "address already in use" on UDP 51337 when another program held the port. A listener on 51337 would have to open it shared, as the Pro DJ Link listener does for 50001.

## Protocol analysis

Everything in this table is what the PyStageLinQ description says for a Prime Go. It is not verified for other devices or firmware.

| Topic | What the description says | Consequence for NyrexVJ |
| --- | --- | --- |
| Discovery frame | UDP broadcast to 255.255.255.255 port 51337. Fields: the characters `airD` (4 bytes), a token (16 bytes), then four strings each with a 4 byte length in front and UTF-16 text (device name, connection type, software name, software version), then a 2 byte port for service requests. Connection type is `DISCOVERER_HOWDY_` (ready) or `DISCOVERER_EXIT_` (going offline). | A passive parser is small: about 40 bytes plus four strings. Every length field comes from the network and needs bounds checks. |
| Who has to announce | "Both sources and sinks must send it, even if they offer no services." | **A client that wants data has to send broadcasts.** That is the opposite of the passive Pro DJ Link listener. |
| Timing | The Prime Go sends two kinds of frame ("Offline Analyzer" and "JP11"), each repeated every 1000 ms, about 500 ms apart. Tested on that device only. | A passive listener would see a frame about once a second per device. |
| Network | The device picked a random address in 169.254.0.0/16 (link local); the author put the PC in the same range. DHCP was not tested. | A PC on an ordinary router network may not reach the device at all, or would need a second address. Changing a network setting is not something the program should do. |
| Token | 16 bytes, randomly generated by the author; if the top bit is 1 the Prime Go ignores service requests. Other rules may exist. | Generated tokens would have to avoid that. Unknown rules are a risk. |
| Service request | The client opens TCP to the service request port from the frame, sends a 4 byte type (`00 00 00 02`) and its token; the device answers with a list of services (name and port each) | Needs a TCP client with timeouts and bounds on every length. |
| Services seen on a Prime Go | StateMap, Broadcast, TimeSynchronization, BeatInfo, FileTransfer | Only BeatInfo would be of use for the beat clock. |
| BeatInfo | Listed, but the description says nothing about how to start it or what its messages look like. Other libraries implement it (credit in the TypeScript library), but I did not read their code and did not find a layout document. | **The message layout is the one thing a beat source needs and it is not available here.** Building it would mean reading a library or capturing real traffic. |
| StateMap | A subscription to named values (deck, mixer). The description gives the frame for one request and says it is not known whether several requests share a header. | Not needed for beats; it is also the largest part of the traffic. |
| Who connects to whom | The PyStageLinQ description has the client connect to the device. SYNC.md (written from other sources at 0.24.0) says a client also offers services that the device connects to. The libraries list the services a client offers (StateMap, BeatInfo, FileTransfer). | **The sources differ.** Which connections a minimal client must accept is **not verified**. |
| Text | UTF-16, big endian works in the Python library | No problem, but every string length must be even and bounded. |

### What a passive listener could and could not do

A listener that only receives the broadcast frames on UDP 51337 (opened shared, sending nothing, like Pro DJ Link on 50001) could show which StageLinq devices are on the network: name, software name and version, and the announced port. It would not receive beats, because beats come through a TCP service that has to be requested after the client announced itself. So it is useful for two things only: to see that a device is present, and to record real frames for fixtures. Whether current Denon firmware announces itself without having heard a client first is described for the Prime Go only.

## Risks

| Risk | Why | What it could do to a show | Mitigation in the proposal |
| --- | --- | --- | --- |
| The protocol is reverse engineered and partly unknown | No manufacturer document; one description from one device; BeatInfo layout not available here | A parser that is wrong on current firmware: no beats, wrong beats, or crashes in our own parser on unexpected data | Parser with bounds checks and a fuzz test before any socket exists; refuse unknown messages; the source reports "no beat" instead of guessing |
| Our packets reach DJ equipment | A client has to broadcast an announcement and open TCP connections to the players | A player or mixer that behaves differently when a new client appears (an unknown peer in a list, a connection that is not closed cleanly, a crash reported for one library on standalone players, cause not verified here). The DJ's set is the one thing that must not be disturbed. | Nothing is sent without the maintainer's approval and a device to test with; a list of the only messages the client may send, checked by a test that records every outgoing byte; read only; no FileTransfer, no StateMap changes; a visible warning when it is switched on |
| Firmware changes | The protocol moves with Engine OS (see above) | A source that works at one version and fails silently at the next | Show device name and software version on the page; the source reports its state; the manual says which firmware was tried |
| Legal and licence | The protocol is not published by the manufacturer; the integrations that exist were made with the manufacturer | Not known. This document is not legal advice. | The maintainer decides; NyrexVJ would contain no code from other projects (as for Pro DJ Link) and no captured traffic of other people |
| Network setup | Link local addressing and broadcast; DHCP not tested | The device is not reachable from a PC on another subnet or behind a router; a firewall blocks the port | No automatic network changes; the page explains what is needed |
| Port conflict | Other programs (lighting software, other VJ software) may hold UDP 51337 | The listener or the client cannot open its port, or another program stops working | Open shared; show the error text; never close or take over another program's port |
| Frame time and threads | A TCP client with timeouts could block | A late frame or a hang at exit | Own thread, non blocking reads, bounded buffers, the stop rule of `StoppableThread` (3 seconds), nothing on the render thread except taking the newest value |
| Security | A device on the network sends data that the program parses | Over long or malformed messages; a hostile device on a shared network | Maximum message size, maximum string length, a limit on the number of devices and messages per second, fuzz tests |
| Hardware not available | No Denon device here | Everything simulated, nothing proven | State it everywhere; do not call anything done before a real device was used |

## Proposal in steps

None of these steps is built or approved.

| Step | What it is | Sends something? | Needs | Gives |
| --- | --- | --- | --- | --- |
| 0 | This analysis and the decision (this change) | no | nothing | the decision and the tests |
| A | A passive listener for the discovery frames on UDP 51337: parser, fuzz tests, a page that lists the devices that announce themselves. Off by default, port opened only while on, opened shared. | **no** | approval; a Denon device for the first real check | the names, software versions and ports of devices, and real frames to build fixtures from |
| B | A minimal read only client for BeatInfo: announce, service request, BeatInfo only. No FileTransfer, no StateMap writes. Off by default, a warning when switched on, an allow list of outgoing messages. | **yes** | approval, a Denon player (better a player and a mixer), real captures of the BeatInfo messages, the licence question answered | beats from a Denon player |
| C | A new beat source `StageLinq` behind the beat clock: `SyncObservation` for phase and tempo, an entry in the Automatic order after Pro DJ Link, a Sync page section, settings keys, translations, manual chapter | no further | step B working on a real device | the source for a show |

Step A could be done without hardware as far as the code and the simulated tests go, but its value is small, and it only means something once a real device has been used to check the parser. It is not started because the prompt requires the maintainer's approval first.

## Approval gate

Before step A or B starts, the maintainer decides (in issue [#128](https://github.com/MarlonXDev/NyrexVJ/issues/128) or the pull request):

1. Is StageLinq wanted at all, and is step A alone enough for now?
2. Is the licence and legal question for a reverse engineered protocol accepted for this project? (This document does not answer it.)
3. Is a Denon player and mixer available for the manual tests, and who runs them?
4. May the program ever send packets to DJ equipment (step B)? If yes, under which conditions: switched on per session, a warning, no FileTransfer?
5. May a fixture come from a capture of the maintainer's own equipment?

## Tests

### What simulated tests can prove (and what they cannot)

If a step is approved, these tests can be written and run without hardware. Their limit is the same for all of them: the simulated device is built from the same reading of the same documents as the code, so a pass shows that the code matches that reading, not that it matches a real device.

| Test | What it checks |
| --- | --- |
| Frame codec | A builder in `tests/` makes discovery frames; the parser round trips them; lengths larger than the datagram, odd or zero string lengths, a frame of exactly the maximum size, text that is not UTF-16, truncated and random bytes (a fuzz test like `testFuzz` in `test_djlink.cpp`) produce "malformed" and never a crash or a large allocation |
| Token rule | Generated tokens never have the top bit set |
| TCP framing | A message parser fed one byte at a time and in random splits gives the same result as one piece; a length above the limit closes the connection; a truncated stream ends cleanly |
| Allow list of outgoing messages (step B) | A recorder behind the socket stores every byte the client sends; the test fails if any message is not on a short list of fixed builders |
| Fake device on loopback (step B) | A test double plays announce, service list and BeatInfo; the client follows it, loses it (silence), reconnects, and survives garbage, a huge length and a slow sender, and shuts down within the stop limit |
| Clock | `SyncObservation` values from a simulated stream move `BeatClock` the way the Pro DJ Link simulator does (phase offset, jumps, loss of source) |
| Off by default | With the setting off, no socket, no thread and no port exist (as for the stream and the person modules) |

### Hardware validation (to be added to the test matrix when a step is approved)

Proposed manual tests; the real limits would be set by the maintainer after the first runs.

| Id (proposed) | Needs | What to do | What to record |
| --- | --- | --- | --- |
| M-STAGELINQ-1 | A Denon player or mixer, a switch, a PC with Wireshark | Capture five minutes of the network with the device on and a track playing, with NyrexVJ not running | The raw frames (anonymised) as the fixture; the addressing of the device (link local or DHCP); the frame rate of the announcements |
| M-STAGELINQ-2 | Step A built | Switch the listener on next to a real device; power it off and on | Whether the listed fields match Wireshark; the time until the device appears and until it is gone; the error text when another program holds the port |
| M-STAGELINQ-3 | Step B built, one real player | Run the client for ten minutes while a track plays | Whether the player or mixer shows anything unusual; whether beats arrive; the phase offset against the audio; the CPU and frame time of NyrexVJ |
| M-STAGELINQ-4 | Step B, two players and a mixer, the firmware versions written down | Same, with a second StageLinq program on the network; pull the cable; power cycle a player | Whether the second program and the DJ equipment are affected; the reconnect time; the firmware versions that worked or failed |

## Not tested and not claimed

- No Denon device was used. No packet was sent to any DJ equipment. No traffic from a real StageLinq device was read.
- No library was run. The public pages were read, not their code.
- The Pro DJ Link review compares the code with a public description; no real player was used.
- Whether the announcements, the services or the addressing are the same on current firmware (4.x, 5.x) is not known.

## What to do today for Denon equipment

The sources that exist are MIDI clock from the mixer or a DAW, timecode, the audio analysis and tap tempo ([SYNC.md](SYNC.md)). None of the pages read for this analysis says that a Denon device sends Pro DJ Link beats, so that is not an alternative that was found.

## Sources (read on 2026-11-26)

- PyStageLinQ, `StageLinQ_protocol.md`: <https://github.com/Jaxc/PyStageLinQ>
- chrisle/StageLinq: <https://github.com/chrisle/StageLinq>
- MarByteBeep/StageLinq: <https://github.com/MarByteBeep/StageLinq>
- icedream/go-stagelinq: <https://github.com/icedream/go-stagelinq>
- Engine DJ community, "Stagelinq protocol API availability?": <https://community.enginedj.com/t/stagelinq-protocol-api-availability-part-1/27578>
- Resolume, "Resolume Arena with Denon DJ StageLinQ Integration": <https://resolume.com/blog/17234/resolume-arena-with-denon-dj-stagelinq-integration>
- Engine DJ 5.0: <https://enginedj.com/news/articles/introducing-engine-dj-5-0-on-board-stems-rendering-for-the-rane-system-one>
- Deep Symmetry, DJ Link Ecosystem Analysis, "Tracking BPM, Beats, and Position": <https://djl-analysis.deepsymmetry.org/djl-analysis/beats.html>

Dependencies on other prompts: none. The network sync, the stream inputs and the person module are not touched.
