# Image and video clips

Pictures and videos are clips like any other: they sit in the Library, go into any cell, take effects, blend modes, the grade, speed and scale, and react to the audio.

## Where the files go

Put the files into the `media` folder (Library, Files, Media folder opens it) or drag them onto the NyrexVJ window. Dragging copies the files into the media folder in the background, up to 256 MiB each, and the Library finds them at once. A second file with the same name but other content gets a number, `name (2).png`; the same file twice is only added once.

| Kind | File types | Clip name |
|------|------------|-----------|
| Image | `.png` `.jpg` `.jpeg` `.bmp` `.tga` `.gif` (first frame) | `Image: <file name without the ending>` |
| Video | `.mp4` `.mov` `.m4v` `.mkv` `.avi` `.webm` | `Video: <file name without the ending>` |

At most 128 media clips are listed. Presets store the clip name, so a preset keeps working when the file is still in the media folder under the same name. A missing file shows an empty clip and a message in the Clip window; nothing else is disturbed.

## Transparency

Images with an alpha channel (PNG, TGA) keep it. Every layer is composited with premultiplied alpha, so a logo with soft edges lies over the layers below it without a halo. The same holds for HAP Alpha and other videos that carry alpha. All older clips, scenes and effects produce a fully opaque picture and render exactly as before: all 18 factory looks were compared pixel by pixel against v0.12.0 and are identical.

The thumbnails in the Library show transparent areas as a checkerboard.

The picture is fitted into the layer with its own aspect ratio (nothing is stretched, empty areas are transparent), like a Spout input. The Scale slider of the Clip window zooms it.

## Video

Video is decoded with FFmpeg on a worker thread. The decoder never blocks the render loop: if a frame is late, the previous one stays on screen.

- Codecs: whatever your FFmpeg build can decode. Tested: H.264 in `.mp4`, HAP and HAP Alpha in `.mov`. HAP is decoded by FFmpeg's own decoder (the DXT blocks are expanded on the CPU), so it needs more CPU than a texture upload would, but it seeks and loops cleanly.
- The video loops. The position follows the layer clock, so Speed, Tap, Pause and the BPM move it like everything else.
- Videos larger than 1920 pixels in either direction are scaled down while decoding.
- A video that is not on a layer for a few seconds is released; an image after two minutes. Both load again when needed.
- Sound in videos is ignored.

### FFmpeg is not part of NyrexVJ

NyrexVJ does not contain FFmpeg and does not link it. It loads the FFmpeg libraries at run time, so the program starts and works without them and only the video clips are unavailable. This keeps NyrexVJ's MIT licence untouched, because the libraries stay separate files that you control.

On Windows get a **shared LGPL build of FFmpeg 8.1** (for example the `ffmpeg-n8.1-latest-win64-lgpl-shared-8.1.zip` asset of the BtbN FFmpeg-Builds project on GitHub) and copy these files from its `bin` folder into the `ffmpeg` folder inside the NyrexVJ data folder (the Clip window has an Open FFmpeg folder button that shows the exact folder and the exact file names), or next to `NyrexVJ.exe`:

The five files are avutil, swscale, avcodec, avformat and swresample, each with the library version in the name. The Clip window shows the exact names this NyrexVJ was built for; the library version must match the headers the program was built with, which is why the FFmpeg release is fixed.

The Clip window of a video clip lists the files and has a Check again button, so no restart is needed. On Linux install the FFmpeg libraries of your distribution (`libavcodec`, `libavformat`, `libavutil`, `libswscale`, `libswresample`); the program uses the library versions it was built against.

Whoever distributes NyrexVJ together with the FFmpeg libraries has to follow the LGPL for them (notice, source offer, replaceable libraries). The official NyrexVJ downloads do not contain them.

## For developers

- Building with video support needs only the FFmpeg **headers** (`-DNYREXVJ_FFMPEG_ROOT=<folder with include/>`, or the system headers on Linux). Without them the build still works and the Clip window says that video support was left out. `-DNYREXVJ_VIDEO=OFF` leaves it out on purpose. The Windows build workflow downloads the 8.1 headers and libraries; if that fails the build continues without video.
- `src/media/ffmpeg_api.hpp` lists the 28 FFmpeg functions that are used. `src/media/ffmpeg_dyn.cpp` loads them (`dlopen` / `LoadLibraryExW`) and refuses to run when one is missing.
- `NYREXVJ_NO_FFMPEG=1` makes the libraries count as missing (to try the error path). `NYREXVJ_REQUIRE_VIDEO=1` makes `nvj_media_tests` fail instead of skipping when video decoding is unavailable.
- `tests/test_media.cpp` covers the file name rules, premultiplication, image loading with alpha, errors, the library scan, the import of dropped files and, when the libraries are present, H.264 and HAP decoding, seeking, looping, alpha and opening and closing many decoders.

## What was and was not tested

Tested on Linux with FFmpeg 6.1 and software OpenGL: everything above, including the thumbnails and the transparency over other layers. Not tested: a Windows run with the 8.1 DLLs (the Windows workflow builds against them and runs the tests with them, which is the first real test), hardware GPUs, videos in 4K, long videos and the memory use of many videos at once.
