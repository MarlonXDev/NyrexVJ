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
