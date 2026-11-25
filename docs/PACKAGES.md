# Community packages: share presets, shaders, node effects and plugins

Added in 0.42.0 ([ADR 0035](adr/0035-community-packages.md)). A community package is one file with the ending `.nyxpack`. It holds a small set of things somebody made for NyrexVJ: presets, shaders, node effects, masks, venue setups and, sometimes, a plugin. File, Install community package (or dropping the file on the window) opens the installer. Nothing is changed until you press Install, nothing is switched on by itself and your own files are never replaced without asking.

A community package is **not** a show package. A show package (`.nyxshow`, [SHOWS.md](SHOWS.md)) moves your own show to another computer. A community package is something you install and may later update or remove. The two readers refuse each other's files with a message that says which one it is.

## What a package holds

| Kind | Where it goes | Allowed endings | Largest file |
| --- | --- | --- | --- |
| `preset` | `presets/` | `.json` | 8 MiB |
| `shader` | `shaders/` | `.glsl .frag .fs` | 256 KiB |
| `node` | `nodes/` | `.nyxfx` | 256 KiB |
| `venue` | `venues/` | `.json` | 2 MiB |
| `mask` | `masks/` | `.png .jpg .jpeg .bmp .tga` | 16 MiB |
| `plugin` | `plugins/<plugin id>/` | `plugin.json` and the libraries `.dll` (Windows) or `.so` (Linux) | 128 MiB |
| `preview` | shown in the window, not installed | `.png .jpg .jpeg` | 1 MiB |
| `license` | `packages/<id>.license.txt` | `LICENSE.txt` | 64 KiB |

A package holds at most 256 entries and 256 MiB. Videos, models and scene packs are not allowed in a community package; use a show package for them.

## The manifest

The manifest is JSON at the end of the file (`"format": "nyrexvj-pack"`, `"version": 1`). Besides the list of files it has the fields below. In the `pack.json` of a folder the version is called `version`; in the finished package it is `packageVersion`.

| Field | Meaning |
| --- | --- |
| `id` | The identity of the package. Lower case letters and digits, and `-`, `_` or `.` after the first character, at most 32 characters. The same rule as a plugin id. |
| `packageVersion` | Up to four numbers separated by dots (`1`, `1.2`, `1.2.3.4`). Anything else is refused, so versions can always be compared. |
| `author`, `description`, `license`, `homepage` | Text for the person who installs. The package states a licence in words; NyrexVJ does not interpret it. |
| `nyrexvj.min`, `nyrexvj.max` | The NyrexVJ versions the package was made for. An empty `max` means every newer version. |
| `dependencies` | A list of `{ "id", "min" }`. Only packages that are **already installed** count. NyrexVJ never fetches a missing one. |
| `preview` | The name of the preview picture inside the package. |

## Building a package

Make a folder with a `pack.json` and the files:

```
my-pack/
  pack.json
  LICENSE.txt
  preview.png
  presets/
  shaders/
  nodes/
  masks/
  venues/
  plugin/        plugin.json and the library files
```

`pack.json` holds `id`, `version`, `name`, `author`, `description`, `license`, `homepage`, `nyrexvj` (`min`, `max`) and `dependencies`. In the window use the Create tab, or on the command line `NyrexVJ --pack-build my-pack my-pack.nyxpack`. A link or a folder inside a folder is left out and reported. A file the kind does not allow, a name that cannot be used and a file that is too large are reported too and nothing is written until they are fixed. An existing file is only replaced with `--pack-overwrite` (or the tick in the window).

## Installing

Install is a plan first and a change second. The plan lists every file with its state and shows these hints. A hint is blocking (nothing can be installed), needs your consent, or is only a notice.

