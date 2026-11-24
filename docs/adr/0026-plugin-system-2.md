# ADR 0026: Plugin System 2.0: a stable C interface 1.x with capability query, lifecycle fixes, limits and error kinds; process isolation evaluated and not built

- **Status:** Accepted for everything that is implemented in v0.33.0 (requested by MarlonXDev, Prompt 09). **Process isolation: Proposed, not implemented, needs a maintainer decision.** Issue [#78](https://github.com/MarlonXDev/NyrexVJ/issues/78).
- **Date:** 2026-11-24
- **Owners:** Maintainers (pending assignment)
- **Related:** [PLUGINS.md](../PLUGINS.md), [ADR 0018](0018-plugin-system.md), [ADR 0012](0012-open-source-vj-platform-plan.md), [INTERFACES.md](../INTERFACES.md), [PERFORMANCE.md](../PERFORMANCE.md)

## Context
ADR 0018 built the plugin system in v0.26.0: a C header, a manifest checked before any code runs, plugins off by default, every call on a thread of its own with a time budget, crash markers and a safe mode list. It said that the header would be declared stable after one release of use and that a separate process for each plugin was rejected for that milestone.

The request asks to check the existing API, example plugin and error handling, to version and document a stable ABI with a capability query and compatibility rules, to evaluate real process isolation for risky plugins as an optional architecture without claiming that crashes in the same process can be caught, to implement safe activation, deactivation, resource limits and error messages, and to add ABI, lifecycle, fuzz and compatibility tests. Package distribution (Prompt 17) is a separate request and was not touched. Process isolation is a large architecture change, so it is only evaluated here.

## Findings (what existed before)
- **ABI.** Structs with a `size` field and `NYREXVJ_HAS_FIELD`, API 1.0. Compatibility was checked on the manifest only. A plugin could not ask what the program offers, and the host table could not grow in a way an old plugin could survive and a new plugin could detect.
- **Lifecycle.** Four faults were found and fixed. How each one is backed: the first with a test that failed against the unchanged host and passes now, the third with a random test that failed on the unchanged settings reader, the second and the fourth by reading the code (see the lines in `plugin_host.cpp` at v0.32.0: `loadMain` calls the entry function at every load, and `threadStarted` raises the counter inside the new thread). The second and the fourth were not reproduced as a failing test against the unchanged host; their tests were written for the new code and pass there.
  1. Switching a plugin off while its library was still loading ended in the state *Loaded*, so the plugin ran although it was off.
  2. The entry function was called again at every re-enable, although libraries are never unloaded (a plugin that initialises global state in it did so twice).
  3. A non-string value in `config.json` or `disabled.json` (for example `{"format":5}`) threw an uncaught `type_error` in the JSON library. The program would have ended at start.
  4. The counter of running plugin threads was raised inside the new thread, so waiting for it to reach zero could return before the thread had started.
- **Limits.** Time per call, log lines, 8 sources, 4 parameters and 16 folders were limited. The number of instances, the number of stuck calls and the share of a thread were not.
- **Errors.** One text per failure, no kind, and the plugin had no way to say why a call failed.
- **Tests.** The plugin tests covered broken plugins, hangs, crashes and the manifest, but not the layout of the structs, an old header, the version combinations, the lifecycle cycles or random input.

## Decision
1. **API 1.x is declared stable, and 1.1 is the first additive step.** The condition of ADR 0018 (one release of use) holds since v0.26.0, but "use" so far means the example plugin and the test plugins, no plugin from outside. The declaration is a promise about the header and the rules in PLUGINS.md and can be taken back in the documents if a maintainer disagrees. 1.1 appends three functions to the host table (`has_capability`, `get_limits`, `set_error`) and adds the `NvjLimits` struct. Nothing is moved, resized or removed. A copy of the 1.0 header (`tests/plugins/abi/v1_0`) is kept, the layout of every struct is checked against it, and a plugin built with it must still load and run (`nyrexvj-legacy`, also run by `--plugin-selftest` in CI).
2. **`api` in the manifest is the lowest version the plugin needs, and the library must report the same numbers.** A difference is a load error. A plugin that wants to run on a 1.0 program writes `"minor": 0` and checks the host fields it uses with `NYREXVJ_HAS_FIELD`.
3. **Capabilities are names.** A plugin asks with `has_capability` or lists names under `requires` in `plugin.json` (at most 16, 1 to 48 characters of `a-z 0-9 - _ .`). A missing name makes the plugin *Not compatible* before any of its code runs. Programs older than 1.1 ignore `requires`, which is why a plugin that needs a 1.1 function also writes `"minor": 1`. The name `isolation.process` is reserved and always 0 here, so a plugin can not mistake this program for one that isolates plugins.
4. **The table the plugin fills is guarded.** `plugin->size` on input is the capacity offered. A plugin that writes a larger `size` or writes behind the table is detected by guard bytes behind the table and behind the source information and fails with the kind *Memory guard*. This detects a plugin that was built for a larger table than the program has. It does not detect any other bad write.
5. **Lifecycle.** The entry function runs once per plugin and per program run, and the table from that call is reused (cached by plugin id and library path). Switching off while loading is honoured. A re-enable waits in the state *Loading* until every instance of the old run has finished `destroy`, so a new instance never overlaps an old one of the same plugin. A failed plugin stays failed until restart.
6. **Resource limits that can be kept in the same process:** 12 instances per plugin, 24 in total, 16 stuck calls per session (each one keeps a thread and its stack; at the limit no further plugin is loaded), and at most 80 % of one thread for the calls of one source. The last limit slows a source down (shown as *Slowed down*) and does not fail it, because a busy plugin that cannot block the picture is not a fault. The numbers are chosen, not measured against a hardware limit. All of them are passed to `PluginHost` in a `PluginLimits` struct so tests can lower them.
7. **Error kinds.** Every failure has a kind (load error, not compatible, call failed, exception, time budget, invalid result, memory guard, limit reached) and a text, both shown in Settings, Plugins and in the log. A plugin can add a reason with `set_error` (at most 200 characters, control characters replaced), which is dropped if the call does not fail.
8. **Settings files never end the program.** Every field read from `config.json` and `disabled.json` is checked for its type. A file that is not valid is handled like a broken file already was.
9. **Tests.** See "Tests" below.

## Process isolation: evaluation
Question: should risky plugins run in a process of their own, as an option?

What it would give, and what it would not:

| Aim | Same process (now) | Helper process | Helper process plus OS rights reduction |
| --- | --- | --- | --- |
| A plugin that is slow or stuck does not stop the picture | Yes (threads and budgets) | Yes | Yes |
| A plugin that crashes does not end the program | **No.** A crash ends NyrexVJ. This can not be caught reliably in the same process. | **Yes.** The program sees the end of the child and goes on. | Yes |
| A plugin that corrupts memory does not damage the program | No | Yes | Yes |
| The memory of a plugin can be limited | No | Possible (`setrlimit` on Linux, job objects on Windows), **not tested** | Yes |
| A plugin can not read files, open sockets or start programs | No | **No.** A child process has the rights of the user. | Possible (job objects and restricted tokens, AppContainer on Windows; seccomp or namespaces on Linux). Each is different work per system and **was not looked at**. |
| Cost per call | none | measured below | measured below |

Isolation of crashes and isolation of rights are two different things. Moving a plugin into a child process buys the first. It buys nothing against a plugin that is written to do harm until the second is built too.

### Measurement
A throwaway program (about 250 lines, not part of the repository) loaded the example plugin and a plugin that crashes on its third call, once directly and once in a child process made with `fork()`, which got the requests over a pipe and wrote the frames into shared memory (`mmap`, 1280 x 720 RGBA, 3.7 MB). Linux x86-64, 2 cores, nothing else running, two runs. NyrexVJ itself was not involved.

| Measurement | In process | In a child process |
| --- | --- | --- |
| One `modulation` call, median | 0.1 µs | 24 to 27 µs |
| One `modulation` call, 99th percentile | 0.1 µs | 50 to 61 µs |
| One `modulation` call, worst of 20,000 | 18 to 44 µs | 2.1 to 3.2 ms |
| One 1280 x 720 `frame` call, median (the example plugin itself needs about 3 ms) | 2.8 to 3.5 ms | 3.9 to 4.1 ms |
| One 1280 x 720 `frame` call, 95th percentile | 3.8 to 4.8 ms | 4.2 to 6.4 ms |
| Modulation at 60 Hz for 5 s, CPU of the host | | 0.31 to 0.32 % of one core |
| Modulation at 60 Hz for 5 s, CPU of the child | | 0.25 to 0.26 % of one core |
| Start of a child (`fork`, load, `create`), median | | 0.29 to 0.31 ms |
| A crash in the child (null write on the third call) | ends the program | the host saw it after 0.17 to 0.18 ms, signal 11, and kept running |

The round trip for a modulation call is about 25 µs, which is 0.15 % of the 16.7 ms of one frame at 60 Hz. A frame source costs about 1.1 ms more for the shared memory and one copy of 3.7 MB, which is 3 % of the 33 ms between two frames at 30 Hz, and it could probably be avoided by uploading from the shared memory directly (not tried).

### What the measurement does not show
- **Windows.** Everything here ran on Linux. A Windows version needs `CreateProcess`, named shared memory and pipes or a socket, and job objects. None of it was tried.
- **A real helper.** The spike used `fork()` from a tiny program. NyrexVJ is multithreaded and holds an OpenGL context, so it can not simply `fork()` and go on running plugin code in the child (only a few functions are safe after `fork` in a multithreaded program). A real design needs a **second executable** that is started with `posix_spawn` or `CreateProcess` and loads the plugin itself. That executable has to ship in every release archive, which touches Prompt 17 (packages) and the size and signing of the release.
- **The rest of the protocol.** Audio and beat snapshots, the four parameters, logging, `set_error`, the start of an instance and its `destroy` all need messages. Their cost was not measured and is small next to the frame.
- **A hung plugin.** The child can hang too. The time budgets still have to be kept by the host, which would then end the child (`kill`) instead of abandoning a thread. This is easier than now (a stuck thread can not be ended, a child can) but was not built.
- **Graphics.** The spike delivers pixels only. A plugin that wants to draw with OpenGL would need a context of its own and sharing of textures between processes. That is a much larger piece of work and not covered by anything here.
- **Real hardware.** There was no graphics card, no VRChat and no projector. The numbers are from a software environment and say nothing about what a helper process costs next to VRChat in VR.

### Recommendation
Not building it now. The measured cost is small enough that it is not the argument against. The arguments against are the second executable and its packaging, the Windows work, the protocol and its versioning, and that crash isolation alone does not make an untrusted plugin safe. If the maintainer wants it, the sensible order is: (1) a helper executable for modulation sources only, because they are cheap and need no shared memory, (2) frame sources through shared memory, (3) a memory limit and a kill on a stuck call with the facilities of each system, (4) a reduction of the rights of the helper, as its own piece of work. Each step would be its own issue and ADR, and the capability `isolation.process` would be set to 1 only when a plugin really runs this way.

**Decision needed from the maintainer:** build it, build it for modulation sources only, or leave it as it is. Nothing in this ADR depends on the answer, and nothing built here has to be undone if the answer is "leave it".

## Not changed, and why
- **Plugin memory** is not limited. It can not be done in the same process (see above).
- **What a plugin may do** (files, network, programs) is not limited. The documents say so on the Plugins page, in PLUGINS.md and here.
- **Prompt 17 (package distribution and signing)** is separate.
- **New kinds of sources** (effects, 3D) were not asked for.
- **The 1.0 behaviour of existing plugins** is unchanged: the example plugin was only moved to 1.1 to show the new functions, and the 1.0 test plugin shows that the old way still works.

## Tests
The tests are in `tests/test_plugins.cpp` and the plugins under `tests/plugins`; the counts of the release are in the changelog.
- **ABI.** The layout (size and offset of every field) of every struct compared with the 1.0 header and with the current one, the 1.0 plugin loaded and run in the 1.1 host, the version combinations (same, older, newer minor, other major, library and manifest disagreeing), compatibility checked before any plugin code runs (a plugin that would crash in its entry function is *Not compatible* and never called).
- **Capabilities.** The names, `requires` (valid, too long, too many, duplicates, unknown), the host function with known, unknown and invalid names, and `isolation.process` being 0.
- **Lifecycle.** The entry function once per run over many enable and disable cycles, disable during loading, enable twice during loading, re-enable while the old instances are still in `destroy`, a failed plugin staying failed, and the thread count back at zero after every case.
- **Limits and errors.** Instance limits per plugin and in total, the stuck-call limit, the duty limit (a source that wants 100 % is slowed and not failed), the error kind of every failure path, the error text from the plugin, and its cut at 200 characters with control characters replaced.
- **Fuzz.** Random input with fixed seeds, so a failure can be repeated: 20,000 mutated manifests, 20,000 mutated settings files (`config.json` and `disabled.json`), 5,000 mutated crash reports, and 400 runs of a fuzz plugin that writes random values, sizes and strings into its table and the frame buffer. No input may end the program, hang it or leave a thread behind. These are random tests in the normal test run, not a coverage guided fuzzer, and about 45,000 inputs in total.
- **Sanitizers.** The plugin tests were also run once with AddressSanitizer and UndefinedBehaviorSanitizer on Linux (gcc 13). The only reports are the null write that the crash test plugin makes on purpose.

## Consequences
- Plugin authors get a header whose rules are written down and tested, a way to ask what the program offers, and error messages that say what went wrong.
- A set of faults that could end the program at start (the settings files) or leave a plugin running after it was switched off are gone, with tests.
- The documents still say that plugins run unsandboxed in the process of NyrexVJ, and the program shows the same warning on the Plugins page.
- Not tested: a plugin by hand on Windows, a real graphics card, VRChat, a plugin from outside. Process isolation exists only as a measurement on Linux.

## Alternatives considered
- **Isolation now, as the default.** Largest gain for crashes, largest cost: a second executable, a protocol, Windows work, and a change for every existing plugin. Needs the maintainer's decision, see above.
- **Catching crashes in the same process** (signal handlers that jump out, structured exception handling around plugin calls). Rejected: after a bad write the state of the program is unknown, and jumping out of a handler leaves locks and the graphics driver in an unknown state. The crash handler only writes the report.
- **Limiting plugin memory with a replaced allocator.** Rejected: a plugin that calls the allocator of its own C library or uses `mmap` is not affected, so it would look like a limit and not be one.
- **A new major version (2.0) for the new functions.** Rejected: appending to the host table is compatible by the rules of ADR 0018, and a major version would make every existing plugin *Not compatible*.
- **Failing a plugin that uses too much of a thread.** Rejected for this change: it would punish a plugin that works correctly. The source is slowed down and the slot says so.
