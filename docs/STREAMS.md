# Network streams: RTSP, SRT and UDP input, and the NDI runtime

Version 0.55.0. [ADR 0048](adr/0048-network-stream-inputs.md) has the reasons, [KNOWN_ISSUES.md](KNOWN_ISSUES.md) the open problems (KI-19 to KI-22), [TESTING.md](TESTING.md) the tests, [LIVE.md](LIVE.md) the other live inputs.

## What this version gives you

Nothing changes until you switch it on. With **Allow network stream inputs** off (the default) no stream thread, socket, decoder or port exists, and the four clips **Stream Input 1** to **Stream Input 4** stay empty. The switch is in the details of a Stream Input clip under **Network stream input**. The inputs also need the FFmpeg libraries that video clips and cameras already use ([DEPENDENCIES.md](DEPENDENCIES.md)); without them the page says so and nothing is opened.

| Scheme | Example | What it does |
| --- | --- | --- |
| `rtsp://` | `rtsp://192.168.1.20:8554/live` | Connects to a camera or server and reads the video over TCP (interleaved). User name and password go in the address (`rtsp://user:pass@host/path`). |
| `srt://` | `srt://host:9000` or `srt://:9000?mode=listener` | Connects to a sender (caller), or opens a port and waits for a sender (listener). Options: `latency`, `rcvlatency`, `peerlatency`, `connect_timeout`, `timeout`, `pkt_size`, `passphrase`, `streamid`, `mode`. |
| `udp://` | `udp://@:5000` | Opens a port and receives MPEG-TS packets. Options: `fifo_size`, `buffer_size`, `overrun_nonfatal`, `reuse`, `pkt_size`, `timeout`, `localaddr`. |

Three profiles trade delay against smoothness (Settings of the clip, **Profile**):

| Profile | Allowed lag before a jump | SRT latency | Probe size | Analysis time | Maximum delay | FIFO | No buffering flag |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Low latency | 0.4 s | 120 ms | 1,000,000 bytes | 0.7 s | 0 | 7168 | yes |
| Balanced (default) | 1.0 s | 250 ms | 2,000,000 bytes | 1.5 s | 0.2 s | 14336 | yes |
| Smooth | 2.0 s | 800 ms | 5,000,000 bytes | 3.0 s | 0.5 s | 28672 | no |

(`kProfiles` in `src/core/stream_url.cpp:12`.)

What is **not** here: audio from streams, an RTSP, SRT or UDP **output**, RTSP over UDP or multicast, any SDK for an SDI card, HDR or 10 bit paths, any claim that a stream is encrypted or authenticated beyond what the sender and FFmpeg do. See "Not built" below.

## NDI: which version is it?

The prompt asked for NDI 6 and for proof. What the code does and does not prove (code of 0.54.0 at commit `5cb5f7a`, and this version):

| Question | Answer | Evidence |
| --- | --- | --- |
| How is the NDI runtime found? | Loaded at run time, never linked: `Processing.NDI.Lib.x64.dll` on Windows, `libndi.so.6`, `libndi.so.5`, `libndi.so` on Linux, `libndi.dylib` on macOS | `src/core/ndi_runtime.cpp:71`, `:83`, `:89`-`:96` (before) |
| Which functions are used? | `NDIlib_initialize`, `NDIlib_send_create`, `NDIlib_send_send_video_v2`, `NDIlib_find_create_v2`, `NDIlib_recv_create_v3`, `NDIlib_recv_capture_v2` | `src/core/ndi_runtime.cpp:120`-`:132` (before) |
| Does the repository prove that a user has NDI 6? | No. It looks for `libndi.so.6` first and `libndi.so.5` second, and takes whichever it finds. Nothing in the repository was built against a vendor header of a given version. **NDI 6: NOT VERIFIED** | read from the code |
| What is new in this version? | The program asks the loaded runtime for `NDIlib_version`, cleans the text (printable ASCII only, at most 160 bytes) and shows it in the NDI output settings ("NDI runtime: ...", or "version not reported") and in the log. If the runtime has no `NDIlib_recv_capture_v2` but has `NDIlib_recv_capture_v3`, the receiver uses `v3` | `src/core/ndi_runtime.cpp:122`, `:139`, `:154`, `:159`, `:170` |
| What was tested? | A stand-in library (`tests/fake_ndi.cpp`) that answers like the runtime: with a version text, without one, and with only `v3` (`tests/fake_ndi.cpp` built a second time as `fake_ndi_v3`). Passes in `nvj_ndi_receive_tests` and `nvj_ndi_v3_tests` | `ctest`, 57 of 57 |
| What was not tested? | The real NDI runtime, any real NDI source or receiver, any NDI 6 feature | KI-22 |

