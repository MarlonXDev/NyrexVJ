# ADR 0002: Unreal interoperability through Spout and OSC

- **Status:** Proposed
- **Date:** 2026-11-18
- **Owners:** Maintainers (pending assignment)
- **Related:** [Architecture](../ARCHITECTURE.md), [Roadmap](../ROADMAP.md)

## Context
Creators may want Unreal Engine visuals inside NyrexVJ while retaining the application's low-latency live workflow. NyrexVJ currently documents Spout **output**; receiving external Spout sources is a proposed addition. Unreal typically uses a different rendering backend, so GPU interop and synchronization must be proven.

## Proposed decision
- Run Unreal Engine and NyrexVJ as separate applications.
- Prototype Unreal video output -> Spout sender -> NyrexVJ Spout receiver.
- Prototype OSC for tempo, beats, spectrum bands, levels, and named parameter control.
- If the prototype is viable, design an optional Unreal plugin/SDK with Blueprint-accessible parameter and event nodes.
- Version the OSC message schema and document discovery, connection lifecycle, and fallback behavior before publishing a stable SDK.

This ADR does **not** assert that a working Unreal sender, receiver, or SDK already exists.

## Alternatives
1. **Embed Unreal into NyrexVJ:** high integration complexity, distribution size, and runtime risk.
2. **NDI-only transport:** network-friendly, but may increase latency and deployment complexity compared with local GPU sharing.
3. **Video file export:** reliable for pre-rendered loops, but not interactive.
4. **Custom GPU interop protocol:** potentially flexible, but increases maintenance and platform complexity.

## Consequences
- Clear process isolation and independent Unreal project workflows.
- Additional dependency on Spout interoperability, GPU adapter compatibility, and synchronization behavior.
- OSC requires naming, typing, rate limiting, error handling, and potentially security decisions.
- Unreal plugin versions may require ongoing compatibility maintenance.

## Validation / open questions
- Identify a reliable Unreal-to-Spout sender path and test it against NyrexVJ's OpenGL context.
- Test sender disconnect/reconnect, mismatched resolutions, alpha/color space, GPU adapters, and frame pacing.
- Define a small proof-of-concept OSC schema and measure control latency.
- Confirm plugin licensing and supported Unreal versions.
- Get maintainer approval before promising a public SDK.

## Implementation note (2026-11-18)
MarlonXDev asked to continue with this ADR after ADR 0001. The prototype part is implemented in v0.10.0: the Spout receiver ([ADR 0007](0007-spout-receiver.md)) and OSC output with a versioned protocol ([ADR 0008](0008-osc-output-protocol.md)), documented in [UNREAL.md](../UNREAL.md). Not done: an Unreal plugin or SDK, because nothing in it could be built or checked without Unreal Engine, and no public SDK is promised without maintainer approval. Open for the maintainer: a test with a real Unreal sender on Windows, latency and frame pacing measurements, and the choice of supported Unreal versions.
