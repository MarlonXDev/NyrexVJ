# ADR 0007: Spout receiver as input slots

- **Status:** Accepted (2026-11-18, approved by MarlonXDev as part of ADR 0002)
- **Date:** 2026-11-18
- **Owners:** Maintainers (pending assignment)
- **Related:** [ADR 0002](0002-unreal-integration.md), [ADR 0008](0008-osc-output-protocol.md), issue #22

## Context
ADR 0002 proposes Unreal Engine video entering NyrexVJ through Spout. NyrexVJ already sends its output through Spout on Windows. Receiving needs a place in the clip model, a connection lifecycle and a decision on how it behaves on platforms without Spout.

## Decision
- **Input slots, not one clip per sender.** The clip library gets four fixed clips, `Spout Input 1` to `Spout Input 4`. They exist on every platform, so compositions and presets that use them load everywhere, and a slot without a signal shows a quiet placeholder instead of a missing clip.
- **Sender choice per slot.** Each slot stores a sender name in the application settings (not in the composition). An empty name means automatic: the first sender in the system list that is not NyrexVJ's own Spout output. The Clip panel lists the detected senders, lets the user pick one, and shows connection state, size and frame rate.
- **Receiving.** On Windows a slot owns one `Spout` receiver object and one RGBA texture. The texture is resized when the sender changes its size. A frame is only copied when the sender produced a new one.
- **Lifecycle.** A receiver exists only while its slot is shown on a layer of the active or the outgoing deck. It is released three seconds after the last use. If the sender closes, the slot shows the placeholder and reconnects by itself when a sender with the same name (or any sender, in automatic mode) appears again.
- **Drawing.** The received texture is drawn as an opaque RGB image, fitted into the layer without distortion (letterbox or pillarbox in black), and then goes through the usual layer effects, blend mode and opacity.
- **Other platforms.** Spout is Windows only. Elsewhere the receiver reports no senders. For headless tests, `--spout-test-pattern` makes a fake sender called `Test Pattern` appear, which writes a moving test image into the slot texture.
- **Unreal side.** Epic ships no Spout plugin. The documentation names community plugins as examples, says that they are not endorsed and were not tested by the maintainers, and describes only what the NyrexVJ side needs: a sender with a unique name.

## Alternatives
1. **One clip per detected sender:** closer to how some VJ programs show sources, but the library would change while playing, presets would lose their clips when a sender is gone, and the thumbnail grid would jump.
2. **A single "Spout Input" clip:** simplest, but no way to layer two Unreal scenes or a camera source and a render.
3. **NDI as the only transport:** friendlier across computers, but adds latency and a new dependency. It stays an option for a later ADR.
4. **Sharing GPU memory without Spout:** needs a custom protocol for every graphics backend.

## Consequences
- Sender names are an application setting. A composition moved to another computer plays the slot with whatever that computer has chosen.
- The receiver costs one GPU copy per frame and slot while the slot is visible.
- Alpha of the incoming image is ignored. Unreal scenes that rely on transparency need a background colour or a later mask feature.
- Windows is the only platform where the real receiver runs. The code compiles with MinGW in the checks here and in CI, but it has not been run against a real Spout sender by the author of this change.

## Validation
Headless runs on Linux with the test pattern cover the library entry, the slot drawing, the aspect fit, the placeholder and the status display. The real Spout path is checked by the Windows CI build only. A test with a real Unreal sender and a real GPU is still open and belongs to the maintainer.
