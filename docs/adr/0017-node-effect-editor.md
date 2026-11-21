# ADR 0017: Node effect editor: graph document, graph to GLSL compiler, shareable effect files

- **Status:** Accepted (2026-11-21) as milestone 6 of [ADR 0012](0012-open-source-vj-platform-plan.md), implemented in v0.25.0. Issue [#52](https://github.com/MarlonXDev/NyrexVJ/issues/52).
- **Date:** 2026-11-21
- **Owners:** Maintainers (pending assignment)
- **Related:** [NODES.md](../NODES.md), [SHADERS.md](../SHADERS.md), [ADR 0016](0016-show-sync.md), [PERFORMANCE.md](../PERFORMANCE.md), [INTERFACES.md](../INTERFACES.md)

## Context
Effects in NyrexVJ are GLSL snippets that are compiled into the program. A person who cannot write GLSL has no way to make a new effect, and a person who can has to rebuild the program or write a shader clip, which is a clip and not an effect on a layer. Resolume has a node based effect editor and it is the feature that lets a VJ share looks without sharing code. The goal is the same here: an effect made of nodes that is an ordinary effect afterwards, with no cost for a show that does not use it.

Constraints that shaped the design:
- the program runs next to VRChat: a node effect must cost what the same effect written by hand costs, so the graph is compiled to one shader and not interpreted,
- no new dependency; the editor is drawn with the Dear ImGui draw list that already exists,
- the document, the validation and the compiler must work without a window or a graphics driver, so they live in the core and are tested without OpenGL,
- a damaged or hostile file must never crash the program, never hang it and never run code: a file can name only nodes of the catalogue and numbers,
- an edit that leaves the graph invalid must not turn a running show black.

## Decision
1. **A versioned graph document in the core** (`src/core/node_graph.*`). A `GraphDocument` holds a name, four controls, nodes (id, type, position, values) and links (from node and port, to node and port). Limits: 96 nodes, 256 links, 4 controls, a label of 24 characters, a file of 256 KiB. `connect` refuses a loop and a type that cannot be converted when the link is made, and replaces the link that already ends in the input. JSON round trip with the format name `nyrexvj-effect` and version 1; a newer version, a wrong format name, an unknown node type or a value outside its range is handled with a message (unknown values are clamped, the rest refuses the file).
2. **A catalogue of 68 nodes** (`node_catalogue.cpp`) in nine categories: input (picture, previous picture, UV, centred UV, time, beat, audio, spectrum, waveform, control, macro, LFO, number, colour, vector), maths, vector, colour, distort, noise, shape, filter and output. Each node is data: ports with types, options with ranges, a GLSL template and the helper functions it needs. Ports are Number, Vector 2, Colour and Colour with alpha; generic ports take the widest type among the inputs; a Number converts to any vector, and Colour and Colour with alpha convert into each other.
3. **A compiler with one pass per effect** (`node_compile.*`). `compileGraph` validates (empty graph, limits, unknown nodes, duplicate ids, one Output node, links to missing nodes or ports, an input with two links, loops, types, picture reads), puts the nodes in order from the Output backwards (so unused nodes cost nothing and are not emitted), fills the template of each node and returns the GLSL body, the errors with the node that causes each one, whether the previous frame or the macros are used, the number of picture reads (limit 64) and a hash. The hash is FNV-1a over the generated shader text, so moving a node, renaming the effect, reordering the file or adding a node that does not reach the Output does not change it, and the same graph always gives the same text. Loops are refused when they are connected and detected again in the compiler because a file can contain one.
4. **The effect is a normal effect.** The compiled body is put between the same header, grade and footer as a built-in effect (`effect_library.cpp`). The library gets `Node: <name>` entries after the built-ins, with up to four controls whose labels come from the document. Library indices never change while the program runs (a deleted file stays listed with the error "The effect file is gone"), so effects on layers and in presets keep their identity. Presets store the effect by name. The source of a graph effect is cached by its hash.
5. **Modulation without a recompile.** Controls 1 to 4 are the effect parameters `p` and the Macro node reads a `uMacros[8]` uniform that the compiler declares only when a Macro node is used, so a graph can be driven by macros, MIDI, OSC and modulation. The LFO is computed in the shader from the beat uniform, so it follows the beat clock of [ADR 0016](0016-show-sync.md) without work on the processor. Numbers typed into an input are baked into the shader; changing one is a rebuild, which is why Control, Macro and LFO exist.
6. **Keep the last good program.** When an edit produces a graph that does not compile, the engine keeps the program that runs, shows the effect in red and the message on the node, and tries again after the next edit. Only an effect that never compiled has no program; it is skipped when the picture is drawn (the picture passes through unchanged), the effect rack shows the message, and a failed compile is written to the log once.
7. **Files in the `nodes` folder** (`node_files.*`), extension `.nyxfx`. Names are cleaned for every file system, files are written through a temporary file and renamed, a folder scan reads at most 64 files, and the library re-reads the folder when the editor saves, when a file is imported (also by dropping it on the window) and on request. Four examples (Wavy Rainbow, Mirror Swirl, Kick Pulse, Trails) are written on request. Import never overwrites, export copies to any path.
8. **The editor** (`ui_nodes.cpp`, `node_editor.hpp`) is drawn with `ImDrawList`: nodes with typed pins, a search menu on right click, value boxes (drag, or double click and Ctrl+click to type), a list popup for options, undo and redo (snapshots of the document, taken when the mouse is idle and ignoring pure moves), arrange, duplicate, delete, a controls panel, a file panel and a status line with the compile result. It saves 0.35 s after the last change. There is no zoom.
9. **Tests and checks run in CI.** The core tests need no GL. `--check-shaders` compiles a test graph for every catalogue node and the four examples with the real driver. `--bench-nodes` compares four graphs with hand written effects (see Validation).

## Alternatives
- **imnodes or imgui-node-editor.** Rejected: both are good, but each is one more dependency to track, license and build on two platforms, and the part we need (boxes, pins, bezier links, a search menu) is about one thousand lines on top of `ImDrawList`. The price is the missing features: no zoom, no mini map, no groups.
- **Interpreting the graph in a generic shader or on the processor.** Rejected: it would cost more every frame and would be slower than the hand written effect it is compared with. Compiling costs 8 to 25 microseconds for the text plus the driver compile, once per edit.
- **One shader per node (multi pass).** Rejected: it multiplies draws and pictures in video memory, and the target is a cost equal to a hand written effect. A single pass can read the incoming picture several times, which is limited to 64.
- **Letting a file contain GLSL.** Rejected: a shared file would run foreign code on the graphics driver. Nodes are data from the catalogue; the only text from the file is the effect name and the control labels, which are cleaned and never become code.
- **A checksum or signature in the file.** Not done: a checksum cannot protect against a person who edits and recomputes it, the document is validated whole anyway, and a damaged file is refused with a message. A signature would need keys and a trust model that no part of the program has.
- **Time and the LFO as uniforms from the processor.** Rejected: the LFO needs only the beat position that the shader already gets, so computing it in the shader adds no upload and no state.
- **A node for loops, a user written node or a script node.** Not in this milestone. Loops would need a bound and a compile model for them; a node with its own code belongs to the plug-in work of v0.26.0 and needs the safety rules written there first.
- **Making unlinked values uniforms.** Rejected for now: it would make every number editable without a rebuild, but it adds uniforms for numbers that never change and complicates the hash. Control, Macro and LFO cover everything that has to move.

## Consequences
- Every graph effect is a shader that the driver compiles at the first use and after every change that alters the graph. A change of a typed number is such a change; a change of a control, a macro or the beat is not.
- The effect list gains entries that depend on files. A preset that names a missing file loads without that effect and says so ("Unknown effect"). Sharing a preset means sharing the `.nyxfx` files.
- A graph can read the incoming picture up to 64 times a pixel. The limit protects against a graph that blurs for seconds on one pixel; it does not protect against a slow card, so the status line shows the number.
- Previous Picture costs a second picture buffer for that effect. Nothing else in the catalogue allocates anything.
- The file format has a version. A change that old programs cannot read raises the number; new nodes only extend the catalogue and are refused by an old program as an unknown node type with the message naming the type.
- The library index is stable for the life of the program, so a deleted file shows as gone until the next start, and an effect that was renamed on disk appears as a new effect.
- The names of nodes, pins and options and the messages of the compiler are English only in every language; the other texts of the editor are translated.

## Validation
Tests (all run in CI):
- `nvj_node_tests` (2,382 checks, no graphics driver): the document (add, remove, connect, replace, loops, types, limits), JSON (round trip, wrong format, newer version, damaged and oversized files, clamping), the compiler (valid graphs, cycles, type errors, limits, missing output, duplicate ids, unused nodes, determinism and a hash that ignores positions and names), files (clean names, import, export, scan limits, no overwrite), every catalogue node compiled alone and in a graph, and the four examples,
- `--check-shaders`: 129 graphs (the four examples and a test graph for every catalogue node, four for the nodes that work with several types) compile with the driver,
- `--bench-nodes 5` in CI: four example graphs against hand written GLSL of the same effect,
- address and undefined behaviour sanitizers for the node tests, and a scripted run of the editor under a virtual display (create, connect, edit a value, undo, save, add to a layer).

Measured with software OpenGL (Mesa llvmpipe), 1920 x 1080, clip Plasma, three runs of 5, 30 and 60 frames:
- the graph effect against the hand written effect, ratio of the effect time (graph divided by hand): 0.92 to 1.17 in the run of 30 frames, 0.96 to 1.04 in the run of 60 and 0.91 to 1.07 in the run of 5, so within the run to run spread and not in one direction; the three pairs without feedback give the same picture (largest difference of any pixel 0), and Trails, which reads the previous picture, is timed but not compared pixel by pixel,
- the compile of the text: 8 to 24 microseconds for graphs of 7 to 13 nodes, and the hash of the same graph is the same every time (the driver compile of the shader comes on top and was not timed separately),
- a graph with a Macro node: the picture follows the macro (difference 0 at macro 0, a visible one at 0.5).

Not measured: node effects next to VRChat, on a real graphics card, or the driver compile time of a large graph on Windows.
