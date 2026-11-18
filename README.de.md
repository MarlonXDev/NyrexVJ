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

> Stand: v0.13.0. Clips, Effekte, 16 Decks mit weichen Übergängen, Presets, Audio-Reaktivität, Spout-Ausgabe und -Eingang, Ausgabefenster, Einstellungsfenster, OSC-Steuerung und OSC-Ausgabe, MIDI-Steuerung, ein 3D-Modell-Player (glTF/GLB) mit realistischer Beleuchtung, der Clip Creator für eigene 3D-Szenen, Bild-Clips mit Transparenz, Video-Clips (H.264 und HAP, braucht die FFmpeg-Bibliotheken, siehe [docs/MEDIA.md](docs/MEDIA.md)), Englisch und Deutsch sowie der Auto-Updater funktionieren.

## Download

Lade `NyrexVJ.exe` von der [Release-Seite](https://github.com/MarlonXDev/NyrexVJ/releases) herunter. Es ist eine einzelne Datei, eine Installation ist nicht nötig. Windows SmartScreen kann warnen, weil die Exe nicht signiert ist: "Weitere Informationen" und "Trotzdem ausführen" wählen.

Im Release liegen außerdem `NyrexVJ-windows-x64.zip` (Exe, Changelogs, Readmes) und `SHA256SUMS.txt`.

## Highlights

- **16 Decks, weiche Übergänge.** Richte bis zu 16 Looks ein und gleite mit Ausblenden, Auflösen, Zoom, Wischen, Kreis, Rotation oder Schieben dazwischen hin und her, in Sekunden oder Beats, auf Wunsch mit automatischem Wechsel.
- **Audio-reaktiv.** Wähle Systemaudio (dein DJ-Programm), ein Mikrofon, einen Line-In oder ein Demo-Signal. Ebenen, Master und jeder Effektparameter können Bass, Mitten, Höhen, Pegel oder Kick folgen, und ein Effekt lässt sich auf Gate stellen, damit er nur bei lauter Musik sichtbar ist. Acht Makro-Regler und vier tempo-synchrone LFOs funktionieren an denselben Stellen als Quellen.
- **Live-Steuerung.** Spiele es mit einem MIDI-Controller (Regler mit einem Klick anlernen, Windows) oder steuere es per OSC aus TouchDesigner, Max, Python und mehr fern. NyrexVJ kann Tempo, Beats und Audiopegel auch per OSC nach außen schicken.
- **Unreal Engine und andere Programme.** Vier Spout-Eingangs-Clips zeigen das Bild von Unreal, TouchDesigner oder einem Spiel (Windows), und die OSC-Ausgabe gibt ihnen Tempo, Beats, Spektrum und eigene benannte Werte. Siehe [docs/UNREAL.md](docs/UNREAL.md) (englisch). Ein Unreal-Plugin gibt es noch nicht.
- **OBS mit zwei Klicks.** Spout-Ausgabe mit frei wählbarem Sendernamen. In OBS die Quelle "Spout2 Capture" hinzufügen und den Sender auswählen.
- **54 Clips, 76 Effekte, 26 Presets.** Fraktale, 3D-Szenen mit Raymarching, Neon-Geometrie, Laser-Netze, Konturlinien-Flüssigkeiten, Kristalltunnel, Blitze, Feuer, Spektrum-Visuals, echte 3D-Modelle und -Szenen und 3D-Effekte wie Image Terrain, Hyperspace, Chrome Liquid, Shatter und Mirror Corridor. Alles, was in einer Version neu ist, trägt ein kleines "neu" hinter dem Namen, bis die nächste Nebenversion kommt.
- **Layout im Resolume-Stil.** Clip-Raster oben, Deck- und Tempozeile darunter, Vorschau und Ausgabe unten links, Master-, Ebenen- und Clip-Fenster daneben und der Browser rechts. Ein Pause-Knopf friert das Bild ein.
- **Neun Farbthemen.** Einstellungen, Aussehen schaltet die ganze Oberfläche zwischen Orange (Standard), Blau, Rot, Grün, Lila, Pink, Cyan, Gold und Mono um.
- **Eigene 3D-Modelle.** Lege .glb- oder .gltf-Dateien in den Modellordner und spiele sie wie jeden Clip, mit Texturen, Animation und Audio-Reaktion.
- **Clip Creator.** Baue eigene 3D-Szenen aus Formen, Modellen und Lichtern, lass alles zur Musik bewegen und speichere sie als tragbare .nyxclip-Dateien.
- **Einstellungsfenster.** Ein Ort für Audio, Ausgabe, Spout, Updates, Sprache und Oberflächengröße, dazu eine Statusleiste mit FPS, Tempo, Deck und Audio auf einen Blick.
- **Englisch und Deutsch.** Englisch ist Standard, umschalten jederzeit unter Einstellungen, Allgemein.
- **Willkommensbildschirm.** Beim ersten Start und nach jedem Update, mit allem, was neu ist.
- **Aktualisiert sich selbst.** Prüft GitHub beim Start, kontrolliert die SHA-256-Summe und zeigt oben rechts eine kleine Karte mit Jetzt neu starten oder Später.

<p align="center">
  <img src="docs/screenshot-layout.png" alt="Das neue Fensterlayout" width="100%">
</p>

<p align="center">
  <img src="docs/screenshot-audio.png" alt="Audio-Reaktivität" width="100%">
</p>

## Decks

Die Zeile über dem Clip-Raster enthält 16 Deck-Knöpfe. Jedes Deck ist ein vollständiger Satz aus vier Ebenen, acht Spalten, Effekten und Farben. Ein Klick auf einen Knopf wechselt das Deck, das neue Deck übernimmt mit einem Übergang statt eines Schnitts. Das aktuelle Deck ist orange, leere Decks sind gedimmt und starten leer, wenn du sie anklickst.

| Übergang | Aussehen |
| --- | --- |
| Ausblenden | Weiche Überblendung |
| Auflösen | Verrauschtes Durchbrennen mit leuchtender Kante |
| Zoom | Zieht durch das alte Bild ins neue |
| Wischen | Weicher diagonaler Wisch |
| Kreis | Kreis, der sich aus der Mitte öffnet |
| Rotation | Beide Bilder drehen sich ineinander |
| Schieben | Neues Deck schiebt sich von der Seite herein |

Stelle die Dauer in Sekunden ein oder hake Beat-Sync an, um in Beats zu zählen, und hake Auto-Wechsel an, um alle 4 bis 128 Beats durch die benutzten Decks zu schalten. Wechselst du mitten im Übergang erneut, friert das aktuell sichtbare Bild ein und blendet von dort aus weiter, es gibt also nie einen Sprung.

Rechtsklick auf einen Deck-Knopf: umbenennen, aktuelles Deck hierher kopieren, mit einem Werks-Look füllen, leeren oder entfernen. Im Presets-Tab lädt ein Klick auf einen Werks-Look ihn ins aktuelle Deck, und "Decks mit Werks-Looks füllen" legt 16 Looks in die 16 Decks. Decks werden mit dem Preset und mit der Sitzung gespeichert.

## Audio

Öffne den Audio-Tab im Browser-Panel oder Einstellungen, Audio.

| Quelle | Worauf gehört wird |
| --- | --- |
| Ausgabegeräte (Loopback) | Alles, was dein PC abspielt, zum Beispiel Rekordbox, Serato, Spotify oder ein Browser |
| Eingabegeräte | Mikrofon oder Line-In |
| Demo-Signal | Ein eingebauter Beat, damit du ohne Musik Looks bauen kannst |

Der Tab zeigt das Live-Spektrum, Bass-, Mitten-, Höhen- und Pegelanzeigen, Beat-Erkennung und eine Tempo-Schätzung. Verstärkung, Glättung und Beat-Schwelle sind in Einstellungen, Audio einstellbar, und "BPM mit Audio synchronisieren" koppelt das Tempo an den erkannten Beat. Liefert das gewählte Gerät mehrere Sekunden lang keinen Ton, weisen der Tab und die Statusleiste darauf hin.

<p align="center">
  <img src="docs/screenshot-settings.png" alt="Einstellungsfenster, Seite Audio" width="100%">
</p>

Die Reaktivität stellst du an drei Stellen ein:

- **Ebenen-Panel**: zwei Modulationsplätze pro Ebene (Band auf Deckkraft, Skalierung, Geschwindigkeit, Farbton, Helligkeit oder Sättigung, mit einer Stärke).
- **Master-Panel**: dasselbe für das fertige Bild.
- **Jeder Effektplatz**: ein Band und ein Modus. Im Modus Parameter bewegt das Band einen Effektwert um eine Stärke. Im Modus Gate ist der Effekt nur sichtbar, solange das Band laut ist, und blendet in der Stille aus, die Stärke bestimmt die Empfindlichkeit.

Bars, Equalizer, Spectrum Circle, Oscilloscope, Spectrum Warp und der Effekt Oscilloscope zeichnen das echte Spektrum oder die Wellenform. Ohne Audioquelle laufen sie auf einem virtuellen Beat, der dem BPM folgt.

## Makros, LFOs und Beat-Raster

Jede Stelle, die Bass, Mitten, Höhen, Pegel oder Kick folgen kann (die zwei Slots einer Ebene, der Master und jeder Effekt-Slot), kann drei weiteren Quellen folgen:

- **Makro 1 bis 8**: Regler im Master-Bereich unter Makros und LFOs. Ein Makro kann viele Parameter gleichzeitig steuern, und jeder Regler lässt sich auf einen MIDI-Controller legen (Einstellungen, Steuerung) oder per OSC setzen (`/nyrex/macro/1` bis `/nyrex/macro/8`).
- **LFO 1 bis 4**: tempo-synchrone Oszillatoren mit den Formen Sinus, Dreieck, Sägezahn, Rechteck, Zufall und Weicher Zufall. Der Zyklus reicht von einem Viertel-Schlag bis zu 32 Schlägen, die Phase bestimmt, wo im Zyklus der LFO startet. Weil sie Schläge zählen, nehmen Tap, Neu sync, Pause und die BPM sie mit, und ein Verlauf läuft nie aus dem Takt.
- **Beat und Takt**: eine Rampe von 0 auf 1 über einen Schlag bzw. einen Takt.

Ein Klick auf die Beat-Anzeige neben den Tempo-Reglern öffnet das Beat-Raster: Schläge pro Takt einstellen (2 bis 8), das Raster mit `<<` und `>>` um einen Sechzehntel-Schlag verschieben, auf den nächsten Schlag einrasten oder den nächsten Schlag zum ersten Schlag des Takts machen. Die Einzelheiten und die Reihenfolge der Quellen stehen in [docs/MODULATION.md](docs/MODULATION.md) (englisch).

## Spout und OBS

1. In NyrexVJ unter Einstellungen, Ausgabe (oder im Menü Ausgabe) "Per Spout an OBS senden" anhaken und einen Sendernamen wählen.
2. In OBS einmalig das [obs-spout2-plugin](https://github.com/Off-World-Live/obs-spout2-plugin) installieren.
3. Eine Quelle "Spout2 Capture" hinzufügen und den Sender aus der Liste wählen.

Spout gibt es nur unter Windows. Das Ausgabefenster (F11) mit OBS-Fensteraufnahme funktioniert weiterhin als Alternative.

### Spout-Eingang

Vier Clips, Spout Input 1 bis 4 (Bibliotheksfilter Spout), zeigen das Bild, das ein anderes Programm per Spout sendet. Lege einen in eine Zelle, wähle ihn aus und such den Sender im Clip-Fenster aus, oder lass ihn auf Automatisch, dann gilt der erste Sender, der nicht NyrexVJs eigene Ausgabe ist. Das Bild wird ohne Verzerrung in die Ebene eingepasst, läuft wie jeder Clip durch die Ebenen-Effekte, verbindet sich wieder, wenn der Sender zurückkommt, und zeigt einen Platzhalter, solange keiner da ist. Kommt das Bild kopfüber an, hake Bild vertikal spiegeln an. Die gewählten Sender stehen in den Programmeinstellungen. Nur Windows, auf anderen Systemen fügt `--spout-test-pattern` einen Test-Sender zum Ausprobieren hinzu. Einzelheiten und die Einrichtung mit Unreal Engine stehen in [docs/UNREAL.md](docs/UNREAL.md) (englisch).

## Clips und Effekte

<p align="center">
  <img src="docs/gallery-clips.jpg" alt="Alle 35 Clips" width="100%">
</p>

Jede Ebene hat vier Effektplätze, der Master ebenfalls.

<p align="center">
  <img src="docs/gallery-effects.jpg" alt="Effekt-Galerie" width="100%">
</p>

Die Effekt-Galerie zeigt jeden Effekt mit Standardwerten auf dem Clip Plasma (Duotone auf Mandelbrot und Line Art auf Crystal Cavern, weil Plasma kaum Helligkeitskontrast hat). Clip- und Effektnamen bleiben englisch, damit sie zu anderen VJ-Programmen passen.

## 3D-Modelle

Neben den Shader-Clips gibt es Clips, die echte 3D-Gitter zeichnen: Torus Knot Mesh, Crystal Cluster Mesh, Orbit Rings Mesh, Chrome Orbs Mesh, Neon Cubes Mesh, Gold Knot Mesh und Pulse Columns Mesh. Sie stehen im Bibliothek-Tab wie jeder andere Clip, folgen Farben, Tempo und Größe der Ebene, pulsieren mit Bass und Kick und laufen durch dieselben Effekte.

Um ein eigenes Modell zu spielen, lege eine `.glb`- oder `.gltf`-Datei in den Modellordner (`%APPDATA%\NyrexVJ\models`, der Bibliothek-Tab zeigt den Pfad und hat einen Knopf, der ihn öffnet) und drücke im Bibliothek-Tab Neu einlesen (ein Neustart ist nicht nötig). Sie erscheint als `Model: Dateiname`. Unterstützt werden Texturen (PNG und JPEG), Knoten-Animation und mehrere Gitter. Ein Modell lädt im Hintergrund, sobald ein Deck es zum ersten Mal enthält, das Bild bleibt also nie stehen, mit einem Zeitlimit von 60 Sekunden und einem Knopf Laden abbrechen, und eine beschädigte, zu große oder nicht unterstützte Datei wird mit einer Meldung im Log und im Clip-Tooltip abgelehnt. Ebenen, die dasselbe Modell spielen, teilen es sich im Speicher.

Modelle mit Metallic-Roughness-Materialien, Normalenkarten, Leuchtkarten und ausgeschnittener Transparenz werden mit physikalisch basierter Beleuchtung gezeichnet. Noch nicht unterstützt: Charaktere mit Skelett, Morph-Targets, Lichter und Kameras aus der Datei, Draco- oder Meshopt-Kompression und weiche Transparenz (wird deckend gezeichnet). Alle Grenzen und die Messwerte stehen in [docs/SCENE3D.md](docs/SCENE3D.md).

### Clip Creator

Das Menü Creator öffnet den Clip Creator. Die linke Spalte listet die Objekte und Lichter der Szene, die Mitte zeigt eine Live-Vorschau (Ziehen kreist, Mausrad zoomt, Diese Ansicht als Clip-Kamera nehmen behält die Ansicht), die rechte Spalte ist der Inspektor für Transformation, Wiederholung, Material, Texturen, Animation, Audio-Reaktionen, Kameras, Himmel und Belichtung. Eine Szene kann bis zu acht Kameras haben: Wähle die, die läuft, oder lass den Clip alle paar Beats zwischen allen schneiden. Starte mit einer Vorlage (Menü Creator, Mit einer Vorlage starten, oder Öffnen im Fenster) oder mit einer leeren Szene (Neu).

Speichern schreibt eine `.nyxclip`-Datei in den Clips-Ordner (`%APPDATA%\NyrexVJ\clips`). Sie enthält die Szenenbeschreibung und als Kopien die .glb-Modelle sowie PNG- oder JPEG-Texturen, die die Szene benutzt (bis 200 MiB), eine Datei genügt also zum Weitergeben. Danach erscheint sie in der Bibliothek unter Szenen als `Scene: Name`. Als Clip nutzen legt sie außerdem in die gewählte Zelle. In einer Szene lassen sich nur `.glb`-Modelle speichern, `.gltf`-Dateien, die weitere Dateien brauchen, stehen in der Dateien-Ansicht, sind aber nicht nutzbar. Das Dateiformat und die Entscheidungen dahinter stehen in [docs/adr/0004-scene-document-and-clip-pack.md](docs/adr/0004-scene-document-and-clip-pack.md), das Beleuchtungsmodell in [docs/adr/0005-pbr-shading.md](docs/adr/0005-pbr-shading.md) und der Editor in [docs/adr/0006-clip-creator.md](docs/adr/0006-clip-creator.md).

## Presets

18 fertige Looks: Neon Club, Milky Red, Crystal Cavern, Crystal Lines, Acid Rainbow, Deep Space, Synthwave Night, Laser Show, Minimal Mono, 3D Showcase, Warp Tunnel, Audio Reactor, Fractal Dream, Cyber Rain, Inferno, 3D Worlds, Crystal Cube und Pop Art. Eigene Looks speicherst du im Presets-Tab, ein gespeichertes Preset enthält alle 16 Decks. Die letzte Sitzung wird beim Start wiederhergestellt.

## Bedienung

| Eingabe | Aktion |
| --- | --- |
| Klick auf eine Zelle | Clip auf seiner Ebene abspielen |
| Klick auf einen Spaltenkopf oder Tasten 1 bis 8 | Ganze Spalte auslösen |
| Leertaste | Tap Tempo |
| Bild auf / Bild ab | Vorheriges / nächstes benutztes Deck |
| Strg + 1 bis 8, Strg + Umschalt + 1 bis 8 | Deck 1 bis 8, Deck 9 bis 16 |
| Strg + , | Einstellungen öffnen |
| B | Blackout |
| F11 | Vollbild-Ausgabefenster |
| Esc | Vollbild verlassen |
| Rechtsklick auf eine Zelle | Clip zuweisen oder entfernen |
| Rechtsklick auf einen Deck-Knopf | Deck umbenennen, kopieren, füllen, leeren oder entfernen |
| Rechtsklick auf einen Regler | Auf Standardwert zurücksetzen |
| Clip in der Bibliothek anklicken | In die gewählte Zelle legen und abspielen |
| Aus der Bibliothek auf eine Zelle oder einen Spaltenkopf ziehen | Clip zuweisen |
| Effekt in der Bibliothek anklicken oder ziehen | Zum gewählten Layer hinzufügen |
| B / S / X an einer Ebene | Bypass / Solo / Ebene stoppen |

## Live-Steuerung

Alles liegt unter Einstellungen, Steuerung. Zuordnungen und Adressen beziehen sich auf das aktuelle Deck, also das, das du im Clip-Raster siehst.

### MIDI

Nur unter Windows. Wähle deinen Controller unter Gerät, drücke Zuordnung hinzufügen, wähle was gesteuert werden soll und bewege dann einen Regler oder drücke eine Taste am Controller. Ab dann sind beide verbunden. Regler und Fader (CC) setzen Werte, Tasten (Noten) lösen Auslöser aus oder schalten Dinge ein, solange sie gehalten werden. Mit Invertieren drehst du einen Fader um, mit Lernen bekommt eine Zeile einen neuen Regler und mit Entfernen löschst du sie. Die Zuordnungen stehen in `controls.json` im Datenordner. Ein Controller, den ein anderes Programm exklusiv geöffnet hat, lässt sich nicht von zwei Programmen gleichzeitig nutzen.

### OSC

Hake OSC-Nachrichten empfangen an. OSC ist standardmäßig aus und lauscht auf Port 9010 (VRChat nutzt 9000, so kommen sich beide nicht in die Quere). Nur dieser Computer darf senden, außer du hakst Nachrichten von anderen Computern annehmen an, dann kann jeder in deinem Netzwerk NyrexVJ steuern. Sende eine Zahl an eine Adresse. Float, Integer und true oder false werden akzeptiert und als 0 bis 1 gelesen. Eine Nachricht ohne Wert feuert einen Auslöser oder schaltet einen Schalter um. Die Einstellungsseite listet jede Adresse, ein Klick kopiert sie.

| Adresse | Bedeutung |
| --- | --- |
| `/nyrex/master/level` | Master-Pegel, 0 bis 1 |
| `/nyrex/master/blackout` | Blackout, an ab 0,5 |
| `/nyrex/master/bpm` | Tempo, 0 bis 1 entspricht 40 bis 240 BPM |
| `/nyrex/master/tap`, `/nyrex/master/resync` | Tap-Tempo, Beat neu starten |
| `/nyrex/master/hue`, `saturation`, `brightness`, `contrast` | Master-Farbe, 0 bis 1 über den üblichen Bereich |
| `/nyrex/deck/1` bis `/nyrex/deck/16` | Zu diesem Deck wechseln |
| `/nyrex/deck/next`, `/nyrex/deck/previous` | Nächstes oder vorheriges benutztes Deck |
| `/nyrex/column/1` bis `/nyrex/column/8` | Spalte auslösen |
| `/nyrex/layer/L/clip/C` | Clip der Ebene L in Spalte C auslösen |
| `/nyrex/layer/L/opacity`, `speed`, `scale` | Ebenenwerte, Tempo 0,1 bis 4 und Größe 0,25 bis 4 |
| `/nyrex/layer/L/hue`, `saturation`, `brightness`, `contrast` | Ebenenfarbe |
| `/nyrex/layer/L/bypass`, `solo` | Ebenenschalter |
| `/nyrex/layer/L/clear` | Ebene stoppen |
| `/nyrex/layer/L/fx/S/enable` | Effektplatz S an oder aus |
| `/nyrex/layer/L/fx/S/param/P` | Parameter P des Effekts in Platz S |
| `/nyrex/master/fx/S/enable`, `/nyrex/master/fx/S/param/P` | Dasselbe für die Master-Effekte |

Ebenen gehen von 1 bis 4, Effektplätze von 1 bis 4 und Parameter von 1 bis 4. Ein schneller Test mit Python und dem Paket `python-osc`: `SimpleUDPClient("127.0.0.1", 9010).send_message("/nyrex/master/level", 0.5)`.

### OSC-Ausgabe

Einstellungen, Steuerung, OSC-Ausgabe, OSC-Daten senden anhaken. NyrexVJ sendet dann an `127.0.0.1` Port 9011, 60 Mal pro Sekunde (Adresse, Port und Rate lassen sich ändern, erlaubt sind nur IPv4-Adressen und `localhost`). Alles beginnt mit `/nyrex/out/`: `hello` (alle 2 s, mit der Schemanummer 1), `audio` (Bass, Mitten, Höhen, Pegel, Kick), `spectrum` (32 Bänder), `clock` (BPM, Beat-Nummer, Position im Beat), `beat` und `onset` (Ereignisse) sowie `param/<Name>` für bis zu 16 eigene Werte, die jeweils einem Regler wie `layer/1/opacity` folgen. Die vollständige Tabelle, die Versionsregeln und die Einrichtung mit Unreal Engine stehen in [docs/UNREAL.md](docs/UNREAL.md) und [ADR 0008](docs/adr/0008-osc-output-protocol.md) (beide englisch).

## Sprachen

Englisch ist die Standardsprache, Deutsch ist eingebaut. Umschalten unter Einstellungen, Allgemein, unter Hilfe, Sprache oder auf dem Willkommensbildschirm, die Wahl wird gespeichert. Für eine neue Sprache braucht es eine Übersetzungstabelle neben `src/core/i18n_de.cpp` und einen Eintrag in `src/core/i18n.cpp`.

## Auto-Update

NyrexVJ prüft beim Start GitHub (Einstellungen, Updates). Gibt es einen neueren Build, wird er im Hintergrund geladen und vorbereitet, und oben rechts erscheint eine Karte mit Jetzt neu starten und Später. Später verkleinert die Karte zu einem schmalen Hinweis, der bis zum Neustart bleibt. Schaltest du "Updates automatisch herunterladen" aus, gibt es stattdessen einen Installieren-Knopf.

| Kanal | Quelle | Aktualisiert, wenn |
| --- | --- | --- |
| Neuester Build (Standard) | rollender `nightly`-Prerelease | jeder Commit auf `main` |
| Stabil | neuestes GitHub-Release | ein neuer Versions-Tag veröffentlicht wird |

Der Updater funktioniert nur, solange das Repository öffentlich ist.

## Projektstruktur

```
NyrexVJ/
├── src/
│   ├── app/        Dear-ImGui-Oberfläche: Deck, Panels, Browser, Einstellungsfenster, Statusleiste, Update-Karte, Willkommensbildschirm, Theme
│   ├── audio/      WASAPI-Aufnahme und Loopback, FFT-Analyse, Beat- und Tempo-Erkennung
│   ├── control/    OSC-Parser, UDP-Eingang und -Ausgang, OSC-Sender, MIDI-Eingang, Lern-Zuordnung, Steuerziele
│   ├── core/       Kompositionsmodell, Presets, Einstellungen, i18n, Log, SHA-256
│   ├── render/     OpenGL-Engine, Clip- und Effektbibliothek, 3D-Szenen-Player, Ausgabefenster, Spout-Ausgabe und -Eingang
│   ├── scene3d/    glTF/GLB-Lader mit Sicherheitsgrenzen, Szenenmodell, Szenendokumente und .nyxclip-Pakete, eingebaute 3D-Szenen, GLB-Schreiber
│   ├── update/     Update-Prüfung, Installer, Changelog-Parser
│   ├── platform/   Windows- und Ersatz-Plattformcode
│   └── main.cpp
├── tests/          Unit-Tests für Core, Audio, Steuerung, i18n und Update-Logik
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
| `--tab IDX` | Anzuzeigender Browser-Tab (0 Presets, 1 Bibliothek, 2 Audio) |
| `--settings IDX` | Einstellungsfenster auf Seite IDX öffnen (0 bis 8, 4 ist Steuerung) |
| `--osc-port N` | OSC auf Port N empfangen, auch wenn es in den Einstellungen aus ist |
| `--spout-test-pattern` | Einen falschen Spout-Sender namens Test Pattern anbieten (zum Ausprobieren des Spout-Eingangs ohne Windows) |
| `--midi-inject FRAME:STATUS:D1:D2` | Mit `--frames` in diesem Frame eine MIDI-Nachricht an die Zuordnungen schicken |
| `--decks A,B,C` | Deck 1, 2, 3 mit diesen Werks-Looks füllen |
| `--transition NAME` | Übergangsart für Deckwechsel |
| `--transition-seconds S` | Übergangsdauer in Sekunden |
| `--switch-deck FRAME:DECK` | Mit `--frames` bei diesem Frame zu Deck DECK (ab 1) wechseln |
| `--dump-at FRAME:DATEI.png` | Mit `--frames` die Engine-Ausgabe bei diesem Frame speichern |
| `--demo-update ZUSTAND` | Update-Karte als `available`, `downloading`, `ready` oder `later` zeigen |
| `--output` | Ausgabefenster beim Start öffnen |
| `--lang en\|de` | In dieser Sprache starten |
| `--audio-demo` | Demo-Audiosignal benutzen |
| `--welcome` / `--no-welcome` | Willkommensbildschirm erzwingen oder überspringen |
| `--fresh` | Gespeicherte Sitzung ignorieren |
| `--crash-test` | Eine Sekunde nach dem Start absichtlich abstürzen, um die Absturzwiederherstellung auszuprobieren |
| `--no-update-check` | Update-Prüfung überspringen |
| `--check-shaders` | Alle Shader kompilieren und beenden |
| `--bench-scene EBENEN:FRAMES[:CLIP]` | Bildzeiten und Speicher messen, mit EBENEN (1 bis 4) Ebenen, die CLIP spielen, dann beenden |
| `--bench-comp FRAMES` | Die geladene Komposition über FRAMES Bilder messen (CPU-Abgabe, GPU und Gesamt-Bildzeit), dann beenden |
| `--bench-budget MS` | Mit `--bench-comp` mit Code 2 beenden, wenn die mittlere Bildzeit über MS liegt |
| `--bench-all` | Mit `--bench-comp` alle Werks-Looks nacheinander messen |
| `--bench-out DATEI.json` | Mit `--bench-scene` oder `--bench-comp` das Ergebnis zusätzlich als JSON schreiben |
| `--frames N` | N Frames mit festen 60 FPS rendern, dann beenden |
| `--screenshot DATEI.png` | Mit `--frames` das Fenster als PNG speichern |
| `--dump-output DATEI.png` | Mit `--frames` die Engine-Ausgabe als PNG speichern |

Nutzerdaten liegen unter Windows in `%APPDATA%\NyrexVJ` und unter Linux in `~/.config/NyrexVJ`: Log, Fensterlayout, Einstellungen, Autosave, `recovery/` und `presets/`. Mit `NYREXVJ_DATA_DIR` lässt sich ein anderer Ordner wählen.

Absturzschutz: Die Sitzung wird alle 30 Sekunden gespeichert, wenn sie sich geändert hat, und die letzten zehn Autosaves liegen rotierend in `recovery/`. Eine ungespeicherte Szene im Clip Creator wird dort ebenfalls gesichert. Wurde NyrexVJ nicht sauber beendet, lädt der nächste Start die alte Sitzung nicht von selbst (ein defekter Clip könnte sie wieder abstürzen lassen), sondern fragt, von welchem Autosave du weitermachen willst, bietet die ungespeicherte Creator-Szene an und zeigt den Absturzbericht mit dem Ende des Logs für Fehlerberichte.

## Fahrplan

- DMX-Strip-Ausgabe für VRSL
- Eigene Shader-Clips aus Dateien
- Weitere Sprachen

## Software von Dritten

- [GLFW](https://www.glfw.org/) (zlib-Lizenz)
- [Dear ImGui](https://github.com/ocornut/imgui) (MIT)
- [Spout2](https://github.com/leadedge/Spout2) (BSD 2-Clause), nur Windows
- [glad](https://github.com/Dav1dde/glad) generierter OpenGL-Loader (WTFPL oder CC0, dazu Apache-2.0 für die Khronos-Teile)
- [stb_image_write](https://github.com/nothings/stb) (gemeinfrei oder MIT)
- [stb_image](https://github.com/nothings/stb) (gemeinfrei oder MIT), liest PNG- und JPEG-Texturen von 3D-Modellen
- [nlohmann/json](https://github.com/nlohmann/json) (MIT)

## Lizenz

MIT, siehe [LICENSE](LICENSE).
