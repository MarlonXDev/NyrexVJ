# Unreal Engine and other programs

NyrexVJ runs as its own program. Unreal Engine (or TouchDesigner, Resolume, a game, your own tool) runs next to it and the two talk over two channels:

- **Picture:** the other program sends its video through **Spout**, NyrexVJ shows it as a clip (Windows only). See [ADR 0007](adr/0007-spout-receiver.md).
- **Control and music data:** NyrexVJ sends tempo, beats, audio levels, spectrum bands and values you choose through **OSC**. See [ADR 0008](adr/0008-osc-output-protocol.md).

The other direction already existed: any program can control NyrexVJ through OSC input (`/nyrex/...`, port 9010), see the README.

## What exists and what does not
- Exists: the Spout receiver (four input slots) and the OSC output with the protocol below, both built for this document.
- Exists as source code only: an experimental Unreal plugin with Blueprint nodes in `integrations/unreal/NyrexVJ` ([ADR 0009](adr/0009-unreal-plugin.md), [ADR 0052](adr/0052-unreal-link-status-and-latency.md)). It is optional, because Unreal also ships an OSC plugin, and it is **not built or released by NyrexVJ**: you copy the folder into your project. It has never been compiled with Unreal Engine, see below.
- Not tested: the author of these features had no Unreal Engine and no Windows GPU. The Spout receiver and the OSC output are covered by unit tests, by headless runs on Linux with a test pattern and by a compile check for Windows. A real Unreal to NyrexVJ run, latency and frame pacing in a real Unreal project are still to be measured on a Windows machine. Treat this guide as a starting point and report what you find.

### Inventory at 0.58.0
State words: **existing** (in the repository and run by a test or a script), **partial** (exists, but with a gap named in the last column), **not found** (was looked for, not there), **not verified** (written, never run in the setting that matters).

| Part | State | Where | What is missing |
| --- | --- | --- | --- |
| Spout receiver, four slots | existing | `src/media/`, [ADR 0007](adr/0007-spout-receiver.md) | Only run on Linux with a test pattern and compiled for Windows. Never run with a real Unreal sender. |
| OSC output schema 1 (`hello`, `bye`, `audio`, `spectrum`, `clock`, `beat`, `onset`, `param/<name>`) | existing | `src/control/osc_out.*`, [ADR 0008](adr/0008-osc-output-protocol.md) | |
| OSC input for controls (`/nyrex/...`) | existing | `src/control/control_system.*` | |
| Ping and answer (`/nyrex/ping`, `/nyrex/out/pong`) | existing since 0.58.0 | `src/control/control_system.cpp`, `src/control/osc_out.cpp` | Answers only while OSC input and OSC output are both on. |
| Plugin protocol core: decode, encode, state | existing | `integrations/unreal/NyrexVJ/Source/NyrexVJ/Private/Core/`, `nvj_unreal_core_tests` | |
| Plugin connection status (states, timeouts, reconnect) and round trip measurement | existing since 0.58.0 as plain C++ | `Private/Core/NyrexLink.*`, `nvj_unreal_link_tests` | Before 0.58.0 only a flag. The glue that feeds it with the real clock and sockets was not compiled. |
| Plugin Unreal glue: subsystem, sockets, ticker, settings, Blueprint nodes, Build.cs | partial | `Source/NyrexVJ/Public`, `Source/NyrexVJ/Private` | **Never compiled** with Unreal Engine. |
| Retry when the listen port is busy | existing since 0.58.0 in the glue | `UNyrexVJSubsystem::RetryListening` | Not compiled, not run. |
| Build of the plugin with the engine tools | not verified | command below | Never run. |
| Supported Unreal Engine versions | not found | | No version was ever built. A proposal is below. |
| Packaged game with the plugin | not verified | | Never built. |
| Example client without Unreal | existing since 0.58.0 | `integrations/unreal/examples/nyrex_monitor.py`, `scripts/test_unreal_link.py` | Python, not Blueprint. |
| Example Blueprints as assets | not found | | No `.uasset` can be made without the editor. Text recipes are below. |
| Spout sender inside Unreal | not found | | Epic ships none; community plugins were not tested. |
| Latency Unreal to NyrexVJ to Unreal in a real project | not verified | | Only the loopback round trip with a Python client was measured, see below. |

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

