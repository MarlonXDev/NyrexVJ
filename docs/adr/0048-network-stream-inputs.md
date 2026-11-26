# ADR 0048: Optional network stream inputs (RTSP, SRT, UDP) through FFmpeg, and the NDI runtime version that the program really reads

- **Status:** Accepted for the automated part (requested by MarlonXDev as prompt 29). Issue [#124](https://github.com/MarlonXDev/NyrexVJ/issues/124). Nothing was run with a real RTSP camera, a real network, Windows, a real NDI runtime, SDI hardware or next to VRChat.
- **Date:** 2026-11-26
- **Owners:** Maintainers (pending assignment)
- **Related:** [STREAMS.md](../STREAMS.md), [LIVE.md](../LIVE.md), [INTERFACES.md](../INTERFACES.md), [DEPENDENCIES.md](../DEPENDENCIES.md), [KNOWN_ISSUES.md](../KNOWN_ISSUES.md), [TESTING.md](../TESTING.md), [ADR 0007](0007-spout-receiver.md)

## Context
The prompt asked for NDI 6 and further video input and output paths (RTSP, SRT, SDI), under strict rules: look at what exists first, do not claim NDI 6 without proof, check licence and build before using FFmpeg for new protocols, never claim SDI without hardware, think about latency, jitter, clock drift, alpha, colour, audio, signal loss, reconnect and untrusted streams, and keep the existing NDI and Spout paths and a live show without the feature unchanged.

## Findings (what existed before)
- **Existed:** an NDI sender and receiver that load the vendor runtime at run time (`src/core/ndi_runtime.cpp`: `libndi.so.6`, `libndi.so.5`, `Processing.NDI.Lib.x64.dll`), a live state machine with reconnect policy (`LiveStateMachine`, `ReconnectPolicy` in `src/core/live_source.hpp`), a camera and capture card input through FFmpeg (`src/media/camera_capture.cpp`, DirectShow on Windows), a Spout input and output on Windows, and a recorder that writes files (`src/media/video_recorder.cpp`).
- **Not found:** any network stream input, any code that reads a version from the NDI runtime, any limit on what a stream may be, any measurement of arrival jitter or of the clock of a sender.
- **Not verified:** the NDI runtime on a real machine (the tests use a stand-in library, `tests/fake_ndi.cpp`), and with it the claim "NDI 6". The program asks the runtime for `NDIlib_send_create`, `NDIlib_send_send_video_v2`, `NDIlib_find_create_v2`, `NDIlib_recv_create_v3` and `NDIlib_recv_capture_v2` by name at run time; whether a given runtime has them is decided on the machine. Nothing in the repository proves which version a user has installed.
- The table with code references is in [STREAMS.md](../STREAMS.md).

## Decision
1. **NDI: read the version, do not claim it.** The program asks the loaded runtime for `NDIlib_version`, shows the text (printable ASCII only, at most 160 bytes) next to the NDI settings and writes it to the log. If the runtime has no `NDIlib_recv_capture_v2` but has `NDIlib_recv_capture_v3`, the receiver uses that. Without a version the line says "version not reported". No document and no screen says "NDI 6" unless the runtime itself says so.
2. **One new input kind, `Stream`, next to NDI, Spout and cameras.** Four clips ("Stream Input 1" to "Stream Input 4") use the same path as the other live inputs (`LiveInput`, the live state machine, the placeholder picture when the signal is gone). The input reads with the FFmpeg libraries that the program already loads for video and cameras; no new library, no new licence.
3. **Off by default and invisible when off.** The setting `streamInputEnabled` is `false` by default. While it is off no stream thread, socket, decoder or port exists; the four clips stay empty. The same is true without the FFmpeg libraries.
4. **Only three schemes, written down in code.** `rtsp://` (over TCP only, interleaved), `srt://` and `udp://`. Everything else (`file:`, `http:`, `concat:`, `pipe:`, `tcp:`, device names, anything FFmpeg would accept) is refused before FFmpeg sees it. The address is parsed by the program (`src/core/stream_url.cpp`), limited to 1024 bytes, checked for allowed characters, ports, host names and options; options of FFmpeg that are not on a list of known, range checked keys are refused. FFmpeg gets a `protocol_whitelist` of exactly the protocols of that scheme.
5. **A stream is untrusted data.** Video only (audio, data and subtitle streams are discarded), at most eight streams in a container, only the codecs H.264, HEVC, AV1, MJPEG, MPEG-2, VP8 and VP9, a picture of 16 to 8192 pixels per side and at most 8192 x 4608 pixels, every blocking call under a timeout and an interrupt callback, passwords and secret values masked in the interface and the log.
6. **Three latency profiles instead of free tuning.** Low latency, Balanced (default) and Smooth set the SRT latency, the probe size, the delay and the buffer of FFmpeg and how far the stream may run behind before the program jumps to the next key frame. The numbers are in [STREAMS.md](../STREAMS.md).
7. **Clock drift and jitter are measured, not guessed.** `stream::LagTracker` follows how far the picture lags behind the wall clock and resynchronises to the next key frame when the lag passes the limit of the profile; it tolerates a sender clock that is off by up to 500 ppm. `stream::JitterMeter` reports the spread of the arrival times.
8. **Reconnect reuses the existing policy.** The same `LiveStateMachine` and `ReconnectPolicy` as NDI and cameras (lost after 1.5 s, reconnect after 3 s, delays 0.5 s doubling up to 10 s). A listener (UDP, SRT `mode=listener`) waits for a sender: an SRT listener stays inside one connection attempt for up to an hour (`kListenSeconds` in `src/media/stream_capture.cpp`), a UDP address is opened at once and the probe waits for packets; the state stays on connecting until a sender appears.
9. **SDI: no generic claim.** A card that shows up as a DirectShow or V4L2 camera works through the existing camera input; the program has no SDK for any SDI card and was never run with one.
10. **Colour and alpha.** The picture is converted to UYVY in 8 bit (`src/media/live_convert.cpp`) with the matrix that the stream names (Rec. 709, or Rec. 601 for BT.470BG and SMPTE 170M); a stream that names none is read as Rec. 709 from 720 lines up and Rec. 601 below; the range is full only when the stream says so (JPEG range, `yuvj` formats). Streams have no alpha. HDR, 10 bit and other colour spaces are converted as 8 bit SDR and are not tone mapped.

## Alternatives considered
- **A second network library (live555, a SRT library of its own).** More code, a new dependency and licence, and the FFmpeg path was enough for the three protocols. Rejected.
- **Accepting every FFmpeg URL.** Gives file and pipe access to a pasted address and to any server that answers with a redirect or a playlist. Rejected.
- **Sending RTSP over UDP.** Opens random ports and is the common cause of picture damage on bad networks. Rejected for now; the sender can always offer TCP.
- **An RTSP or SRT output.** An SRT output through the recorder is possible later; RTSP output needs a server and was not built. NDI and Spout stay the outputs.
- **Claiming NDI 6 because the headers of the new SDK would build.** Nothing was built against a vendor header, and the runtime on the machine decides. Rejected.
- **Audio from streams.** The program has no audio path for live inputs (NDI audio is not read either). Not built.

## Consequences
- With the feature off the program behaves as at 0.54.0; the only additions are the NDI version line and the four empty clips in the clip library.
- A listener opens a port on all addresses. The interface says so before the first use and the document names it as a known issue (KI-21).
- Stream addresses with a password are saved in plain text in `settings.json` (KI-20).
- New tests run in the continuous build: `nvj_stream_tests`, `nvj_ndi_v3_tests`, `NyrexVJ --stream-selftest` and `scripts/test_stream_input.py` (real SRT, UDP and RTSP senders from FFmpeg and a small RTSP server in Python).

## Validation and open questions
- Run with a real RTSP camera, a real network with loss and jitter, a hardware SDI card (as a camera), Windows, the real NDI runtime and next to VRChat. Tests `M-STREAM-1` to `M-STREAM-5` in [TESTING.md](../TESTING.md).
- Decide whether the SRT output should be built through the recorder, whether passwords should move out of `settings.json`, and whether the stream clips should be hidden while the feature is off.
