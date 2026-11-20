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
