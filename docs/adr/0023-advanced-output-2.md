# ADR 0023: Advanced Output 2.0, mapped NDI per output, curved, freehand and picture masks, colour per slice

- **Status:** Accepted (2026-11-24, requested by MarlonXDev), implemented in v0.30.0. Issue [#72](https://github.com/MarlonXDev/NyrexVJ/issues/72).
- **Date:** 2026-11-24
- **Owners:** Maintainers (pending assignment)
- **Related:** [OUTPUTS.md](../OUTPUTS.md), [PERFORMANCE.md](../PERFORMANCE.md), [INTERFACES.md](../INTERFACES.md), [ADR 0015](0015-advanced-output.md), [ADR 0014](0014-live-sources.md)

## Context
The output system (ADR 0015) has up to four outputs with up to eight slices each, with corners, a warp grid up to 17 x 17, edge blending, polygon masks, a colour correction per output, delay and venue setups. The request for this change asks for mapped NDI output, freehand and picture masks, Bezier mask edges and colour correction per slice, built on the existing warp, blend, mask and venue structures, with old venue files loading unchanged, traceable GPU and video memory budgets, and tests for masks, the NDI fallback and several outputs. Projection onto 3D surfaces and LED fixtures belong to other requests and are not part of this change.

## Findings (what existed before)
- **Masks.** `SliceMask` is a polygon of 3 to 32 points, rasterised on the processor into an 8 bit coverage texture (1024 x 576 hard, 512 x 288 for a soft edge) when it changes, and sampled by the output shader. The mask lives in output space, like the corners.
- **Colour.** One `OutputColor` per output, computed in the output fragment shader behind one uniform switch, skipped for the test pattern.
- **NDI.** `NdiSender` loads the runtime lazily and works with any frame source; `NdiOutput` sends the composition (not the mapped picture) through a pixel buffer ring and only reads back while a receiver is connected. NDI could not carry the mapped output because the mapping happens in the output windows.
- **Files.** Venue setups and `settings.json` use `outputToJson` and `outputFromJson`; unknown keys are ignored and missing keys keep their defaults, so adding optional keys is compatible in the reading direction.
- **Budgets.** Memory was reported (`OutputManager::gpuBytes`) but there was no limit and no way to predict it.

## Decision

### Compatibility
1. **Everything new is off by default and written only when it differs from the default.** `sliceColor`, `ndi` and a mask's `curves` and `image` do not appear in a file that does not use them, so such a file is byte for byte what 0.29.0 wrote (a test compares the text) and 0.29.0 files load unchanged. Damaged values are repaired when read: a handle list of the wrong length is dropped, handles are clamped, a picture name that is not a plain file name with a picture extension is dropped, an NDI name is cut at 60 characters.
2. **No new file format.** Freehand is a way to make a polygon, not a new mask type: the stroke is simplified into points. Curves are an optional list of handles next to the points. Only the picture mask is a different kind of mask, and it is a name, not data.

### Masks
3. **Bezier edges.** Each point has an in and an out handle, stored relative to the point, so a point and its handles move together. A curve is flattened into 16 steps per edge, and the flattened outline is used by the rasteriser, the hit test and the canvas, so what you see is what is drawn. Smooth edges sets Catmull-Rom handles (the neighbour difference divided by six) that pass through every point; dragging one handle mirrors the other unless Alt is held; inserting a point splits the edge with de Casteljau's algorithm and keeps its shape.
4. **Freehand.** The stroke is simplified with Douglas-Peucker, raising the error limit until at most 32 points remain (falling back to an even sample). Near duplicates and the closing point are dropped. A stroke that leaves fewer than three points is refused.
5. **Picture masks.** A mask can name a picture in the `masks` folder of the data folder. The picture is decoded on a thread of its own (PNG, JPG, BMP, TGA, up to 16 MiB and 8192 pixels), reduced to one 8 bit channel (alpha when the picture has any non-opaque pixel, otherwise the Rec. 709 brightness) at 1024 x 576 and cached by name. Invert and soft edge work on the result like for polygons. **A picture mask fails closed:** while the picture is loading, or when it is missing, damaged or too large, the slice is hidden (an inverted mask included), never shown unmasked. A failed picture is retried after five seconds or on Reload image. A picture is copied into the folder when it is chosen, so the original can move, and the venue file stores the name only; the name is checked so it cannot point outside the folder.

### Colour
6. **Colour per slice** has the same controls as the colour per output and is applied before it, in the same shader. It costs one more uniform pair, no pass and no texture, is skipped at its default and is not applied to the test pattern.

### NDI per output
7. **Each output window owns its sender.** The sender is created when the window is open and the output has NDI on, restarts when the name changes, and stops when NDI is turned off or the window closes. The default name is `NyrexVJ Output N`.
8. **The picture is the window's back buffer** after all slices are drawn, so the NDI picture is exactly what the window shows, including the mapping, the masks, the blending, the colour and the delay. It is read back into two pixel buffers (one read in flight, the other mapped and sent), so the graphics card is not stalled, at the price of one frame of delay. Nothing is allocated or read while no receiver is connected. The window may be at most 4096 pixels in either direction.
9. **Fallback.** Without the NDI runtime, or when the sender cannot be created, the output keeps drawing and reports the reason in the output tab; the start is retried every three seconds. The separate NDI of the main picture in Settings is unchanged.

### Budgets
10. **One budget of 384 MiB** covers meshes, mask textures, the delay ring and the NDI readback buffers of all outputs. `estimateOutputBudget` computes them from the settings without a graphics card, `OutputManager::gpuBytes` measures them, and `--output-selftest` checks that both give the same number. Only the NDI readback is refused when it does not fit (it is the only part that is large, optional and can be started later); meshes and masks are small and the delay ring was chosen by the person. The tab shows the used memory and warns when the setup would need more than the budget.

## Alternatives considered
- **Sending the composition with a second mapping pass for NDI.** It would repeat the whole mapping on a framebuffer of its own and could differ from the window. Reading the back buffer gives the real picture for the cost of the readback.
- **A framebuffer object for the readback instead of the back buffer.** It would need the output to be drawn into a texture first. That is a larger change of the output path and not needed while the window is not larger than 4096 pixels.
- **Storing picture masks inside the venue file.** It would make a file large and hard to edit by hand. A name keeps the file small, at the price that the pictures have to be copied with the venue.
- **Showing the slice while a picture loads.** It would flash the unmasked picture on a projector at the start of a show. Hidden is the safe state.
- **Spline handles for freehand strokes.** Fitting curves to a stroke is more work and less predictable than a simplified polygon that can be smoothed afterwards.

## Consequences
- Old venue files and settings load and save unchanged; a downgrade to 0.29.0 ignores the new keys and shows the polygons without curves and picture masks without a mask (the whole slice).
- Every NDI output is a source on the network and uses the processor for the copy while a receiver watches. The cost was measured with software OpenGL and a fake library only.
- A venue that uses picture masks needs the pictures in the `masks` folder of the other computer.

## Validation
- `nvj_output_tests` (curves, strokes, picture decoder, picture cache, budget, JSON, damaged input), `nvj_venue_tests` (old files byte for byte, new keys, damaged keys) and `--output-selftest` (pixels of curved, freehand and picture masks against a CPU model, colour order, NDI against a fake library with receiver, name change, budget refusal and fallback without runtime) all run in the CI on Linux.
- Not validated: a real NDI runtime and receiver, real projectors, a real graphics card and Windows rendering. The numbers in [PERFORMANCE.md](../PERFORMANCE.md) come from a software renderer.
