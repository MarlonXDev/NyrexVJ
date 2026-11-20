# Live sources: NDI input, cameras and capture cards

Eight clips show a picture that does not come from a file: **NDI Input 1 to 4** (the picture of another program or computer on the network) and **Camera 1 to 4** (a webcam or a capture card). They are in the library under the filter **Live**. Put one into a cell, select it, and choose the source in the Clip window. The design is in [ADR 0014](adr/0014-live-sources.md).

A live clip behaves like any other clip: it goes through the layer effects, the blend modes and the outputs. A source costs nothing while no layer shows its clip. It is opened when a clip is triggered and closed three seconds after the last layer stopped showing it, so the camera light goes off and the network connection ends. The choice of source is saved in the program settings and not in the preset, because names of devices and of senders differ between computers.

## NDI input

NDI sends video over a network. NyrexVJ receives it when the NDI runtime is installed. The NDI library is **not part of NyrexVJ** (its licence does not allow it), so install the free NDI Tools or the NDI Runtime from <https://ndi.video/tools/> first. Without it the Clip window says so and offers a button to the download page, and the rest of the program is unaffected. Where NyrexVJ looks for the library is listed in [DEPENDENCIES.md](DEPENDENCIES.md).

- **Finding sources.** While the Clip window of an NDI clip is open, NyrexVJ looks for senders in the background and lists them. **Automatic** takes the first sender that is not the NyrexVJ output itself (so NyrexVJ does not receive its own picture). Choose a name from the list to pin one. A name that is saved but not on the network stays selected and is searched for again and again, so the picture comes back when the sender does.
- **Picture formats.** NyrexVJ asks the sender for UYVY and the highest bandwidth. UYVY pictures are sent to the graphics card as they are (two bytes per pixel) and converted there; pictures with an alpha channel (UYVA) keep it. RGBA pictures also work. Any other format, or a width that is not even in UYVY, is shown as unsupported in the Clip window with the reason.
- **Newest picture only.** If the render is slower than the sender, older pictures are dropped and counted. The picture on screen is never older than the last one received.

## Cameras and capture cards

A camera is opened through the FFmpeg device library: Video4Linux2 on Linux and DirectShow on Windows. A capture card that shows up as a camera works the same way. It needs the same FFmpeg 8.x libraries as video clips plus the **device library**, see [DEPENDENCIES.md](DEPENDENCIES.md). Without it only the camera clips say that cameras are not available.

- **Choosing the camera.** The Clip window lists the cameras found; **Refresh** looks again (plug a device in, then press it). **None** leaves the slot empty.
- **Mode.** Resolution and frame rate are a request. The default is 1280 x 720 at 30 pictures a second; **Auto** leaves the choice to the device. A camera offers fixed modes, and the closest one is used. For modes above 640 x 480 at 30, NyrexVJ first asks for MJPEG (it needs less bandwidth on USB) and falls back to the camera's own format if that fails. The mode you choose is shared by all four camera slots and saved in the settings.
- **Colour.** The picture is handed to the graphics card as UYVY and converted there. The matrix is BT.709 for 720 lines and above and BT.601 below, with the limited range of video; pictures that FFmpeg reports as full range keep it.

## State, reconnecting and the placeholder

The Clip window shows the state of the source in a colour:

| State | Meaning |
| --- | --- |
| Searching | No matching source yet (NDI: the sender is not on the network; camera: the device is not found) |
| Connecting | The source was found and is being opened |
| Live | Pictures arrive |
| Signal lost | No picture for 1.5 seconds |
| Reconnecting | The connection is closed and a new attempt is made |
| Idle | No layer shows this clip, the source is closed |

