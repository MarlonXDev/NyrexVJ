# ADR 0051: Hardware frame lock and genlock: detect and show what the driver offers, keep the software frame lock, and do not join a swap group yet

- **Status:** Accepted for the detection and the status line (requested by MarlonXDev as prompt 12). Issue [#130](https://github.com/MarlonXDev/NyrexVJ/issues/130). **The use of a swap group, a barrier or a genlock input is not built and needs the maintainer's decision and real hardware.** Nothing was run on a sync board, a professional graphics card, a second system or Windows.
- **Date:** 2026-11-26
- **Owners:** Maintainers (pending assignment)
- **Related:** [GENLOCK.md](../GENLOCK.md), [SYNC.md](../SYNC.md), [OUTPUTS.md](../OUTPUTS.md), [KNOWN_ISSUES.md](../KNOWN_ISSUES.md), [TESTING.md](../TESTING.md), [ADR 0016](0016-show-sync.md)

## Context
The prompt asked to review the software frame lock and the output delay, to look into professional GPU sync hardware and driver interfaces without promising genlock in software, to add detection, status and integration where appropriate, to keep the software frame lock as the fallback, and to treat real hardware tests as a precondition for a release.

## Findings (what existed before)
- **Existed:** the software frame lock and the output delay (0 to 8 frames) with a shared ring of copies, the swap skew measurement and the sync bars test pattern (`src/core/frame_lock.*`, `src/render/output_manager.cpp`, `src/render/output_window.cpp`), the setting `outputFrameLock`, tests (`nvj_frame_lock_tests`, `--output-selftest`) and text in [SYNC.md](../SYNC.md) that says "It is not a hardware genlock".
- **Not found:** any detection or use of swap group, swap barrier or genlock extensions.
- **Not verified:** any behaviour on hardware that supports them.
- The table with code locations is in [GENLOCK.md](../GENLOCK.md).

## Decision
1. **Detect, do not use.** `readHardwareSyncProbe()` (`src/render/hardware_sync_probe.cpp`) reads `GL_VENDOR`, `GL_RENDERER` and asks GLFW whether six names are listed: `WGL_NV_swap_group`, `GLX_NV_swap_group`, `WGL_I3D_genlock`, `WGL_I3D_swap_frame_lock`, `GLX_SGIX_swap_group`, `GLX_SGIX_swap_barrier`. `assessHardwareSync()` (`src/core/hardware_sync.cpp`, no OpenGL, unit tested) turns that into a vendor class and one of three states: `Unknown` (shown as "Not checked"), `NotOffered`, `OfferedByDriver`.
2. **A name is not a board.** The state says what the driver lists. The program cannot see a sync board, a cable, a house signal or a locked state, so no text says "locked", "available" or "genlock" without the qualifier "offered by the driver", and the tooltip says what the line cannot tell.
3. **A software renderer never counts as offering anything**, even when its driver lists a name.
4. **The software frame lock stays the only lock that runs.** It is on by default as before. No code reads the new status to change it, and the output self test checks that it is still on after the status was read.
5. **Where it is shown.** Settings, Output, below the swap skew text; one log line at start-up; the report of `--gpu-diag-selftest` (`summary.hardwareFrameLock`). No new setting, no new file format, no preset key.
6. **Cost.** One probe at start-up (two string reads and six extension queries, no window, no draw call). No per-frame work.
7. **No swap group call.** The functions `wglJoinSwapGroupNV`, `wglBindSwapBarrierNV`, `glXJoinSwapGroupNV` and the genlock functions of `WGL_I3D_genlock` are not loaded or called.

## Why joining a swap group is not built now
- NyrexVJ swaps its output windows one after the other on one thread. A group swaps only when every member has issued its swap, and a forum answer reports that this pattern can deadlock the swap. Making it safe means another swap design. That is a change to the output path for the maintainer to decide, not a small addition.
- The test machine has no sync board, no professional card and no second system. Untested code on the output path is a risk on a live show, where rendering must not stall.
- The sync board, its cables, the timing server and the house signal are set up in the driver's tool, outside the program.
- Whatever is built later must be an explicit switch that is off by default.

## Alternatives considered
- **Join a swap group when the driver offers it.** Rejected for the reasons above; it would also change the behaviour of machines whose owners did not ask for it.
- **Detect through the driver's own query functions (`wglQueryMaxSwapGroupsNV`, `wglQueryFrameCountNV`).** More information (the number of groups, the frame counter), but it means loading and calling driver functions that were never run here. The names are enough for an honest status line. Left for the follow-up.
- **Report "genlock available" for any NVIDIA or AMD card.** Wrong for most cards and for every card without a board. Rejected.
- **Simulate genlock in software (a shared clock, a phase lock between the render loops).** The prompt forbids presenting software as genlock. The existing network sync and frame lock already do what software can do and say what they are not. Not built.
- **A separate settings page or a new setting.** One line below the frame lock is enough while nothing is switchable.

## Consequences
- With a driver that lists nothing, which is the usual case, the line says "Not offered by this graphics driver" and nothing else changes.
- With a driver that lists names, the line says "Offered by the driver, not used by NyrexVJ" and lists them. Nothing changes in the output, and the page says so.
- New tests run in the continuous build: `nvj_hardware_sync_tests` (60 checks) and the checks added to `--gpu-diag-selftest` and `--output-selftest`.
- A few strings were added in all seven languages.

## Open decisions for the maintainer
1. Should a swap group integration be built at all, and for which of NVIDIA (`WGL_NV_swap_group`, `GLX_NV_swap_group`) and AMD (FirePro S400)? Both use the same extension, so one implementation would cover both.
2. Which swap design? One thread per output window with a barrier, one window with several displays (Mosaic), or accept the risk. This is the larger decision.
3. On which hardware is it tested, who tests it, and what counts as passed (a locked state read from the driver, the sync bars photographed on several outputs, several systems with a barrier)?
4. Should `WGL_I3D_genlock` (timing source and rate) ever be driven from the program, or is the driver's control panel the only place for that?
5. Should the status line read the driver's own group and frame counter queries once it is clear that an integration is wanted?

## Validation and open questions
- Manual tests `M-GENLOCK-1` to `M-GENLOCK-4` in [TESTING.md](../TESTING.md) are open. Until they have run on real cards, the claim of this ADR is only: the names are read, the states follow from them, and the software frame lock is unchanged.