| Hint | Kind | What it means and what you can do |
| --- | --- | --- |
| Incompatible NyrexVJ version | blocking | The package names a range that does not include your version. |
| Missing or too old dependency | blocking | A package it needs is not installed or older than it needs. Install that one first. |
| Breaks an installed package | blocking | Updating would leave an installed package without the version it needs. |
| Different file with the same name | blocking, until you choose | Your file has other content. Choose Keep my files (your file stays, the package's copy is not installed) or Replace my files (yours is saved to the backup first). |
| Belongs to another package | blocking | The file is already owned by another installed package. |
| A folder has this name | blocking | The place for the file is taken by a folder. |
| Plugin in the way | blocking | A plugin with that id exists, the plugins folder is full (16), the plugin does not fit this version of NyrexVJ, or the plugin is still switched on (switch it off first when you update or remove it). |
| Older version | needs consent | The package is older than the installed one. Tick Install the older version anyway. |
| Native plugin | needs consent | The package holds program code that runs inside NyrexVJ with the same rights as NyrexVJ. Tick that you trust the author. |
| Shader code | notice | Shaders run on your graphics card. A faulty one can make the picture stutter or freeze. |
| No licence | notice | The package states no licence. |
| Same version | notice | Installing again puts back files that are missing. |
| Update | notice | Shows the old and the new version. |
| Plugin not for this system | notice | The plugin has no library for this system. It is left out, the rest is installed. |
| Plugin was stopped before | notice | The plugin crashed earlier and is in safe mode. It stays off until you press Enable again in the plugin settings. |

The sums at the end show how many files are new, updated or kept and how much space is needed. The check for free space is made before anything is written.

### What a plugin from a package can and cannot do

- A plugin is copied to `plugins/<plugin id>/` and **left switched off**. The package cannot switch it on. Changing the plugin's enabled state stays an act in the plugin settings, where the safe list, the crash marker, `--no-plugins` and the manifest check of [ADR 0018](adr/0018-plugin-system.md) apply as before.
- `plugins/config.json`, the safe list `plugins/disabled.json` and the crash marker are never written or deleted by an install, an update or a removal.
- A plugin must be switched off to be updated or removed.
- The installer does not load the library. It reads `plugin.json` with the same checks as the plugin loader does and copies the files.

### Update and rollback

An install writes every file to a temporary name first and checks its SHA-256. Only when all files are good are they moved into place. Files that are replaced, and old files of the package that the new version no longer has, are first moved to `package_backups/<time>/`. If anything fails, the moved files are put back and the backups are returned, so a failed update leaves what you had. Old files that you changed yourself are never moved; they stay and are reported.

### The record

Every installed package has a record in `packages/<id>.json` (`"format": "nyrexvj-installed-pack"`): id, version, name, author, licence, when it was installed and with which NyrexVJ, the SHA-256 of the package, its dependencies and each file with its checksum. A copy of the licence text is kept in `packages/<id>.license.txt`. The record is how NyrexVJ knows what belongs to a package, so that a removal only touches those files.

## Removing

Removing is a plan too. It moves the package's files to `package_backups/<time>/` instead of deleting them. A file that you changed after installing stays where it is. A package that another installed package depends on cannot be removed, and a plugin must be switched off first. Your own files with other names are never touched.

## Checks that protect you

- A package is read from its end: a cut-off file fails at the footer, a changed file fails at the manifest checksum or at the checksum of a file.
- Files are **stored, not compressed**, so a small package cannot unpack into something huge. Sizes are checked against the limits above and against the free space.
- The importer decides the folder. A manifest only has a kind and a plain file name; names with a folder part, `..`, a drive, forbidden characters, device names, a trailing space or dot or an ending that does not fit the kind are blocked.
- The fingerprint (SHA-256) of the package is shown. If the author published one, compare it: a different value means the file is not the one the author made.
- Tampered, oversized, incomplete, wrong-format and newer-format packages are refused before anything is written. A newer format version is refused with a message that says "newer".

## Command line

| Option | What it does |
| --- | --- |
| `--community-packages [0\|1\|2]` | Open the Community packages window at start (0 Installed, 1 Install, 2 Create). |
| `--pack-file <file>` | Preload that package in the Install tab. |
| `--pack-build <folder> <file>` | Build a package from a folder and exit. |
| `--pack-info <file>` | Print name, id, version, files, fingerprint and the hints, then exit. Changes nothing. |
| `--pack-install <file>` | Install and exit. Stops with a message when something blocks or consent is missing. |
| `--pack-accept-plugin` | Gives the consent for a native plugin. Without it a package with a plugin is not installed. |
| `--pack-allow-downgrade` | Gives the consent for an older version. |
| `--pack-keep-mine` / `--pack-replace` | Policy for files that differ: keep yours, or replace them after saving them. Without either, a different file blocks the install. |
| `--pack-remove <id>` | Remove an installed package and exit. |
| `--pack-overwrite` | Let `--pack-build` replace an existing file. |

The exit code is 0 on success and 1 on a refusal or an error.

## Online catalogue

There is none. Packages are files you get from somebody and install yourself. A catalogue that lists packages and offers a download is described in [ADR 0035](adr/0035-community-packages.md) as a separate proposal and is **not built**; it would need a server, signing of packages and a decision of the maintainer.

## Limits to know

- A package is not signed. The fingerprint tells you the file is the one somebody published, not that the content is safe.
- Dependencies are checked, not installed.
- Plugins cannot be activated by a package. Running a plugin is a decision you make in the plugin settings.
- The window was opened and looked at in screenshots on Linux with the Mesa software renderer; it has not been clicked through by a person, and nothing was run on Windows beyond the CI build.
