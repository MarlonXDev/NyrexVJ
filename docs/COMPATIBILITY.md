# Compatibility

What NyrexVJ is known to work with, what is known not to, and what nobody has tried. Every row says how it is known. A row is never "works" because it should.

Nothing in this document was measured on a real graphics card, a real projector or a real network. The test machine of the automated runs is a Linux container with two processor cores, Mesa llvmpipe (software OpenGL), no graphics card and no display. The words below mean this:

| Word | Meaning |
| --- | --- |
| tested on hardware | A person ran a manual test of [tests/hardware/matrix.json](../tests/hardware/matrix.json) on a real machine and recorded it in [tests/hardware/results.json](../tests/hardware/results.json) with system, driver, resolution, clips, effects and duration. |
| automated | A test ran with software OpenGL, fake libraries or test sources. It shows that the code does what it was written to do. It does not show that a graphics card, a driver or a device does. |
| implemented | The code exists. Nothing was recorded. |
| untested | Nobody has tried it. This is the state of every combination that is not in a table below as tested. |

How a run is classed (software, virtual machine or a renderer that may be a real card) and which gates a release has to pass is in [RELEASE.md](RELEASE.md). Known problems with steps to reproduce are in [KNOWN_ISSUES.md](KNOWN_ISSUES.md).

## Graphics cards and operating systems

The table is generated from the results file by `python3 scripts/check_test_matrix.py --write`. It counts the manual tests that were recorded as passed on hardware for that card and system, out of all manual tests that exist for that system, and keeps software OpenGL on its own row so that it cannot be mistaken for a card.

<!-- vendors:start -->
| Graphics card | Windows | Linux |
| --- | --- | --- |
| NVIDIA | no hardware run recorded | no hardware run recorded |
| AMD | no hardware run recorded | no hardware run recorded |
| Intel | no hardware run recorded | no hardware run recorded |
| Software OpenGL (CI, containers, virtual machines) | software OpenGL, automated 0 of 166 checks, not hardware | software OpenGL, automated 132 of 163 checks, not hardware |
<!-- vendors:end -->

Reading it today: no hardware run is recorded for any card on any system. The program is built and unit tested on Windows and Linux for every change. It was run (self tests, soak, screenshots) on Linux with software OpenGL only. No test starts it on Windows.

Operating systems and builds:

| System | Built in the continuous build | Run by a test | Notes |
| --- | --- | --- | --- |
| Windows 10 and 11, 64 bit | Yes, `windows-latest`, 64 bit, with FFmpeg when it can be downloaded | The unit tests (`ctest`) and the Python checks | The workflow never starts `NyrexVJ.exe` itself: no self test, no soak, no screenshot. Spout, the Windows system information of the reports, the driver counters of `scripts/measure_live.py` and hot plugging are manual. |
| Linux, 64 bit, glibc 2.35 or newer | Yes, Ubuntu 22.04 | Unit tests, self tests, soak smoke tests, on Mesa llvmpipe | `scripts/check_linux_deps.sh` checks that the file needs no newer glibc symbols than 2.35. No Wayland-only run: the build uses X11. |
| macOS | No | No | No build exists and nothing is claimed. |

## Video codecs and decode paths

The self test `NyrexVJ --video-selftest tests/data` decodes each test clip on the processor and through the route the program picks, compares the pictures and fails if they differ more than a limit. Its result on the build machine (Linux, llvmpipe, FFmpeg decoders in software, no hardware decoder) was **passed for all 20 clips**:

| File | Decoder | Route | Mean difference | Worst difference |
| --- | --- | --- | --- | --- |
| `test_cs_2020_444_10.mp4` | hevc | gpu convert | 0.00 | 2 |
| `test_cs_2020_full10.mp4` | hevc | gpu convert | 1.74 | 82 |
| `test_cs_2020_limited10.mp4` | hevc | gpu convert | 1.87 | 94 |
| `test_cs_601_limited.mp4` | h264 | gpu convert | 2.50 | 114 |
| `test_cs_709_full.mp4` | h264 | gpu convert | 2.40 | 106 |
| `test_dxv_dxt1.mov` | dxv | cpu rgba | 0.00 | 0 |
| `test_h264.mp4` | h264 | gpu convert | 2.42 | 108 |
| `test_h264_422_10.mp4` | h264 | gpu convert | 1.91 | 87 |
| `test_h264_444.mp4` | h264 | gpu convert | 0.00 | 2 |
| `test_hap.mov` | HAP direct upload | compressed direct | 0.27 | 3 |
| `test_hap_alpha.mov` | HAP direct upload | compressed direct | 0.02 | 3 |
| `test_hap_chunks.mov` | HAP direct upload | compressed direct | 0.27 | 3 |
| `test_hap_plain.mov` | HAP direct upload | compressed direct | 0.27 | 3 |
| `test_hap_q.mov` | HAP direct upload | compressed direct | 0.61 | 24 |
| `test_hdr_hlg.mp4` | hevc | gpu convert | 1.87 | 94 |
| `test_hdr_pq.mp4` | hevc | gpu convert | 1.87 | 94 |
| `test_hevc10.mp4` | hevc | gpu convert | 1.69 | 98 |
| `test_index_h264.mp4` | h264 | gpu convert | 1.07 | 3 |
| `test_prores_alpha.mov` | prores | gpu convert | 0.00 | 2 |
| `test_ycocg.mkv` | ffv1 | gpu convert | 0.00 | 0 |

"Route" is what the program chose: `gpu convert` sends the colour planes to the graphics card and converts there, `compressed direct` uploads HAP blocks without decoding, `cpu rgba` lets FFmpeg convert on the processor (DXV). The difference columns compare the picture of that route with FFmpeg's own conversion, per colour channel out of 255, over the whole picture (mean) and for the single worst pixel. A clip passes when the mean difference is under 4, the route is the expected one and the picture is not black. The worst pixel is only reported, it has no limit. The clips are small synthetic test files, so these numbers say nothing about real footage.