NDI audio and NDI metadata are not read: the receiver captures video only (`recvCapture(recv, &video, nullptr, nullptr, 100)`, `src/core/ndi_receiver.cpp:294`, before and after).

## Before and after: what really keeps what

"Before" cites the code of 0.54.0 (commit `5cb5f7a`), "after" the code of this version. "Measured" means a command in "Tests and measurements" below.

| Path | Before (0.54.0) | After (0.55.0) | Evidence |
| --- | --- | --- | --- |
| NDI receive | `NdiReceiver` captures UYVY, UYVA and RGBA video (`src/core/ndi_receiver.cpp:211`, `:217`, `:294`) | the same; `v3` fallback when the runtime lacks `v2`; the version is read | `nvj_ndi_receive_tests`, `nvj_ndi_v3_tests` |
| NDI send | `NdiSender` creates a sender with `clockVideo = false` and sends with `send_send_video_v2` (`src/core/ndi_sender.cpp:63`, `:66`, `:122`) | untouched | `--output-selftest` with the stand-in library |
| Spout (Windows) | `SpoutInput::ReceiveTexture` (`src/render/spout_input_win.cpp:156`); Windows only | untouched | not run here (no Windows) |
| Camera and capture card | DirectShow on Windows (`src/media/camera_capture.cpp:48`, `:59`), `swscale` to UYVY in the file itself (`:358`) | the conversion moved to `convertToUyvy` (`src/media/live_convert.cpp:15`) and the camera calls it (`src/media/camera_capture.cpp:315`); same result | `nvj_camera_tests` (121 checks) pass |
| Live state, lost signal, reconnect | `LiveStateMachine` and `ReconnectPolicy` (`src/core/live_source.hpp:14`, `:25`; lost after 1.5 s `src/core/live_source.cpp:116`, reconnect after 3 s `:121`, attempt timeout 6 s `:126`) | reused for streams; `LiveStatus` gained `jitterMs`, `resyncs`, `detail` (`src/core/live_source.hpp:101`-`:103`) | scenarios: loss seen after 1.4 to 1.5 s, recovered |
| Kinds of live input | NDI, camera, plugin (`LiveInput::sameConfig` `src/render/live_input.cpp:19`, creation `:81`, `:89`, `:104`) | adds `LiveKind::Stream` (`src/render/live_input.cpp:30`, `:107`) | scenarios; `--live-selftest` unchanged |
| Settings | no stream key | `streamInputEnabled` (default false), `streamInputs` (4 addresses), `streamProfile` (0 to 2, default 1) (`src/core/settings.cpp:286`-`:302`) | `nvj_stream_tests` (default left out of the file, round trip, a wrong profile or wrong types read as the default, an address over 1024 bytes dropped) |
| Recording | `VideoRecorder` writes files: H.264, HEVC, ProRes (`src/media/video_recorder.cpp:59`-`:67`, `:825`) | untouched; no network output | read from the code |
| Output of the picture to other programs | NDI and Spout only | the same | read from the code |
| Show without the feature | no stream code runs | no stream code runs: four empty clips and one line of NDI version text | `--live-selftest`, `--output-selftest`, smoke start unchanged |

## Matrix: protocol, platform, hardware

"Tested" means: run in this project's tests on the stated machine. Everything on a real network, a real camera, Windows or a real card is **not tested**.

| Protocol or path | Direction | Linux, FFmpeg 8.1 LGPL, software OpenGL (this container) | Windows | Real hardware or network |
| --- | --- | --- | --- | --- |
| RTSP over TCP | input | tested on loopback with a small RTSP server written for the test (MJPEG over RTP/JPEG, `scripts/test_stream_input.py`); H.264 and H.265 over RTSP were **not** run | built and the unit tests run in the continuous build; the receiver was not run | no real camera, no real network |
| RTSP over UDP or multicast | input | not used (refused: only TCP is set) | - | - |
| SRT caller | input | tested on loopback with `ffmpeg` as sender: MPEG-2 (Low latency, Balanced, Smooth) and H.264 (libopenh264, Balanced) | as above | no network loss, no real encoder |
| SRT listener | input | tested on loopback with `ffmpeg` as caller (MPEG-2, Balanced) | as above | opens a port, see KI-21 |
| SRT with `passphrase` | input | the option is accepted and range checked (`nvj_stream_tests`); no encrypted stream was run | - | - |
| UDP MPEG-TS, unicast | input | tested on loopback with `ffmpeg` (MPEG-2, Balanced); multicast was **not** tested | as above | no real network |
| HEVC, AV1, VP8, VP9 | input | allowed by the codec list; the decoders exist in the test SDK; none of them was run through a stream | - | - |
| Audio of a stream | input | discarded (`AVDISCARD_ALL`, `src/media/stream_capture.cpp:166`) | - | - |
| RTSP, SRT, UDP | output | **not built** | - | - |
| NDI input and output | both | tested only against the stand-in library | not run | real runtime never loaded (KI-22) |
| Spout | both | - | not run here | - |
| SDI | input | no SDK, no claim | - | only through a card that shows up as a camera (DirectShow or Video4Linux2); no card was ever used |

