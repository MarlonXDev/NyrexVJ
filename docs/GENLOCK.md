# Hardware frame lock and genlock

This page says what NyrexVJ does about professional graphics sync hardware (since 0.57.0), what it does not do, and why. It was written for prompt 12 ("Optional: Hardware Genlock"), issue [#130](https://github.com/MarlonXDev/NyrexVJ/issues/130), decision record [ADR 0051](adr/0051-hardware-sync-status.md).

**Short version.** NyrexVJ detects whether the graphics driver lists the swap group extensions and shows that in Settings, Output, below the frame lock. It does not use them. It does not join a swap group, does not bind a barrier, and changes no driver setting. The software frame lock ([SYNC.md](SYNC.md), [OUTPUTS.md](OUTPUTS.md)) is the only frame lock that runs, on every machine. Nothing here was run on a sync board, a professional graphics card or any real hardware.

## What was decided

| Question | Decision |
|---|---|
| Is genlock promised in software? | No. The software frame lock draws all outputs first and swaps them one after the other; it is not a genlock and the program says so. |
| Is anything detected? | Yes. The names of the swap group and genlock extensions the driver lists, the kind of graphics (NVIDIA, AMD, Intel, software renderer, other) and one of three states: not checked, not offered, offered by the driver. |
| Is anything joined or switched on? | No. Joining a swap group is held back until the maintainer approves it and real hardware exists (see "Why joining is not built"). |
| What is the fallback? | The software frame lock. No code reads the new status to change it, so it behaves as before on every machine; the output self test checks that it is still on after the status was read. |
| Can the program tell that a sync board is connected or locked? | No. The extension names say what the driver can do, not what is plugged in. The text on the page says so. |

## What existed before (inventory)

Checked at commit `36d9c5f` (0.56.0). **Existing**, **Partial**, **Not found**, **Not verified**.

| Item | State | Where |
|---|---|---|
| Software frame lock: draw all outputs, then swap them one after the other | Existing | `src/render/output_manager.cpp` (`OutputManager::render`, `setFrameLock`), `src/render/output_window.cpp` (`OutputWindow::swap`) |
| Output delay of 0 to 8 frames from a shared ring of copies | Existing | `src/core/frame_lock.hpp` (`kMaxDelayFrames`, `FrameHistory`), `src/render/texture_history.*` |
| Swap skew measurement, sync bars test pattern | Existing | `src/core/frame_lock.hpp` (`SkewTracker`, `syncBarValue`), `src/render/output_manager.cpp` |
| Setting `outputFrameLock` (default on) and its checkbox | Existing | `src/core/settings.cpp`, `src/app/ui_outputs.cpp` ("Show the same frame on all outputs") |
| Tests of the software lock | Existing | `tests/test_frame_lock.cpp` (`nvj_frame_lock_tests`), `--output-selftest` (`caseFrameLock`) |
| The statement that this is not a hardware genlock | Existing | [SYNC.md](SYNC.md) "Output frame lock", [OUTPUTS.md](OUTPUTS.md), the manual |
| A list of which swap group extensions the driver offers | Not found before 0.57.0 | a search for `swap_group`, `genlock` and `swap_barrier` in `src`, `tests` and `CMakeLists.txt` found nothing |
| A call of any swap group, barrier or genlock function | Not found | none exists, before or after this change |
| Behaviour with a sync board, a professional card or several systems | Not verified | no such hardware was available |

New with 0.57.0: `src/core/hardware_sync.hpp/.cpp` (the assessment, no OpenGL), `src/render/hardware_sync_probe.hpp/.cpp` (reads the driver names), `Engine::hardwareSync()`, one log line at start-up, one status line and a tooltip in Settings, Output, and tests.

## What is known about the hardware and the drivers

Taken from the public documents listed under "Sources". Nothing in this table was run here.

| Fact | Source |
|---|---|
| `WGL_NV_swap_group` (Windows) and `GLX_NV_swap_group` (Linux) let the windows of a group swap their buffers together, and a swap barrier can synchronise groups on different systems. The registry text says it has shipped on Quadro GPUs with framelock support since 2003. Functions: join a group, bind a barrier, query, reset and read a frame counter. | OpenGL registry, NVIDIA |
| The specification says a swap takes place only when every window of the group is ready (a swap was issued for it and its swap interval has elapsed). | OpenGL registry |
| Frame lock and genlock work on specific hardware only. On NVIDIA they need a sync board (Quadro Sync) and a setup of the board and the displays in the driver's tool (`nvidia-settings` on Linux, the control panel on Windows); swap sync across systems requires that the systems are frame locked. | NVIDIA README, NVIDIA control panel help |
| On AMD the FirePro Framelock sample uses `WGL_NV_swap_group` as well, on systems whose FirePro GPUs are connected to an S400 board. | AMD GPUOpen |
| `WGL_I3D_genlock` is listed by NVIDIA next to the swap group extension for Windows. | NVIDIA |
| A developer reported that the application hung in the buffer swap after `wglJoinSwapGroupNV`; a reply says that swapping several windows of one group one after another on one thread can deadlock, because the group is ready only when all its windows have issued their swap. This is a forum report, not a specification. | NVIDIA developer forum |

Two consequences for this program:

1. **Extensions are not hardware.** A driver can list `WGL_NV_swap_group` on a card without a sync board, and the listing says nothing about a cable, a house sync signal or a locked state. The status line therefore says "offered by the driver", never "locked" or "genlock available".
2. **GeForce and integrated graphics are expected to list nothing.** The line then reads "Not offered by this graphics driver". That is the normal case for most machines that run NyrexVJ.

## What the program shows

Settings, Output, below the frame lock and its swap skew text:

- **Hardware frame lock:** `Not checked` (no graphics context was available), `Not offered by this graphics driver`, or `Offered by the driver, not used by NyrexVJ`.
- **Driver extensions:** the names found, only when there are any.
- A tooltip that explains the three limits above: it needs a sync board, cables and a driver setup; the line cannot see a board or a lock; NyrexVJ joins no group and changes no setting.

The same result is written once to the log at start-up (`Hardware frame lock: ... the software frame lock is the one in use`) and to the report of `--gpu-diag-selftest`.

A software renderer (llvmpipe, softpipe, swrast, GDI Generic, Microsoft Basic Render Driver) is never reported as offering anything, even if its driver lists a name. Without a graphics context the state is `Not checked`.

The probe runs once in `Engine::init()` on the main window's context. It reads `GL_VENDOR`, `GL_RENDERER` and asks GLFW for six names (`WGL_NV_swap_group`, `GLX_NV_swap_group`, `WGL_I3D_genlock`, `WGL_I3D_swap_frame_lock`, `GLX_SGIX_swap_group`, `GLX_SGIX_swap_barrier`). It does not create a window, call any swap group function or touch the render loop; the cost is a few string reads at start-up.

## Why joining is not built

The prompt asks for detection, status and integration "where appropriate". Detection and status are built. The integration is held back, on purpose:

- **It can hang the output.** NyrexVJ swaps its output windows one after the other on the render thread (`OutputManager::render`). The forum report above describes exactly that pattern as a way to deadlock a swap group. A fix would mean a different swap design (one thread per window, or one window), which is an architecture change for the maintainer to decide on.
- **It cannot be tried here.** There is no sync board, no professional card and no second system in the test machine. Code that joins a group and has never run once is a risk on a live show, and the hard rule of this project is that rendering must not stall.
- **It needs set-up outside the program.** The board, the cables, the timing server and the house sync are configured in the driver's own tool. The program cannot check them.
- **It would change behaviour for people who have not asked for it.** Every use of a swap group changes when a swap happens. It would have to be an explicit switch that is off by default.

What a later integration would have to contain is in [ADR 0051](adr/0051-hardware-sync-status.md), "Open decisions".

## Tests

Automated (run for this change, Linux, 2 cores, no graphics card, Mesa llvmpipe, software OpenGL):

| Test | What it checks |
|---|---|
| `nvj_hardware_sync_tests` (`ctest`, no OpenGL) | the name lists of software renderers, the vendor from `GL_VENDOR` and `GL_RENDERER`, no context gives `Not checked`, a consumer card gives `Not offered`, a card that lists names gives `Offered by the driver` with the names in a fixed order, a software renderer is never `Offered`, text of any length and text with a null character, the same input gives the same result (60 checks) |
| `NyrexVJ --fresh --no-update-check --no-welcome --gpu-diag-selftest` | the real engine reads the status, a software renderer is not reported as offering anything, the state agrees with the listed names |
| `NyrexVJ --fresh --no-update-check --no-welcome --output-selftest` | the status is read from the driver and the software frame lock is still on afterwards (the existing frame lock checks run as before) |
| `python3 scripts/check_i18n.py` | every new text has a translation in all seven tables |

None of this proves that a sync board works. The mapping from extension names to states is covered; whether real drivers list what the table above says is not.

**Manual tests on real hardware** are listed in [TESTING.md](TESTING.md) as `M-GENLOCK-1` to `M-GENLOCK-4`: what a consumer card, a professional card and a card with a sync board say; the status next to an NVIDIA or AMD control panel with and without a lock; a second graphics vendor on the same machine; and Windows against Linux. They are open. A release that claims hardware frame lock needs them, and a first version of the integration needs them even more.

## Not tested and not done

- Any real graphics card of any vendor. The probe was only run on a software renderer.
- A professional card, a sync board, genlock to a house signal, several systems, swap barriers.
- Windows. The probe uses GLFW's extension query, which also reads the WGL list, but no Windows run was made.
- Wayland or EGL: the GLX and WGL names are not listed there, so the state is `Not offered` even if the card could do it through another API.
- Output windows on another graphics card than the main window. Only the main window's context is asked.
- A switch to join a swap group, and any use of `WGL_I3D_genlock` (timing control).

## Sources

Read in November 2026. The program uses none of the code behind them.

- OpenGL registry: [WGL_NV_swap_group](https://skia.googlesource.com/external/github.com/KhronosGroup/OpenGL-Registry/+/master/extensions/NV/WGL_NV_swap_group.txt) and [GLX_NV_swap_group](https://developer.download.nvidia.com/opengl/specs/GLX_NV_swap_group.txt) (the registry mirror and NVIDIA's copy of the text).
- NVIDIA: [Genlock/Frame lock extensions](https://www.nvidia.com/en-us/drivers/IO-10753) and [Frame lock](https://www.nvidia.com/en-us/drivers/IO-10794).
- NVIDIA driver README, [Configuring Frame Lock and Genlock](https://http.download.nvidia.com/solaris/470.103.01/README/framelock.html), and the [Quadro Sync II user guide](https://images.nvidia.com/content/quadro/product-literature/user-guides/Quadro-Sync-II-User-Guide.pdf).
- AMD GPUOpen, [FirePro Framelock OpenGL sample](https://gpuopen.com/learn/framelock-opengl-sample).
- NVIDIA developer forum, [wglJoinSwapGroupNV](https://forums.developer.nvidia.com/t/wgljoinswapgroupnv/30884) (a user report, not a specification).
