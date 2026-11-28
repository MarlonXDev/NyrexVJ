# Native 3D scene player (prototype)

This is the 3D scene player that [ADR 0001](adr/0001-native-3d-runtime.md) asked for. It plays 3D scenes as ordinary clips inside the existing layer and effect pipeline. Since v0.9.0 the scenes can also come from the Clip Creator and from `.nyxclip` files ([ADR 0004](adr/0004-scene-document-and-clip-pack.md), [ADR 0005](adr/0005-pbr-shading.md), [ADR 0006](adr/0006-clip-creator.md)); see "Decisions" at the end.

## How it fits into the renderer

```text
.glb / .gltf file or built-in scene
    -> gltf.cpp / procedural.cpp          (src/scene3d, no OpenGL, loads on a worker thread)
    -> Scene (nodes, meshes, materials, images, animations)
    -> ScenePlayer                         (src/render/scene_player.*, uploads to the GPU on the render thread)
    -> SceneRenderer                       (draws into the layer's own texture; the depth buffer is one per world since 0.70.0)
    -> layer effect chain -> composite     (unchanged)
```

- A scene clip is a `ClipDef` with `kind` `BuiltinScene`, `ModelFile` or `ScenePack` (a `.nyxclip` file) instead of a fragment shader body. Presets store it by name like any other clip, an unknown name stays an empty cell.
- `SceneRenderer` attaches the layer texture (`layerA_`) to its own framebuffer together with one depth renderbuffer. There is no extra colour target, no blit and no per-clip framebuffer. There is one depth renderbuffer that every scene clip uses, and it is cleared before each draw, so it is one resource and not a depth that the layers share. Since 0.70.0 layers in a shared world use a depth buffer of their own that is kept between the layers of the world (see Shared 3D scene).
- `Engine::renderDeck` draws a scene clip and a shader clip through the same code path afterwards: effects, blend mode, opacity and master chain are untouched.
- Scene data is loaded by `std::async`, so file reading, parsing and image decoding never run on the render thread. The upload to the GPU is done on the render thread the first time the clip is in a deck cell. Until it is ready the layer is simply empty.
- After the upload the CPU copy of the geometry and images is released. Several layers (and several decks) that play the same scene share one set of GPU buffers.

## What is supported

| Area | Supported | Not supported (yet) |
| --- | --- | --- |
| Container | `.glb` and `.gltf` (external `.bin`, `data:` URIs, relative image files), `EXT_meshopt_compression` (the decoder of meshoptimizer, on by default), `KHR_draco_mesh_compression` (Draco, off by default, see "Compressed files") | any other required extension |
| Geometry | triangles, triangle strips, triangle fans, indexed or not, `POSITION`, `NORMAL` (computed when missing), `TEXCOORD_0`, `COLOR_0`, normalized and quantized integer attributes, sparse accessors, `JOINTS_0` and `WEIGHTS_0`, morph targets with `POSITION` and `NORMAL` deltas | points, lines, second UV set, tangents, morph target tangents and colours |
| Materials | base colour factor and texture (PNG and JPEG), metallic-roughness factors and texture, normal texture, emissive factor, texture and `KHR_materials_emissive_strength`, alpha mode opaque, mask and blend, double sided | occlusion, texture transforms, other material extensions |
| Hierarchy | node tree with translation, rotation, scale or matrix, mesh sharing between nodes, skins (joints with inverse bind matrices), default morph weights per mesh and per node | lights from the file are read and counted but **not applied**; perspective cameras can be added to a scene by hand since 0.69.0 (see "Lights and cameras in a file") |
| Animation | translation, rotation and scale channels, weights channels (morph targets), skeletal animation through the joints of a skin, linear and step, cubic spline approximated as linear, looping | none that was asked for |

Shading is a fixed, cheap model: two directional lights, a little ambient light and a rim light. Built-in scenes are drawn in the layer palette (Color A to Color B, or the rainbow switch); models keep their own colours. The camera orbits the scene automatically, is fitted to its bounding sphere and zooms with the layer Scale. Layer Speed drives both the orbit and the animation, Bass pulses the scale and Kick adds a small punch and a stronger rim.

