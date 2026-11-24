# Public interfaces

Everything another program, another person or an older NyrexVJ can touch: network protocols, file formats and folders. This page says what each one is, where it is documented, which version it has and how it behaves when versions differ. It describes the state of v0.18.0 and is the list to check before changing any of them.

Levels used here:
- **Versioned**: carries a version number that is checked or sent, with rules for change.
- **Documented**: written down and tested, but without a version number. Treat additions as safe and renames or removals as breaking.
- **Internal**: may change without notice, do not build on it.

The compatibility rules in the last column are the behaviour of the code today and the way to extend it. A maintainer has to approve any decision to relax them.

## Network

| Interface | Level | Where defined | Version and change rule |
| --- | --- | --- | --- |
| OSC input, UDP port 9010 (off by default), addresses `/nyrex/<control id>` | Documented | README (OSC), Settings, Control lists every address | No version number. The addresses are the control target ids. Adding a target is compatible, renaming or removing one breaks senders. A message without a value fires a trigger or flips a switch, a number is read as 0 to 1. |
| OSC output, UDP default `127.0.0.1:9011`, addresses `/nyrex/out/...` | Versioned (schema 1) | [ADR 0008](adr/0008-osc-output-protocol.md), [UNREAL.md](UNREAL.md) | Schema number in `hello` and `bye`. Inside a schema new addresses and appended arguments only, anything else raises the number. Receivers ignore what they do not know. |
| Pro DJ Link beat packets, UDP port 50001, listen only (off by default, opened shared) | Documented (external protocol) | [SYNC.md](SYNC.md), [ADR 0016](adr/0016-show-sync.md) | Follows the public analysis of the protocol by Deep Symmetry. NyrexVJ sends nothing and does not announce itself. Only beat packets are used. |
| Art-Net output, ArtDmx on UDP 6454 | Documented (external standard) | [DMX.md](DMX.md) | Follows Art-Net 4. Universe mapping and rate are settings. |
| VRChat avatar parameters over OSC, `/avatar/parameters/<name>` | Documented (external protocol) | [DMX.md](DMX.md) | Defined by VRChat, NyrexVJ only sends floats, ints and bools. |
| Spout output and Spout input (four slots) | Documented (external protocol) | [OUTPUTS.md](OUTPUTS.md), [UNREAL.md](UNREAL.md), [ADR 0007](adr/0007-spout-receiver.md) | Spout 2.007 shared textures, Windows only. Sender names are chosen by the user. |
| NDI output (main picture, and one source per output window since 0.30.0, default name `NyrexVJ Output N`) | Documented (external protocol) | [OUTPUTS.md](OUTPUTS.md), [ADR 0023](adr/0023-advanced-output-2.md) | Needs the NDI runtime, which is not shipped. Source names are chosen by the user. |
| DMX strip in the output video, 120 x 13 blocks | Documented | [DMX.md](DMX.md) | Layout follows the public VRSL description, not verified against a real world. A layout change would break readers. |

## Files and folders

All user data lives under the data folder (Windows `%APPDATA%\NyrexVJ`, elsewhere `~/.config/NyrexVJ`). The environment variable `NYREXVJ_DATA_DIR` replaces it.

