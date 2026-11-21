# Dependencies

This page lists what NyrexVJ needs to be built, what the release files contain and which libraries the program opens at run time. The licenses are in [THIRD_PARTY_LICENSES.md](../THIRD_PARTY_LICENSES.md).

## What a release file needs

| File | Platform | Needs |
| --- | --- | --- |
| `NyrexVJ.exe` | Windows 10 or newer, 64 bit | A graphics driver with OpenGL 3.3. Nothing else, the C++ runtime is linked in |
| `NyrexVJ-linux-x64` | Linux, 64 bit | glibc 2.35 or newer, an X11 or XWayland session and a driver with OpenGL 3.3. libstdc++ and libgcc are linked in |
| `NyrexVJ-linux-x64.tar.gz` | Linux, 64 bit | The same program with licenses, changelogs and a list of its libraries |

The Linux program is linked against libc, libm and the dynamic loader only. GLFW opens the X11 libraries and the OpenGL driver itself when the window is created. CI runs `scripts/check_linux_deps.sh` on every build: it fails when the program needs any other shared library or a glibc newer than 2.35 (Ubuntu 22.04). The tar.gz contains `DEPENDENCIES-ldd.txt`, the `ldd` output of the shipped binary.

To check a binary yourself:

```
scripts/check_linux_deps.sh NyrexVJ-linux-x64 2.35
```

## Optional libraries opened at run time

Every library below is optional. When one is missing, only the matching feature is off and the program tells you which file it could not find. Nothing here has to be installed to start NyrexVJ.

| Feature | Windows | Linux | Override |
| --- | --- | --- | --- |
| Video clips (FFmpeg 8.x, LGPL build) | `avutil-60.dll`, `swscale-9.dll`, `avcodec-62.dll`, `avformat-62.dll`, `swresample-6.dll` in `ffmpeg\` next to the program or in the user data folder | `libavutil.so.60`, `libswscale.so.9`, `libavcodec.so.62`, `libavformat.so.62`, `libswresample.so.6` in `ffmpeg/` next to the program, in the user data folder `ffmpeg/`, or on the system library path | `NYREXVJ_FFMPEG_DIR` (Linux), `NYREXVJ_NO_FFMPEG=1` switches video off |
| Cameras and capture cards (FFmpeg device library, needs the video libraries above) | `avdevice-62.dll` and `avfilter-11.dll` next to the other FFmpeg DLLs | `libavdevice.so.62` and `libavfilter.so.11` next to the other FFmpeg libraries | the same as for video, `NYREXVJ_FFMPEG_DIR` (Linux) |
| NDI input and output | NDI runtime (`Processing.NDI.Lib.x64.dll`) | `libndi.so.6`, `libndi.so.5` or `libndi.so`, also from `NDI_RUNTIME_DIR_V6` and `NDI_RUNTIME_DIR_V5` | `NYREXVJ_NDI_LIB` |
| Audio capture | Windows audio (WASAPI), part of Windows | `libpulse-simple.so.0` and `libpulse.so.0` (PulseAudio, or PipeWire with its PulseAudio server) | `NYREXVJ_PULSE_LIB` |
| MIDI input, MIDI clock and MTC | Windows multimedia (WinMM), part of Windows | `libasound.so.2` (ALSA sequencer) | `NYREXVJ_ALSA_LIB` |
| LTC timecode input | Windows audio (WASAPI), part of Windows | the same PulseAudio libraries as audio capture | `NYREXVJ_PULSE_LIB` |
| Update download | WinHTTP, part of Windows | `libcurl.so.4` | none |
| Plugins (v0.26.0) | the library of each plugin, loaded with `LoadLibrary` | the library of each plugin, loaded with `dlopen` | none |
| Open a web page or folder | Windows shell | `xdg-open` | none |

The device library number is the same as the number of `avformat`. `libavdevice` itself needs `libavfilter`, so both files have to be in the same folder; the LGPL shared builds of FFmpeg contain them. Cameras need the device library on top of the video libraries; without it video clips still work.

The exact FFmpeg file names that your build looks for are listed in the clip details when a video clip cannot be loaded, together with a button that opens the folder. They depend on the FFmpeg headers the program was built with, so a program built with other headers asks for other numbers.

### FFmpeg on Linux

Download an LGPL shared build of FFmpeg 8.x (for example the `linux64-lgpl-shared` archive of the BtbN builds), and copy the `.so` files from its `lib` folder into an `ffmpeg` folder next to `NyrexVJ-linux-x64`. A distribution package works as well when it provides the same major versions.

## Building

| Needed | Why |
| --- | --- |
| CMake 3.21 or newer, a C++20 compiler | GCC 11 or newer, Clang or Visual Studio 2022 |
| Git and network access during configuration | GLFW 3.4, Dear ImGui 1.93.0 and, on Windows, Spout2 2.007.017 are downloaded by CMake |
| Linux: X11 development packages | `libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev libxext-dev` and an OpenGL development package such as `libgl1-mesa-dev` |
| FFmpeg headers (optional) | Only for video clips. Pass `-DNYREXVJ_FFMPEG_ROOT=<folder with include/>`, or `-DNYREXVJ_VIDEO=OFF` to build without |

The Linux release is built with Wayland support switched off (`-DGLFW_BUILD_WAYLAND=OFF`), so it runs through X11 or XWayland. This keeps window placement for the output windows predictable.

Bundled in the repository: glad (OpenGL loader), stb_image, stb_image_write and nlohmann/json.

The node effect editor (v0.25.0) adds no dependency: the editor is drawn with the Dear ImGui draw list, the document uses the bundled nlohmann/json, and the graph is compiled to GLSL by the program itself. Node effect files are data and need no library to read.

The plugin system (v0.26.0) adds no dependency either: a plugin library is opened with the system's own `LoadLibrary` or `dlopen`, the interface is a C header in the repository (`plugins/include/nyrexvj_plugin.h`, MIT like the rest) and the example plugin needs only a C compiler and the math library. NyrexVJ ships no plugin and no plugin is loaded unless the person enables it; a plugin brings its own licence, see [PLUGINS.md](PLUGINS.md).

## Rules for new dependencies

1. Prefer loading a library at run time with a small self-declared interface over linking it, so the program starts without it.
2. A library that is linked or shipped needs a compatible open source license and an entry in THIRD_PARTY_LICENSES.md.
3. Update this page and `scripts/check_linux_deps.sh` in the same change.
