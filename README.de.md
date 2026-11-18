<p align="center">
  <img src="docs/icon.png" alt="NyrexVJ" width="96">
</p>

<h1 align="center">NyrexVJ</h1>

<p align="center">
  Live-Visuals für VRChat-Events und Partys. Natives C++ und OpenGL, keine Game-Engine, eine einzige Windows-Exe.<br>
  <a href="README.md">English</a> · <a href="CHANGELOG.de.md">Änderungsprotokoll</a> · <a href="https://github.com/MarlonXDev/NyrexVJ/releases">Releases</a>
</p>

<p align="center">
  <img src="docs/screenshot-welcome-de.png" alt="NyrexVJ mit deutscher Oberfläche und Willkommensbildschirm" width="100%">
</p>

NyrexVJ spielt generative Clips auf Ebenen ab, legt Effekte darüber, folgt deiner Musik und schickt das Bild per Spout an OBS oder in ein Vollbild-Ausgabefenster. Das Programm orientiert sich an Resolume Arena und startet trotzdem in einer Sekunde.

> Stand: v0.3.0. Clips, Effekte, Presets, Audio-Reaktivität, Spout-Ausgabe, Ausgabefenster, Englisch und Deutsch sowie der Auto-Updater funktionieren. Video-Clips und MIDI gibt es noch nicht.

## Download