| Interface | Level | Where defined | Version and change rule |
| --- | --- | --- | --- |
| Presets, `presets/<name>.json`, and the session file `autosave.json` | Documented, `"format": 2` | README (Presets) | The reader is tolerant: missing values get defaults, unknown values are ignored, values are clamped. The format number is written but not checked on load. A file needs a `layers` array. A newer file loads with whatever the older program understands. |
| Clip packs, `.nyxclip` | Versioned (pack version 1) | [ADR 0004](adr/0004-scene-document-and-clip-pack.md), [SCENE3D.md](SCENE3D.md) | A pack with a higher version number is refused with a message that names the version. Version 0 is invalid. |
| Scene documents inside clips and packs | Versioned (document version 1) | [ADR 0004](adr/0004-scene-document-and-clip-pack.md), [SCENE3D.md](SCENE3D.md) | A document with a higher version is refused. |
| MIDI mapping, `controls.json` | Versioned (`"version": 1`) | README (MIDI) | Targets are stored by control id, so the control id rule above applies. |
| Language files, `languages/*.json` | Documented | [LANGUAGES.md](LANGUAGES.md) | `code`, `name`, `translations` keyed by the English text. Texts that are missing stay English, so new texts in a release never break an old file. The English text is the key, so rewording an English text drops that translation. |
| Shader clips, `shaders/*.glsl`, `*.frag`, `*.fs` | Documented | [SHADERS.md](SHADERS.md) | Two styles, NyrexVJ and Shadertoy. The list of uniforms is the contract, new uniforms may be added. The `#pragma nyrexvj` directives (`alpha`, `common`, `buffer`, `image`, `channel`) of Shader 2.0 are part of the contract, a file without such a line is compiled exactly as before. |
| Media, `media/`, 3D models `models/`, clip folder `clips/` | Documented | [MEDIA.md](MEDIA.md), [SCENE3D.md](SCENE3D.md) | Plain files in common formats, nothing NyrexVJ specific. |
| Program settings, `settings.json` | Internal | none | Read tolerantly, may gain and lose keys in any release. Do not edit by script. The video keys `videoDecoder` (`auto`, `hardware`, `software`), `hapDirect`, `videoGpuConvert` and `fpsLimit` (30, 60, 90, 120, 144 or 0 for unlimited) came with v0.18.0, an unknown decoder value is repaired to `auto` and an unlisted limit to the nearest listed one. `renderQuality` (0 Full, 1 Balanced, 2 Performance, 3 Automatic) came with v0.19.0, a missing key means 0 and an out-of-range value is clamped. |
| Venue setups, `venues/<name>.json` | Versioned (`"version": 1`, `"format": "nyrexvj-venue"`) | [OUTPUTS.md](OUTPUTS.md), [ADR 0015](adr/0015-advanced-output.md), [ADR 0023](adr/0023-advanced-output-2.md) | Four outputs with slices. Keys added later are optional and written only when not default (`sliceColor`, `ndi`, a mask's `curves` and `image`, since 0.30.0), so older files load unchanged and a setup without them is byte for byte the old file. A newer version, another format or a file over 2 MiB is refused. Mask pictures are stored by file name only. |
| Mask pictures, `masks/*.png`, `*.jpg`, `*.bmp`, `*.tga` | Documented | [OUTPUTS.md](OUTPUTS.md), [ADR 0023](adr/0023-advanced-output-2.md) | Plain pictures up to 16 MiB and 8192 pixels, copied here when chosen. A file name with a path or another extension is refused. |
| Cue list, `cues.json` | Versioned (`"version": 1`) | [SYNC.md](SYNC.md), [ADR 0016](adr/0016-show-sync.md) | `cues` holds `seconds`, `target` (a control id, so the control id rule applies) and `value`. A damaged file loads no cues and changes nothing. At most 512 cues. |
| Node effects, `nodes/<name>.nyxfx` | Versioned (`"version": 1`, `"format": "nyrexvj-effect"`) | [NODES.md](NODES.md), [ADR 0017](adr/0017-node-effect-editor.md) | Plain JSON with nodes, links, numbers and four control labels, at most 96 nodes, 256 links and 256 KiB. A newer version, another format name or a file over the limits is refused with a message and changes nothing. Unknown values are clamped. The node types are the ids of the catalogue: adding a node is compatible, an old program refuses a graph with a node it does not know and names it, and renaming or removing a node id breaks every file that uses it. The library name of an effect is `Node: ` and the name, and presets store that name.
| Plugin interface, `plugins/include/nyrexvj_plugin.h` (C header, API 1.1) | **Stable for every 1.x version** (API major and minor) | [PLUGINS.md](PLUGINS.md), [ADR 0018](adr/0018-plugin-system.md), [ADR 0026](adr/0026-plugin-system-2.md) | The major number must be the same as the program's, the minor number of a plugin at most the program's. Structs carry a `size` field, so fields are only appended inside a major version, and a 1.0 plugin still loads on 1.1 (tested with a plugin built from a copy of the 1.0 header). New in 1.1: the host functions `has_capability`, `get_limits` and `set_error` and the `requires` list in `plugin.json`. A plugin with another major version, a newer minor version or a missing required capability is listed as not compatible and never loaded. Plugins run in the process of NyrexVJ without a sandbox; process isolation is evaluated in ADR 0026 and not built. |
| Plugin manifest, `plugins/<folder>/plugin.json` | Versioned (`"version": 1`, `"format": "nyrexvj-plugin"`) | [PLUGINS.md](PLUGINS.md), [ADR 0018](adr/0018-plugin-system.md) | Checked before any plugin code runs. Id, name, API version and one library file for each system. A newer version or another format is refused with a message. At most 16 folders and 16 KiB for a manifest. The plugin id is the key of the stored settings, so renaming it makes a new plugin. |
| Plugin settings, `plugins/config.json` | Versioned (`"version": 1`, `"format": "nyrexvj-plugin-settings"`) | [PLUGINS.md](PLUGINS.md) | Enabled plugin ids, the source in each of the 8 modulation slots and 4 image slots (as `<plugin id>/<source id>`), and the 4 parameter values of each source. A damaged file loads as empty and changes nothing. Plugins are off until listed here. |
| Plugin safe list, `plugins/disabled.json` | Versioned (`"version": 1`, `"format": "nyrexvj-plugin-safe-list"`) | [PLUGINS.md](PLUGINS.md), [ADR 0018](adr/0018-plugin-system.md) | Plugins that were running when the program crashed. Written by the program at the next start, before plugins load; a listed plugin is not loaded until the person presses Enable again. |
| Crash report, `recovery/crash-report.txt` (the line `plugin: <id>`) | Documented | [PLUGINS.md](PLUGINS.md) | One line for each plugin call that was running at the crash. The line format is read at the next start; adding other lines is compatible. |
| Recovery data, `recovery/` | Internal | none | Used for crash recovery only. |

## Integration code in this repository

| Interface | Level | Where defined | Version and change rule |
| --- | --- | --- | --- |
| Unreal Engine plugin `integrations/unreal/NyrexVJ` | Experimental, plugin version 0.1.0 | [Plugin README](../integrations/unreal/NyrexVJ/README.md), [ADR 0009](adr/0009-unreal-plugin.md) | Understands OSC output schema 1 only. Its protocol core has host tests that run against the real sender and parser, the Unreal glue was never compiled. |

## Command line
The options in the README table (`--frames`, `--screenshot`, `--lang`, `--fresh`, `--bench-shaders`, `--bench-memory`, `--profile`, `--quality`, `--bench-video`, `--bench-convert`, `--convert-format`, `--convert-keep`, `--video-selftest`, `--scene-selftest`, `--video-mode`, `--no-hap-direct`, `--no-gpu-convert`, `--frame-report`, `--node-editor`, `--bench-nodes`, `--plugin-selftest`, `--shader-selftest`, `--no-plugins` and the others) exist for testing, benchmarks and screenshots. They are **Internal**: names and meanings may change in any release.

## Control ids
The control ids are the addresses of OSC input and the stored targets of MIDI mapping and of the named parameters in OSC output (for example `master/level`, `layer/1/opacity`, `column/3`, `deck/2`, `macro/1`). The complete list is the table on Settings, Control, which comes from the program itself, and the host tests check that the Unreal plugin's convenience addresses (`master/level`, `master/tap`, `column/1..8`, `deck/1..16`, `layer/1..4/opacity`) exist there. Because three interfaces rest on these ids, **renaming or removing an id is a breaking change** and belongs in the changelog under its own line.

## Checklist before changing an interface
1. Find it in the tables above and read its rule.
2. Prefer adding to changing. For OSC output raise the schema number only when nothing else works, and write an ADR.
3. Keep old files loading. Where a version number exists, test one number higher and one lower.
4. Update the table, the document that defines the interface, the changelog and, if the Unreal plugin is affected, `integrations/unreal/NyrexVJ` and its tests.
5. Say in the pull request which row of this page you touched.
