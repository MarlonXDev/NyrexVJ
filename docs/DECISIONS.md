# Architecture decisions

Architecture Decision Records (ADRs) capture why a technical direction is proposed or accepted, including trade-offs and alternatives. They supplement [ARCHITECTURE.md](ARCHITECTURE.md) and [ROADMAP.md](ROADMAP.md).

## Status meanings
- **Proposed**: for discussion only; not permission to implement.
- **Accepted**: explicitly approved by NyrexVJ maintainers.
- **Superseded**: replaced by another ADR; link the replacement.
- **Rejected**: considered but not adopted.

Only maintainers should change a decision to Accepted, Rejected, or Superseded following review. Record the date and relevant GitHub issue/PR when the status changes.

## Decision index

| ID | Topic | Status |
| --- | --- | --- |
| [0001](adr/0001-native-3d-runtime.md) | Native 3D rendering and Clip Creator separation | Accepted (2026-11-18), prototype implemented in v0.6.0 |
| [0002](adr/0002-unreal-integration.md) | Unreal Engine interoperability through Spout and OSC | Accepted for the prototype (2026-11-18), implemented in v0.10.0; plugin follows in ADR 0009 |
| [0003](adr/0003-shared-ai-collaboration.md) | Shared Claude/Codex instructions and review workflow | Proposed |
| [0004](adr/0004-scene-document-and-clip-pack.md) | Scene document, `.nyxclip` pack format and asset library | Accepted (2026-11-18) |
| [0005](adr/0005-pbr-shading.md) | Physically based shading for scene clips | Accepted (2026-11-18) |
| [0006](adr/0006-clip-creator.md) | Clip Creator | Accepted (2026-11-18) |
| [0007](adr/0007-spout-receiver.md) | Spout receiver as input slots | Accepted (2026-11-18) |
| [0008](adr/0008-osc-output-protocol.md) | OSC output and the versioned protocol (schema 1) | Accepted (2026-11-18) |
| [0009](adr/0009-unreal-plugin.md) | Experimental Unreal Engine plugin with Blueprint access | Accepted as experimental (2026-11-20) |
| [0010](adr/0010-video-decode-pipeline.md) | Video decode pipeline: hardware decoding, HAP direct upload, GPU colour conversion, frame rate limit | Accepted (2026-11-20), implemented in v0.18.0 |
| [0011](adr/0011-render-pipeline-optimisation.md) | Render pipeline optimisation: hidden layers, layer resolution, freed pictures, pass profiler | Accepted (2026-11-20), implemented in v0.19.0 |
| [0012](adr/0012-open-source-vj-platform-plan.md) | Plan towards a full open source VJ platform: platforms, media control, live sources, outputs, show sync, node editor, plugins | Accepted (2026-11-20), milestones v0.20.0 to v0.26.0 |
| [0013](adr/0013-media-control.md) | Media control: play head, reverse playback, exact seeking, in and out points, loop modes, beat sync, pre-rolled switching | Accepted (2026-11-20), implemented in v0.21.0 |
| [0014](adr/0014-live-sources.md) | Live sources: NDI input with discovery, cameras and capture cards, reconnecting, a cheap upload path | Accepted (2026-11-20), implemented in v0.22.0 |
| [0015](adr/0015-advanced-output.md) | Advanced output: slices, polygon masks, colour per output, black level in blend zones, warp grids up to 17 x 17, venue setups | Accepted (2026-11-21), implemented in v0.23.0 |
| [0016](adr/0016-show-sync.md) | Show sync: one beat clock, timecode (LTC, MTC), MIDI clock, passive Pro DJ Link, cues, output frame lock | Accepted (2026-11-21), implemented in v0.24.0 |
| [0017](adr/0017-node-effect-editor.md) | Node effect editor: graph document, graph to GLSL compiler, `.nyxfx` files, editor on the ImGui draw list | Accepted (2026-11-21), implemented in v0.25.0 |
| [0018](adr/0018-plugin-system.md) | Plugin system: C interface for modulation and image sources, manifest checked before code, time budget on a thread of its own, crash marker and safe mode | Accepted (2026-11-21), implemented in v0.26.0 |
| [0019](adr/0019-dxv-software-codecs.md) | DXV and other software-only codecs: processor decoder kept, no graphics card attempt, clear message, YCoCg conversion | Accepted (2026-11-21), implemented in v0.26.1 |
| [0020](adr/0020-studio-design-and-more-languages.md) | Studio design as the new default (neutral dark, red-orange accent, rounded style, one-time migration from Orange), Italian, Portuguese, Dutch and Japanese, Japanese through a merged system font | Accepted (2026-11-21), implemented in v0.27.0 |
| [0021](adr/0021-dxv-hap-conversion-and-ffmpeg-setup.md) | DXV videos converted to HAP in the background (variant to format mapping, no HAP Q Alpha, import modes with Ask as default, content keyed cache, hot swap and fallback to DXV), FFmpeg capability check at start and a one-button setup from BtbN builds with checksum and licence checks, never touching existing installations | Accepted (2026-11-21), implemented in v0.28.0 |
| [0022](adr/0022-glsl-shader-2.md) | GLSL Shader 2.0: opt in by `#pragma nyrexvj` (old files byte for byte unchanged), alpha output, Buffer A to D as RGBA16F ping-pong pairs per layer instance, four channels with audio, buffers, images and videos, restart and release rules, a 512 MiB buffer budget with a lower scale before refusing, errors per pass with the line of the person's file | Accepted (2026-11-24), implemented in v0.29.0 |
| [0023](adr/0023-advanced-output-2.md) | Advanced Output 2.0: mapped NDI per output read from the window's back buffer through two pixel buffers (only while a receiver is connected, at most 4096 pixels), Bezier mask edges, freehand strokes simplified to at most 32 points, picture masks that fail closed, colour per slice before the output colour, one 384 MiB output budget with an exact estimate, new JSON keys written only when not default | Accepted (2026-11-24), implemented in v0.30.0 |
| [0024](adr/0024-video-pipeline-audit-and-measurement.md) | GPU video pipeline audit: zero copy stays open (no hardware to build it on), HDR (PQ, HLG) detected and shown without tone mapping, first picture time and upload 95th percentile and maximum added to the monitor and the benchmark JSON, nine colour space test clips compared with FFmpeg through the real OpenGL path, `--video-selftest` in the CI | Accepted (2026-11-24), implemented in v0.31.0 |
| [0025](adr/0025-3d-engine-2.md) | 3D Engine 2.0: skinning and morph targets on the graphics card (joint and delta textures, 128 joints per skin, 32 targets, eight active weights per draw), blended transparency sorted back to front per mesh, Meshopt import on by default and Draco import optional and off by default, file lights and cameras read but not applied, a limit of 640 MiB of video memory per scene, `--scene-selftest` with pixel checks | Accepted (2026-11-24), implemented in v0.32.0 |
| [0026](adr/0026-plugin-system-2.md) | Plugin System 2.0: API 1.x declared stable with 1.1 appended (`has_capability`, `get_limits`, `set_error`, `requires` in the manifest), library and manifest must agree on the version, guarded plugin table, entry function once per run, switch off during loading honoured, limits of 12 and 24 instances, 16 stuck calls and 80 % of a thread, error kinds with text from the plugin, checked types in the settings files; process isolation evaluated with a Linux measurement and **not built** | Accepted (2026-11-24) for what is built in v0.33.0, process isolation Proposed and waiting for a maintainer decision |
| [0027](adr/0027-live-stability-and-hardware-test-matrix.md) | Live stability and hardware tests: `--soak` run in the program with a median based growth verdict, a test matrix as data with a checker and a generated table (states n/a, implemented, automated, hardware), `scripts/measure_live.py` for the outside view, two fault tests (media outage, hard restart); no gates, no telemetry, no hot plug change | Accepted (2026-11-25); pass criteria Proposed |
| [0028](adr/0028-live-recording.md) | Live recording of the final picture: one blit and a fenced read back on the render thread, one encoder thread at background priority (`src/media/video_recorder.*`, no OpenGL, reusable for offline rendering), a bounded frame pool as back pressure (full means dropped and counted), a timeline that follows the wall clock (stalls stay gaps), sound synced to the same clock with a 40 ms tolerance, clean end on a full disk, crash safe MP4 and MOV, nine formats probed at run time; alpha, stereo sound, recording after mapping and hardware encoder presets **not built** | Accepted (2026-11-25); open points Proposed |
| [0029](adr/0029-rest-api.md) | Optional local web interface and REST API (API v1): off by default and on `127.0.0.1` unless remote access is ticked, one background thread with a poll based state machine, reads from an immutable snapshot built by the main thread, writes through a bounded queue run by the main thread through `ControlSystem::fireTarget` (the control ids of OSC, no second logic) with a one second deadline (503 `busy`), a 256 bit token in its own file, Host, Origin and `Sec-Fetch-Site` checks without CORS, a strict parser, rate limits and lockout, a built in page in eight languages under a strict content security policy; TLS, saving presets, composition structure edits, recording control, events and discovery **not built** | Accepted (2026-11-25); open points Proposed |

## New ADR template

Create a numbered file in `docs/adr/` with:

```markdown
# ADR NNNN: Short title

- Status: Proposed
- Date: YYYY-MM-DD
- Owners: Maintainers (pending assignment)
- Related: issue/PR links when available

## Context
What problem are we solving? What existing constraints matter?

## Decision
What direction is proposed or accepted?

## Alternatives
What else was considered and why?

## Consequences
Benefits, costs, risks, and migration requirements.

## Validation / open questions
What experiments or evidence are needed before acceptance?
```