What this does not say: nothing here was run through NVDEC, D3D11VA, DXVA2, VAAPI or any other hardware decoder, because the machine has none. The hardware paths exist in the program and are covered by the manual test `M-FF-1`. HDR files (PQ, HLG) are detected and shown without tone mapping, see [MEDIA.md](MEDIA.md). Sound in videos is ignored. A video that is cut off may stay silent without a message, see [KNOWN_ISSUES.md](KNOWN_ISSUES.md).

Containers and codecs other than these are decoded if the FFmpeg build can decode them, and are untested.

## Inputs, outputs and the PCs around the program

| Link | Automated evidence | Not tested | Manual test |
| --- | --- | --- | --- |
| Spout output and input (Windows only) | Built in the Windows job, never run. `--spout-test-pattern` and `nvj_output_tests` cover the parts that do not need Spout | Any Spout receiver or sender, OBS, Unreal, Unity, VRChat capture | `M-SPOUT-1`, `M-SPOUTIN-1` |
| NDI output and input | `nvj_ndi_tests`, `nvj_ndi_receive_tests`, `nvj_live_tests`, `--output-selftest` and `--live-selftest` against a fake NDI library | The real NDI runtime, a network, another NDI program | `M-NDIOUT-1`, `M-NDIIN-1` |
| MIDI input, clock and feedback | `nvj_midi_clock_tests`, `nvj_control_tests`, `nvj_linux_midi_tests` with a fake ALSA, `--midi-inject` | A real controller or interface, Windows MIDI | `M-MIDI-1` |
| OSC input and output | `nvj_control_tests` | Unreal Engine, TouchOSC, VRChat | none yet, see the OSC rows of [INTERFACES.md](INTERFACES.md) |
| Art-Net / DMX | `nvj_venue_tests`, `nvj_control_tests` | A real node, a lighting desk, a fixture | `M-ART-1` |
| Projectors, several displays, hot plugging | `nvj_output_tests`, `nvj_frame_lock_tests`, `--output-selftest` (output window logic, slices, warp, blending, frame lock) | Any real projector or display, unplugging one during a show | `M-DISP-1` |
| Cameras and capture cards | `nvj_camera_tests`, `nvj_live_tests` with fake sources | A real camera or capture card | `M-CAM-1` |
| Several PCs: sync and hot standby | `nvj_netsync_tests`, `nvj_failover_tests`, self tests, two processes on one PC | Two real PCs, a real network, a pulled cable | `M-SYNC-1` and the areas for multi-PC sync and hot standby |
| Running next to VRChat | `scripts/measure_live.py` can sample both | Any run next to VRChat | `M-VRC-1` |
| Recording | `nvj_record_tests`, `--record-selftest` | Hardware encoders | `M-RECORD-1` to `M-RECORD-5` |

The manual tests are written out in `tests/hardware/matrix.json` and listed with their status in [TESTING.md](TESTING.md).

## File formats: older and newer files

The rules for every file and protocol are in [INTERFACES.md](INTERFACES.md). The test `nvj_compat_tests` checks the behaviour of the code today with files kept in `tests/data/compat`:

| Format | Number in the file | An older shape | A newer shape |
| --- | --- | --- | --- |
| Preset and session (`presets/*.json`, `autosave.json`) | `format` 2, written, not checked | A file with only a `layers` list loads with defaults for everything else | Unknown keys are ignored. They are lost when the preset is saved again (read from the code). There is no refusal, so a preset that needs something the older program lacks loads without it and without a message |
| Settings (`settings.json`) | none | A file with a few keys loads, the rest stay at their defaults, values off the allowed steps snap to the nearest step (read from the code) | Unknown keys are ignored and dropped on the next save. A file that is not JSON loads nothing and the defaults apply |
| Venue setup (`venues/*.json`) | `version` 1, checked | A file without a version loads | Refused with the message "This venue setup was made with a newer version of NyrexVJ" |
| Controller profile (`*.json` with `nyrexvj-midi-profile`) | `version` 1, checked | A file without a version is refused with "The profile has no valid version" | Refused with "The profile was written by a newer version of NyrexVJ" |
| Cue list (`cues.json`) | `version` 1, written, not checked | A cue without a value gets 1.0 | Unknown keys are ignored |
| Show timeline (`timeline.json`) | `version` 1, written, not checked | A key without a shape is a linear key, a file without a loop has none | Unknown keys are ignored |
| Show and community packages, clip packs, scene documents | version numbers, checked | see [INTERFACES.md](INTERFACES.md) | Refused with a message that names the version (read from the code; not part of `nvj_compat_tests`) |

Two kinds of fixtures are kept. Files named `*_v0.50.0.json` were written by the code of 0.50.0 and are frozen: the test loads them, checks values, and checks that the current code writes the same JSON again, so a change of the writer fails the test until somebody writes fixtures for the new version on purpose. Files named `*_older*.json` or `venue_no_version.json` were written by hand to show an older shape. **They were not written by an older release.** They show what the loader accepts today, not what 0.30.0 really wrote. No file of an older release has been collected yet; when one comes in, it belongs into `tests/data/compat` with the release number in its name.

Since 0.64.0 the settings fixture is `settings_v0.64.0.json` (five audio keys were added); `settings_v0.50.0.json` is kept and loaded as a settings file of an earlier release, with the new keys at their defaults.

What nobody has checked: opening a file of 0.49.0 or older on a real user's machine with real media, and opening a 0.50.0 preset in an older program.