## Skins, morph targets and blended transparency (v0.32.0)

[ADR 0025](adr/0025-3d-engine-2.md) has the reasons. The short version of what a file with these features does:

- **Skinning** runs on the graphics card. Each skin's joint matrices (`world of the joint node x inverse bind matrix`) are evaluated once per frame on the processor, written into one floating point texture (four texels per joint) and read by the vertex shader with four influences per vertex (`JOINTS_0`, `WEIGHTS_0`). A skinned mesh is drawn with an identity model matrix, as the glTF specification says, so the transform of its own node is ignored. All meshes of one node share one set of joint matrices. Several figures made from the same model each get their own joints.
- **Morph targets** are stored as one floating point texture of position and normal deltas per mesh and are blended in the vertex shader. A mesh may have up to 32 targets and the shader applies the **eight strongest weights** of each draw (the others are dropped, the strongest first, so a file with more active targets looks nearly right instead of failing). Default weights come from the mesh and the node, animated weights from a `weights` channel; a skinned and morphed mesh applies the morph first and the skin second, as the specification says.
- **Blended transparency** (`alphaMode` `BLEND`) is drawn in a second pass after all opaque and masked meshes, with depth testing on and depth writing off, sorted back to front **per mesh instance** (by the distance of the centre of the instance, for a skinned mesh the average of its joints). It is not sorted per triangle, so two blended triangles of the same mesh can overlap in the wrong order, the usual limit of this method. A double sided blended mesh draws its back faces first and then its front faces. Draw order in the file does not matter (a test renders the same two planes in both orders and compares the pixels). Opaque and masked materials and the built-in scenes are drawn exactly as before.
- **Limits** protect the video memory next to VRChat: 128 joints per skin, 64 skins, 32 morph targets per mesh, 1024 joints per frame for all skinned figures together (a figure that would exceed it is skipped, counted, and the first time logged as `N animated figures were skipped`; the other figures keep playing), 128 MiB of morph data when loading and 640 MiB of graphics memory per scene (`kSceneMaxGpuBytes`, checked when loading, in the Clip Creator and again before the upload). A skin is about 24 bytes per vertex, a morph target 24 bytes per vertex and target.
- The Clip Creator accepts a model with skins and morph targets as a model object. A model that would take the scene over the skin limit is left out with a warning in the clip window, and a scene that would need more than the memory limit is refused with a message there. The details in the library (hover over a clip) list skins, joints, morph targets and blended materials.

## Compressed files

| Extension | Where it is decoded | Build option | Default |
| --- | --- | --- | --- |
| `EXT_meshopt_compression` | on the buffer views, before any accessor is read (modes attributes, triangles and indices, filters octahedral, quaternion and exponential) | `NYREXVJ_MESHOPT` | on |
| `KHR_draco_mesh_compression` | on the primitive, instead of its accessors (the number of decoded points must match the accessors) | `NYREXVJ_DRACO` | **off** |

The meshoptimizer decoder (v1.3, MIT) is a few small C++ files that are compiled into the program. Draco 1.5.7 (Apache 2.0) is large, so it is only built when you ask for it with `-DNYREXVJ_DRACO=ON`; it is **not** fetched or built by default. Without a decoder a file that needs it is refused before anything is read, and the message names the CMake option (for example `the file needs KHR_draco_mesh_compression, which this build of NyrexVJ was made without (CMake option NYREXVJ_DRACO)`). A file that lists Meshopt only as used and brings a fallback buffer (`gltfpack -cf`) loads in a build without the decoder from the fallback buffer (tested with a model and its `.fallback.bin`; the fallback file is not read when the decoder is there); a Draco primitive that also has plain accessors takes them when there is no Draco decoder (in the code, no test file for it). Both decoders get the same limits as plain data (vertex, index, memory and morph limits), and truncated or tampered compressed data fails with a message and does not reach the GPU (tests cut and flip bytes of every test file).

## Lights and cameras in a file

