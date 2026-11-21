# Plugins

A plugin is a shared library (`.dll` on Windows, `.so` on Linux) that adds sources to NyrexVJ:

- **Modulation sources** give one number from 0 to 1 about 60 times a second. They appear as `Plugin 1` to `Plugin 8` in every list of sources (modulations, effect reactions) once you have put a plugin source into the slot.
- **Image sources** give a picture of 1280 x 720 pixels about 30 times a second. They appear as the clips `Plugin Source 1` to `Plugin Source 4` in the clip browser and behave like a camera or an NDI input.

The interface is a C header, [`plugins/include/nyrexvj_plugin.h`](../plugins/include/nyrexvj_plugin.h), version 1.0. A working plugin is in [`plugins/example`](../plugins/example). The reasoning is in [ADR 0018](adr/0018-plugin-system.md).

## Trust

**A plugin is native code. It runs with the same rights as NyrexVJ and there is no sandbox.** A plugin can read your files, open the network and do everything a program can do. The rules below protect the show against mistakes, not against a plugin that wants to do harm. Enable only plugins from a source you trust, and read the source if you can.

Because of this, plugins are **off by default**. NyrexVJ lists what it finds, but runs no plugin code until you switch the plugin on in Settings, Plugins. `--no-plugins` starts the program with plugins off for the whole session.

## Install and use

1. Open Settings, Plugins, and press **Open plugins folder** (it is the folder `plugins` in the data folder).
2. Put the plugin folder into it. Each plugin has its own folder with a `plugin.json` and the library file: `plugins/<folder>/plugin.json`.
3. Press **Rescan**. The plugin is listed with its state. Tick **Enable**.
4. In the section *Modulation sources*, pick a plugin source for `Plugin 1` to `Plugin 8`. Then choose that `Plugin N` as the source of a modulation or an effect reaction, like any audio band.
5. In the section *Image sources*, pick a plugin source for a slot. The clip `Plugin Source N` then shows its picture. It is an ordinary live clip: put it on a layer, add effects, use it in a preset.
6. Each plugin source has up to four parameters. They are sliders from 0 to 1 and are saved in `plugins/config.json`.

A plugin source only runs while something uses it. A modulation source that nobody has read for 3 seconds is stopped and starts again at the next read. An image source runs while its clip is on a layer.

Plugin bands are not available in the stage (3D scene) source list, which is a fixed list.

## States

| State | Meaning |
| --- | --- |
| Not enabled | Found, not switched on. No code of the plugin has been run. |
| Loading | The library is being opened. This happens on a separate thread. |
| Loaded | The library is open and its sources can be used. |
| Failed | The plugin broke a rule. It stays off until NyrexVJ is restarted. The reason is shown. |
| Not compatible | The manifest or the API version does not fit. No code of the plugin has been run. |
| Safe mode | The plugin was running when NyrexVJ crashed (see below). Press **Enable again** after you have dealt with the cause. |

## The manifest

`plugin.json` is read and checked before any code of the plugin runs. It is plain JSON of at most 16 KiB.

```json
{
  "format": "nyrexvj-plugin",
  "version": 1,
  "id": "nyrexvj-example",
  "name": "Example Plugin",
  "pluginVersion": "1.0.0",
  "api": { "major": 1, "minor": 0 },
  "library": {
    "linux-x64": "nyrexvj_example.so",
    "windows-x64": "nyrexvj_example.dll"
  }
}
```

| Field | Rule |
| --- | --- |
| `format`, `version` | `"nyrexvj-plugin"` and `1`. Another format name or a newer version is refused. |
| `id` | 1 to 32 characters of `a-z`, `0-9`, and `-`, `_`, `.` (not as the first character). Must be unique. It is the name under which settings are stored. |
| `name` | Shown to the user, 1 to 40 printable characters. |
| `pluginVersion` | Free text, at most 24 characters. |
| `api` | The API version the plugin was built for. The **major** number must be the same as the one of NyrexVJ. The **minor** number must be the same or lower. A plugin for a newer minor version is refused, because it may need functions that this program does not have. |
| `library` | The file for each system, `linux-x64` and `windows-x64`. A file name in the plugin folder: no path, extension `.so` or `.dll`. The file must exist. |

At most 16 plugin folders are read. A folder with a damaged manifest is listed with the reason and nothing else happens.

## Writing a plugin

A plugin exports one function, `nyrexvj_plugin_entry`. NyrexVJ calls it once after loading, and the plugin fills in a `NvjPlugin` table.

```c
#include "nyrexvj_plugin.h"

NYREXVJ_PLUGIN_EXPORT int32_t nyrexvj_plugin_entry(const NvjHost* host, NvjPlugin* plugin);
```

Every struct begins with a `size` field. The side that fills a struct writes its size; the side that reads it checks with `NYREXVJ_HAS_FIELD` before reading a field. This is how fields can be added in a later minor version without breaking an old plugin.

**The entry function** sets `api_major` and `api_minor` to the `NYREXVJ_PLUGIN_API_*` values, an `id` that is the same as in `plugin.json`, a `name`, a `version`, the number of sources (at most 8), and the functions below. It returns `NVJ_OK`. Any other result, a source count above 8, an id that differs from the manifest, or a missing function marks the plugin as Failed.

**`get_source(index, out)`** describes source `index`: an `id` (unique inside the plugin), a `name`, a `kind` (`NVJ_SOURCE_MODULATION` or `NVJ_SOURCE_FRAME`) and up to four parameter names.

