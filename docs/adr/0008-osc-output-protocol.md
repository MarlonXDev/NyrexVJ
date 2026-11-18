# ADR 0008: OSC output and the versioned protocol (schema 1)

- **Status:** Accepted (2026-11-18, approved by MarlonXDev as part of ADR 0002)
- **Date:** 2026-11-18
- **Owners:** Maintainers (pending assignment)
- **Related:** [ADR 0002](0002-unreal-integration.md), [ADR 0007](0007-spout-receiver.md), issue #21

## Context
ADR 0002 asks for tempo, beats, spectrum bands, levels and named parameters to reach other programs, Unreal Engine first. NyrexVJ already receives OSC (v0.5.0, addresses under `/nyrex/`). Output has to be documented and versioned before anything is called an SDK.

## Decision
- **Transport.** OSC 1.0 over UDP, sent as one bundle per tick. Default target `127.0.0.1` port `9011`. Input stays on port 9010, so a receiver can answer on `/nyrex/...`.
- **Namespace.** Everything sent lives under `/nyrex/out/`, so input and output addresses can never be confused. Only the types `i`, `f` and `s` are used.
- **Schema number.** The current schema is `1`. It is sent in `hello` and `bye`. Within a schema new addresses may be added and arguments may only be appended. Anything else raises the schema number. Receivers should ignore unknown addresses and extra arguments.
- **Messages (schema 1).**

| Address | Types | When | Meaning |
| --- | --- | --- | --- |
| `/nyrex/out/hello` | `s i s i` | three times after start, then every 2 s | app name, schema, app version, OSC input port (0 if input is off) |
| `/nyrex/out/bye` | `i` | once when sending stops | schema |
| `/nyrex/out/audio` | `f f f f f` | every tick | bass, mid, high, level, kick, each 0 to 1 |
| `/nyrex/out/spectrum` | `f` x 32 | every tick, optional | 32 bands, each the mean of four analyser bins, 0 to 1 |
| `/nyrex/out/clock` | `f i f` | every tick | BPM, whole beat number, phase within the beat 0 to 1 |
| `/nyrex/out/beat` | `i i` | once per beat | beat number, beat within the bar 0 to 3 |
| `/nyrex/out/onset` | `i` | once per detected audio beat | running count of detected beats |
| `/nyrex/out/param/<name>` | `f` | every tick | user defined value 0 to 1 |

- **Named parameters.** The user defines up to 16 parameters. Each has a name (letters, digits, `_`, `-`, up to 32 characters) and a source, which is any continuous or toggle control target of the input side, for example `layer/1/opacity` or `master/level`. The value is the same 0 to 1 number that the input side would accept for that target. Triggers cannot be sources.
- **Rate.** Default 60 ticks per second, adjustable from 10 to 120, never more than one tick per rendered frame. Beat and onset events are sent in the tick that notices them, regardless of the rate setting.
- **Discovery.** None in schema 1: no multicast and no mDNS. The user enters host and port. `hello` is the heartbeat; a receiver should treat five seconds without it as a lost connection. Because UDP is fire and forget, a missing receiver costs nothing and shows no error.
- **Fallback.** If the target cannot be resolved or the socket cannot be opened, the Control page shows the reason and sending stays off. Nothing else in the application depends on it.
- **Addresses.** Only IPv4 literals and `localhost` are accepted. There is no name lookup.
- **Security.** The default is loopback. Another computer has to be typed in on purpose. The output carries no secrets and has no authentication; control from the other side uses the existing input settings.

## Alternatives
1. **Reuse `/nyrex/` for output:** shorter, but a program that echoes messages back would create loops.
2. **JSON over WebSocket:** easier for web tools, but Unreal's built-in OSC plugin and most VJ and lighting tools already speak OSC.
3. **Individual messages instead of bundles:** simpler to decode by hand, but more packets and no atomic frame.
4. **Pushing every control target:** 200+ values per tick for little use. The user-defined list keeps the traffic small and the names stable.

## Consequences
- The protocol is small enough to be written down in `docs/UNREAL.md` and tested byte by byte.
- 32 spectrum floats are about 150 bytes per tick; with the other messages one bundle stays far below the UDP limit.
- An Unreal plugin could later wrap the protocol, but this ADR does not promise one.

## Validation
Unit tests encode every message, decode the bundle again and compare it, and send a bundle through a real loopback socket. Latency against Unreal was not measured; no Unreal Engine was available to the author of this change.
