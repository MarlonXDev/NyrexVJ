# ADR 0003: Shared AI contributor instructions and review workflow

- **Status:** Accepted (2026-11-18)
- **Date:** 2026-11-18
- **Owners:** MarlonXDev (maintainer)
- **Related:** [AGENTS.md](../../AGENTS.md), [CLAUDE.md](../../CLAUDE.md), [CONTRIBUTING.md](../../CONTRIBUTING.md), issues #26 and #27

## Context
NyrexVJ may receive contributions from a developer using Claude and another collaborator using Codex. Different chat contexts and local workspaces can lead to contradictory assumptions, duplicated work, and accidental changes to shared branches.

## Decision
- Root `AGENTS.md` is the shared instruction set for every AI assistant. `CLAUDE.md` points to it. Tool-specific exceptions go into the tool's own file, stay narrow and never loosen a common rule.
- `CONTRIBUTING.md` stays authoritative for build, style, localization, shader and changelog rules. `AGENTS.md` links to it instead of repeating it.
- Scope, ownership, acceptance criteria and design discussion live in GitHub Issues and pull requests, not in chat.
- Every task has an issue and its own branch or git worktree. Claiming an issue means assigning it or commenting with the branch name, and setting the `in progress` label. Nobody edits files that another open branch is changing without agreeing in the issue first.
- Roadmap ideas and Proposed ADRs are unapproved until a maintainer accepts them in an issue, pull request or decision record. The `approved` label marks an accepted issue.
- Status labels are `proposal`, `approved`, `in progress` and `done`. `ai-authored` marks issues and pull requests written with an AI assistant. A status is never inferred from a version number.
- AI-authored changes get a human review before they are merged. Exception: the repository owner may allow an assistant in a session that the owner directs to merge that assistant's own pull requests once the checks pass. This permission belongs to the owner and that session, is never inferred, and does not extend to assistants working for other contributors.
- Pull requests that change `AGENTS.md`, `CLAUDE.md`, `CONTRIBUTING.md` or the status of an ADR are always merged by a human.

## Alternatives
1. **Separate, independent agent instruction files:** tool-specific flexibility, but rules may diverge.
2. **Chat-only coordination:** convenient but not durable or accessible across contributors.
3. **One large project instruction document:** centralized but difficult to maintain as decisions evolve.

## Consequences
- Easier handoff and review across AI tools and human contributors.
- Requires keeping shared instructions, labels and issues current.
- Agent-specific exceptions stay narrow and never contradict the common rules.
- The owner's merge permission is a documented exception, so the rule and the practice no longer disagree.

## Validation
- Claude Code loads `CLAUDE.md` from the repository root. Confirmed on 2026-11-18, the file was injected at the start of the session.
- Codex and other assistants read `AGENTS.md` from the repository root by convention. This was not verified from this repository.
- Labels `proposal`, `in progress`, `done` and `ai-authored` were created next to the existing `approved` ([#27](https://github.com/MarlonXDev/NyrexVJ/issues/27)).
- The maintainer approved the work on 2026-11-18. The decision takes effect when the maintainer merges the pull request that carries this change.
