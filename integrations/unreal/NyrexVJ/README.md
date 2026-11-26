# NyrexVJ plugin for Unreal Engine (experimental)

A small Unreal Engine plugin that gives Blueprints and C++ the music data of a running NyrexVJ and lets them send controls back. It talks OSC over UDP with the documented protocol, schema 1 ([ADR 0008](../../../docs/adr/0008-osc-output-protocol.md), [UNREAL.md](../../../docs/UNREAL.md)). It does not carry pictures, for pictures use Spout.

## Status: read this first
- **This plugin has never been compiled or run with Unreal Engine.** The author had no Unreal Engine. What exists is checked in a different way: the code under `Source/NyrexVJ/Private/Core/` (OSC decoding and encoding, the state, the link monitor and the round trip statistics) is plain C++17 without Unreal types. It is covered by the NyrexVJ unit tests (`nvj_unreal_core_tests`, `nvj_unreal_link_tests`) against the real NyrexVJ sender and parser, including mutated and truncated packets, and `scripts/test_unreal_link.py` runs the real NyrexVJ against the Python example client.
- The Unreal-facing files (`NyrexVJSubsystem`, `NyrexVJRuntime`, settings, Blueprint library, module and Build.cs) were written against the public Unreal Engine 5 API from memory of the documentation. Expect to fix compile errors and small API differences. **No engine version is stated as supported**, see [UNREAL.md](../../../docs/UNREAL.md) for the proposal.
- The plugin is marked beta and experimental in `NyrexVJ.uplugin`. Version 0.2.0.
- Nothing is promised about latency or frame pacing inside Unreal. The round trip described below was only measured with a Python client on one Linux computer.

## Install
1. Copy the folder `integrations/unreal/NyrexVJ` of this repository into `<YourProject>/Plugins/NyrexVJ` (so that `NyrexVJ.uplugin` sits directly in that folder). Your project needs to be a C++ project, or you need to build the plugin once with the engine tools, because the plugin has source code and no prebuilt binaries.
2. Open the project, accept the rebuild question, then check Edit, Plugins, Audio, NyrexVJ.
3. In NyrexVJ open Settings, Control, OSC output and tick **Send OSC data** (default `127.0.0.1`, port `9011`). To see the round trip, also tick **Receive OSC messages** (port `9010`).
4. Run the game or Play In Editor. The plugin starts by itself.

Do not enable Unreal's own OSC plugin on the same port. Only one program can listen on a UDP port, the second one fails and the log says so.

