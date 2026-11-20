# ADR 0011: Render pipeline optimisation: hidden layers, layer resolution, freed pictures and a pass profiler

- **Status:** Accepted (2026-11-20, requested by MarlonXDev: lower processor, graphics and video memory load next to VRChat)
- **Date:** 2026-11-20
- **Owners:** Maintainers (pending assignment)
- **Related:** [PERFORMANCE.md](../PERFORMANCE.md), issue #45

## Context
The renderer already skipped switched-off effects, layers without a clip, gated effects, unused feedback pictures and recompiling of shaders. The request was to check the rest of the pipeline for waste, to keep the picture unchanged by default, to measure before and after, and to say honestly what was gained.

The constraints: C++20, OpenGL 3.3 core, one OpenGL context, RGBA8 render targets, a ping-pong chain per layer (`layerA_`, `layerB_`), fixed-function blending into the composition for the first blend modes and a shader pass for the others, feedback effects with a full-size picture per effect slot, and an author with no graphics card for tests, only a software OpenGL driver whose times are a relative measure.

## Decision
1. **Measure first.** A GPU pass profiler (`GpuProfiler`): one `GL_TIME_ELAPSED` query per pass in a ring of four frames, results read without waiting, off by default and not saved. The pure bookkeeping (smoothing, share, dropping stale rows) is `ProfileTable` in the core library with unit tests. Command line `--profile`, `--bench-shaders`, `--bench-memory` and `--quality` make the numbers reproducible, and `scripts/make_bench_presets.py` writes the scenario presets.
2. **Hidden layers are not drawn.** A layer with an effective opacity of 0.001 or lower (`kHiddenOpacity`) skips its clip, its effects and its blend pass. Exceptions: a layer with an enabled feedback effect is always drawn, and the layer preview of a hidden layer is refreshed every 8th frame so the thumbnail stays alive. Compiling of the clip's shader is not skipped, so a fade-in has no pause. The outgoing deck of a transition follows the same rule.
3. **Layer resolution as a setting.** `renderQuality`: Full (default, 100 %), Balanced (75 %), Performance (50 %), Automatic (a controller, `QualityController`, between 100 % and 50 % in steps of 12.5 %, driven by the GPU time of the whole frame against the frame budget, with hysteresis and cool-down so it cannot flap). Only the clip and the effect chain of each layer are drawn smaller; the composition, master effects, colour and level stage, transition, cue preview and every output stay at full output resolution. While reduced, the full-size layer pictures are freed and smaller ones used, and feedback pictures that no longer match the size are recreated.
4. **Free what is not used.** The unused `transTex_` render target was removed. The two transition pictures are freed 300 frames after the last transition (`IdleRelease`).
5. **Default behaviour stays.** At Full quality the output of every factory look, of the five scenario presets and of three transitions is identical, pixel by pixel, to 0.18.0.

## Evaluated and not done
- **Constant tables instead of per-pixel sine, cosine, root and exponential in the multi-tap effects.** Measured with three runs each: no change beyond the spread (-5 to +4 percent). The compiler folds them. Not changed.
- **Fewer texture reads in the expensive effects (Star Burst, Light Rays, Glow, Image Terrain).** Changes the picture, so it is not a default. Lower resolution is the lever that keeps the effect itself.
- **Per-effect reduced resolution.** Needs a reduce and an enlarge pass and one more picture around every effect. The layer-wide setting costs none of that.
- **Removing the final stage, ping-pong feedback pictures, uniform and state caches, lazy second layer pictures, blackout skipping.** Reasons in [PERFORMANCE.md](../PERFORMANCE.md). None is a measured problem, and each adds memory or risk.
- **Estimating the cost of a composition without hidden layers.** `estimateRenderCost` stays an upper bound; an audio modulation can raise a layer's opacity at any frame.

## Alternatives
1. **Skip hidden layers without refreshing their preview.** Simpler and cheaper, but the layer strip would show a frozen or black thumbnail for a layer the user is about to fade in.
2. **A single global resolution for the composition (render at 75 % and enlarge at the end).** Cheaper still, but the output, Spout and NDI would lose resolution, which is a different feature (output resolution already exists as a setting).
3. **Quality chosen per layer.** More control, more interface, and a layer-wide mix of sizes makes the blend and feedback paths harder to reason about.
4. **Automatic as the default.** The picture could change without the user knowing. Full stays the default and Automatic is a choice.

## Consequences
- Compositions with faded-out layers cost much less (see the table in [PERFORMANCE.md](../PERFORMANCE.md): -68 % with three of four layers hidden), compositions with all layers visible cost the same as before.
- Reduced resolution is a visible trade for users who choose it. Effects that use the pixel grid look coarser, and trails restart once when the resolution changes.
- Hidden layer previews update at about 7.5 frames a second at 60 FPS.
- All numbers come from a software rasteriser. No real graphics card, no VR and no VRChat measurement exists. The first measurements on real hardware (`--bench-shaders`, `--profile`) are the next step and may change the ranking, the 8-frame preview interval and the thresholds of Automatic.
- The settings key `renderQuality` is new and tolerant: out-of-range values are repaired, a missing key means Full.