SDI: the program has no SDI code. A capture card with SDI inputs that offers itself as a camera works through **Camera or capture card** like any other camera, if its driver and the FFmpeg device library support it. Whether a given card does is hardware dependent and **not tested**.

## Licence and build check

- FFmpeg is used as before: loaded at run time from an LGPL shared build; NyrexVJ itself links nothing new ([DEPENDENCIES.md](DEPENDENCIES.md)).
- The test SDK (`ffmpeg-n8.1-latest-linux64-lgpl-shared-8.1`) reports `--enable-version3` without `--enable-gpl` and lists the protocols `srt`, `rist`, `udp`, `tcp` and `rtp`. The protocols and decoders therefore come from the FFmpeg build, not from NyrexVJ. A build without them answers with an FFmpeg error text, which is shown in the details of the clip.
- The Windows SDK of the continuous build is named `ffmpeg-n8.1-latest-win64-lgpl-shared-8.1`; whether it contains `srt` was **not checked**.
- The system `ffmpeg` of the container (6.1.1, GPL, with `libx264`) was not used for any test of NyrexVJ's own receiving.

## Security

A stream is untrusted data, and so is the address that someone types or pastes. What the program does (`src/core/stream_url.cpp`, `src/media/stream_capture.cpp`):

- **Schemes.** Only `rtsp://`, `srt://` and `udp://`. `file:`, `http:`, `concat:`, `pipe:`, device names and everything else are refused before FFmpeg sees them. FFmpeg also gets a `protocol_whitelist` of exactly the protocols of that scheme (`src/core/stream_url.cpp:495`).
- **Address.** At most 1024 bytes (`:260`), only characters from a fixed list (everything else must be written as `%XX`), no `..` in the path, a valid host and a port from 1 to 65535. An SRT or UDP address has no path.
- **Options.** Only the keys in the table above, each with a range (for example `latency` 0 to 10,000,000 microseconds, `pkt_size` 188 to 1456 for SRT); an unknown key, a duplicate or a value out of range is refused with a message.
- **What a stream may be.** Video only; at most 8 streams in the container (`src/media/stream_capture.cpp:141`); every stream except the chosen video stream is discarded (`:166`); codecs H.264, HEVC, AV1, MJPEG, MPEG-2, VP8 and VP9 only (`src/core/stream_url.cpp:512`); a picture of 16 to 8192 pixels per side and at most 8192 x 4608 pixels (`:521`), checked before a frame buffer exists.
- **Time.** Every blocking call runs under a deadline and an interrupt callback (`src/media/stream_capture.cpp:122`, `:235`, `:265`), so a sender that never answers cannot hold the program.
- **Secrets.** `maskUrl` (`src/core/stream_url.cpp:414`) hides the password of an RTSP address and the values of options with names like `passphrase`, `streamid`, `pass`, `key`, `token` in the interface and the log; the input field shows dots when the address has a secret.
- **Ports.** A UDP address and an SRT `mode=listener` open a port. The page says so in orange and names the port (KI-21).
- **Not hidden:** the address is stored in `settings.json` as typed, with its password (KI-20).

Tested: `nvj_stream_tests` (261 checks) and the scenarios `reject-codec`, `reject-size`, `garbage` and `corruption`. The last two send random bytes and a damaged MPEG-TS over UDP to the receiver; the program is not ended by a signal, writes no crash text, shows no picture and exits with code 1 (the self test got no usable picture); on the development computer this held in every run, but in the first continuous build the `corruption` scenario did not end within its limit once, and the cause is not known (KI-23). This is not a fuzz test and does not prove the absence of faults in FFmpeg's demuxers and decoders.

## Latency, jitter, drift, colour, alpha, audio, reconnect

