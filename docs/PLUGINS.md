# Plugins

A plugin is a shared library (`.dll` on Windows, `.so` on Linux) that adds sources to NyrexVJ:

- **Modulation sources** give one number from 0 to 1 about 60 times a second. They appear as `Plugin 1` to `Plugin 8` in every list of sources (modulations, effect reactions) once you have put a plugin source into the slot.
- **Image sources** give a picture of 1280 x 720 pixels about 30 times a second. They appear as the clips `Plugin Source 1` to `Plugin Source 4` in the clip browser and behave like a camera or an NDI input.

The interface is a C header, [`plugins/include/nyrexvj_plugin.h`](../plugins/include/nyrexvj_plugin.h), version 1.1 (every 1.x version is stable, see below). A working plugin is in [`plugins/example`](../plugins/example). The reasoning is in [ADR 0018](adr/0018-plugin-system.md), the questions of stability, limits and isolation are in [ADR 0026](adr/0026-plugin-system-2.md).

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
  "api": { "major": 1, "minor": 1 },
  "requires": ["source.modulation", "host.audio"],
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
| `api` | The lowest API version the plugin needs. The **major** number must be the same as the one of NyrexVJ. The **minor** number must be the same or lower. A plugin for a newer minor version is refused, because it may need functions that this program does not have. The library reports the same numbers in its table, and a difference between the two is an error. |
| `requires` | Optional. Up to 16 names of capabilities (see below) that the plugin cannot work without, each 1 to 48 characters of `a-z`, `0-9` and `-`, `_`, `.`. A name that this NyrexVJ does not know makes the plugin *Not compatible* and no code of the plugin is run. Only NyrexVJ 0.33.0 and later read this field, an older program ignores it, so a plugin that needs a 1.1 function also writes `"minor": 1`. |
| `library` | The file for each system, `linux-x64` and `windows-x64`. A file name in the plugin folder: no path, extension `.so` or `.dll`. The file must exist. |

At most 16 plugin folders are read. A folder with a damaged manifest is listed with the reason and nothing else happens.

## Writing a plugin

A plugin exports one function, `nyrexvj_plugin_entry`. NyrexVJ calls it once after loading, and the plugin fills in a `NvjPlugin` table.

```c
#include "nyrexvj_plugin.h"

NYREXVJ_PLUGIN_EXPORT int32_t nyrexvj_plugin_entry(const NvjHost* host, NvjPlugin* plugin);
```

Every struct begins with a `size` field. The side that fills a struct writes its size; the side that reads it checks with `NYREXVJ_HAS_FIELD` before reading a field. This is how fields can be added in a later minor version without breaking an old plugin.

**The entry function** gets a table in which `size` is the number of bytes the program offers. A plugin that needs the whole 1.x table checks `plugin->size >= sizeof(NvjPlugin)` first and returns `NVJ_FAILED` if it does not hold; a plugin built for a larger table than the program offers must not write the fields that do not fit (`NYREXVJ_HAS_FIELD(plugin, NvjPlugin, field)`). The plugin then writes its own `size`, sets `api_major` and `api_minor` to the lowest version it needs (the `NYREXVJ_PLUGIN_API_*` values when it uses everything), an `id` that is the same as in `plugin.json`, a `name`, a `version`, the number of sources (at most 8), and the functions below. It returns `NVJ_OK`. Any other result, a source count above 8, an id that differs from the manifest, a `size` larger than the offered table, a write behind the table or a missing function marks the plugin as Failed.

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
| `has_capability(context, name)` | 1 if this NyrexVJ has the capability, otherwise 0 (also for a name it does not know). Since 1.1. |
| `get_limits(context, &limits)` | An `NvjLimits` struct with the numbers of the table "Limits" below, so a plugin can adapt to the program instead of guessing. Since 1.1. |
| `set_error(context, message)` | Tells the program why a call is about to fail. The text (at most 200 characters) is added to the error message if the call that follows fails, and is thrown away otherwise. Since 1.1. |

Pass `host->context` as the first argument. A plugin written for 1.1 that also wants to run on a program with API 1.0 writes `"minor": 0` in its manifest and checks `NYREXVJ_HAS_FIELD(host, NvjHost, has_capability)` (and the same for the other two functions) before it calls them.