Lights (`KHR_lights_punctual`) and cameras of a `.glb` are read and counted (up to 16 each; the library details show the numbers), a spot light is stored as a point light with a note, but **they are not applied automatically**. The scene keeps its own lights, sky and automatic or authored camera, so a model that brings its own lights looks like it did before, and a scene made in the Clip Creator is not changed by a model that is added to it. The data is in `Scene::fileLights` and `Scene::fileCameras`. Since 0.69.0 a perspective camera can be added to a scene as a choice, see "Virtual camera (0.69.0)"; the lights are still not offered. A test renders a model with lights and cameras and compares it with the same model without them.

## Loading safety

Files come from the user's disk, but model packs come from the internet, so the loader treats them as untrusted. All limits are in `GltfLimits` and can be changed in code.

| Check | Limit |
| --- | --- |
| File size, buffers together | 192 MiB |
| JSON text and nesting depth | 16 MiB, 64 levels |
| Vertices, indices | 2 000 000, 6 000 000 per scene |
| Nodes, meshes, animation keys | 10 000, 4 096, 200 000 |
| Node hierarchy depth, one parent per node | 64, no cycles |
| Image size, decoded image memory | 4096 x 4096, 192 MiB in total |
| Accessors | every range is checked against its buffer view and buffer with overflow safe arithmetic, indices must be in range, values must be finite |
| External files | relative paths only, no drive letters, no `..`, no symlink escape from the model folder, in-memory data may not reference files |
| Required extensions | anything outside a short allow list is refused with its name; Draco and Meshopt are allowed only when their decoder is built in |
| Skins, morph targets | 64 skins, 128 joints per skin, 32 morph targets per mesh, 128 MiB of morph data, joint indices inside the skin, weights finite and not negative, 640 MiB of graphics memory per scene |

A refused file produces an error text (in the log and in the clip tooltip) and never reaches the GPU. A texture that cannot be read only produces a warning, the model still loads without it. `tests/test_scene3d.cpp` covers these cases (truncated and tampered files, lying chunk lengths, out of range indices, cycles, shared children, huge counts, deep JSON, path tricks, bad base64, broken images, limits).

Not done yet: a time limit or cancellation for a very slow load. Closing the program while a huge model is still loading waits for that load to finish.

## Built-in scenes

| Clip | Vertices | Triangles | GPU geometry |
| --- | --- | --- | --- |
| Torus Knot Mesh | 5 061 | 9 600 | 349 KiB |
| Crystal Cluster Mesh (16 instances of one crystal) | 72 | 24 | 3 KiB |
| Orbit Rings Mesh (three animated rings and a pulsing core) | 5 744 | 10 560 | 393 KiB |

They are generated by code (`src/scene3d/procedural.cpp`) and use the same `Scene` type and the same renderer as loaded files. `writeGlb` turns any `Scene` back into a `.glb`, which the tests use for round trips.

## Measuring

```
NyrexVJ --bench-scene LAYERS:FRAMES[:CLIP] [--bench-out result.json]
```

It puts CLIP (default `Torus Knot Mesh`, a shader clip such as `Plasma` works too, which gives a baseline) on 1 to 4 layers, waits for the load, warms up for 30 frames and then measures FRAMES frames: CPU submit time, GPU time (`GL_TIME_ELAPSED`), scene statistics, the estimated GPU memory split into render targets, depth and geometry, and the resident memory of the process.

Memory is deterministic and was measured with 4 layers at 1920 x 1080:

| Part | Size |
| --- | --- |
| Render targets (5 full size textures, no scene needed) | 39.5 MiB |
| Depth buffer, only once a scene clip has drawn (one more of this size for each shared world in use, at most four) | 8.0 MiB |
| Geometry of Orbit Rings Mesh, shared by all 4 layers | 0.7 MiB |
| Clip thumbnails and layer previews | 2.4 MiB |

So the fixed cost of the first 3D clip is about 8 MiB of depth buffer, every further layer and every further instance of the same scene costs almost nothing, and the geometry of a model is paid once per model.

