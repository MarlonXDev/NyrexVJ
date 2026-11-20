# NyrexVJ plugin for Unreal Engine (experimental)

A small Unreal Engine plugin that gives Blueprints and C++ the music data of a running NyrexVJ and lets them send controls back. It talks OSC over UDP with the documented protocol, schema 1 ([ADR 0008](../../../docs/adr/0008-osc-output-protocol.md), [UNREAL.md](../../../docs/UNREAL.md)). It does not carry pictures, for pictures use Spout.

## Status: read this first
- **This plugin has never been compiled or run with Unreal Engine.** The author had no Unreal Engine. What exists is checked in a different way: the protocol code under `Source/NyrexVJ/Private/Core/` is plain C++17 without Unreal types, and it is covered by the NyrexVJ unit tests (`nvj_unreal_core_tests`) against the real NyrexVJ sender and parser, including mutated and truncated packets.
- The Unreal-facing files (`NyrexVJSubsystem`, `NyrexVJRuntime`, settings, Blueprint library, module and Build.cs) were written against the public Unreal Engine 5 API from memory of the documentation. Expect to fix compile errors and small API differences, especially on engine versions before 5.3.
- The plugin is marked beta and experimental in `NyrexVJ.uplugin`. Version 0.1.0.
- Nothing is promised about latency or frame pacing, nothing was measured.

## Install
1. Copy the folder `integrations/unreal/NyrexVJ` of this repository into `<YourProject>/Plugins/NyrexVJ` (so that `NyrexVJ.uplugin` sits directly in that folder). Your project needs to be a C++ project, or you need to build the plugin once with the engine tools, because the plugin has source code and no prebuilt binaries.
2. Open the project, accept the rebuild question, then check Edit, Plugins, Audio, NyrexVJ.
3. In NyrexVJ open Settings, Control, OSC output and tick **Send OSC data** (default `127.0.0.1`, port `9011`).
4. Run the game or Play In Editor. The plugin starts by itself.

Do not enable Unreal's own OSC plugin on the same port. Only one program can listen on a UDP port, the second one fails and the log says so.

## Project settings
Edit, Project Settings, Plugins, NyrexVJ:
- **Auto Start**: start listening when a game instance starts (default on).
- **Listen Port**: UDP port to listen on, must match NyrexVJ's OSC output (default 9011). The plugin listens on all network adapters so another computer can send to it. Allow the port in the firewall for that case.
- **Control Host** and **Control Port**: where controls are sent (default `127.0.0.1` and `9010`, NyrexVJ's OSC input, switched on in Settings, Control). Only an IPv4 address or `localhost` is accepted.

## Blueprint
Use **Get NyrexVJ** (needs a world context object, any actor or widget works) to get the subsystem, or call the functions on a `NyrexVJ Subsystem` reference. The subsystem lives as long as the game instance.

| Group | Nodes |
| --- | --- |
| Connection | `Start Listening`, `Stop Listening`, `Is Listening`, `Is Connected`, `Get NyrexVJ Version`, `Get Schema`, `Get NyrexVJ Input Port` |
| Audio | `Get Bass`, `Get Mid`, `Get High`, `Get Level`, `Get Kick` (0 to 1), `Get Spectrum` (32 values), `Get Spectrum Band`, `Get Onset Count` |
| Tempo | `Get Bpm`, `Get Beat Number`, `Get Beat Phase` (0 to 1), `Get Beat In Bar` (0 to 3) |
| Parameters | `Get Parameter` (by name, the names set in NyrexVJ under Parameters to send), `Get Parameter Names` |
| Control | `Set Control Target`, `Send Control Value`, `Send Control Trigger`, `Set Master Level`, `Set Layer Opacity` (layer 1 to 4), `Trigger Column` (1 to 8), `Switch Deck` (1 to 16), `Tap` |
| Events | `On Beat` (beat number, beat in bar), `On Onset` (count), `On Connected`, `On Disconnected` |

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
- `Source/NyrexVJ/Private/Core`: OSC decoder and encoder and the state machine, plain C++17, the part that has tests.
- `Source/NyrexVJ/Private`: the Unreal glue (sockets, ticker, thread hand-over).

## Limits
- IPv4 only, no name lookup, no discovery, no authentication. Use it on a network you trust.
- At most 64 parameters are kept, names up to 32 characters.
- One NyrexVJ at a time. A second sender on the same port would mix its data into the same state.
- Receiving is not tied to a frame, the values are the newest packet. No smoothing is applied.

## Reporting problems
Open an issue on GitHub and say the Unreal Engine version, the compiler output or the log lines starting with `LogNyrexVJ`. Compile fixes from people with Unreal installed are especially welcome.