After a loss or a failed attempt NyrexVJ waits 0.5 seconds, then 1, 2, 4, 8 and from then on 10 seconds between attempts, and starts again by itself when a source returns. A connection that is not up after 6 seconds counts as failed. Before the first picture arrives, the layer shows the placeholder look of the clip. When the signal is lost later, the **last picture stays on the layer** (a frozen frame) until the source comes back, so a pulled cable does not flash a placeholder on stage; the state in the Clip window shows that the signal is gone (there is no warning on the layer itself). The panel shows the size, frame rate, the number of dropped pictures, the number of attempts and the latency.

**Latency** in the panel is the time from the arrival of a picture in NyrexVJ's capture thread to the end of its upload to the graphics card. It does **not** include the camera, the cable, the network, a capture card or the display. Measure the whole chain with a phone camera filming a stopwatch next to the screen.

## What it costs

Measured with FFmpeg's test source and software OpenGL (not a real camera or a real graphics card, see below):

| Source | Source rate | Upload time per picture | Capture to upload (average, 95th percentile, maximum) | Processor | Video memory of the slot |
| --- | ---: | ---: | --- | ---: | ---: |
| 1080p at 60 | 59.6 per second | 3.1 ms | 12.5 ms, 14.4 ms, 23.9 ms | 16.5 % of one core (including generating the test picture) | 16.6 MB |
| 720p at 30 | 30 per second requested | 2.3 ms | 19.5 ms average | 6.9 % of one core | 7.4 MB |

The memory of a 1080p slot is 8.29 MB for the converted picture, 4.15 MB for the packed picture and 4.15 MB for the upload buffer. Nothing is allocated for a slot that is not in use. To measure your own setup:

```
NyrexVJ --bench-live 20:camera:/dev/video0
NyrexVJ --bench-live 20:ndi:"MYPC (OBS)" --bench-out live.json
NyrexVJ --live-selftest
```

`--bench-live SECONDS:camera:SOURCE` and `SECONDS:ndi:NAME` play the source for SECONDS and print the numbers above. `--live-selftest` plays a test source through the whole path (graphics card conversion, colour, release after the idle time) and exits with code 1 when something is wrong. A source name that starts with `lavfi:` is an FFmpeg test source, for example `lavfi:testsrc2=size=1280x720:rate=30`.

## What was and was not tested

Tested (automatically, see `nvj_live_tests`, `nvj_ndi_receive_tests`, `nvj_camera_tests`): the state machine and its delays, keeping only the newest picture, choosing an NDI source, receiving from a fake NDI library (formats, lost sender, reconnecting, unsupported formats), cameras with FFmpeg test sources (sizes, rates, colours in both matrices, ranges, a device that does not exist), the conversion shader against a reference on the processor, the three second release, and that graphics memory and threads return to their start values.

**Not tested, because no hardware was available:** a real camera or capture card (format negotiation, the MJPEG request, plugging in and out), DirectShow device listing and opening on Windows, the real NDI runtime (versions 5 and 6, alpha handling, other colour formats, several senders), real graphics card timings, end to end latency, and the effect on VRChat. NDI alpha is assumed to be straight (not premultiplied). The numbers above come from a software rasteriser. Please run `--bench-live` on your computer and report what you see.

## For developers

The pieces are separate and testable without hardware:

- `src/core/live_source.*`: `LiveStateMachine`, `ReconnectPolicy`, `LiveFrameBox`, the abstract `LiveSource`.
- `src/core/ndi_runtime.*` and `src/core/ndi_receiver.*`: the shared NDI loader, discovery (`NdiFinder`) and the receiver. `tests/fake_ndi.cpp` is a fake NDI library for both directions.
- `src/media/camera_capture.*`: `CameraCapture` and the camera list.
- `src/render/live_input.*`: one slot (source, upload, latency); `src/render/engine.*` owns the slots and their lifetime.
- `src/app/ui_live.cpp`: the Clip window controls.

A new kind of source (for example a direct Media Foundation camera or a different network protocol) is a new `LiveSource` that delivers `LiveFrame` objects; the engine, the upload and the panel do not change.
