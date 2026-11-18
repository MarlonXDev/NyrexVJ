# Änderungsprotokoll

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
