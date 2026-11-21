# FFmpeg setup

NyrexVJ does not contain FFmpeg. It loads the FFmpeg libraries at run time, so the program starts and works without them and only video clips and the conversion of DXV videos are unavailable. This page says what NyrexVJ needs, how the one-click setup works, what it checks and what it does not, and how to set FFmpeg up by hand instead.

## What NyrexVJ needs

| Part | Needed for |
| --- | --- |
| The five FFmpeg libraries (`avutil`, `swscale`, `swresample`, `avcodec`, `avformat`) in the version NyrexVJ was built for | Every video clip |
| A DXV decoder | Converting DXV videos to HAP |
| A HAP encoder that works (`hap`, `hap_alpha` and `hap_q` are tried with a small test picture) | Converting DXV videos to HAP |
| The QuickTime (MOV) muxer | Converting DXV videos to HAP |
| Snappy support in the HAP encoder | Optional, makes the converted files smaller |

The check is silent, runs on a separate thread a moment after the program started and again when you press Check again, so the window opens at the same speed with or without FFmpeg. The result is shown in Settings, Performance, in the section FFmpeg (a table with FFmpeg libraries, DXV decoder, HAP encoder and QuickTime (MOV) writer, each Works or Missing, the version, the licence and where it was loaded from). When FFmpeg is missing, or loaded but not ready for the conversion while your library contains a `.mov` video, a notice card offers the setup. Its button Set up FFmpeg only opens that section, and Not now hides the card for good.

## One-click setup

Nothing is downloaded or installed until you press Download and set up FFmpeg in Settings, Performance. NyrexVJ never starts the download on its own, not after an update and not when it finds a DXV video.

What happens after the press, in order (each step is shown with a progress bar and can be cancelled):