Lade `NyrexVJ.exe` von der [Release-Seite](https://github.com/MarlonXDev/NyrexVJ/releases) herunter. Es ist eine einzelne Datei, eine Installation ist nicht nötig. Windows SmartScreen kann warnen, weil die Exe nicht signiert ist: "Weitere Informationen" und "Trotzdem ausführen" wählen.

Im Release liegen außerdem `NyrexVJ-windows-x64.zip` (Exe, Changelogs, Readmes) und `SHA256SUMS.txt`.

## Highlights

- **Audio-reaktiv.** Wähle Systemaudio (dein DJ-Programm), ein Mikrofon, einen Line-In oder ein Demo-Signal. Ebenen, Master und jeder Effektparameter können Bass, Mitten, Höhen, Pegel oder Kick folgen.
- **OBS mit zwei Klicks.** Spout-Ausgabe mit frei wählbarem Sendernamen. In OBS die Quelle "Spout2 Capture" hinzufügen und den Sender auswählen.
- **32 Clips, 56 Effekte, 15 Presets.** Fraktale, 3D-Szenen mit Raymarching, Blitze, Feuer, Spektrum-Visuals und mehr.
- **Englisch und Deutsch.** Englisch ist Standard, umschalten jederzeit unter Hilfe, Sprache.
- **Willkommensbildschirm.** Beim ersten Start und nach jedem Update, mit allem, was neu ist.
- **Aktualisiert sich selbst.** Prüft GitHub, kontrolliert die SHA-256-Summe und startet im neuen Build neu.

<p align="center">
  <img src="docs/screenshot-audio.png" alt="Audio-Reaktivität" width="100%">
</p>

## Audio

Öffne den Audio-Tab im Browser-Panel.

| Quelle | Worauf gehört wird |
| --- | --- |
| Ausgabegeräte (Loopback) | Alles, was dein PC abspielt, zum Beispiel Rekordbox, Serato, Spotify oder ein Browser |
| Eingabegeräte | Mikrofon oder Line-In |
| Demo-Signal | Ein eingebauter Beat, damit du ohne Musik Looks bauen kannst |

Der Tab zeigt das Live-Spektrum, Bass-, Mitten-, Höhen- und Pegelanzeigen, Beat-Erkennung und eine Tempo-Schätzung. Verstärkung, Glättung und Beat-Schwelle sind einstellbar, und "BPM mit Audio synchronisieren" koppelt das Deck-Tempo an den erkannten Beat.

Die Reaktivität stellst du an drei Stellen ein:

- **Ebenen-Panel**: zwei Modulationsplätze pro Ebene (Band auf Deckkraft, Skalierung, Geschwindigkeit, Farbton, Helligkeit oder Sättigung, mit einer Stärke).
- **Master-Panel**: dasselbe für das fertige Bild.
- **Jeder Effektplatz**: ein Band, ein Parameter und eine Stärke.

Bars, Equalizer, Spectrum Circle, Oscilloscope, Spectrum Warp und der Effekt Oscilloscope zeichnen das echte Spektrum oder die Wellenform. Ohne Audioquelle laufen sie auf einem virtuellen Beat, der dem BPM folgt.

## Spout und OBS

1. In NyrexVJ im Master-Panel (oder im Menü Ausgabe) "Per Spout an OBS senden" anhaken und einen Sendernamen wählen.
2. In OBS einmalig das [obs-spout2-plugin](https://github.com/Off-World-Live/obs-spout2-plugin) installieren.
3. Eine Quelle "Spout2 Capture" hinzufügen und den Sender aus der Liste wählen.

Spout gibt es nur unter Windows. Das Ausgabefenster (F11) mit OBS-Fensteraufnahme funktioniert weiterhin als Alternative.

## Clips und Effekte

<p align="center">
  <img src="docs/gallery-clips.jpg" alt="Alle 32 Clips" width="100%">
</p>

Jede Ebene hat vier Effektplätze, der Master ebenfalls.

<p align="center">
  <img src="docs/gallery-effects.jpg" alt="Alle 56 Effekte" width="100%">
</p>

Die Effekt-Galerie zeigt jeden Effekt mit Standardwerten auf dem Clip Plasma (Duotone auf Mandelbrot, weil Plasma kaum Helligkeitskontrast hat). Clip- und Effektnamen bleiben englisch, damit sie zu anderen VJ-Programmen passen.

## Presets

15 fertige Looks: Neon Club, Acid Rainbow, Deep Space, Synthwave Night, Laser Show, Minimal Mono, 3D Showcase, Warp Tunnel, Audio Reactor, Fractal Dream, Cyber Rain, Inferno, 3D Worlds, Crystal Cube und Pop Art. Eigene Looks speicherst du im Presets-Tab. Die letzte Sitzung wird beim Start wiederhergestellt.

## Bedienung

| Eingabe | Aktion |
| --- | --- |
| Klick auf eine Zelle | Clip auf seiner Ebene abspielen |
| Klick auf einen Spaltenkopf oder Tasten 1 bis 8 | Ganze Spalte auslösen |
| Leertaste | Tap Tempo |
| B | Blackout |
| F11 | Vollbild-Ausgabefenster |
| Esc | Vollbild verlassen |
| Rechtsklick auf eine Zelle | Clip zuweisen oder entfernen |
| Rechtsklick auf einen Regler | Auf Standardwert zurücksetzen |
| Aus der Bibliothek auf eine Zelle ziehen | Clip zuweisen |
| B / S / X an einer Ebene | Bypass / Solo / Ebene stoppen |

## Sprachen

Englisch ist die Standardsprache, Deutsch ist eingebaut. Umschalten unter Hilfe, Sprache, im Optionen-Tab oder auf dem Willkommensbildschirm, die Wahl wird gespeichert. Für eine neue Sprache braucht es eine Übersetzungstabelle neben `src/core/i18n_de.cpp` und einen Eintrag in `src/core/i18n.cpp`.

## Auto-Update

NyrexVJ prüft beim Start GitHub (abschaltbar im Optionen-Tab). Gibt es einen neueren Build, zeigt die Menüleiste einen Hinweis mit Installieren-Knopf.

| Kanal | Quelle | Aktualisiert, wenn |
| --- | --- | --- |
| Neuester Build (Standard) | rollender `nightly`-Prerelease | jeder Commit auf `main` |
| Stabil | neuestes GitHub-Release | ein neuer Versions-Tag veröffentlicht wird |

Der Updater funktioniert nur, solange das Repository öffentlich ist.

## Projektstruktur

```
NyrexVJ/
├── src/
│   ├── app/        Dear-ImGui-Oberfläche: Deck, Panels, Browser, Willkommensbildschirm, Theme
│   ├── audio/      WASAPI-Aufnahme und Loopback, FFT-Analyse, Beat- und Tempo-Erkennung
│   ├── core/       Kompositionsmodell, Presets, Einstellungen, i18n, Log, SHA-256
│   ├── render/     OpenGL-Engine, Clip- und Effektbibliothek, Ausgabefenster, Spout
│   ├── update/     Update-Prüfung, Installer, Changelog-Parser
│   ├── platform/   Windows- und Ersatz-Plattformcode
│   └── main.cpp
├── tests/          Unit-Tests für Core, Audio, i18n und Update-Logik
├── scripts/        Repository-Prüfungen (Übersetzungsabdeckung)
├── resources/      Windows-Icon und Ressourcenvorlage
├── docs/           Bilder für die Readme
├── third_party/    Mitgelieferte Bibliotheken glad und nlohmann/json
└── .github/        CI, Issue-Vorlagen
```

## Selbst bauen

Voraussetzungen: CMake 3.21 oder neuer, ein C++20-Compiler und eine GPU mit OpenGL 3.3.

Windows mit Visual Studio:

```
cmake -S . -B build -A x64
cmake --build build --config Release
ctest --test-dir build -C Release
```

Unter Linux werden die X11-Entwicklungspakete gebraucht, die GLFW verlangt (Xrandr, Xinerama, Xcursor, Xi):

```
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build
```

GLFW, Dear ImGui und Spout2 (Windows) lädt CMake beim Konfigurieren herunter. Audioaufnahme und Spout gibt es nur unter Windows, auf anderen Systemen wird das Demo-Signal benutzt.

## Tests

```
ctest --test-dir build
python scripts/check_i18n.py
NyrexVJ --check-shaders
```

`check_i18n.py` stellt sicher, dass jeder Oberflächentext eine deutsche Übersetzung hat. `--check-shaders` kompiliert jeden Clip und Effekt und beendet sich bei einem Fehler mit Code 1. Die CI führt alles bei jedem Push aus.

## Release veröffentlichen

1. Version in `CMakeLists.txt` erhöhen (`project(NyrexVJ VERSION x.y.z)`) und einen Abschnitt in `CHANGELOG.md` und `CHANGELOG.de.md` ergänzen.
2. Committen, dann taggen und pushen: `git tag vx.y.z && git push origin vx.y.z`.
3. GitHub Actions baut, testet und veröffentlicht das Release mit `NyrexVJ.exe`, dem Zip und `SHA256SUMS.txt`. Der Release-Text stammt aus dem passenden Abschnitt von `CHANGELOG.md`.

Jeder Push auf `main` aktualisiert außerdem den `nightly`-Prerelease, dem der Standard-Updatekanal folgt.

## Kommandozeile

| Option | Wirkung |
| --- | --- |
| `--preset NAME` | Factory- oder eigenes Preset beim Start laden |
| `--column N` | Spalte N (ab 1) beim Start auslösen |
| `--clip NAME` | Mit leerem Deck starten, das nur diesen Clip spielt |
| `--effect NAME` | Effekt in den ersten Master-Effektplatz legen |
| `--select-layer N` | Ebene N (ab 1) auswählen |
| `--resolution IDX` | Index der Ausgabeauflösung |
| `--tab IDX` | Anzuzeigender Browser-Tab |
| `--output` | Ausgabefenster beim Start öffnen |
| `--lang en\|de` | In dieser Sprache starten |
| `--audio-demo` | Demo-Audiosignal benutzen |
| `--welcome` / `--no-welcome` | Willkommensbildschirm erzwingen oder überspringen |
| `--fresh` | Gespeicherte Sitzung ignorieren |
| `--no-update-check` | Update-Prüfung überspringen |
| `--check-shaders` | Alle Shader kompilieren und beenden |
| `--frames N` | N Frames mit festen 60 FPS rendern, dann beenden |
| `--screenshot DATEI.png` | Mit `--frames` das Fenster als PNG speichern |
| `--dump-output DATEI.png` | Mit `--frames` die Engine-Ausgabe als PNG speichern |

Nutzerdaten liegen unter Windows in `%APPDATA%\NyrexVJ` und unter Linux in `~/.config/NyrexVJ`: Log, Fensterlayout, Einstellungen, Autosave und `presets/`. Mit `NYREXVJ_DATA_DIR` lässt sich ein anderer Ordner wählen.

## Fahrplan

- Video-Clips (H.264 und HAP) über FFmpeg
- MIDI-Controller-Zuordnung
- OSC-Eingang
- DMX-Strip-Ausgabe für VRSL
- Eigene Shader-Clips aus Dateien
- Weitere Sprachen

## Software von Dritten

- [GLFW](https://www.glfw.org/) (zlib-Lizenz)
- [Dear ImGui](https://github.com/ocornut/imgui) (MIT)
- [Spout2](https://github.com/leadedge/Spout2) (BSD 2-Clause), nur Windows
- [glad](https://github.com/Dav1dde/glad) generierter OpenGL-Loader (WTFPL oder CC0, dazu Apache-2.0 für die Khronos-Teile)
- [stb_image_write](https://github.com/nothings/stb) (gemeinfrei oder MIT)
- [nlohmann/json](https://github.com/nlohmann/json) (MIT)

## Lizenz

MIT, siehe [LICENSE](LICENSE).
