# ADR 0052: Unreal link: a tested connection state and a ping and pong round trip, the plugin stays optional, and nothing that needs Unreal Engine is called verified

- **Status:** Accepted for the NyrexVJ side (ping and pong) and for the plain C++ part of the plugin (link state, round trip statistics, example client, check script), requested by MarlonXDev as prompt 13. Issue [#132](https://github.com/MarlonXDev/NyrexVJ/issues/132). **Everything in the plugin that uses Unreal types is written but not compiled, and its adoption needs the maintainer's decisions below.**
- **Date:** 2026-11-26
- **Owners:** Maintainers (pending assignment)
- **Related:** [UNREAL.md](../UNREAL.md), [ADR 0002](0002-unreal-integration.md), [ADR 0007](0007-spout-receiver.md), [ADR 0008](0008-osc-output-protocol.md), [ADR 0009](0009-unreal-plugin.md), [KNOWN_ISSUES.md](../KNOWN_ISSUES.md), [TESTING.md](../TESTING.md)

## Context
The prompt asked to review what exists of the Unreal integration, to add connection status, latency measurement, examples and documentation where it makes sense, to keep the integration optional and to say clearly what could not be tested without Unreal Engine. No Unreal Engine was available, so nothing that needs the engine could be compiled or run.

## Findings (what existed before)
- **Existing:** Spout receiver (four slots), OSC output schema 1, OSC input for controls, the experimental plugin source with a protocol core that is plain C++17 and tested against the real sender and parser (`nvj_unreal_core_tests`).
- **Partial:** the plugin knew only "connected or not", computed inside the Unreal glue from a five second timeout. It could not tell a stopped sender (`bye`) from a lost one, and it did not try again when the listen port was busy at start.
- **Not found:** any latency measurement, any example client that runs without Unreal, a list of supported engine versions, a build command for the plugin.
- **Not verified:** everything that uses Unreal types, a packaged build, Spout from a real Unreal sender.
- The table with locations is in [UNREAL.md](../UNREAL.md).

## Decision
1. **A ping and an answer, inside schema 1.** The OSC input accepts `/nyrex/ping` with exactly one integer from 0 to 16777215 (a float is stored exactly up to 2^24, the numbers stay inside that range). NyrexVJ queues the number and answers `/nyrex/out/pong` with the same integer in the next output update. New addresses are allowed inside a schema ([ADR 0008](0008-osc-output-protocol.md)), so the schema number stays 1 and old receivers ignore the address.
2. **Where the answer goes.** To the configured OSC output address, never to the sender of the ping. A forged ping cannot make NyrexVJ send to a third computer. The answer exists only while OSC input and OSC output are both on.
3. **Bounds.** At most 8 pings are taken per frame, at most 8 answers wait, anything else (wrong type, number out of range, more than one argument) is refused. A ping is 20 bytes, a pong alone is 44 bytes. The pending answers are dropped when the output restarts or stops.
4. **What the round trip is.** The time from sending the ping to receiving the pong. It contains the network and the wait for NyrexVJ's main loop (once per frame). It is not the delay from music to picture and says nothing about Unreal's render time. This is written next to every place where the number is shown.
5. **A link state in plain C++.** `LinkMonitor` in `Private/Core/NyrexLink.*` has six states (stopped, waiting, connected, lost, ended, incompatible) and the five second rule of ADR 0008. `bye` gives "ended", silence gives "lost", a schema other than 1 gives "incompatible". It also counts connections and losses and the longest gap, and gives the packet rate. `LatencyTracker` in the same file keeps the last 64 round trips (minimum, average, maximum, 95th percentile), counts pings sent, lost (no answer within 2 seconds) and ignored (unknown, repeated or too late answers), and wraps the ping number at 16777215. Both take the clock as an argument, so tests use made-up times, including backwards and non-finite ones.
6. **The Unreal glue only feeds them.** `FNyrexVJRuntime` passes packets and the clock to the two classes; `UNyrexVJSubsystem` sends the ping once per `PingIntervalSeconds` while connected (switch `bMeasureLatency`, on by default) and exposes `GetLinkState` and `GetLinkStatus` to Blueprints. The retry of a busy listen port (every `ListenRetrySeconds`, one log line) is in the subsystem. None of this was compiled.
7. **The plugin stays optional and unreleased.** It is not built, packaged or shipped by NyrexVJ. The plugin version is 0.2.0, the `.uplugin` has no `EngineVersion` entry, so nothing is blocked. Versions are listed in [UNREAL.md](../UNREAL.md) only as a proposal.
8. **An example client and a check script.** `integrations/unreal/examples/nyrex_monitor.py` (no packages, same states and statistics) and `scripts/test_unreal_link.py` (starts the real NyrexVJ, runs the example client, two clean restarts and one killed run, then checks the numbers). They stand in for the Unreal run that could not be done and are not a replacement for it.
9. **No Blueprint assets.** They cannot be made without the editor. [UNREAL.md](../UNREAL.md) has five recipes as text, marked as not built.

## Alternatives considered
- **Measure latency with the existing `hello` heartbeat.** The heartbeat goes one way and has no number to match. Rejected.
- **Answer to the sender of the ping.** Simpler for a client behind another port, but it lets anybody on the network make NyrexVJ send packets to an address of their choice (reflection). The configured output address is already the user's decision. Rejected.
- **A new schema (2) with a ping in the output stream.** Not needed, an added address is compatible.
- **Time stamps inside the pong (NyrexVJ's own clock).** Would invite readers to compute one-way delays between clocks that are not in sync. The round trip needs only the sender's own clock. Rejected.
- **Ship a prebuilt plugin, or build one in CI.** Needs an Unreal Engine installation per version, which the project does not have. Not built.
- **Write Blueprint assets by hand-editing `.uasset` files.** Not possible to do safely or to check without the editor.
- **Pings on by default for everybody.** The plugin pings only while connected and only to the control target, one 20 byte packet per second. It is still a new default for people who update the plugin, so the switch exists (see open decisions).

## Consequences
- NyrexVJ users who use another OSC tool see nothing new unless they send `/nyrex/ping`. An unknown address was already refused before, a valid ping now counts as received and is answered.
- The plugin side of the connection logic is now covered by tests that run in every build (`nvj_unreal_link_tests`), so the logic that decides "connected" no longer depends on the part that was never compiled.
- A schema 2 sender still makes the plugin ignore the data and show "incompatible".
- New tests in the continuous build: `nvj_unreal_link_tests`, 3 new test functions in `nvj_control_tests`, and the script `scripts/test_unreal_link.py` as a step of the Linux job.

## Open decisions for the maintainer
1. Which Unreal Engine versions are supported? Proposal: the newest stable release and the one before it, written down per version only after someone built it.
2. Who runs the plugin in a real Unreal Editor, a packaged game and a Windows machine, and what counts as passed? The manual tests `M-UNREAL-1` to `M-UNREAL-5` in [TESTING.md](../TESTING.md) are open.
3. Should the plugin ever be offered as a built package (one per engine version), or stay a folder that people copy?
4. Should `Measure Latency` stay on by default?
5. Should the plugin get a status panel in the editor, or is the Blueprint struct enough?

## Validation and open questions
- Done: `nvj_unreal_link_tests`, `nvj_unreal_core_tests`, `nvj_control_tests` (ping, pong, bounds, a pong in the same update as the hello), `scripts/test_unreal_link.py` against the real program.
- Not done: any compile or run with Unreal Engine, any measurement in a real project, packaged builds, a second computer, a graphics card. The loopback round trip depends on NyrexVJ's frame rate and says nothing about these.
