# ADR 0049: Optional person detection from a live camera picture (mask and 2D tracking), a backend interface, and a background difference method without any model

- **Status:** Accepted for the automated part (requested by MarlonXDev as prompt 28). Issue [#126](https://github.com/MarlonXDev/NyrexVJ/issues/126). **No model, no model runtime and no download were chosen or added**: the prompt says that a model and an inference backend are decided only after the maintainer approves them. Nothing was run with a real camera, a real graphics card, Windows or next to VRChat.
- **Date:** 2026-11-26
- **Owners:** Maintainers (pending assignment)
- **Related:** [PERSON.md](../PERSON.md), [MODULATION.md](../MODULATION.md), [LIVE.md](../LIVE.md), [INTERFACES.md](../INTERFACES.md), [KNOWN_ISSUES.md](../KNOWN_ISSUES.md), [TESTING.md](../TESTING.md), [ADR 0047](0047-optical-flow.md), [ADR 0048](0048-network-stream-inputs.md)

## Context
The prompt asked for an optional module for person segmentation, background removal and 2D tracking from camera and NDI pictures, under strict rules: look at the existing inputs, masks and effects first, make the module optional and off by default, choose a model and an inference backend only after the maintainer approves them, work offline, protect privacy, document licences and the origin of any model, never download a model automatically, set budgets for the processor and the graphics card, run the analysis at a reduced rate with tracking in between, have a fallback without AI, test alpha edges, temporal stability, several people, occlusion, light changes and camera loss, feed the result as a mask and as a control signal into the existing clip, effect and modulation paths, reuse the masks and the graphics diagnostics, and send no telemetry.

## Findings (what existed before)
- **Existed:** live pictures from cameras, NDI and (since 0.55.0) network streams, all through `LiveInput` (`src/render/live_input.cpp:75`) with a state machine for loss and reconnect; a mask type (`MaskBitmap`, `src/core/output_map.hpp:63`) and a loader for mask images (`src/core/mask_images.cpp`) used for output masks; an effect chain with a fixed header and footer (`src/render/effect_library.cpp:47`, `:114`); modulation sources with plugin bands appended at the end of the list (`src/core/composition.hpp:57`, `src/core/modulation.cpp:86`); a graphics memory report (`Engine::gpuMemory`, `src/render/engine.cpp:1564`); a worker thread pattern with a governor (optical flow, [ADR 0047](0047-optical-flow.md)).
- **Not found:** any code that finds people, any mask that comes from a live picture, any source of position or size, any model file or inference runtime in the repository or in [DEPENDENCIES.md](../DEPENDENCIES.md).
- **Not verified:** how any method behaves on a real camera. The tests use computer-made pictures.
- The table with code references is in [PERSON.md](../PERSON.md).

## Decision
1. **One interface, one method.** `person::Backend` (`src/core/person_mask.hpp`) gets a small RGB picture and returns a score per pixel plus a `Provenance` (id, title, licence, origin, whether it needs a model, whether it uses the network, whether it is deterministic). The only implementation is `DifferenceBackend`, a background difference with a learned picture of the empty scene. It is the "fallback without AI" of the prompt and, at this point, the only method. A model backend can be added later behind `makeBackend` without changing anything after it.
2. **No model is chosen.** Choosing a model means choosing its licence, its origin, its runtime (a new dependency), how the file reaches the machine and what it costs on real hardware. Those are the maintainer's decisions. The module neither bundles nor downloads anything, and the settings page and the document say so.
3. **Off by default and invisible when off.** `personMode` is `off` by default. While it is off no session, worker thread, texture or observer exists, the effect returns the picture unchanged and the six sources read 0. Settings keys are written only when they differ from the default.
4. **Everything after the backend is shared:** a stabiliser (softness and stability), connected blobs, a tracker with numbers for up to 8 people (hits needed before a person counts, coasting, merge handling), a signal filter for the biggest tracked person, a worker thread at background priority that keeps only the newest picture, a governor, and a session that owns the mask texture.
5. **Where the picture is taken.** `LiveInput::update` hands every frame it takes to an optional `LiveFrameObserver` before it uploads it. The session reduces the frame on the render thread (0.2 to 0.6 ms) at the chosen rate and gives the small copy to the worker. The live picture and the clip are not changed.
6. **Budget and fallback.** The analysis may use 25 percent of one analysis interval. A governor lowers the tier, then the rate, and switches the module off at the lowest setting, with the reason on the page. A late analysis never blocks the render thread: it shows the last result.
7. **Control paths.** The result reaches the program in two ways that already exist: as a texture for an ordinary effect (**Person Key**, bound on texture unit 3 for every effect pass; no change to the clip shaders, the layer compositing or the master) and as six modulation sources appended after the plugin sources. No new routing, no new file format and no preset key were added; presets store the source name.
8. **No transparency.** The compositor keeps opaque pictures opaque (the effect footer keeps the alpha of the picture). `Person Key` therefore shows the person on a backdrop colour, and the person is laid over other layers with the layer blend modes. A true alpha path would change the compositing for every show and was not built.
9. **Privacy.** The module keeps no picture beyond the newest small copy, writes nothing to disk, opens no socket and contains no network code. The page shows the method and states "no model, no download, no network, nothing is sent anywhere".
10. **Reuse.** The mask is a `MaskBitmap`; the blobs, the memory report (`Engine::gpuMemory`, live inputs) and the worker and governor pattern follow the existing code. The mask texture is counted in the graphics memory report.

## Alternatives considered
- **Choose a model now (for example a small segmentation network with ONNX Runtime).** Needs a licence, a runtime dependency, a distribution path and measurements on real hardware, none of which exist. Rejected for this version by the prompt's own rule.
- **Use a hardware or driver feature (NVIDIA Broadcast, Windows Studio Effects).** Not portable, not controllable from the program, and not verifiable here. Rejected.
- **Chroma key (green screen) instead of background difference.** A simpler method for a green screen and a good addition later; it needs a prepared set, so it does not answer "find a person in any room". Not built.
- **Analysis on the graphics card.** The analysis costs about 0.5 ms on the processor at the default size; nothing showed that it needs the graphics card. Rejected.
- **A transparent output through the compositor.** Would change the blending of every layer and the output formats. Rejected for now; see decision 8.
- **Following the person with the layer (cropping, zoom to the person).** A consumer of the sources, not part of the module; users can map the sources to layer parameters today.

## Consequences
- With the module off the program behaves as at 0.55.0; the additions are one effect in the library (`Person Key`), six appended source names (hidden in the source lists while the module is off), seven optional settings keys and two lines of cost: a branch on a null observer in `LiveInput::update` and a branch on a null session in the effect pass.
- The method finds anything that differs from the learned background. It does not know people, it needs a fixed camera and an empty scene while it learns, and it is fooled by changing projections and by a person who stood still for minutes ([KI-24](../KNOWN_ISSUES.md), [KI-25](../KNOWN_ISSUES.md)).
- New tests run in the continuous build: `nvj_person_tests` (3,341 checks) and `NyrexVJ --person-selftest` (25 checks).

## Validation and open questions
- Run with real cameras, a real graphics card, Windows and next to VRChat; manual tests `M-PERSON-1` to `M-PERSON-4` in [TESTING.md](../TESTING.md).
- Decide whether a model backend is wanted, which one, under which licence and runtime, how its file is delivered without an automatic download, and which hardware the budget is measured on.
- Decide whether a chroma key backend, a transparent output path, a mask that follows layer scale and position, or a picture-in-picture crop that follows the person should be built.
