# NyrexVJ product vision

> **Status: proposal for maintainer review.** This document records ideas discussed by contributors; it does not replace decisions by MarlonXDev.

## Goal
Build a free, open-source, performant VJ application that can grow into a capable alternative to Resolume Arena, while preserving NyrexVJ's approachable, native, lightweight workflow and its existing use for VRChat events and live performances.

## Product principles
- **Live first:** low latency, stable frame pacing, fast recovery, and reliable output matter more than feature count.
- **Composable:** video, images, generative shaders, 3D scenes, and external live sources should eventually behave as clips in the same layer/effect system.
- **Audio reactive:** BPM, beats, spectrum bands, and modulation should work across all relevant clip and effect types.
- **Creator friendly:** an integrated, focused 3D Clip Creator can make motion graphics accessible without becoming a general-purpose 3D modeling suite.
- **Interoperable:** open control and video interfaces should connect tools such as Unreal Engine, OBS, and other live production applications.
- **Open development:** retain the project's MIT license where compatible with third-party dependencies; document exceptions and packaging constraints.
- **Incremental:** ship useful, tested improvements without destabilizing existing shows or presets.

## Non-goals for now
- Embedding Unreal Engine directly into the NyrexVJ process.
- Recreating all of Blender, Unreal, or Resolume at once.
- Promising feature release dates before architecture, performance, and maintenance costs are evaluated.

## Collaboration
The repository owner and maintainers approve scope. Contributors may propose and refine ideas through issues and pull requests. Claude and Codex should share the same repository documents rather than relying on separate chat histories.
