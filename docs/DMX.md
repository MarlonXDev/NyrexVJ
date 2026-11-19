# DMX, VRSL strip and VRChat control

NyrexVJ can turn its own controls into stage lighting data. Three parts work independently of each other and are set up in Settings, Stage:

- **Art-Net output** sends DMX channels over the network, for example to HNode, QLC+ or a lighting console.
- **DMX strip in the video** draws the same channels as a row of grey blocks into the output picture, the way VRSL (VR Stage Lighting) reads DMX inside a video stream.
- **VRChat avatar parameters** send values to VRChat over OSC so an avatar or a world prop can follow the music or the beat.

The settings are saved in `settings.json` under `dmx` and `vrchat`.

## Channels

Every channel row has a universe (1 to 4), a channel (1 to 512), an optional 16 bit mode and a source:

- **Fixed** sends the value you set with the slider.
- Any other source is one of the modulation sources used everywhere in NyrexVJ: the audio bands Bass, Mid, High, Level and Kick, LFO 1 to 4, Macro 1 to 8, Beat and Bar. The two numbers set the value at source 0 and at source 1 (turn them around or tick Invert to reverse the direction). A macro therefore moves a light exactly like it moves an effect.
- 16 bit uses the chosen channel for the high byte and the next channel for the low byte (not possible on channel 512).

Up to 128 channels can be set. Channels that are not used send 0.

## Art-Net

- Universe 1 to 4 of NyrexVJ are sent as Art-Net universes First universe, First universe + 1 and so on, inside the Net you pick (0 to 127). Only universes that have a channel are sent.
- Packets go out as ArtDmx (UDP port 6454 by default, 512 channels each) at the rate you choose (1 to 44 per second), with a running sequence number.
- The address is an IPv4 address or `localhost`. Unicast to one node is the safe choice; a broadcast address such as `2.255.255.255` also works because broadcasting is allowed on the socket.
- NyrexVJ only sends. It does not answer Art-Net polls, so a node that needs a poll reply to list a controller will not show it. Most receivers take ArtDmx sent to their address without that, but this was not tried with HNode.

## DMX strip in the video (VRSL)

With Draw the strip into the video switched on, the output picture (the one Spout, NDI, the preview and every output window carry) gets a strip of 120 x 13 blocks at its top or bottom edge. At 1920 x 1080 every block is 16 x 16 pixels and the strip is 1920 x 208 pixels. A block shows one channel as grey: value 0 is black, 255 is white. The strip covers the picture underneath, so keep that edge free in your visuals.

The Channel layout decides which block belongs to which channel:

- **Universes back to back (512 cells)**: cell number = (universe - 1) x 512 + channel - 1. Universe 4 only has room for 24 channels.
- **40 columns per universe (520 cells)**: every universe gets 40 columns of 13 blocks, so universe 1 is columns 1 to 40, universe 2 columns 41 to 80 and universe 3 columns 81 to 120 (universe 4 does not fit). This is the layout in which 13 channel fixtures (39 per universe) never cross a universe border. It is an assumption, not confirmed.

In both layouts cells run down a column first: cell 0 is column 1 row 1, cell 1 is column 1 row 2, cell 13 is column 2 row 1. The Channels list shows the column / row of each channel.

**Not verified.** The 120 x 13 grid, the value range and the 16 pixel blocks come from the public VRSL description. The exact channel to block order (and how a real VRSL world numbers the universes) is taken from third party documentation and has not been compared with a real VRSL world or with the output of HNode. Do not rely on it on a show before you have checked it:

1. Set the same Art-Net universe in HNode and NyrexVJ.
2. Run the Chase test pattern in NyrexVJ with the strip on, and in HNode send a single channel that you step through.
3. Compare where the lit block appears for the same channel. If the order differs, use the other Channel layout, and if both differ please open an issue with a screenshot of both strips so the mapping can be fixed.

The proven route stays: NyrexVJ sends Art-Net to HNode and HNode builds the strip. The built-in strip is for setups where you want one video stream that already carries the light data.

Things to know about the video route:

- The strip has to survive video compression. Big 16 pixel blocks and the centre of each block are what make this work, but a heavily compressed stream shifts grey values slightly. Keep the bitrate high.
- To use only the strip in OBS, crop the Spout capture so that just the strip stays, for example crop the bottom 872 pixels of a 1920 x 1080 picture when the strip is at the top.
- No gamma correction is applied. The block value equals the DMX value divided by 255 in the output picture.
- The RGB mode (three universes in the red, green and blue of one block) and the vertical strip are not built.

## VRChat avatar parameters

VRChat listens for OSC on port 9000 of the machine it runs on (address 127.0.0.1 when NyrexVJ runs on the same PC). Turn OSC on in the VRChat action menu (Options, OSC, Enabled). Every row sends one parameter as `/avatar/parameters/<name>`:

- **Float** sends 0 to 1.
- **Int** sends 0 to 255 (the 0 to 1 value times 255, rounded).
- **Bool** sends true when the value is 0.5 or higher.

The name must exist as a parameter of the avatar (letters, digits, `_`, `-` and `.`, at most 64 characters, no duplicates), otherwise VRChat ignores it. Values are sent when they change and once per second as a refresh. Up to 32 parameters can be set. VRChat decides which parameters of an avatar may be driven and how they are synced, that is outside NyrexVJ.

## Test patterns

Off, All 255, Ramp (0 to 255 over the 512 channels of every universe) and Chase (one channel after the other in universe 1) replace the channels on Art-Net and in the strip, to check the cabling and the strip layout. They are not saved, and are off at every start.

## Tested and not tested

- Tested automatically: the value mapping, the Art-Net packet bytes, the strip layout and the settings (`nvj_stage_tests`), real UDP delivery of Art-Net and OSC over loopback, and the rendered strip (block position, size and value) on Linux with software OpenGL.
- Not tested: a real Art-Net node, HNode, QLC+, a real VRSL world, VRChat itself and video compression of the strip.

## Not built

sACN, Art-Net input and poll replies, more than 4 universes, BinaryStageFlight, RGB and vertical VRSL layouts, a gamma option, named stage setups per venue.
