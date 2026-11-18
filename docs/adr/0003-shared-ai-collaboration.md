# ADR 0003: Shared AI contributor instructions and review workflow

- **Status:** Proposed
- **Date:** 2026-11-18
- **Owners:** Maintainers (pending assignment)
- **Related:** [AGENTS.md](../../AGENTS.md), [CLAUDE.md](../../CLAUDE.md), [CONTRIBUTING.md](../../CONTRIBUTING.md)

## Context
NyrexVJ may receive contributions from a developer using Claude and another collaborator using Codex. Different chat contexts and local workspaces can lead to contradictory assumptions, duplicated work, and accidental changes to shared branches.

## Proposed decision
- Use root `AGENTS.md` as shared AI instructions, with `CLAUDE.md` pointing to it.
- Keep `CONTRIBUTING.md` authoritative for existing build, style, localization, shader, and changelog rules.
- Track scope, ownership, acceptance criteria, and design discussion in GitHub Issues/PRs.
- Use separate branches/worktrees per task; avoid simultaneous edits to the same files without coordination.
- Treat roadmap ideas and proposed ADRs as unapproved until maintainers explicitly accept them.
- Require human review before merging AI-authored changes.

## Alternatives
1. **Separate, independent agent instruction files:** tool-specific flexibility, but rules may diverge.
2. **Chat-only coordination:** convenient but not durable or accessible across contributors.
3. **One large project instruction document:** centralized but difficult to maintain as decisions evolve.

## Consequences
- Easier handoff and review across AI tools and human contributors.
- Requires keeping shared instructions and issues current.
- Agent-specific exceptions should be narrowly documented without contradicting common rules.

## Validation / open questions
- Confirm both tools load their intended instruction files.
- Agree on issue labels, ownership, and PR review policy.
- Confirm maintainers want this workflow before marking the ADR Accepted.
