# AI contributor instructions

These instructions apply to Codex, Claude, and other AI coding assistants working in NyrexVJ.

## Read before making changes
1. Read `README.md`, `CONTRIBUTING.md`, and the relevant source files.
2. Read `docs/VISION.md`, `docs/ROADMAP.md`, and `docs/ARCHITECTURE.md` when planning features or changing architecture.
3. Check relevant GitHub Issues and pull requests for ownership, scope, and ongoing work. Do not assume an idea is approved because it appears in the roadmap.

## Source of truth and decision making
- The existing code and tests describe current behavior. The README describes released behavior. The roadmap describes proposals, not implementation guarantees.
- Respect the repository owner's decisions. Propose substantial architecture changes in an issue or pull request before implementing them.
- Do not silently change the project's scope, license, platform requirements, or dependencies.
- Coordinate parallel work using separate branches or worktrees and scoped issues. Never assume another AI agent knows about uncommitted work.
- Avoid editing another contributor's branch or rebasing/force-pushing shared branches without explicit agreement.

## Engineering constraints
- Preserve the native C++20, OpenGL, Dear ImGui, CMake architecture unless an approved decision changes it.
- Prioritize low-latency live performance, stability, predictable memory use, and graceful failure.
- Keep the application usable during content editing; avoid blocking rendering on disk, decoding, network, or asset import.
- Keep platform-specific code isolated with fallbacks, as required by `CONTRIBUTING.md`.
- Follow existing conventions for translations, shader registration, tests, and changelogs. Do not duplicate or override `CONTRIBUTING.md`.
- Add dependencies only after considering licenses, packaging size, compatibility, and maintenance.

## Workflow
1. Define a small, testable scope and acceptance criteria in an issue or PR.
2. Inspect relevant code before proposing a design. Mark unknowns explicitly.
3. Implement in a feature branch; do not commit directly to `main`.
4. Run the relevant build, tests, translation checker, and shader checker when possible; report commands and any tests that could not run.
5. Update user-facing docs and both changelogs when required by `CONTRIBUTING.md`.
6. Open a PR explaining the change, risks, tests, and linked issue. Wait for human review before merging.

## Roadmap statuses
- **Existing**: documented in the current application or code.
- **Proposed**: an idea under discussion, not authorized for implementation.
- **Approved**: explicitly accepted by the maintainers in an issue, PR, or decision record.
- **In progress / Done**: tracked in GitHub Issues and PRs, not inferred from a version label.

If instructions conflict, ask maintainers rather than silently choosing.