Since 0.58.0 it also reports the state of the link (`GetLinkState`, `GetLinkStatus`: waiting, connected, lost, ended, incompatible, with counts, the gap since the last packet and the packet rate), measures the round trip with `/nyrex/ping` once a second while connected, and tries to open the listen port again every few seconds when another program holds it.

Install and use: copy the folder to `<YourProject>/Plugins/NyrexVJ`, rebuild the project, tick Send OSC data and Receive OSC messages in NyrexVJ and run the game. The plugin README lists every node and all rules: [integrations/unreal/NyrexVJ/README.md](../integrations/unreal/NyrexVJ/README.md).

What is verified and what is not:
- Verified on every build: the protocol part of the plugin is plain C++ and is tested against NyrexVJ's real OSC sender and parser, with damaged and cut-off packets, size limits, value clamping and the schema rules (`nvj_unreal_core_tests`, also built with MSVC in CI). The link status and the round trip statistics are plain C++ too and have their own tests (`nvj_unreal_link_tests`: states, five second timeout, `bye`, reconnect, incompatible schema, out of order and unknown pongs, lost pings, id wrap around, bad clock values).
- Verified by a script on Linux: `scripts/test_unreal_link.py` starts the real NyrexVJ, runs the Python example client against it, and checks the connection, two clean restarts, one killed run and the round trip numbers.
- Not verified: everything that uses Unreal types (subsystem, sockets, ticker, settings, Blueprint library, Build.cs, the new status struct and the retry). It was written without an Unreal Engine installation and has never been compiled. Expect compile errors to fix, especially before Unreal Engine 5.3. Nothing is known about latency or frame pacing inside a real Unreal project.
- If you have Unreal installed, your report or pull request with fixes is the missing test.

### Link status and round trip
The plugin tells you in which state the link is:

| State | Meaning |
| --- | --- |
| Stopped | The plugin is not listening (stopped, or the port could not be opened yet). |
| Waiting | Listening, nothing valid has arrived yet. |
| Connected | A `hello` with schema 1 arrived in the last 5 seconds and no `bye` after it. |
| Lost | It was connected and no `hello` arrived for 5 seconds, and no `bye` was seen. NyrexVJ closed without saying so, froze, or the network dropped. |
| Ended | NyrexVJ sent `bye`. It stopped sending on purpose. |
| Incompatible | `hello` announced a schema other than 1. The data is ignored. |

The round trip: while connected, the plugin sends `/nyrex/ping` with a number once a second (setting **Ping Interval Seconds**, switch **Measure Latency**) to the Control Host and Control Port. NyrexVJ answers with `/nyrex/out/pong` and the same number. The plugin keeps the last 64 round trips and shows the last, the average, the minimum, the maximum and the 95th percentile, how many pings it sent and how many got no answer within 2 seconds. A pong with an unknown or repeated number, or one that comes later than 2 seconds, is ignored and counted.

What the number is and is not: it is the time from sending the ping until the pong is received. It contains the network and the wait until NyrexVJ's main loop reads the ping and sends the pong. That happens once per rendered frame, so the wait is up to one frame time of NyrexVJ: about 17 ms at 60 frames per second, hundreds of milliseconds when NyrexVJ renders slowly. The pong is sent in that frame, it does not wait for the next OSC tick. It is **not** the delay from a beat in the music to a picture on a screen, and it contains no display or render time of Unreal. It is also only valid when the Control Host and Control Port point to the same NyrexVJ whose output you receive.

NyrexVJ answers only when OSC input (**Receive OSC messages**) and OSC output (**Send OSC data**) are both on. If only the output is on, the plugin connects but shows no round trip, and says so by `Has Latency` staying false.

Blueprint recipes (text, nothing here was built in the editor):
1. **Show the state on screen.** Event Tick, Get NyrexVJ (with a world context), Get Link Status, Break NyrexLinkStatus, Format Text `NyrexVJ {State}  {Latency Average Ms} ms avg  {Pings Lost} lost`, Print String with a duration of 0.
2. **Flash on every beat.** Event Begin Play, Get NyrexVJ, bind On Beat to a custom event. In it, play a timeline that sets a light intensity from 1 to 0 over 0.2 seconds. Use Beat In Bar 0 for a stronger flash.
3. **Move with the bass.** Event Tick, Get NyrexVJ, Get Bass, Set Scalar Parameter Value on a Dynamic Material Instance. Check Is Connected first, because the getters return 0 while not connected.
4. **React to a lost link.** Bind On Disconnected, set a bool `ShowFallback`, and switch to your own animation. Bind On Connected and switch back.
5. **Control NyrexVJ.** On a key press call Set Master Level with 0.5, or Trigger Column with 3.

