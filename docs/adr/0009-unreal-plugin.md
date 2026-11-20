# ADR 0009: Experimental Unreal Engine plugin with Blueprint access

- **Status:** Accepted as experimental (2026-11-20, requested by MarlonXDev in the roadmap work)
- **Date:** 2026-11-20
- **Owners:** Maintainers (pending assignment)
- **Related:** [ADR 0002](0002-unreal-integration.md), [ADR 0008](0008-osc-output-protocol.md), issue #41

## Context
ADR 0002 left a plugin or SDK open until the prototype (Spout in, OSC out) existed and the maintainers approved it. Both channels exist since v0.10.0. MarlonXDev asked for the Unreal plugin and a Blueprint SDK. Unreal already ships an OSC plugin, so the question is what a plugin adds, and how it can be delivered honestly when no Unreal Engine is available to the author.

## Decision
- **Scope.** An optional plugin in `integrations/unreal/NyrexVJ`, not part of the NyrexVJ executable and not built by default for users. It receives OSC schema 1 (ADR 0008) and sends controls to the existing `/nyrex/...` input. Pictures stay with Spout.
- **What it adds over Unreal's OSC plugin.** Typed Blueprint getters and events instead of manual address binding, the connection logic of ADR 0008 (hello heartbeat, five second timeout, bye, schema check), input validation, and no dependency on the OSC plugin.
- **Shape.** One `UGameInstanceSubsystem`, settings in Project Settings, a Blueprint function library for access, a module without editor code.
- **Testable core.** Decoding, encoding, validation and the state machine are plain C++17 without Unreal types in `Private/Core`. NyrexVJ's own build compiles them as `nvj_unreal_core` and tests them against the real sender and parser (`nvj_unreal_core_tests`), including packet mutation and truncation, limits, and the schema rules. Unreal types appear only in the glue.
- **Rules of the core.** Packets up to 65536 bytes, bundle depth 4, 256 messages per packet, 64 arguments per message, strings up to 512 bytes, decoding is all or nothing per packet. Values are clamped to 0 to 1, NaN counts as 0. Only `/nyrex/out/` addresses change state, `hello` needs the application name `NyrexVJ` and an integer schema, an unsupported schema ignores all data, at most 64 parameters. Outgoing addresses must start with `/nyrex/`, may not start with `/nyrex/out/` and are limited to letters, digits, `_`, `-` and `/`.
- **Status label.** Marked beta and experimental. The documentation and the plugin README say plainly that the Unreal-facing files were never compiled with Unreal Engine.
- **Versioning.** The plugin has its own version (0.1.0). It depends on OSC schema 1 and follows ADR 0008: it accepts only schema 1, so a future schema 2 needs a plugin update, and a NyrexVJ that sends schema 2 makes the plugin warn instead of misreading.
- **Security.** Listens on UDP on all adapters because NyrexVJ may run on another computer, the content is validated and bounded. No authentication, as in ADR 0008.

## Alternatives
1. **Documentation only (Unreal's OSC plugin):** no code to maintain, no compile risk, but every user rebuilds the same Blueprint plumbing and connection logic.
2. **Shipping a prebuilt binary:** not possible without Unreal Engine and a Windows build, and binaries per engine version would be a large maintenance load.
3. **Unreal-only code without a host-testable core:** shorter, but nothing about the protocol side could be verified before a user installs it.
4. **Editor-side tooling and nodes:** more convenient, but needs the same unavailable test setup and was left out.

## Consequences
- The protocol part is verified against the real NyrexVJ code on every build. The Unreal glue is not verified, expect compile fixes. The first person with Unreal Engine becomes the real validator, the README asks for reports.
- Every change to the OSC output schema now has a second consumer to keep in step, the unit tests will fail if the sender and the plugin core drift apart.
- The plugin copies a small amount of parsing logic. The alternative, sharing code with the application, would couple the plugin to the NyrexVJ source tree, which an Unreal project does not have.

## Validation
- Done: host tests (sender output into the plugin state, plugin messages through the NyrexVJ parser, the control address helpers against `controlTargetIndex`, limits, 5000 mutated packets), a C++17 build of the core, a Windows MSVC build of the core and its tests in CI.
- Not done: any build with Unreal Engine, any run in Play In Editor, packaged builds, other engine versions, latency measurement, a second network adapter or computer.
