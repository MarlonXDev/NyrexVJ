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
| [0001](adr/0001-native-3d-runtime.md) | Native 3D rendering and Clip Creator separation | Accepted (2026-11-18) |
| [0002](adr/0002-unreal-integration.md) | Unreal Engine interoperability through Spout and OSC | Proposed |
| [0003](adr/0003-shared-ai-collaboration.md) | Shared Claude/Codex instructions and review workflow | Proposed |

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
