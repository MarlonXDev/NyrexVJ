# Änderungsprotokoll

## 0.5.0 - 2026-11-18

- Live-Steuerung per OSC: Schalte sie unter Einstellungen, Steuerung ein (standardmäßig aus, Port 9010, nur dieser Computer, außer du erlaubst andere). NyrexVJ reagiert dann auf 207 Adressen unter /nyrex/ für den Master, die 16 Decks, die 8 Spalten, jeden Clip, Deckkraft, Tempo, Größe und Farbe jeder Ebene, Bypass, Solo, Leeren sowie alle Effektschalter und Parameter. Sende einen Wert von 0 bis 1 oder gar nichts, um einen Auslöser zu feuern. Das klappt mit TouchDesigner, Max, Pure Data, Python-Skripten und allem anderen, das OSC spricht.
- MIDI-Controller-Zuordnung unter Windows: Wähle deinen Controller, drücke Zuordnung hinzufügen, wähle was gesteuert wird und bewege einen Regler oder drücke eine Taste, um beides zu verbinden. Fader und Regler setzen Werte, Tasten lösen Clips, Spalten und Decks aus oder schalten Dinge an und aus, und Invertieren dreht einen Fader um. Zuordnungen werden in controls.json gespeichert.
- Neue Seite Steuerung im Einstellungen-Fenster mit dem Status von OSC und MIDI, der zuletzt eingegangenen Nachricht, der Zuordnungsliste mit Lernen und Entfernen und einer durchsuchbaren Liste aller OSC-Adressen, die sich mit einem Klick kopieren lassen.

## 0.4.0 - 2026-11-18

- Decks: Richte bis zu 16 Decks mit eigenen Ebenen, Clips, Farben und Effekten ein und wechsle mit einem weichen Übergang statt eines harten Schnitts. Sieben Übergänge: Ausblenden, Auflösen, Zoom, Wischen, Kreis, Rotation und Schieben, mit Länge in Sekunden oder in Beats und optionalem Auto-Wechsel. Klicke die Deck-Knöpfe über dem Clip-Raster, drücke Bild auf und Bild ab oder nutze Strg und 1 bis 8 (mit Umschalt für Deck 9 bis 16). Per Rechtsklick auf ein Deck kannst du es umbenennen, kopieren, füllen oder leeren. Alle Decks werden in Presets gespeichert.
- 3 neue Clips nach Vorbild von Resolume-Looks: Milky Wave (fließende Konturlinien), Crystal Cavern (dunkler gespiegelter Kristall-Tunnel) und Crystal Lines (derselbe Tunnel als weiße Linienzeichnung). Alle bewegen sich zur Musik.
- 2 neue Effekte, insgesamt 58: Linienbild macht aus jedem Bild leuchtende Umrisse, Spiegelfaltung spiegelt es zu Kristallformen.
- Effekte können jetzt nur auf Audio reagieren: Stelle einen Effekt von Parameter auf Gate, dann erscheint er nur, solange Bass, Mitten, Höhen, Pegel oder Kick laut sind, und blendet sich bei Stille aus.
- 3 neue fertige Presets, insgesamt 18: Milky Red, Crystal Cavern und Crystal Lines. Ein Werks-Look wird jetzt in das aktuelle Deck geladen, und ein Knopf füllt alle 16 Decks mit Werks-Looks.
- Neues Einstellungen-Fenster über Einstellungen im oberen Menü (oder Strg und Komma) mit Allgemein, Audio, Ausgabe, Updates, Oberfläche, Tastenkürzel und Info. Ausgabefenster, Spout, Auflösung, VSync, Audio-Analyse, Sprache und Oberflächengröße findest du jetzt dort.
- Updates: NyrexVJ sucht bei jedem Start nach einer neuen Version und bereitet sie im Hintergrund vor. Oben rechts erscheint eine kleine Karte mit Jetzt neu starten oder Später und bleibt als schmale Anzeige sichtbar, bis du neu startest. Der automatische Download lässt sich ausschalten.
- Übersichtlichere Oberfläche: eine Statusleiste mit FPS, Tempo, Deck, Audio und Spout, einklappbare Bereiche in den Ebenen- und Master-Fenstern, Tooltips und eine einstellbare Oberflächengröße. Der Audio-Tab warnt, wenn das gewählte Gerät keinen Ton liefert.

## 0.3.0 - 2026-11-18