- **Latency.** The `latencyMs` of the test is the time from the decoded frame to the upload into the texture (about 17 to 26 ms in the container, [numbers below](#tests-and-measurements)). It is **not** glass to glass: the sender's encoder, the network, the SRT latency (120, 250 or 800 ms by profile) and the output are not in it. Nothing was measured from camera to projector.
- **Jitter.** `JitterMeter` (`src/core/stream_clock.cpp:65`) averages the difference between the arrival step and the stream's own time step over 16 packets and shows it in milliseconds on the page.
- **Clock drift.** `LagTracker` (`src/core/stream_clock.cpp:23`) compares how late the picture is against the lowest lag seen, which rises by 1 ms per second so a sender whose clock runs slow is followed. When the lag stays above the limit of the profile for 0.5 s, the decoder is flushed and the input waits for the next key frame (at most 2 s, `kKeyWaitSeconds`); the page counts these as "catch-ups". `nvj_stream_tests` shows no jump for clock errors of plus and minus 500 ppm and of plus and minus 200 ppm over one hour of simulated time, and a first jump after 4 to 6.5 s when the stream is read at 80 percent speed (wall time 1.25 times stream time). A jump in the stream time of more than 5 s starts the measurement again.
- **Colour.** The picture is converted to 8 bit UYVY with the matrix the stream names (Rec. 709, or Rec. 601 for BT.470BG and SMPTE 170M); a stream that names none is read as Rec. 709 from 720 lines up and Rec. 601 below; full range only when the stream says so (`src/media/live_convert.cpp:43`-`:52`). HDR and 10 bit sources are converted to 8 bit without tone mapping. Whether the colours match another program's was **not checked** against a reference.
- **Alpha.** Streams carry none; the picture is opaque. (NDI with alpha is unchanged: UYVA and RGBA are read as before.)
- **Audio.** Not read, for streams and for NDI.
- **Signal loss and reconnect.** The same state machine as NDI and cameras: after 1.5 s without a picture the state is "signal lost", the last picture or the placeholder stays, after 3 s the input reconnects, the delay between attempts starts at 0.5 s and doubles to at most 10 s, and an attempt is given up after 6 s. RTSP over TCP notices a closed connection at once (`src/media/stream_capture.cpp:272`); SRT and UDP notice it by silence. A listener waits for a sender: an SRT listener stays inside one attempt for up to an hour (`kListenSeconds`), UDP opens at once and the probe waits up to 6 s for packets.

## Tests and measurements

Environment of all numbers: Linux x86-64 cloud container, 2 cores, 8 GB, Xvfb, Mesa llvmpipe (software OpenGL 4.5), no graphics card, loopback network (127.0.0.1), FFmpeg 8.1 LGPL shared SDK for the program and the senders, release build of NyrexVJ 0.55.0 on the branch `feature/network-streams`. One run per row (**not a statistic**); the container's processor is shared.

Commands:

```
ctest --test-dir build
LD_LIBRARY_PATH=$FFMPEG_SDK/lib NYREXVJ_REQUIRE_VIDEO=1 python3 scripts/test_stream_input.py --binary build/NyrexVJ --ffmpeg $FFMPEG_SDK/bin/ffmpeg --out stream.json
python3 scripts/test_stream_input.py ... --scenario srt-h264 --scenario srt-low --scenario srt-smooth   (adds the three that are not in the default list)
build/NyrexVJ --fresh --no-update-check --no-welcome --stream-selftest "balanced@udp://@:5000" --bench-out stream-self.json
```

The driver starts its own virtual display, starts `ffmpeg` as sender (or a small RTSP server for the RTSP scenario), starts the program with `--stream-selftest`, waits for the phases "live", "lost" and "recovered" and checks the picture (a red picture arrives at the middle pixel). All 12 scenarios passed in one run of the final build (the nine defaults and `srt-h264`, `srt-low`, `srt-smooth`, named with `--scenario`); two earlier runs of the same code gave the same pass result and numbers within the noise of one run each (first picture 1.0 to 2.0 s, jitter 0.8 to 3 ms and 0.8 to 14 ms for RTSP over TCP, decode-to-upload 17 to 26 ms).

| Scenario | Profile | Codec, size | First picture | Picture rate | Jitter | Signal loss seen after | Recovered after |
| --- | --- | --- | --- | --- | --- | --- | --- |
| SRT caller | Balanced | MPEG-2, 640x360 | 1.19 s | 25.0 | 3.03 ms | 1.43 s | 4.61 s |
| SRT listener | Balanced | MPEG-2, 640x360 | 1.26 s | 25.0 | 1.36 ms | 1.47 s | 4.61 s |
| UDP MPEG-TS | Balanced | MPEG-2, 640x360 | 1.97 s | 25.0 | 1.36 ms | 1.50 s | 4.93 s |
| RTSP over TCP (own server) | Balanced | MJPEG, 640x360 | 1.16 s | 25.0 | 0.83 ms | 0.0 s (connection closed) | 1.64 s |
| SRT caller | Balanced | H.264 (libopenh264), 640x360 | 1.13 s | 25.0 | 2.38 ms | 1.36 s | 4.63 s |
| SRT caller | Low latency | MPEG-2, 640x360 | 1.01 s | 25.0 | 1.71 ms | 1.42 s | 4.49 s |
| SRT caller | Smooth | MPEG-2, 640x360 | 1.50 s | 25.0 | 1.43 ms | 1.42 s | 4.95 s |

Profile run (the same stream over SRT, one start per profile): first picture 1.00 s, 1.19 s and 1.46 s; decode-to-upload 26.0, 20.5 and 20.7 ms; jitter 1.51, 2.17 and 1.41 ms for Low latency, Balanced and Smooth. The order of the first-picture times follows the probe sizes; the other differences are within the noise of one run each and show no profile effect.

Other scenarios:

- `reject-codec`: a stream with MPEG-4 part 2 is refused with "The video codec is not allowed for network streams: mpeg4"; no picture is shown.
- `reject-size`: a stream of 8400x64 is refused with "The picture size is not allowed for network streams: 8400x64".
- `garbage`, `corruption`: see Security.
- Release after use: when the clip is removed from its layer the input and its graphics memory are released (`releasedWhenUnused`), and the thread count is not above its value before.
- Stopping: a stream input is asked to stop through the interrupt of every blocking FFmpeg call; if its thread has not ended after 3 seconds it is left behind, a log line names the step it was in (`open`, `probe`, `read`, `decode`, `convert` or `close`), and the next start gets a fresh input. This bound was tested with a stand-in thread that never ends (`nvj_stream_tests`), not with a real hang of FFmpeg, which has not been seen on the development computer (KI-23).

The rate is taken from the time stamps of the stream over windows of at least one second; an earlier version computed it from arrival times and showed 34.7 for a burst after the Smooth profile's buffer, which was a measuring fault, not extra pictures.

Everything else: `--live-selftest`, `--flow-selftest tests/data`, `--color-selftest tests/data`, `--output-selftest` (with `NYREXVJ_NDI_LIB=build/fake_ndi.so`), `--shader-selftest`, `--check-shaders` and `--api-selftest` exit with 0 at this version; the graphics here is software, so frame times mean nothing.

## Not tested (so far)

A real RTSP camera or server; H.264 or H.265 over RTSP; a real network with loss, reordering or jitter; SRT encryption; multicast; Windows (built and unit tested in the continuous build only); the real NDI runtime; any SDI card; HEVC, AV1, VP8 or VP9 streams; running next to VRChat; many streams at once on one machine; the graphics memory and processor use of four 1080p or 4K streams; a hardware decoder. These are in [TESTING.md](TESTING.md) as manual tests `M-STREAM-1` to `M-STREAM-5`.

## Not built

- An RTSP, SRT or UDP **output**. An SRT output through the recorder is possible later; RTSP output needs a server.
- RTSP over UDP and multicast.
- Audio from streams and from NDI.
- A promise for SDI. A card only counts as a camera.
- Moving the password out of `settings.json`.
- Hiding the four Stream clips while the feature is off. They show in the clip library and stay empty.

## For developers

- `src/core/stream_url.hpp/.cpp`: address parser, `maskUrl`, `makeOpenPlan` (what is handed to FFmpeg), the profile table, the limits. No FFmpeg, no OpenGL; `nvj_stream_tests`.
- `src/core/stream_clock.hpp/.cpp`: `LagTracker`, `JitterMeter`; `nvj_stream_tests`.
- `src/media/stream_capture.hpp/.cpp`: the thread that opens, reads, decodes and hands frames to the live path; `StreamSupport` says whether it is compiled in and whether the libraries are found.
- `src/media/live_convert.hpp/.cpp`: the conversion to UYVY that the camera and the stream share.
- `src/app/bench.cpp`: `runStreamSelfTest` and `--bench-live SECONDS:stream:[profile@]ADDRESS`.
- `scripts/test_stream_input.py`: the scenario driver; no extra Python packages.