Use the built-in OSC plugin instead if you only need one or two values, or if you want to run on an engine version where the plugin does not compile yet.

Sending from Unreal to NyrexVJ works the other way round: in NyrexVJ tick Receive OSC messages (port 9010), in Unreal create an OSC client for `127.0.0.1:9010` and send messages to the `/nyrex/...` addresses listed in Settings, Control.

Sending to another computer: type its IPv4 address into the Address field. Only IPv4 addresses and `localhost` are accepted, there is no name lookup, no discovery and no authentication. Use it in a network you trust.

## Build the plugin (commands not verified)
These commands were taken from public descriptions and were **never run**, because no Unreal Engine was available. They show the usual shape; change the paths to your installation.

- Inside a C++ project: copy the folder to `<Project>/Plugins/NyrexVJ`, right click the `.uproject` file, Generate Visual Studio project files, open the solution, build the Development Editor target for Win64.
- As a standalone plugin package (the form people use to check that a plugin compiles on its own): `"<UE_ROOT>/Engine/Build/BatchFiles/RunUAT.bat" BuildPlugin -Plugin="<repo>/integrations/unreal/NyrexVJ/NyrexVJ.uplugin" -Package="<out>/NyrexVJ" -TargetPlatforms=Win64`. The result is a folder that can be copied into any project of that engine version.

### Engine versions (a proposal, not a claim)
- No engine version was ever tried. The plugin uses `FTSTicker`, which the current engine documentation lists and which the 4.26 and 4.27 documentation shows as `FTicker`, so it cannot be expected to compile before 5.0.
- Proposal for the maintainer: name **the newest stable engine release and the one before it** as the target, and write down in the plugin README each version that someone has actually built, with their name and the date. Until a version is built it stays "target, not verified". The uplugin file deliberately has no `EngineVersion` entry, so nothing is blocked.
- Packaged builds on other platforms (Linux, Mac, mobile, consoles) are not considered: the plugin has no platform list and uses the engine's socket subsystem, but nothing was tried.

## Example client without Unreal
`integrations/unreal/examples/nyrex_monitor.py` is a Python file with no extra packages. It listens on the OSC output port, tracks the same states as the plugin (waiting, connected, lost, ended, incompatible), sends `/nyrex/ping` and prints the round trip numbers. Start NyrexVJ with Send OSC data and Receive OSC messages on, then:

`python3 integrations/unreal/examples/nyrex_monitor.py`

Options: `--listen-port` (9011), `--control-host` (127.0.0.1), `--control-port` (9010), `--interval` seconds between pings (0.5), `--duration` seconds (0 means until Ctrl+C), `--json` for one line of summary, `--require-pongs N` (exit code 1 when fewer arrived), `--stop-on ended|lost`. It is the same logic as the plugin core written a second time, and it is what `scripts/test_unreal_link.py` uses to check the real program.

### What was measured
Two runs of `scripts/test_unreal_link.py` on 2026-11-26: NyrexVJ 0.58.0-dev with software OpenGL (Mesa llvmpipe) on a Linux container with 2 cores, no graphics card, the Python example client on the same machine over loopback, a ping every 0.25 s. In each run NyrexVJ ran three times in a row: twice for 12 seconds and ending cleanly, once killed by the script.

| Number | Run 1 | Run 2 |
| --- | --- | --- |
| Connections seen by the client | 3 (1 loss: the killed run; 2 clean ends with `bye`) | 3 (same) |
| Pings sent, pongs received | 126, 111 | 125, 111 |
| Pings without an answer after 2 seconds, still waiting at the end | 7, 8 | 6, 8 |
| Round trip over the last 64 answers: average, minimum, maximum, 95th percentile | 41.7, 1.2, 96.5, 79.0 ms | 42.2, 2.0, 123.7, 83.4 ms |
| Audio and clock ticks received over the three NyrexVJ runs | 372 each | 361 each |

