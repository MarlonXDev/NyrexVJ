# Test log

Copy this file, fill it in on the machine while you test, and keep it with the measurement files. Write only what you saw. Leave a field empty rather than guessing it.

## The machine

| Item | Value |
| --- | --- |
| Tester | |
| Date | |
| NyrexVJ version and build number | |
| File tested (name and SHA-256) | |
| Operating system and version | |
| Processor | |
| Memory | |
| Graphics card | |
| Graphics driver version | |
| Displays and projectors (model, connection, resolution, refresh rate) | |
| Other programs running (VRChat, OBS, a browser, a recorder) | |
| Controllers, capture cards, network devices | |

## The show used

| Item | Value |
| --- | --- |
| Resolution of the master output | |
| Frame rate target and quality setting | |
| Clips (names and codecs) | |
| Effects and blend modes | |
| Outputs and their sizes | |
| Duration of the run | |

## The tests

One block per test from `tests/hardware/matrix.json`.

### Test ID and title

| Item | Value |
| --- | --- |
| Started at | |
| Command or steps as run | |
| Exit code | |
| Result (passed, failed, not run) | |
| Measurement file | |

What I saw:

What I measured (numbers with unit):

If it failed, how to make it happen again:

1.
2.
3.

What I expected and what happened instead:

## Gate output

Paste the output of `python3 scripts/release_gate.py ... --profile ...` here.

## Entry for tests/hardware/results.json

Fill in one entry per test as shown in docs/TESTING.md, including `conditions` with system, driver, resolution, clips, effects and durationSeconds. Then run `python3 scripts/check_test_matrix.py --write`.
