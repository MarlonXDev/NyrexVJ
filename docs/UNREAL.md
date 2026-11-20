# Unreal Engine and other programs

NyrexVJ runs as its own program. Unreal Engine (or TouchDesigner, Resolume, a game, your own tool) runs next to it and the two talk over two channels:

- **Picture:** the other program sends its video through **Spout**, NyrexVJ shows it as a clip (Windows only). See [ADR 0007](adr/0007-spout-receiver.md).
- **Control and music data:** NyrexVJ sends tempo, beats, audio levels, spectrum bands and values you choose through **OSC**. See [ADR 0008](adr/0008-osc-output-protocol.md).

The other direction already existed: any program can control NyrexVJ through OSC input (`/nyrex/...`, port 9010), see the README.

## What exists and what does not
- Exists: the Spout receiver (four input slots) and the OSC output with the protocol below, both built for this document.
- Exists as source code only: an experimental Unreal plugin with Blueprint nodes in `integrations/unreal/NyrexVJ` ([ADR 0009](adr/0009-unreal-plugin.md)). It is optional, because Unreal also ships an OSC plugin, and it is **not built or released by NyrexVJ**: you copy the folder into your project. It has never been compiled with Unreal Engine, see below.
- Not tested: the author of these features had no Unreal Engine and no Windows GPU. The Spout receiver and the OSC output are covered by unit tests, by headless runs on Linux with a test pattern and by a compile check for Windows. A real Unreal to NyrexVJ run, latency and frame pacing are still to be measured on a Windows machine. Treat this guide as a starting point and report what you find.