Read these numbers with care. They come from two runs of one machine. The round trip is the last 64 answers only. NyrexVJ sends at most one tick per rendered frame, and about 370 ticks over the three runs is far fewer than the 1440 that 60 per second would give for 24 seconds of sending, so the software renderer was the limit. Since the answer waits for NyrexVJ's next frame (see above), most of a round trip of up to 124 ms is probably NyrexVJ's slow frames and not the network; that was not measured separately. The summary does not tell which pings were lost. The client kept pinging for up to 5 seconds after the killed run (20 pings) before it called the link lost, which is consistent with the 14 to 15 pings that got no answer, but it is not proof that no ping was lost while NyrexVJ was running. Nothing here says anything about a Windows machine with a graphics card, a real Unreal project, or a network between two computers.

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
| `/nyrex/out/pong` | `i` | answer to `/nyrex/ping`, with the next output update, not tied to the tick rate | the number of the ping, unchanged. Added at 0.58.0 inside schema 1 |

Rules a receiver can rely on:
- **Versioning.** The schema number is `1`. Inside one schema new addresses can be added and arguments can only be appended at the end. Any other change raises the number. Ignore addresses you do not know and arguments you do not need.
- **Rate.** 10 to 120 ticks per second (default 60), never more than one per rendered frame, so a slow frame means fewer ticks. `beat` and `onset` are sent in the frame that notices them and do not wait for the next tick.
- **Beat numbers** count from the start of the clock. A resync or tap can move the clock, a smaller beat number simply restarts the count without an event.
- **Connection.** UDP has no connection. `hello` is the heartbeat: treat five seconds without it as lost, and `bye` as a clean stop. If nobody listens, nothing happens and NyrexVJ shows no error. If the address is invalid or the socket cannot be opened, the Control page shows the reason and nothing is sent.
- **Timing.** The values describe the frame that was just rendered. Latency inside Unreal was not measured. The round trip of a ping is described above.
- **Ping.** The input address `/nyrex/ping` takes one integer from 0 to 16777215. At most 8 are taken per frame and at most 8 answers wait, more are dropped. Anything else (a float, text, two arguments, a negative number) is refused. The pong goes to the configured output address, **not** to the sender of the ping, so a forged ping cannot be used to send data to a third computer. A ping is 20 bytes, a pong sent alone is 44 bytes in a bundle.

## Troubleshooting
- No Spout senders listed: the sender must run and use Spout 2.007 compatible shared textures. Both programs must run with the same user and the same GPU. Press nothing, the list refreshes by itself about once a second.
- Black or placeholder clip: look at the line under the sender box, it says whether the sender was found and which size it has.
- Nothing arrives in Unreal: check that the OSC plugin is enabled, that the server listens on the same port as NyrexVJ sends to, and that a firewall does not block UDP. On the same computer use `127.0.0.1` on both sides.
- The plugin shows Lost or Waiting: look at the log lines starting with `LogNyrexVJ`, at the Link status in the Project Settings (listen port and control port), and run the Python example client on the same ports. If the Python client connects and the plugin does not, the port may be held by another program (the plugin tries again every few seconds and says so once in the log).
- No round trip shown: NyrexVJ must have **Receive OSC messages** on, and the Control Port must be NyrexVJ's OSC input port (the fourth value of `hello`).
- Test without Unreal: any OSC monitor, for example a few lines of Python with the `python-osc` package or the OSC tools of TouchDesigner or Pure Data, can listen on `127.0.0.1:9011`. On Linux or macOS, `NyrexVJ --spout-test-pattern` makes a test sender called `Test Pattern` appear in the Spout list, which is how the receiver was tested without Windows.

## Open points
- A test with a real Unreal sender on Windows, including DirectX 12 projects, and measured latency and frame pacing in a real project.
- A first compile and run of the plugin with Unreal Engine, then the list of supported engine versions, a build of the plugin package with the engine tools and a packaged game. Packaged builds on other platforms are not considered. See [ADR 0009](adr/0009-unreal-plugin.md) and [ADR 0052](adr/0052-unreal-link-status-and-latency.md).
- Discovery of NyrexVJ on the network and a live parameter list that changes while running are not part of schema 1.
- Sender alpha, NDI as a network alternative and a choice of the graphics adapter are not part of this version.