Since v0.32.0 the benchmark JSON of `--bench-scene` also has `skins`, `joints`, `morphTargets` and `blendMaterials` under `scene`. Models for it can be made with `scripts/make_bench_models.py OUTPUT_FOLDER` (a tube of 64,000 vertices, plain, skinned, morphed, both and blended; copy them to the `models` folder of the data folder and use `--bench-scene 1:60:"Model: bench_skin"`). Measurements and the comparison with the previous version are in [PERFORMANCE.md](PERFORMANCE.md). `NyrexVJ --scene-selftest tests/data/models` renders the test models and checks the pixels.

**Frame times are not measured yet on real hardware.** The numbers collected so far come from a software OpenGL renderer (llvmpipe) on a build server: 4 layers of Torus Knot Mesh took 190 ms per frame, 4 layers of the Plasma shader 209 ms. They show that the mesh path is not slower than a full screen shader there and that the benchmark works, they say nothing about a real GPU. The benchmark has to be run on the target PC (and on a weaker one) before anything is decided from it.

## Music on materials (0.68.0)

[ADR 0062](adr/0062-audio-reactive-3d-materials-sources-and-targets-for-scene-bindings.md). Every object, light and camera of a scene has up to four audio bindings (it was two): a source, a target and an amount. Rows on the same target add up and a child node takes the sum of its parent.

| Sources | What they are |
| --- | --- |
| Bass, Mid, High, Level, Kick | the five values the scene player always had |
| Beat | a pulse `exp(-5 * fract(beat))` from the beat position, so it follows tap tempo, MIDI clock or Link and works without sound |
| Band 1 to 8 | the mean of 16 neighbouring bins of the 128 logarithmic bins of the spectrum, about 30, 66, 144, 316, 690, 1,500, 3,300, 7,300 and 16,000 Hz between the edges; no new analysis |

| Targets on an object | Effect | Limit of the sum |
| --- | --- | --- |
| Size, Glow, Twist, Lift | as before | as before |
| Colour shift | turns the hue of the base colour and of the glow colour, 1.0 is one whole turn | plus and minus 8 turns |
| Roughness | added to the roughness before the shader's own clamp (0.045 to 1) | plus and minus 1 |
| Texture shift X, Y | added to the texture coordinates of every lookup of the material; the textures repeat, so a whole number comes back to the start | plus and minus 8 |
| Inflate | moves each vertex along its normal by 0.25 * amount * source times the half diagonal of the mesh bounding box | plus and minus 1 |
| Ripple | a travelling wave along the normal, 0.12 * amount * source * radius * sin(dot(p / radius, (3.1, 4.3, 2.7)) + 5 * time) | plus and minus 1 |

Lights keep Light intensity and the camera keeps Zoom. Inflate and Ripple run in the vertex shader after the morph targets and before the skin, in the local space of the mesh. They keep the old normals, so a deep ripple on a sharp shape is lit as if it were flat. Colour shift, Roughness and Texture shift live in the PBR fragment shader, which every scene from the Clip Creator and every glTF file uses; Inflate and Ripple also work in the classic shading of the built-in scenes, which have no material bindings of their own.

A scene document is written as `"version": 1` while it uses only the old sources, the old targets and two rows, and as `"version": 2` otherwise; programs older than 0.68.0 refuse version 2 with their message about a newer program. A binding with an unknown source or target, a binding that is not an object and a binding after the fourth are dropped, the scene still plays, and each one adds a line to the warnings (at most 16) in the Clip Creator and the log. Settings, Performance, **Music moves 3D materials** (on at the start, written to `settings.json` only when off) leaves the six material targets out and draws the material as authored. The preview in the Clip Creator uses `virtualAudio(beat, time)` when Simulate audio is ticked, a fixed function, so the same moment always gives the same picture.

## Virtual camera (0.69.0)

[ADR 0063](adr/0063-virtual-camera-glide-roll-offsets-and-gltf-cameras-for-3d-scenes.md). The camera of a scene is a virtual one: it moves inside the scene. It does not follow a real camera (see the open question in the ADR).