## Picture: Unreal to NyrexVJ through Spout (Windows)
1. Install a **Spout sender plugin for Unreal**. Epic does not ship one. Community plugins exist (searching for "Spout Unreal" on GitHub or Fab finds several, for example `kessoning/Spout-UE5`). They are not endorsed by NyrexVJ and were not tested by us, and each one supports certain engine versions and graphics APIs (Spout works with DirectX 11; Unreal on DirectX 12 needs a plugin that bridges this). Read the plugin page.
2. In Unreal let the plugin send a render target or the viewport under a **unique sender name**, for example `UnrealMain`.
3. In NyrexVJ open the Library, filter by **Spout** and click `Spout Input 1` to put it into the selected cell. Select the clip: the Clip window shows the sender list.
4. Pick the sender, or leave it on Automatic (the first sender that is not NyrexVJ's own output). The clip window says Connected to ... with size and frame rate.

How it behaves:
- Four slots, `Spout Input 1` to `Spout Input 4`, so you can layer several sources. The chosen sender belongs to the slot and is saved in the program settings, not in the preset.
- The image is fitted into the layer without stretching, with black bars where the shapes differ. The Scale slider of the layer zooms it. Alpha is ignored, the picture is shown as opaque.
- A slot receives only while it is shown on a layer. Three seconds after the last use it lets go of the sender.
- If the sender closes, the clip shows a quiet placeholder and connects again when the sender returns. If the sender changes its resolution, the slot adapts by itself.
- If the image is upside down in your setup, tick **Flip the image vertically** in the Clip window.
- NyrexVJ's own Spout output is never chosen in Automatic mode, so the two cannot feed back into each other. Choosing it by hand is possible but gives a feedback loop.
- Both programs need to use the same graphics adapter. On laptops with two GPUs set the same one for both programs in the Windows graphics settings.

## Control and music data: NyrexVJ to Unreal through OSC
1. In NyrexVJ open Settings, Control, OSC output and tick **Send OSC data**. The defaults are address `127.0.0.1`, port `9011`, 60 updates per second.
2. In Unreal enable the built-in **OSC** plugin (Edit, Plugins, search for OSC) and restart the editor.
3. In a Blueprint create an OSC server on `127.0.0.1` port `9011` ("Create OSC Server") and bind events to the address patterns you need, for example `/nyrex/out/audio` or `/nyrex/out/beat` ("Bind Event to OSC Address Pattern" or the equivalent in your engine version). In the event, read the arguments of the message with the "Get Float from OSC Message" and "Get Int32 from OSC Message" nodes. Node names change a little between engine versions, the OSC plugin documentation of your version is authoritative.
4. Use the values: drive a material parameter with the bass value, trigger something on every `/nyrex/out/beat`, scale an actor with a band of `/nyrex/out/spectrum`.

For your own named values open **Parameters to send**, press Add parameter, choose a name (letters, digits, `_` and `-`, up to 32 characters) and the control it follows, for example `layer/1/opacity` or `master/level`. It is sent as `/nyrex/out/param/<name>` with a value from 0 to 1, the same number the input side of that control would accept. Up to 16 parameters. Triggers cannot be used.

## Blueprint plugin (experimental)
The folder `integrations/unreal/NyrexVJ` of this repository is an Unreal Engine plugin. It listens on the OSC output of NyrexVJ and gives Blueprints and C++ typed access to it: audio levels, the 32 spectrum bands, BPM, beat number and phase, your named parameters, and the events On Beat, On Onset, On Connected and On Disconnected. It also sends controls back (master level, layer opacity, column, deck, tap, or any `/nyrex/...` address). It does the connection handling of the protocol for you: the five second heartbeat, `bye` and the schema check.

Install and use: copy the folder to `<YourProject>/Plugins/NyrexVJ`, rebuild the project, tick Send OSC data in NyrexVJ and run the game. The plugin README lists every node and all rules: [integrations/unreal/NyrexVJ/README.md](../integrations/unreal/NyrexVJ/README.md).

What is verified and what is not:
- Verified on every build: the protocol part of the plugin is plain C++ and is tested against NyrexVJ's real OSC sender and parser, with damaged and cut-off packets, size limits, value clamping and the schema rules (`nvj_unreal_core_tests`, also built with MSVC in CI).
- Not verified: everything that uses Unreal types (subsystem, sockets, ticker, settings, Blueprint library, Build.cs). It was written without an Unreal Engine installation and has never been compiled. Expect compile errors to fix, especially before Unreal Engine 5.3. Nothing is known about latency or frame pacing.
- If you have Unreal installed, your report or pull request with fixes is the missing test.

Use the built-in OSC plugin instead if you only need one or two values, or if you want to run on an engine version where the plugin does not compile yet.

Sending from Unreal to NyrexVJ works the other way round: in NyrexVJ tick Receive OSC messages (port 9010), in Unreal create an OSC client for `127.0.0.1:9010` and send messages to the `/nyrex/...` addresses listed in Settings, Control.

Sending to another computer: type its IPv4 address into the Address field. Only IPv4 addresses and `localhost` are accepted, there is no name lookup, no discovery and no authentication. Use it in a network you trust.

## OSC output protocol, schema 1
Transport: OSC 1.0 over UDP. Each tick is one OSC bundle (immediate time tag), split into several packets if it ever exceeds 1400 bytes. Only the argument types `i` (int32), `f` (float32) and `s` (string) are used. Every address starts with `/nyrex/out/`.

| Address | Types | When | Meaning |
| --- | --- | --- | --- |
| `/nyrex/out/hello` | `s i s i` | three times after start, then every 2 s | app name (`NyrexVJ`), schema number, app version, OSC input port (0 when input is off) |
| `/nyrex/out/bye` | `i` | once when sending stops | schema number |
| `/nyrex/out/audio` | `f f f f f` | every tick | bass, mid, high, level, kick, each 0 to 1 |
| `/nyrex/out/spectrum` | `f` x 32 | every tick, can be switched off | 32 bands from low to high, each 0 to 1 |
| `/nyrex/out/clock` | `f i f` | every tick | BPM, whole beat number, position within the beat (0 to 1) |
| `/nyrex/out/beat` | `i i` | once per beat | beat number, beat within a bar of four (0 to 3) |
| `/nyrex/out/onset` | `i` | once per detected audio beat | running count of detected beats |
| `/nyrex/out/param/<name>` | `f` | every tick | one of your named parameters, 0 to 1 |

Rules a receiver can rely on:
- **Versioning.** The schema number is `1`. Inside one schema new addresses can be added and arguments can only be appended at the end. Any other change raises the number. Ignore addresses you do not know and arguments you do not need.
- **Rate.** 10 to 120 ticks per second (default 60), never more than one per rendered frame, so a slow frame means fewer ticks. `beat` and `onset` are sent in the frame that notices them and do not wait for the next tick.
- **Beat numbers** count from the start of the clock. A resync or tap can move the clock, a smaller beat number simply restarts the count without an event.
- **Connection.** UDP has no connection. `hello` is the heartbeat: treat five seconds without it as lost, and `bye` as a clean stop. If nobody listens, nothing happens and NyrexVJ shows no error. If the address is invalid or the socket cannot be opened, the Control page shows the reason and nothing is sent.
- **Timing.** The values describe the frame that was just rendered. Network and engine latency were not measured.

## Troubleshooting
- No Spout senders listed: the sender must run and use Spout 2.007 compatible shared textures. Both programs must run with the same user and the same GPU. Press nothing, the list refreshes by itself about once a second.
- Black or placeholder clip: look at the line under the sender box, it says whether the sender was found and which size it has.
- Nothing arrives in Unreal: check that the OSC plugin is enabled, that the server listens on the same port as NyrexVJ sends to, and that a firewall does not block UDP. On the same computer use `127.0.0.1` on both sides.
- Test without Unreal: any OSC monitor, for example a few lines of Python with the `python-osc` package or the OSC tools of TouchDesigner or Pure Data, can listen on `127.0.0.1:9011`. On Linux or macOS, `NyrexVJ --spout-test-pattern` makes a test sender called `Test Pattern` appear in the Spout list, which is how the receiver was tested without Windows.

## Open points
- A test with a real Unreal sender on Windows, including DirectX 12 projects, and measured latency and frame pacing.
- A first compile and run of the plugin with Unreal Engine, then the list of supported engine versions. Packaged builds and mobile or console targets are not considered. See [ADR 0009](adr/0009-unreal-plugin.md).
- Discovery of NyrexVJ on the network and a live parameter list that changes while running are not part of schema 1.
- Sender alpha, NDI as a network alternative and a choice of the graphics adapter are not part of this version.