## Project settings
Edit, Project Settings, Plugins, NyrexVJ:
- **Auto Start**: start listening when a game instance starts (default on).
- **Listen Port**: UDP port to listen on, must match NyrexVJ's OSC output (default 9011). The plugin listens on all network adapters so another computer can send to it. Allow the port in the firewall for that case.
- **Control Host** and **Control Port**: where controls and pings are sent (default `127.0.0.1` and `9010`, NyrexVJ's OSC input, switched on in Settings, Control). Only an IPv4 address or `localhost` is accepted.
- **Measure Latency** (default on) and **Ping Interval Seconds** (default 1, 0.2 to 10): while connected the plugin sends `/nyrex/ping` to the control target and NyrexVJ answers with `/nyrex/out/pong`. Switch it off if you do not want any traffic besides your own controls.
- **Listen Retry Seconds** (default 3): when the listen port cannot be opened because another program holds it, the plugin tries again after this time and writes one log line.

## Blueprint
Use **Get NyrexVJ** (needs a world context object, any actor or widget works) to get the subsystem, or call the functions on a `NyrexVJ Subsystem` reference. The subsystem lives as long as the game instance.

| Group | Nodes |
| --- | --- |
| Connection | `Start Listening`, `Stop Listening`, `Is Listening`, `Is Connected`, `Get Link State`, `Get Link Status`, `Reset Latency Statistics`, `Get NyrexVJ Version`, `Get Schema`, `Get NyrexVJ Input Port` |
| Audio | `Get Bass`, `Get Mid`, `Get High`, `Get Level`, `Get Kick` (0 to 1), `Get Spectrum` (32 values), `Get Spectrum Band`, `Get Onset Count` |
| Tempo | `Get Bpm`, `Get Beat Number`, `Get Beat Phase` (0 to 1), `Get Beat In Bar` (0 to 3) |
| Parameters | `Get Parameter` (by name, the names set in NyrexVJ under Parameters to send), `Get Parameter Names` |
| Control | `Set Control Target`, `Send Control Value`, `Send Control Trigger`, `Set Master Level`, `Set Layer Opacity` (layer 1 to 4), `Trigger Column` (1 to 8), `Switch Deck` (1 to 16), `Tap` |
| Events | `On Beat` (beat number, beat in bar), `On Onset` (count), `On Connected`, `On Disconnected` |

`Get Link Status` returns one struct: the state (Stopped, Waiting, Connected, Lost, Ended, Incompatible), the number of connections and losses, the seconds since the last packet (-1 before the first), the packets per second, the longest gap, whether a round trip is known (`Has Latency`), the last, average, minimum, maximum and 95th percentile of the last 64 round trips in milliseconds, and the pings sent and lost. Lost means silence for five seconds without a `bye`, Ended means NyrexVJ said `bye`.

The round trip is the time from sending a ping to receiving its pong. It contains the network and up to one NyrexVJ frame, it is not the delay from the music to the picture, and it is only meaningful when the Control Host and Control Port are the NyrexVJ whose output you receive. NyrexVJ answers only when both Receive OSC messages and Send OSC data are on.

Rules:
- All getters return 0 (or false, or an empty text) while NyrexVJ is not connected. Connected means a valid `hello` with schema 1 arrived during the last five seconds, and no `bye` after it.
- If NyrexVJ announces another schema number, the plugin logs a warning once, stays disconnected and ignores the data instead of guessing.
- Events are delivered on the game thread, once per tick, in the order Connected, Beat, Onset, Disconnected. At most 512 events are kept between two ticks.
- Control functions return true when the message was handed to the network. UDP gives no answer, so true does not mean that NyrexVJ received it.
- `Send Control Value` and `Send Control Trigger` accept any address of NyrexVJ's OSC input that starts with `/nyrex/` and has only letters, digits, `_`, `-` and `/`. Addresses under `/nyrex/out/` are refused. Send controls from the game thread (Blueprints do).
- The plugin works where a game instance exists: Play In Editor, standalone, packaged. It does not run in editor-only tools outside of a game instance.

## C++
```cpp
UNyrexVJSubsystem* NyrexVJ = GetGameInstance()->GetSubsystem<UNyrexVJSubsystem>();
if (NyrexVJ && NyrexVJ->IsConnected())
{
	const float Bass = NyrexVJ->GetBass();
}
NyrexVJ->OnBeat.AddDynamic(this, &AMyActor::HandleBeat);
```
Add `"NyrexVJ"` to the module dependencies of your own module.

## What is in the folder
- `NyrexVJ.uplugin`, `Source/NyrexVJ/NyrexVJ.Build.cs`: plugin and module description.
- `Source/NyrexVJ/Public`: the subsystem, the settings and the Blueprint library.
- `Source/NyrexVJ/Private/Core`: OSC decoder and encoder, the state machine, the link monitor and the round trip statistics, plain C++17, the part that has tests.
- `Source/NyrexVJ/Private`: the Unreal glue (sockets, ticker, thread hand-over).

## Limits
- IPv4 only, no name lookup, no discovery, no authentication. Use it on a network you trust.
- At most 64 parameters are kept, names up to 32 characters.
- One NyrexVJ at a time. A second sender on the same port would mix its data into the same state.
- Receiving is not tied to a frame, the values are the newest packet. No smoothing is applied.

## Without Unreal
`integrations/unreal/examples/nyrex_monitor.py` in this repository does the same job as the plugin core in Python (no packages needed): it listens, tracks the link state, sends pings and prints the round trip. Use it to check NyrexVJ's output and the network before debugging Unreal: `python3 integrations/unreal/examples/nyrex_monitor.py`. The options are listed in [UNREAL.md](../../../docs/UNREAL.md).

## Reporting problems
Open an issue on GitHub and say the Unreal Engine version, the compiler output or the log lines starting with `LogNyrexVJ`. Compile fixes from people with Unreal installed are especially welcome.