- Audio-Reaktivität: Wähle im neuen Audio-Tab Systemaudio (zum Beispiel dein DJ-Programm), ein Mikrofon oder Line-In oder ein Demo-Signal. Live-Spektrum, Pegelanzeigen für Bass, Mitten, Höhen und Gesamtpegel, Beat- und Tempoerkennung und eine optionale BPM-Synchronisation.
- Ebenen, Master und jeder Effekt können Bass, Mitten, Höhen, Pegel oder Kick folgen: Deckkraft, Skalierung, Geschwindigkeit, Farbton, Helligkeit, Sättigung und jeden Effektparameter.
- Spout-Ausgabe: Schicke das Bild direkt an OBS, unter einem Sendernamen deiner Wahl. Füge in OBS die Quelle "Spout2 Capture" hinzu und wähle den Sender aus. Nur unter Windows.
- Willkommensbildschirm beim ersten Start und nach jedem Update, mit allem, was neu ist. Jederzeit wieder erreichbar über Hilfe, Was ist neu.
- Oberfläche auf Englisch und Deutsch. Englisch ist die Standardsprache, umschalten geht jederzeit unter Hilfe, Sprache, im Optionen-Tab oder auf dem Willkommensbildschirm.
- 16 neue Clips: Mandelbrot, Metaballs, Voronoi, Lightning, Fire, Rings Burst, Hex Grid, Warp Speed, Oscilloscope, Spectrum Circle, Liquid Marble, Cube Tunnel 3D, Gyroid 3D, Terrain 3D, Sunburst, Digital Rain. Bars und Equalizer zeigen jetzt das echte Spektrum.
- 33 neue Effekte, insgesamt 56: Radial Blur, Light Rays, Chromatic Aberration, VHS, CRT, Halftone, Hex Mosaic, Slice Shift, Shockwave, Ripple, Polar Coordinates, Infinite Zoom, Flow Displace, Duotone, Solarize, Neon Edge, Sharpen, Camera Shake, Film Grain, Threshold, Color Echo, Spectrum Warp, Oscilloscope, Pixel Smear, Anamorphic Streak, Dither, Block Glitch, Prism und die 3D-Effekte Sphere Map, Cube Spin, Cylinder Wrap, Floor Reflect und Plane 3D.
- 7 neue fertige Presets, insgesamt 15, mehrere davon audio-reaktiv: Audio Reactor, Fractal Dream, Cyber Rain, Inferno, 3D Worlds, Crystal Cube, Pop Art.
- Shader werden erst beim ersten Gebrauch kompiliert und die Clip-Vorschaubilder erscheinen nacheinander, dadurch startet das Programm deutlich schneller.
- Aufgeräumtes Repository: Der Quellcode ist in app, audio, core, render, update und platform aufgeteilt, das Programm hat ein Icon und es gibt neue Tests für Audioanalyse, Sprachen und das Änderungsprotokoll.

## 0.2.0 - 2026-11-18

- Auto-Updater: NyrexVJ prüft GitHub auf neue Builds und installiert sie nach einer Prüfsummen-Kontrolle. Zwei Kanäle: neuester Build (jeder Commit) und stabile Releases.
- Presets: eigene Looks speichern, laden und löschen. Acht fertige Presets. Die letzte Sitzung wird beim Start wiederhergestellt.
- 8 neue Clips, fünf davon in 3D oder Pseudo-3D: Cube Field 3D, Torus 3D, Sphere Grid 3D, Synthwave, Starfield, Laser Fan, Equalizer, Julia.
- 23 Effekte mit vier Slots pro Layer und im Master, darunter die 3D-Effekte Perspective Plane, Tunnel Map und Fisheye.
- Farbeinstellungen pro Layer: zwei Farben, Regenbogen-Modus, Paletten, Tempo und Größe, dazu Farbton, Sättigung, Helligkeit und Kontrast pro Layer und im Master.
- Clip-Vorschaubilder und Live-Vorschau der Layer im Deck.
- Eigenes Ausgabefenster mit Vollbild und Monitorwahl (F11), nutzbar mit der OBS-Fensteraufnahme.
- Auflösungen von 960x540 bis 2560x1440, Tap-Tempo, BPM halbieren und verdoppeln, Blackout.
- Unit-Tests für Presets, Versionen, Prüfsummen und den Update-Ablauf.
- Release-Workflow: Getaggte Builds veröffentlichen NyrexVJ.exe, eine Zip-Datei und Prüfsummen als GitHub-Release, jeder Push auf main veröffentlicht einen Nightly-Build.

## 0.1.0

- Erstes Grundgerüst: vier Layer, acht Spalten, acht Clips, Master-Effekte, andockbares Layout.