| Camera setting | What it does | Range |
| --- | --- | --- |
| Distance, Height angle, Start angle, Field of view, Orbit speed, Height sway | as before | as before |
| Roll | turns the picture around the line of sight | plus and minus 180 degrees |
| Move X, Y, Z | moves the point the camera looks at, in scene radii, and the camera with it | plus and minus 4 |
| Shot length (beats) | how long this camera stays in the cut sequence, 0 uses Cut (beats) | 0 to 32 |
| Glide into the next camera (scene) | the share of each shot used to move to the next camera; 0 is the old hard cut | 0 to 1 |
| Glide curve (scene) | Smooth or Linear, the same curves as the keys of the Show timeline | |

| Camera target of a binding | One unit of the source is | Limit of the sum |
| --- | --- | --- |
| Zoom | as before | as before |
| Orbit | 90 degrees around the scene | none (it turns) |
| Tilt | 45 degrees up | the height angle stays between plus and minus 85 degrees |
| Roll | 45 degrees | none (it turns) |
| Field of view | 30 degrees | 15 to 110 degrees |
| Move X, Y, Z | half a scene radius | plus and minus 4 |

Rows on the same target add up. The camera has the same four rows as every other node.

**The glide.** With Cut (beats) above 0 and two or more cameras, a camera plays for its shot (its own length or Cut (beats)). Over the last part of the shot, as long as Glide says, the pose moves to the pose of the next camera, with the curve chosen. The blend is of poses: yaw and roll take the short way round, the distance changes by a ratio, field of view, height angle and look-at offset change in a straight line. Glide 0, or a scene with one camera, is a hard cut as before. A glide needs the beat, so it follows the beat clock, tap tempo and MIDI clock like the cuts do.

**Controls for the automation.** `layer/N/camera-orbit` (plus and minus 180 degrees), `layer/N/camera-tilt` (plus and minus 60) and `layer/N/camera-fov` (plus and minus 30) are control targets like `layer/N/scale`. They add to the camera of the scene the layer plays and do nothing for other clips. MIDI mapping, OSC, the web interface, macros and Show timeline lanes can drive them, so a camera move over time is a lane with keys (Hold, Linear, Smooth), not a second keyframe system. The Clip window shows them under Camera for a scene clip. They are saved in presets only when not 0.

**Model cameras.** The Clip Creator lists the perspective cameras of the models of the scene under Cameras in the models. Use camera adds one as a normal camera: its world matrix, with the transform of the object, becomes a height angle, a start angle, a distance, a roll and a look-at offset relative to the middle and size of the whole scene, with the field of view of the file. It is the rest pose at that moment (animation of the camera node is not applied), it follows the scene and not the model if you move the model later, the distance is held between 0.2 and 6 and the height angle between plus and minus 85 degrees. Orthographic cameras are listed and cannot be used. At most eight cameras per scene.

**The switch.** Settings, Performance, **Camera moves in 3D scenes** (on at the start, written to `settings.json` only when off). Off draws the old camera: no glide (a hard cut), no roll, no look-at offset, no shot lengths, and the seven camera targets and the three layer controls are ignored; cuts and Zoom still work.

**Files.** A scene is written as `"version": 3` when it uses a camera feature (roll, offset, shot length, glide, a camera target), as 2 when it uses the 0.68.0 bindings and as 1 otherwise; a program that does not know the version refuses it with its message about a newer program. The keys `move`, `roll` and `beats` of a camera and `glide` and `glideShape` of the scene are written only when they are not 0 (the curve only when the glide is above 0). The Free camera of the editor ignores all of it, as it ignored the camera of the scene before.

## Shared 3D scene (0.70.0)

[ADR 0064](adr/0064-shared-3d-scene-hierarchy-model-reuse-and-shared-worlds-across-layers.md). Objects of one clip always hid each other, because a clip is drawn as one scene with one view and one depth test. 0.70.0 adds three things on top: a parent for an object, a model that is used twice and is built once, and **worlds** in which several layers are drawn into one space with one depth buffer, so the objects of two layers hide each other where they overlap. Physics, collisions and ray casts are not part of it.

