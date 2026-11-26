# ADR 0050: StageLinq (Denon DJ): analysis first, no code until the maintainer approves, and a passive listener as the only proposed first step

- **Status:** Proposed. The analysis part is done in 0.56.0; **the implementation waits for the maintainer's decision.** Requested by MarlonXDev as prompt 11 (optional). Issue [#128](https://github.com/MarlonXDev/NyrexVJ/issues/128).
- **Date:** 2026-11-26
- **Owners:** Maintainers (pending assignment)
- **Related:** [STAGELINQ.md](../STAGELINQ.md), [SYNC.md](../SYNC.md), [ADR 0016](0016-show-sync.md), [ADR 0041](0041-multi-pc-sync.md), [TESTING.md](../TESTING.md)

## Context
The prompt asked to check the earlier StageLinq evaluation and the Pro DJ Link integration, to write a risk and protocol analysis first, to implement StageLinq only after the maintainer approves it explicitly, to start read only and opt in, and never to send unchecked network packets to DJ equipment. [ADR 0016](0016-show-sync.md) had evaluated StageLinq at 0.24.0 and not built it.

## Findings (what exists and what was read)
- **Existing:** the beat clock with a place for new sources (`SyncSource`, `SyncObservation`), a passive Pro DJ Link listener that sends nothing, a shared UDP socket, a test simulator for Pro DJ Link packets.
- **Not found:** any StageLinq code, a Denon device, a capture of its traffic, a manufacturer document.
- **Read (public pages, 2026-11-26):** one protocol description made from a single Denon Prime Go, the pages of three reverse engineered libraries, community threads and the manufacturer's firmware news. They agree that a client has to announce itself by UDP broadcast and then open TCP connections; the one description that was read does not say how the beat service works. The sources differ on which side opens which TCP connection.
- **Pro DJ Link review:** the offsets, the pitch unit and the handling of beat timing match the public analysis in every point that could be checked; no defect was found and nothing was changed.
- Details, sources and the risk table are in [STAGELINQ.md](../STAGELINQ.md).

## Decision
1. **No StageLinq code in this version.** Without a Denon device nothing that a real device would see can be checked, and the prompt forbids unchecked packets and requires explicit approval.
2. **Proposal in steps, none approved:** A, a passive listener for the discovery frames on UDP 51337 that sends nothing (device names, software versions and ports; real frames for fixtures); B, a minimal read only client for the beat service, only after approval and a real device; C, a `StageLinq` beat source behind the beat clock.
3. **Gate:** the maintainer's written approval in the issue, a Denon player (and mixer) for the manual tests, an accepted answer on the licence and legal question, and a decision whether the program may ever send packets to DJ equipment.
4. **If it is built:** off by default and invisible when off (no socket, thread or port), opened shared, nothing on the render thread, bounds on every length from the network, a fuzz test before any socket, a recorder test that proves the client only sends messages from a short list, no FileTransfer, no StateMap changes, and no code taken from other projects.
5. **Do not claim support** in the README, the manual or the changelog until a real device was used.

## Alternatives considered
- **Build the whole client now from the libraries.** Rejected: it would be written from a reading of other people's partial work, tested only against a simulation made from the same reading, and it would send packets to equipment that nobody here could test against.
- **Build only the passive listener now.** Not rejected, but not started: it gives device names only, no beats, and the prompt asks for approval before implementing. It is step A of the proposal and can be done quickly once approved.
- **Delete the topic.** Rejected: Denon gear is common in DJ booths and the beat source would be useful; the analysis keeps the facts for a later decision.
- **Use a library (a StageLinq library or a bridge program).** Not studied further: a dependency needs a licence check and a build on two platforms, and the libraries state they were tested on a few devices.
- **Another route to the same tempo:** MIDI clock from a mixer or DAW, the audio analysis and tap tempo exist today.

## Consequences
- No change to the program, the settings or the files. A show behaves as before.
- Denon users get the existing sources only; the README keeps saying that StageLinq is not supported.
- The decision list for the maintainer is in [STAGELINQ.md](../STAGELINQ.md), "Approval gate".

## Validation / open questions
- Does current Denon firmware (4.x, 5.x) announce itself in the form that was described for the Prime Go, and without hearing a client first?
- Which side opens which TCP connection, and what does the beat service send? Needs a capture from a real device or a document.
- Is the legal and licence question for a reverse engineered protocol acceptable for this project?
- Do current players send Pro DJ Link beat packets as the public analysis describes? (M-SYNC-1, still open)