### Capabilities

A capability is a name for something the program offers. A plugin asks at run time with `has_capability`, or says in `plugin.json` under `requires` that it cannot work without it. The names of API 1.1:

| Name | Meaning |
| --- | --- |
| `source.modulation` | Modulation sources (one number from 0 to 1 at about 60 Hz). |
| `source.frame` | Image sources (1280 x 720, about 30 Hz). |
| `frame.rgba8` | Image sources deliver 8 bit RGBA, straight alpha, top row first. |
| `host.log` | `log` works. |
| `host.audio` | `get_audio` works. |
| `host.beat` | `get_beat` works. |
| `host.parameters` | `get_parameter` works. |
| `host.limits` | `get_limits` works. |
| `host.set_error` | `set_error` works. |

The name `isolation.process` is **reserved** for a program that runs plugins in a process of their own. This NyrexVJ does not, so `has_capability("isolation.process")` is 0 and a plugin must not take it as a promise of anything.

### Threads and time

Plugin code never runs on the render thread. Every plugin call happens on a thread of its own, and the render thread only reads the last result, so a plugin that is slow or stuck can not stop the picture. The rules:

| Call | Time allowed |
| --- | --- |
| `modulation` | 20 ms |
| `frame` | 100 ms |
| `create`, `destroy` | 500 ms |
| loading the library and the entry function | 1 s |

A call that is longer, that returns an error, or that breaks a rule (a changed `image`, a write behind the buffer, a wrong source table) marks the plugin as **Failed** for the rest of the session. A call that never returns is abandoned: its thread is left behind, the plugin is switched off, and the show goes on. Libraries are never unloaded, so an abandoned thread never runs into freed code. Do not use blocking system calls that can wait for ever, do not keep global state without a lock (calls of different sources and instances run on different threads at the same time), and do not start threads that outlive `destroy`.

### Lifecycle

| Step | What happens |
| --- | --- |
| Scan | The manifest of every folder is read and checked (format, id, API version, `requires`, library name). No plugin code runs. Plugins that do not pass are listed as **Not compatible** with the reason. |
| Enable | Only on the switch in Settings, Plugins. The library is loaded on a thread of its own and its entry function is called **once per plugin and per program run**. If the plugin is switched off and on again, the table from the first call is reused and the entry function is not called a second time. |
| Assign | A source in a slot creates an instance (`create`) on a runner thread. Several slots may use the same source, each with an instance of its own, within the limits below. |
| Run | `modulation` about 60 times and `frame` about 30 times per second, only while a slot uses the source. A modulation source that nobody has read for 3 s is stopped (`destroy` is called) and started again at the next read. |
| Remove | Taking the source out of its slot, or switching the plugin off, calls `destroy` on the runner thread and ends the thread. Switching off during loading is honoured, the library is then simply not used. |
| Switch on again | If instances of the plugin are still being destroyed, the plugin waits in the state *Loading* until they are done. A new instance is never created while an old one of the same plugin is still inside `destroy`. |
| Failed | The state lasts until the program is restarted. Switching off and on again does not bring a failed plugin back, because the code behind it is in an unknown state. |

### Limits

These limits protect the picture and the thread count. They do **not** limit memory, CPU use by the plugin's own threads or anything else a plugin can do with the rights of the program (see "Process isolation").

| Limit | Value | When it is hit |
| --- | --- | --- |
| Instances per plugin | 12 | The next instance is refused, the plugin and the other slots go on. The slot shows the reason. |
| Instances in total | 24 | Same. |
| Sources per plugin | 8 | The plugin is Failed. |
| Parameters per source | 4 | The plugin is Failed. |
| Stuck calls in this session | 16 | A plugin is not loaded any more. Every stuck call keeps a thread and its stack. Restart the program. |
| Share of one thread | 80 % | The runner of the source waits between calls. The source is *slowed down* (shown in the slot), it does not fail. |
| Log lines | 20 per second per plugin, 400 characters each | The rest is dropped. |
| Time per call | see the table above | The plugin is Failed (or abandoned if the call never returns). |

A plugin can read the numbers that apply with `get_limits` (API 1.1).

