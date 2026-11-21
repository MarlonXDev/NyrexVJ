# Node effect editor

Since v0.25.0 NyrexVJ has a node editor for effects. You build an effect by connecting nodes (inputs, maths, colour, distortion, noise, shapes, a bounded blur) and the program turns the graph into one GLSL fragment shader. The result is a normal effect: it is in the library under the name `Node: <name>`, it can be put on a clip, a layer or the master, it has up to four controls and it can read the macros, the audio analysis and the beat. Effects are stored as `.nyxfx` files that can be shared.

Design and reasons: [ADR 0017](adr/0017-node-effect-editor.md). The numbers in this file come from tests, a benchmark with a software rasteriser and a scripted run of the editor. Nothing was tried on a real graphics card or on Windows at run time, see [What was not tested](#what-was-not-tested).

## Quick start

1. Open the editor: Creator menu, Node effect editor, or the **Edit nodes** button under an effect of this kind in the effect rack, or start the program with `--node-editor`.
2. Press **New**, or **Create examples** for four ready made effects (Wavy Rainbow, Mirror Swirl, Kick Pulse, Trails).
3. Right click in the graph and search for a node. Drag from a pin to another pin to connect. The **Output** node is the end of every effect.
4. The effect is saved a moment after every change (0.35 s after you let go of the mouse) and the shader is rebuilt. The status line at the bottom says "The effect compiles", the number of picture reads and whether the previous frame is used, or shows the error on the node that causes it.
5. Press **Add to layer** (the layer that is selected in the program) to use the effect, or find it in the effect browser under `Node: <name>`.

## The editor

| Action | How |
| --- | --- |
| Add a node | Right click on empty space, type to search, click the node |
| Connect | Drag from an output pin (right side) to an input pin (left side) |
| Remove a link | Drag a connected input away, or right click the node, Disconnect inputs or Disconnect outputs |
| Change a value | Drag the value box left or right, or double click or Ctrl+click it and type a number; a list value (Shape, Mode, Edges) opens a menu |
| Delete a node | Select it and press Delete or Backspace, or right click, Delete (the Output node cannot be deleted) |
| Duplicate | Ctrl+D or right click, Duplicate |
| Move the view | Middle mouse button or drag on empty space |
| Undo and redo | Ctrl+Z and Ctrl+Y, or the buttons |
| Tidy up | **Arrange** places the nodes in columns from the inputs to the output |

Inputs that have a connection show no value box, because the link wins. An input without a link shows its own value, which is baked into the shader (see [Limits](#limits-and-honest-notes)). A new link into an input that already has one replaces the old link. A connection that would make a loop or that cannot be converted is refused when you drop it.

Types: **Number** (one value), **Vector 2** (a position), **Colour** (red, green, blue) and **Colour with alpha**. A number can feed a vector or a colour (it is copied into every component), a Colour can feed a Colour with alpha (alpha 1) and the other way round (alpha is dropped). Nodes that work for several types (Add, Mix, Clamp and so on) take the widest type of their inputs. Vector 2 and a colour cannot be mixed.

## Controls and macros

Every effect has four controls, Control 1 to 4. They are the sliders of the effect in the rack. In the editor, under Effect controls, each control has a label and a default value; a control without a label is not shown in the effect. A **Control** node reads one of them (option 1 to 4). A **Macro** node reads one of the eight composition macros, so a graph can be driven by the macro sliders, by MIDI or OSC mapped to them and by modulation. The **LFO** node is computed in the shader from the beat clock: one cycle takes 0.25 to 64 beats, and it follows the sync source ([SYNC.md](SYNC.md)) without any work on the processor.

## Limits and honest notes

| Limit | Value |
| --- | --- |
| Nodes in a graph | 96 |
| Links in a graph | 256 |
| Reads of the incoming picture per pixel | 64 (Blur counts its samples, Edges counts 4) |
| Size of a `.nyxfx` file | 256 KiB |
| Node effects in the library | 64 |
| Length of a control label | 24 characters |
| Output nodes | exactly one |

- A graph is checked as a whole before a shader is made: an empty graph, too many nodes, an unknown node, a second Output node, a missing Output, a connection to a missing port, an input with two links, a loop, a type that cannot be combined and too many picture reads are all refused with a message on the node that causes it. The names of nodes, pins and options and these messages are English in every language; the other texts of the editor are translated.
- The shader is made in one pass for the whole graph, so there is one draw for the effect, as for a hand written effect. It is cached by a hash of the generated shader text. Moving nodes, the name of the effect, the order of the links in the file and nodes that do not lead to the Output change nothing in the text, so they cause no rebuild.
- If an edit makes the graph invalid, the last good shader keeps running, the effect shows a red mark in the browser and the rack, and the message is shown on the node. A half finished edit does not turn the picture black. An effect that never compiled is skipped and the picture passes through unchanged.
- Numbers typed into an input without a link are written into the shader text. Changing one rebuilds the shader (a few microseconds for the text, then the driver compiles it). For something that should move, use a Control, a Macro or an LFO node, which are uniforms and cost nothing to change.
- **Previous Picture** needs a second picture buffer for this effect and costs video memory in proportion to the picture size; the status line shows that the previous frame is used. It is the only node that does this.
- There is no zoom in the editor. The graph is drawn with `ImDrawList` and has no new library.
- Deleting a `.nyxfx` file while the program runs leaves the effect in the library (marked "The effect file is gone") until the next start, because the library indices of the effects on layers must stay stable.
- The file has no checksum. A damaged file is refused with a message and changes nothing.
- Files are plain JSON and hold only nodes of the catalogue, links and numbers. A file cannot contain shader code, so a `.nyxfx` from someone else can at worst make a slow or refused effect, within the limits above.

## The file format

A `.nyxfx` file is JSON in the `nodes` folder of the user data folder (see [INTERFACES.md](INTERFACES.md)).

```
{
  "format": "nyrexvj-effect",
  "version": 1,
  "name": "Wavy Rainbow",
  "controls": [ { "label": "Amount", "value": 0.5 }, ... four entries ... ],
  "nodes": [ { "id": 1, "type": "uv", "x": 0, "y": 0, "values": [ ... ] }, ... ],
  "links": [ { "from": 1, "fromPort": 0, "to": 2, "toPort": 0 }, ... ]
}
```

- `values` has one number for every input (in port order) followed by one for every option of the node. Missing numbers get the default, numbers outside the range are clamped.
- A file with a higher `version`, a wrong `format`, more than 96 nodes or 256 links, or more than 256 KiB is refused with a message that names the reason.
- Names are cleaned for all file systems (also Windows device names) and may contain umlauts; the library name is `Node: ` and the name. Import (the Import button or dropping a `.nyxfx` file on the window) never overwrites: an existing name gets a number.

## In the library

Node effects appear after the built-in effects, with the name `Node: <name>`, in every place where effects can be chosen: the effect browser, the rack, presets, the Control page and MIDI. A preset that uses a node effect stores the name. If the file is missing on another computer the preset still loads, that slot stays empty and a warning names the effect ("Unknown effect"), so copy the `.nyxfx` file together with the preset. Export copies the file to any path.

Command line: `--node-editor` opens the editor at start, and `--node-editor NAME` opens that effect in it. `--check-shaders` compiles 129 graphs with the driver: the four examples and a small test graph for every catalogue node (four for the nodes that work with numbers, vectors, colours and colours with alpha). `--bench-nodes N` is an internal benchmark, see [PERFORMANCE.md](PERFORMANCE.md).

## The catalogue

68 nodes. The columns are what the node does, the inputs, the options (values on the node itself) and the outputs.

### Input

| Node | What it does | Inputs | Options | Outputs |
| --- | --- | --- | --- | --- |
| Picture | The picture that comes into the effect, with alpha. | - | - | Colour |
| Previous Picture | The finished picture of the previous frame (feedback). Needs a second picture buffer. | - | - | Colour |
| UV | The position in the picture, 0 to 1 from the lower left corner. | - | - | UV |
| Centered UV | The position with 0,0 in the middle, x corrected by the aspect ratio. | - | - | UV |
| Time | Seconds since the program started. | - | - | Seconds |
| Beat | The beat position, counting up by one every beat. | - | - | Beats |
| Audio | The audio analysis: bass, mid, high, the overall level and the kick pulse. | - | - | Bass, Mid, High, Level, Kick |
| Spectrum | The audio spectrum at a position from 0 (low) to 1 (high). | Position | - | Value |
| Waveform | The audio waveform at a position from 0 to 1. | Position | - | Value |
| Control | The value of effect control 1 to 4 (the sliders of the effect). | - | Control (1 to 4, whole numbers) | Value |
| Macro | The value of macro 1 to 8 of the composition. | - | Macro (1 to 8, whole numbers) | Value |
| LFO | A low frequency oscillator on the beat clock: one cycle takes the chosen number of beats. | - | Shape (Sine,Triangle,Saw,Square); Beats per cycle (0.25 to 64) | Value |
| Number | A constant number. | - | Value (-16 to 16) | Value |
| Colour | A constant colour. | - | Red (0 to 1); Green (0 to 1); Blue (0 to 1) | Colour |
| Vector 2 | A constant pair of numbers. | - | X (-2 to 2); Y (-2 to 2) | Vector |

### Maths

| Node | What it does | Inputs | Options | Outputs |
| --- | --- | --- | --- | --- |
| Add | A plus B. Works with numbers, vectors and colours. | A, B | - | Result |
| Subtract | A minus B. | A, B | - | Result |
| Multiply | A times B. | A, B | - | Result |
| Divide | A divided by B, protected against a division by zero. | A, B | - | Result |
| Minimum | The smaller of A and B for each component. | A, B | - | Result |
| Maximum | The larger of A and B for each component. | A, B | - | Result |
| Mix | A to B by Amount (0 gives A, 1 gives B). | A, B, Amount | - | Result |
| Clamp | Limits the value to the range between Lowest and Highest. | Value, Lowest, Highest | - | Result |
| Absolute | The absolute value. | Value | - | Result |
| Negate | The value with the sign turned. | Value | - | Result |
| Fraction | The part after the decimal point. | Value | - | Result |
| Floor | Rounds down. | Value | - | Result |
| Sine | The sine of an angle in radians. | Radians | - | Result |
| Cosine | The cosine of an angle in radians. | Radians | - | Result |
| Power | Base to the power of Exponent (a base below 0.00001 is raised to that). | Base, Exponent | - | Result |
| Smooth Step | A soft step from 0 to 1 between From and To. | From, To, Value | - | Result |
| Step | 0 below the edge, 1 from the edge on. | Edge, Value | - | Result |
| Length | The length of a number, vector or colour. | Value | - | Length |
| Map Range | Maps a value from one range to another and limits it to the new range. | Value, From low, From high, To low, To high | - | Result |

### Vector

| Node | What it does | Inputs | Options | Outputs |
| --- | --- | --- | --- | --- |
| Make Vector 2 | Builds a pair of numbers from X and Y. | X, Y | - | Vector |
| Make Colour | Builds a colour from red, green and blue. | Red, Green, Blue | - | Colour |
| Add Alpha | Adds an alpha value to a colour. | Colour, Alpha | - | Colour |
| Split Colour | Splits a colour into red, green, blue and alpha. | Colour | - | Red, Green, Blue, Alpha |
| Split Vector 2 | Splits a pair of numbers into X and Y. | Vector | - | X, Y |

### Colour

| Node | What it does | Inputs | Options | Outputs |
| --- | --- | --- | --- | --- |
| Hue Shift | Turns the hue; an amount of 1 is one full turn. | Colour, Amount | - | Colour |
| Saturation | 0 gives grey, 1 leaves the colour, above 1 is stronger. | Colour, Amount | - | Colour |
| Brightness Contrast | Adds brightness and scales the contrast around the middle grey. | Colour, Brightness, Contrast | - | Colour |
| Invert | Inverts the colour by Amount. | Colour, Amount | - | Colour |
| Luminance | The brightness of a colour as one number. | Colour | - | Luminance |
| Rainbow Palette | A rainbow colour for a position (the palette of the Rainbow clips). | Position | - | Colour |
| Blend | Blends a top colour over a base colour (Mix, Add, Multiply, Screen, Difference). | Base, Top, Amount | Mode (Mix,Add,Multiply,Screen,Difference) | Colour |

### Distort

| Node | What it does | Inputs | Options | Outputs |
| --- | --- | --- | --- | --- |
| Rotate | Rotates the position around the middle; 1 is one full turn. | UV, Angle | - | UV |
| Zoom | Zooms into the middle (above 1) or out (below 1). | UV, Amount | - | UV |
| Move | Moves the position. | UV, X, Y | - | UV |
| Mirror | Mirrors the left to the right half and or the top to the bottom half. | UV | Mirror left to right (0 to 1, whole numbers); Mirror top to bottom (0 to 1, whole numbers) | UV |
| Kaleidoscope | A kaleidoscope with 2 to 32 segments. | UV, Segments, Rotation | - | UV |
| Swirl | Twists the picture around the middle inside a radius. | UV, Amount, Radius | - | UV |
| Wave | Waves the position with sine and cosine over time. | UV, Amount, Frequency, Speed | - | UV |
| Pixelate | Snaps the position to a grid of the given number of cells. | UV, Cells | - | UV |
| Polar | Turns the position into angle and distance from the middle. | UV | - | UV |
| Tile | Repeats the picture in mirrored tiles. | UV, Count | - | UV |
| Lens | A barrel (positive) or pincushion (negative) lens distortion. | UV, Strength | - | UV |
| Sample Picture | Reads the incoming picture at a position; Edges decides what happens outside (Clamp, Repeat, Mirror). | UV | Edges (Clamp,Repeat,Mirror) | Colour |

### Noise

| Node | What it does | Inputs | Options | Outputs |
| --- | --- | --- | --- | --- |
| Noise | Smooth value noise that moves over time. | UV, Scale, Speed | - | Value |
| Fractal Noise | Fractal noise of 1 to 5 layers. | UV, Scale, Speed | Octaves (1 to 5, whole numbers) | Value |
| Cells | The distance to the nearest point of a moving cell pattern. | UV, Scale, Speed | - | Distance |

### Shape

| Node | What it does | Inputs | Options | Outputs |
| --- | --- | --- | --- | --- |
| Circle | A soft circle as a mask from 0 to 1. | Position, Radius, Softness | - | Mask |
| Box | A soft rectangle as a mask from 0 to 1. | Position, Width, Height, Softness | - | Mask |
| Rings | Moving rings. | Position, Frequency, Speed | - | Value |
| Gradient | A straight gradient from 0 to 1 at an angle. | UV, Angle | - | Value |

### Filter

| Node | What it does | Inputs | Options | Outputs |
| --- | --- | --- | --- | --- |
| Blur | A blur of the incoming picture with 2 to 16 samples on a disc. | UV, Radius | Samples (2 to 16, whole numbers) | Colour |
| Edges | An edge detector on the brightness of the incoming picture (4 picture reads). | UV, Strength | - | Value |

### Output

| Node | What it does | Inputs | Options | Outputs |
| --- | --- | --- | --- | --- |
| Output | The result of the effect. There must be exactly one, and its colour is what the effect returns. | Colour | - | - |

## What was not tested

- No real graphics card was used: the shader compiler and the picture come from Mesa llvmpipe (a software rasteriser). A driver that is stricter or has other precision limits could refuse a graph or show small differences.
- Windows was built (MSVC in CI) but the editor was not run there.
- The editor was operated by a scripted mouse and keyboard under a virtual display, not by a person with a tablet or a touch screen. The feel of dragging was not judged.
- The effects in the examples were looked at on a test picture, not on a real show, and not next to VRChat.
- The cost of the node effects next to VRChat was not measured.