1. **Checksum list.** `checksums.sha256` of the release is loaded and must contain the file that is about to be downloaded.
2. **Download.** The FFmpeg build is downloaded over HTTPS (Windows: WinHTTP, Linux: `libcurl.so.4`, which is loaded at run time, see [DEPENDENCIES.md](DEPENDENCIES.md)) from the release `latest` of [BtbN/FFmpeg-Builds](https://github.com/BtbN/FFmpeg-Builds) on GitHub, for example `ffmpeg-n8.1-latest-win64-lgpl-shared-8.1.zip` or `ffmpeg-n8.1-latest-linux64-lgpl-shared-8.1.tar.xz`. Both addresses (the checksum list and the archive) must start with that release address, only `https` is accepted and the size is limited to 400 MB. GitHub hands the download on to its own storage servers, and those redirects are followed (https only); the protection is the checksum in the next step, not the name of the server.
3. **Verify.** The SHA-256 of the download is compared with the published one. A different value stops the setup and nothing is installed.
4. **Unpack.** The archive is unpacked into a temporary folder next to the target by NyrexVJ's own reader (zip and tar.xz). Unsafe paths (`..`, absolute paths, drive letters), links, encrypted entries and anything larger than the limits are refused. Only the five libraries and `LICENSE.txt` are taken out of the archive; everything else in it is skipped.
5. **Licence.** The `LICENSE.txt` of the archive must be the LGPL text, and the configuration string that FFmpeg stores inside its `avutil` library must be present and must not contain `--enable-gpl` or `--enable-nonfree`. A GPL build, a non-free build or a build whose configuration cannot be read is not installed (see `src/media/ffmpeg_setup.cpp`). This is a check of what the build says about itself, not a legal review.
6. **Install.** The folder is moved into the NyrexVJ data folder as `ffmpeg` (Settings, Performance shows the exact path and has an Open FFmpeg folder button). A file `NYREXVJ_FFMPEG.txt` records the version, the asset name, the address, the SHA-256 and the licence. An `ffmpeg` folder that was already there is renamed to `ffmpeg.previous` first and put back if the move fails; it stays until the next setup.
7. **Load.** The libraries are loaded and the capability check runs again, so the conversion is available without a restart.

**Existing installations are never touched.** The setup writes only into the NyrexVJ data folder. A system FFmpeg (a package of your distribution, a folder on `PATH`) and a copy next to `NyrexVJ.exe` stay as they are. NyrexVJ looks into its own `ffmpeg` folder first, then at the other places listed in [MEDIA.md](MEDIA.md).

**Which build.** The build depends on the FFmpeg version NyrexVJ was compiled against: avcodec 62 (FFmpeg 8.1) and avcodec 63 (FFmpeg 9.0) have a ready-made package. A NyrexVJ that someone built against an older FFmpeg (avcodec 60 or 61, for example the one in Ubuntu 24.04) gets the message that no ready-made build matches, and the manual setup is the way. Supported systems: Windows x64 and ARM64, Linux x64 and ARM64. On other systems the button explains why it is not available.

## Set up by hand

Press Set up by hand in Settings, Performance, FFmpeg (the Clip window of a video clip keeps its Open FFmpeg folder button) and:

1. Get a **shared LGPL build of FFmpeg** that has the five library versions NyrexVJ lists (the list shows the exact file names), the DXV decoder, the HAP encoder and the MOV muxer. The BtbN builds above have all of them; builds of your distribution usually have the decoder and the muxer, and the HAP encoder depends on how it was configured.
2. Copy the libraries (`bin` on Windows, `lib` on Linux) into the `ffmpeg` folder shown there.
3. Press Check again.

You can also point `NYREXVJ_FFMPEG_DIR` at a folder on Linux.

## How far the trust goes

What the setup protects against: a download that was cut off or damaged, a file that is not the one the release lists, an archive that tries to write outside the target folder, a GPL or non-free build, and a setup that runs without you asking.

What it does **not** protect against: a compromised BtbN release or GitHub account. The checksum list comes from the same release as the file and the release called `latest` is replaced by every build, so the checksum proves that the file is the one published there, not that the publisher is honest. The builds are not signed. A pinned hash per release would close that gap but would need a NyrexVJ update for every FFmpeg build, which is not how this release does it (see [ADR 0021](adr/0021-dxv-hap-conversion-and-ffmpeg-setup.md)). If that matters to you, use the manual setup with a build you verified yourself.

The libraries remain separate files under the LGPL. Delete the `ffmpeg` folder in the data folder to remove them; NyrexVJ then behaves as if FFmpeg had never been set up.

## What was and was not tested

Tested on Linux (`nvj_convert_tests` with `NYREXVJ_TEST_NETWORK` set): the capability check with and without the libraries, a real download of the Linux x64 build for FFmpeg 8.1 through the same `libcurl` code the program uses (66 MB, 16 seconds here: checksum list, download progress, checksum, unpacking, licence check, install, loading the installed libraries, DXV decoder, HAP encoder and MOV muxer found), the real Linux `tar.xz` archive and the real Windows `zip` archive of that release (installed by the archive code on Linux, checksum, licence, file list), a wrong checksum, test archives that try to leave the folder, contain links, are encrypted, damaged, cut, too large or have too many entries, fake GPL, non-free and incomplete builds, the replacement of an existing folder (kept as `ffmpeg.previous`, put back when the new build is refused), an address outside the release and cancelling during unpacking and at the very start.

Not tested: the Windows download (WinHTTP) and the loading of the installed DLLs on Windows (both compile in the Windows build in CI and with MinGW, but were never run); the ARM64 packages (only the file names exist in the code); cancelling in the middle of a download of a real file; a computer without `libcurl` (the message exists, the path was read, not run); a download behind a proxy that needs a password; and the avcodec 63 package (FFmpeg 9.0): the archive exists in the release, its checksum matched the published list and its library numbers (`avutil` 61, `swscale` 10, `swresample` 7, `avcodec` and `avformat` 63) match the table in the code, but no NyrexVJ was built against FFmpeg 9.0 headers, so loading it was not tried.