**`create(source_index, host)`** makes an instance of a source and returns a pointer, or `NULL` to fail. `host` is valid until `destroy`.

**`destroy(instance)`** frees it.

**`modulation(instance, time, delta, &value)`** writes a number into `value`. It is clamped to 0 to 1, and not a number becomes 0. `time` is the seconds since the instance was created, `delta` the seconds since the last call.

**`frame(instance, time, delta, &image)`** writes a picture into `image.data`: 1280 x 720, 4 bytes per pixel in the order R, G, B, A, straight alpha, the first row is the top, `stride` is `width * 4`. Do not change the fields of `image`, do not write outside the buffer and do not keep the pointer after the call.

### Host functions

The `NvjHost` table has these functions. All of them may be called from the plugin's own calls and are safe to call at the rate of the source.

| Function | Gives |
| --- | --- |
| `log(context, level, message)` | A line in the NyrexVJ log, with the plugin id. At most 20 lines a second for each plugin, the rest is dropped. A line is cut at 400 characters. |
| `get_audio(context, &audio)` | `bass`, `mid`, `high`, `level` and `kick`, each 0 to 1, a snapshot of the last frame. |
| `get_beat(context, &beat)` | `beat` (the position of the beat clock), `bpm` and `beats_per_bar`. |
| `get_parameter(context, index)` | The value of the parameter `index` (0 to 3) from the sliders in Settings, 0 to 1. Default 0.5. |

Pass `host->context` as the first argument.

### Threads and time

Plugin code never runs on the render thread. Every plugin call happens on a thread of its own, and the render thread only reads the last result, so a plugin that is slow or stuck can not stop the picture. The rules:

| Call | Time allowed |
| --- | --- |
| `modulation` | 20 ms |
| `frame` | 100 ms |
| `create`, `destroy` | 500 ms |
| loading the library and the entry function | 1 s |

A call that is longer, that returns an error, or that breaks a rule (a changed `image`, a write behind the buffer, a wrong source table) marks the plugin as **Failed** for the rest of the session. A call that never returns is abandoned: its thread is left behind, the plugin is switched off, and the show goes on. Libraries are never unloaded, so an abandoned thread never runs into freed code. Do not use blocking system calls that can wait for ever, do not keep global state without a lock (calls of different sources and instances run on different threads at the same time), and do not start threads that outlive `destroy`.

### Crashes and safe mode

NyrexVJ writes down which plugin call is running. If the whole program crashes during a plugin call, the crash report names the plugin (a line `plugin: <id>`). At the next start, before any plugin is loaded, that plugin is put into **safe mode**: it is listed in `plugins/disabled.json`, switched off and not loaded. Settings, Plugins shows *Safe mode: the plugin was running when NyrexVJ crashed.* and an **Enable again** button. A plugin comes back only when you ask for it.

This does not make a crash in a plugin harmless: the program still ends. It makes sure that a plugin that crashes does not crash the program at every start.

## Build the example

The example is written in C99 and has no dependency.

With CMake (the target is part of the normal build):

```
cmake -S . -B build
cmake --build build --target nyrexvj_example_plugin
```

The result is the folder `build/plugins/nyrexvj-example` with `plugin.json` and the library. Copy it into the `plugins` folder of the data folder.

By hand on Linux:

```
cc -std=c99 -O2 -fPIC -shared -fvisibility=hidden -Iplugins/include \
   plugins/example/example_plugin.c -o nyrexvj_example.so -lm
```

The example has two sources: **pulse** (modulation, parameters *Rate* and *Kick mix*) and **plasma** (image, parameters *Speed*, *Color shift* and *Zoom*).

## Check a plugin

```
NyrexVJ --plugin-selftest plugins/nyrexvj-example
```

The self test copies the plugin folder into a temporary data folder, enables it, assigns its first modulation source and its first image source, and checks that the modulation stays in range and changes, that the image has the right size and is not empty, that the call times stay inside the budget and that no thread is left behind. It exits with 1 on an error. `--bench-out FILE` writes the result as JSON. CI runs it on Linux with the example plugin.

## Command line

| Option | Effect |
| --- | --- |
| `--no-plugins` | Plugins are off for this session. Settings, Plugins says so. |
| `--plugin-selftest FOLDER` | Load the plugin in FOLDER, check it and exit (see above). |

## Files

| File | Content |
| --- | --- |
| `plugins/<folder>/plugin.json` | The manifest of the plugin. |
| `plugins/config.json` | Which plugins are enabled, which source sits in which slot, and the parameter values. Format `nyrexvj-plugin-settings`, version 1. |
| `plugins/disabled.json` | The plugins in safe mode. Format `nyrexvj-plugin-safe-list`, version 1. |

## What is not checked

- NyrexVJ does not stop a plugin from doing something harmful, only from blocking or damaging the picture by mistake.
- A plugin that writes into memory that is not its own can still crash the program or damage it. The program checks the image buffer, not the rest of the memory.
- Plugins were tried with the example plugin and a set of test plugins that break each rule on purpose, on Linux. Plugins on Windows (`.dll`) are built and tested in CI but were not run by the maintainers.
- The header is **version 1.0 and not yet declared stable**. It is declared stable after one release of use. Until then a minor change may still happen, and the major number rises only for a break.