### Error messages

Every failure has a **kind** and a text. Settings, Plugins shows both, in the form *[Time budget exceeded] frame took 140 ms, the limit is 100 ms*.

| Kind | Meaning |
| --- | --- |
| Load error | The library or the entry function could not be loaded, the entry function failed, or the table the plugin filled in is wrong. |
| Not compatible | The manifest says that the plugin does not fit this program (API version, a capability from `requires` that is missing, a bad field). No code was run. |
| Call failed | A function returned an error code, or `create` returned nothing. |
| Exception | A C++ exception left a plugin function. Exceptions must never cross the C boundary, the program catches them only where the platform allows it and a plugin must not rely on that. |
| Time budget exceeded | A call took longer than allowed or never returned. |
| Invalid result | The result is not valid (a changed image description). A modulation value outside 0 to 1 is not an error, it is cut to that range, and a value that is not a number becomes 0. |
| Memory guard | The program found a write behind a table or the image buffer. The memory behind it may be damaged, so the plugin is switched off at once. |
| Limit reached | One of the limits above. |

A plugin can improve the text of a failure: it calls `set_error(context, "why")` and then returns an error code. The text (at most 200 characters, control characters replaced by spaces) is shown next to the kind. It is thrown away if the call does not fail.

### Compatibility rules

The API version has two numbers. Everything below holds for every 1.x version and is covered by the plugin tests (a copy of the 1.0 header and a plugin built with it run in the tests).

| Case | Result |
| --- | --- |
| Plugin and program have the same major version, the plugin needs the same or a lower minor version | Loads. |
| The plugin needs a higher minor version than the program | **Not compatible**, no code is run. |
| Different major version | **Not compatible**, no code is run. |
| A 1.0 plugin on a 1.1 program | Loads. A table that is smaller than the 1.1 one is read as far as its `size` says. |
| A 1.1 plugin that writes `"minor": 0` on a 1.0 program | Loads, if it checks the fields it uses with `NYREXVJ_HAS_FIELD`. |
| The version in the manifest and the version the library reports differ | The plugin is Failed (Load error). |
| Fields in a struct | Only added at the end, never moved, resized or removed within 1.x. A new field is read only after a `NYREXVJ_HAS_FIELD` check. |
| Constants and error codes | Never change meaning within 1.x. |
| Names in the capability list | Never removed or changed within 1.x. New names are only added. |

A change that breaks one of these rules raises the major version.

### Process isolation

Plugins run **in the process of NyrexVJ**, with its rights. Nothing is sandboxed and nothing is isolated:

- A plugin that crashes (a write to a wrong address, a stack overflow, an abort) ends NyrexVJ. This can not be caught reliably in the same process, and NyrexVJ does not try. What it does is write down the plugin call, name the plugin in the crash report, and put the plugin into safe mode at the next start.
- A plugin can read and write files, open network connections and allocate as much memory as it likes.
- The time budgets and the share of one thread keep a *slow* plugin from stopping the picture. They do not help against a plugin that damages the memory of the program.

Running risky plugins in a process of their own is a possible later architecture, and a set of measurements and the open questions are in [ADR 0026](adr/0026-plugin-system-2.md). It is **not implemented**. The capability name `isolation.process` is reserved for it and is always 0 in this program.

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

The build also produces `nyrexvj-legacy`, a plugin built with the 1.0 header, which the same check runs against to show that old plugins still load.

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
- Plugins were tried with the example plugin and a set of test plugins that break each rule on purpose, on Linux. The plugin tests also pass on Windows in CI (loading a `.dll`, the crash handler, the hang test), but nobody has used a plugin by hand on Windows.
- The header is **version 1.1 and declared stable** for every 1.x version (see "Compatibility rules"). "Stable" is a promise about the header and the rules, backed by automatic tests. Only the example plugin and the test plugins have used it so far, no plugin from outside.
- Plugin memory is not limited, and there is no process isolation (see above).
- The plugin tests (including the broken, hanging and fuzzed plugins) ran on Linux with the memory checker (AddressSanitizer and UndefinedBehaviorSanitizer) and on Windows in CI. They did not run on real hardware with a real graphics card.