**Hierarchy.** An object has an `id` (up to 24 characters of `A-Z a-z 0-9 _ -`, "o1", "o2" and so on when none is given) and a `parent`. A child is built under the node of its parent and so takes its move, turn and scale and the music reactions its parent got. Up to 8 levels. A parent that does not exist, is the object itself, forms a loop or is nested too deep is cleared on load and on build, the object goes to the top level and the warnings name it; the scene plays. A hidden parent hides its children. Deleting a parent moves its children to the parent of the deleted object; duplicating an object gives the copy a new id and the same parent, without its children. In the Clip Creator the object list is indented under the parents and each object has a Parent drop-down that offers only objects that make no loop and stay within 8 levels.

**One model, several objects.** When objects name the same model, its meshes, materials and images are added to the scene once; nodes, skins and animations are made per object. The vertex limit counts the geometry once. A model used twice has 24 vertices and 2,448 bytes of graphics memory where a copy per use has 48 and 4,896, and draws the same picture. It is not GL instancing: each mesh of each object is still one draw call.

**Worlds.** In the Layer panel a scene clip has a **Shared 3D world** section. The choice **World** is Own scene (the default and the old behaviour) or World 1 to 4. Layers in the same world are drawn into one space:

| Part | What it does |
| --- | --- |
| Owner | the lowest visible layer of the world that has a scene which is ready; the first one drawn. It sets the camera (with all the camera features of 0.69.0), the lights, the sky and the exposure, and it clears colour and depth of the depth buffer of the world |
| Member | every other layer of the world. It clears only its own colour (transparent), keeps the depth that is already there and draws with the view, projection, lights and sky of the owner. It keeps its own scene, its own animation time and its own audio rows |
| World position X, Y, Z | where the layer sits, in sizes of the owner (the radius of the owner's bounding sphere), minus 4 to 4; the owner moves too, relative to the camera |
| World turn | a turn around the vertical axis, minus 180 to 180 degrees |
| World size | the size of the layer in sizes of the owner, 0.1 to 4 (a logarithmic slider); 1 fits the layer to the size of the owner |

Each layer is still drawn into **its own texture**, so the layer effects, the blend mode, the opacity and the master chain work on every layer as before. The order is explicit: 3D draw with the shared depth, then the layer effects, then the 2D composite in the order of the layers, then the master chain. The effects see the colour of their layer and never the depth of the world. Where the geometry is opaque, a member composited over the owner with Normal blend is the picture of one joint scene (differing share 0.00 percent in the test, in front and behind). The occlusion comes from the depth, not from the order of the layers.

The old renderer had **one depth renderbuffer for all scene layers** and cleared it before each layer, so the geometry of two layers never met. That is why the layers covered each other in the order of the list. A world has its own depth renderbuffer and framebuffer (at most four, one per world), kept from layer to layer and cleared only by the owner; two worlds do not see each other. A world that no layer used for 180 frames gives its depth buffer back.

**Limits.**
- **Transparency.** A translucent surface (blend material) blends with what is behind it inside its own layer only, sorted as before. Between layers the blend mode of the layer applies to the layer picture, so a translucent surface of a member does not blend with the geometry of the owner by depth.
- **Camera.** A world has one view. A member ignores its own camera and its own Zoom.
- **Resolution.** The layers of a world need the same size. A member with another size draws on its own.
- **Lights.** PBR scenes (every scene from the Clip Creator and every glTF file) take the lights and the sky of the owner; the classic-shaded built-in scenes keep their fixed lights.
- **Who is not in a world.** A hidden or bypassed layer, a scene that is not ready or failed to load, and every layer when the switch is off draw on their own. A broken owner leaves the others on their own. None of this writes to the log; the layer is an ordinary layer.
- **Owner.** It is the lowest visible layer of the world, so hiding it hands the camera to the next one.

**Culling.** An instance that is not skinned and has no Inflate, Ripple or morph target is tested with its bounding sphere (the mesh radius times the largest axis scale, with 2 percent and a small constant of margin) against the six planes of the view, and is not drawn when it is outside. It is conservative: the built-in scenes at three zoom levels and four test models are byte for byte the same picture with it on and off. In the test scene 26 of 40 instances were skipped. Skinned, inflated, rippled and morphed instances are always drawn.

**Controls.** `layer/N/world-x`, `world-y`, `world-z` (minus 4 to 4) and `world-yaw` (minus 180 to 180) are control targets like `layer/N/scale`, so MIDI, OSC, the web interface, macros and the lanes of the Show timeline can move a layer in its world. The world number and the world size are choices and not controls.

**The switch.** Settings, Performance, **Shared 3D worlds** (on at the start, written to `settings.json` only when off). Off draws every 3D layer on its own, whatever world the layers are set to.

**Files.** A scene document is written as `"version": 4` only when an object has a parent; `id` and `parent` are written only then. A document without a parent is version 1, 2 or 3 and byte for byte the old file; a program older than 0.70.0 refuses version 4 with its message about a newer program. A layer is written to a preset with `world`, `worldX`, `worldY`, `worldZ`, `worldYaw` and `worldScale` only when they are not the default (0, 0, 0, 0, 0 and 1); old presets load with every layer on its own scene.

**Demo.** `docs/examples/shared-3d-world/` has three clips and a preset. `World Demo Arm.nyxclip` is a skinned arm with a smaller copy of the same model as its child (one model used twice, with a parent). `World Demo Cube.nyxclip` is a spinning cube with a moon as its child. `Shared Scene Demo.nyxclip` puts the arm and the cube, two separately added models, into one scene where they overlap in space, with the moon as a child of the cube. `Shared 3D World.json` is a preset that plays the first two clips on two layers in World 1, the cube placed at 0.45 owner sizes in X and 0.6 in Z, turned 20 degrees and at 0.8 of the size, so the two clips overlap and hide each other by depth. Copy the clips to the `clips` folder and the preset to the `presets` folder of the user data folder (`%APPDATA%\NyrexVJ` on Windows, `~/.config/NyrexVJ` on Linux). `nvj_world_tests --write-demo <folder>` writes the same files.

## Decisions

ADR 0001 said to benchmark before committing. The decisions that followed are recorded in the ADRs and implemented in v0.9.0:

- A scene graph of our own: the scene runtime evaluates node motions, repeats, framing and audio bindings itself ([ADR 0004](adr/0004-scene-document-and-clip-pack.md)).
- Scene file and asset packaging: a JSON scene document inside a `.nyxclip` container with embedded `.glb` models and textures; a missing asset gives a warning and the rest of the scene still plays.
- Physically based shading with an analytic sky and up to four lights, next to the unchanged classic shading ([ADR 0005](adr/0005-pbr-shading.md)).
- The Clip Creator as a floating window with a live preview ([ADR 0006](adr/0006-clip-creator.md)).
- Unloading: a scene that is in no cell and was not used for 120 seconds releases its GPU memory and loads again on demand; the models folder is rescanned without a restart.
- glTF breadth (v0.9.0): materials and textures as listed above.
- 3D Engine 2.0 (v0.32.0): skinning, morph targets, blended transparency, Meshopt and Draco import and the limits above, see [ADR 0025](adr/0025-3d-engine-2.md). glTF lights and cameras are read but not applied (cameras can be chosen by hand since v0.69.0).
- Music on materials (v0.68.0): eight spectrum bands and the beat as new sources, six material targets, four rows, see [ADR 0062](adr/0062-audio-reactive-3d-materials-sources-and-targets-for-scene-bindings.md).
- Virtual camera (v0.69.0): roll, look-at offset, shot lengths and a glide between cameras, seven camera targets, three layer controls, model cameras as a choice, see [ADR 0063](adr/0063-virtual-camera-glide-roll-offsets-and-gltf-cameras-for-3d-scenes.md).
- Shared 3D scene (v0.70.0): a parent for objects, a model used twice is built once, shared worlds with one depth buffer across layers, culling, four layer controls, see [ADR 0064](adr/0064-shared-3d-scene-hierarchy-model-reuse-and-shared-worlds-across-layers.md).

Still open: frame times on a real GPU. Run `NyrexVJ --bench-scene 4:300:"Torus Knot Mesh" --bench-out result.json` and the same with `Chrome Orbs Mesh` on the target PC and on a weaker one.
