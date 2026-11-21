# ADR 0015: Advanced output: slices, masks, colour per output, black level, large warp grids and venue setups

- **Status:** Accepted (2026-11-21) as milestone 4 of [ADR 0012](0012-open-source-vj-platform-plan.md), implemented in v0.23.0. Issue [#50](https://github.com/MarlonXDev/NyrexVJ/issues/50).
- **Date:** 2026-11-21
- **Owners:** Maintainers (pending assignment)
- **Related:** [ADR 0011](0011-render-pipeline-optimisation.md), [OUTPUTS.md](../OUTPUTS.md), [PERFORMANCE.md](../PERFORMANCE.md)

## Context
Version 0.18 gave every output window one picture area, four corner handles, a warp grid up to 9 x 9, edge blending and a brightness. For a real venue that is not enough: a stage often has several surfaces fed from one output, surfaces that are not rectangles (a pillar, a round screen, a set piece), projectors whose colour and black level differ, and a layout that has to be rebuilt in every venue.

Constraints that shaped the design:
- the output window runs next to VRChat, so the default case (one rectangle) must stay as cheap as before and use no extra memory, and every new feature must cost nothing while it is not used,
- old `settings.json` files must keep working, and the output of an unchanged setup must stay pixel identical,
- no new dependency, GCC 11 and the Windows toolchain must compile it, and the logic must be testable without a window,
- a damaged or foreign file must never crash the program or change the outputs.

## Decision
1. **Slices.** `OutputMapping` stays "one slice" (source rectangle, corners, warp grid, edge blend, brightness, flips, fill) and gains a `SliceMask`. `OutputSpec` keeps `map` as slice 1 and adds `std::vector<OutputSlice> slices` for up to seven more (`kMaxSlices` = 8 in total). Keeping `map` as the first slice means that every old settings file, every call site and every test that uses `spec.map` keeps working, and the JSON format only grows: the extra slices are an optional `slices` array. Slices are drawn in order, later ones on top, with alpha blending only from the second slice on or when a slice has a mask, so a single plain slice still draws with blending off. The first slice defines the window frame (fill, letterbox); all slices share it.
2. **Masks as polygons in output space** (`SliceMask`: 3 to 32 points, a soft edge of up to 25 % of the picture height, invert). The polygon is rasterised on the processor into an 8 bit coverage image (`buildMaskImage`: four sub-scanlines per row with exact horizontal coverage, even-odd fill, then three box blurs for the soft edge, invert baked in) and sampled as alpha in the output fragment shader. It is built once when the mask changes. A hard mask uses 1024 x 576, a soft mask 512 x 288, because the blur hides the detail and the build is about fourteen times cheaper (2.1 ms instead of 30 ms measured). A slice without a mask has no texture. The mask space is the same as that of the corners and the warp, so a mask follows the surface and not the picture.
3. **Colour per output in the existing shader** (`OutputColor`: gain per channel, saturation, contrast, gamma, black lift). One uniform switches the whole block off while it is at its default, so there is no extra pass and no extra texture. The CPU mirror `applyColor` defines the order and is what the self test compares against. The colour applies to the picture but not to the test pattern, because the pattern is for lining up geometry.
4. **Black level in blend zones** (`EdgeBlend::blackLevel`, 0 to 30 %). Two overlapping projectors add their black, so the overlap is lighter. The shader lifts everything outside the blend zones by `level * (1 - zone)` and scales the picture into the remaining range (`out = l + (1 - l) * c`); the zone border is antialiased with the screen-space derivative. It only acts on a slice that has an edge blend.
5. **Warp grids up to 17 x 17.** The mesh grows with the grid: `meshDivisionsFor` keeps 32 divisions up to 9 points (unchanged output) and uses four cells per grid cell above that, at most 128. Vertex and index buffers are per slice and are rebuilt only when the grid size or the mapping changes.
6. **Per-slice GPU resources, created on demand.** A slice has its own vertex array, buffers and, if masked, texture; they are created when a slice is drawn and deleted when it is hidden, removed or has no mask. `OutputManager::gpuBytes` reports the total, and the self test checks that it returns to its start value (42 KB for one default slice).
7. **Venue setups** (`src/core/venue_setup.{hpp,cpp}`). A venue is a JSON file `{"format": "nyrexvj-venue", "version": 1, "name", "outputs": [...]}` in `venues/` of the user data folder; `outputs` are the same objects as in `settings.json` (`outputToJson`), including display, fullscreen and enabled. Loading refuses a wrong format and a newer version, limits the size to 2 MiB, accepts missing entries (defaults), forces output 1 on, and returns an error string instead of throwing. Names are cleaned (no separators or reserved characters, Windows device names get an underscore, at most 60 characters) and paths go through UTF-8 conversions so names with umlauts work on Windows. The functions take the directory as an argument, so the tests use a scratch folder. The UI lists, saves, loads, duplicates, deletes (with a question), exports, imports and takes a dropped `.json` file; `--venue NAME|PATH` loads one at start. Loading applies the specs, closes windows that became disabled and gives open windows the fullscreen state, but does not open windows by itself.
8. **A real output window test.** `--output-selftest` (runs in CI on Linux under a virtual display) renders a synthetic picture with four coloured quadrants through the real `OutputManager` and checks pixels read back with `capture`: 64 checks, plus timings and memory numbers in the log.

## Alternatives
- **Make every slice an output of its own.** Rejected: outputs are windows, and four windows is already the practical limit. Several surfaces on one projector or one capture card need several slices in one window.
- **Masks as images the user loads.** Rejected for this milestone: a polygon is editable on the canvas, small to store in a venue file and needs no file management. An image mask would suit organic shapes and stays possible, because the shader only needs a coverage texture.
- **Masks in the render targets of the engine, before mapping.** Rejected: a mask on a surface belongs to the surface, not to the picture; moving a projector must move the mask with the corners.
- **A colour pass per output with a lookup table.** Rejected: the maths is a few operations per pixel and fits the existing fragment shader; a pass would cost a render target and a full-screen draw per output.
- **Always blend all slices.** Rejected: blending the first plain slice would change the pixels of every old setup and cost a little for nothing.
- **Higher mask resolution for soft masks.** Rejected: a 1024 x 576 blur cost about 30 ms per change in the test, noticeable while dragging a point, and the result is not visibly different.
- **A binary venue format.** Rejected: JSON can be read, diffed and repaired by hand, and the sizes are tiny.

## Consequences
- `OutputSpec` is no longer trivially small (it holds vectors); code must not put many of them on a stack. The UI copies at most one array of four when loading a venue.
- Colour is per output, not per slice. That matches how projectors differ, but a slice that needs its own colour has to be a different output.
- The black level is a simple model (a constant lift outside the blend zones). It matches projectors with a similar grey and a straight blend, and is not a measured calibration.
- Slices overlap by painter's order. There is no per-slice blend mode yet, and the edge blend of a later slice fades to black, not to the slice below.
- A venue stores display numbers. Moved to another computer, the numbers may point to a display that is not there; the last display is used.
- The self test needs a window system, so it runs only in the Linux CI job that has a virtual display. The Windows job does not run it.

## Validation
Measured with software OpenGL (llvmpipe, 1280 x 720 window on a virtual display, three runs in a row, earlier runs on the same computer were up to 20 percent faster); a software rasteriser pays for every covered pixel, so these numbers show relative cost, not the cost on a graphics card.

| Case | Frame | Video memory |
| --- | ---: | ---: |
| One default slice (as before) | 4.2 to 4.7 ms | 42 KB |
| One slice with a hard mask | 6.6 to 7.1 ms | +590 KB |
| One slice with a soft mask | 6.5 to 6.9 ms | +147 KB |
| Colour correction on | 7.0 to 7.2 ms | unchanged |
| Four masked full-window slices | 21.4 to 22.5 ms | not measured separately |
| Eight soft masked slices, 17 x 17 warp each | 61 to 63 ms | 2.5 MB |

Mask build: 2.3 ms for a hard mask (1024 x 576), 2.1 to 2.2 ms for a soft mask (512 x 288), 30 ms for a soft mask at 1024 x 576 (not used). Tests: about 25,000 checks for geometry, masks, colour, black level and JSON, 100 for venue setups, 64 pixel checks in the output self test. No comparison with v0.22.0 under identical conditions was made for the default case; the default path differs by a few uniform checks in the shader and one `meshDivisionsFor` call.

Not validated: real projectors and blending by eye, the black level against a real projector, multi-monitor fullscreen, a real graphics card, and venue files moved between computers.
