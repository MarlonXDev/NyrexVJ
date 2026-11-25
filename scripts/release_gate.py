import argparse
import json
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GATES = os.path.join(ROOT, "tests", "hardware", "gates.json")
SCHEMA = "nyrexvj-run-report"
SCHEMA_VERSION = 1
CLASSES = {"software-gl": "software", "virtual-gpu": "software", "gpu-reported": "gpu"}
GROWTH_KEYS = ["rss", "gpuEstimate", "threads", "handles"]
REQUIRED = [
    ("conditions.system.os", str),
    ("conditions.system.logicalCores", int),
    ("conditions.system.memoryBytes", int),
    ("conditions.build.version", str),
    ("conditions.build.configuration", str),
    ("conditions.graphics.renderer", str),
    ("conditions.graphics.driver", str),
    ("conditions.graphics.validationClass", str),
    ("conditions.resolution.width", int),
    ("conditions.resolution.height", int),
    ("conditions.durationSeconds", (int, float)),
    ("conditions.scenario", str),
    ("conditions.clips", list),
    ("conditions.effects", list),
]


def load_json(path):
    with open(path, encoding="utf-8") as handle:
        return json.load(handle)


def dig(item, path):
    node = item
    for part in path.split("."):
        if not isinstance(node, dict) or part not in node:
            return None
        node = node[part]
    return node


class Result:
    def __init__(self, gate, status, text, value=None, limit=None):
        self.gate = gate
        self.status = status
        self.text = text
        self.value = value
        self.limit = limit

    def as_dict(self):
        return {"gate": self.gate, "status": self.status, "text": self.text, "value": self.value, "limit": self.limit}


def passed(gate, text, value=None, limit=None):
    return Result(gate, "pass", text, value, limit)


def failed(gate, text, value=None, limit=None):
    return Result(gate, "fail", text, value, limit)


def skipped(gate, text):
    return Result(gate, "skipped", text)


def check_format(report):
    problems = []
    if report.get("schema") != SCHEMA:
        problems.append("schema is not %s" % SCHEMA)
    if report.get("schemaVersion") != SCHEMA_VERSION:
        problems.append("schemaVersion is not %d" % SCHEMA_VERSION)
    for path, kind in REQUIRED:
        value = dig(report, path)
        if value is None:
            problems.append("%s is missing" % path)
        elif not isinstance(value, kind) or isinstance(value, bool):
            problems.append("%s has the wrong type" % path)
        elif isinstance(value, str) and not value.strip():
            problems.append("%s is empty" % path)
        elif isinstance(value, (int, float)) and path.endswith(("width", "height", "durationSeconds", "memoryBytes", "logicalCores")) and value <= 0:
            problems.append("%s must be above zero" % path)
    cls = dig(report, "conditions.graphics.validationClass")
    if isinstance(cls, str) and cls not in CLASSES:
        problems.append("validationClass %r is not one of %s" % (cls, ", ".join(sorted(CLASSES))))
    clips = dig(report, "conditions.clips")
    if isinstance(clips, list) and not clips:
        problems.append("conditions.clips is empty")
    if problems:
        return failed("G-FORMAT", "the report does not name its conditions: " + "; ".join(problems))
    return passed("G-FORMAT", "the report names system, build, graphics driver, resolution, clips, effects and duration")


def evaluate(report, gates, profile_name, exit_code=None):
    results = []
    profile = gates["profiles"][profile_name]
    results.append(check_format(report))
    cls = dig(report, "conditions.graphics.validationClass")
    if exit_code is not None:
        if exit_code == 0:
            results.append(passed("G-EXIT", "the program ended with exit code 0", exit_code, 0))
        else:
            results.append(failed("G-EXIT", "the program ended with exit code %d (a crash or a failed check)" % exit_code, exit_code, 0))
    else:
        results.append(skipped("G-EXIT", "no exit code was given (--exit-code)"))
    if report.get("finished") is True:
        results.append(passed("G-FINISHED", "the run reached its end and wrote the final report"))
    else:
        results.append(failed("G-FINISHED", "the report is not final: the run was cut off or crashed"))
    required = profile.get("requireClass")
    if required is None:
        results.append(passed("G-CLASS", "evidence class %s (%s)" % (cls, "automated, not hardware" if CLASSES.get(cls) == "software" else "real renderer reported, a person must still confirm the machine")))
    elif cls == required:
        results.append(passed("G-CLASS", "the renderer is %s, as the profile requires" % cls))
    else:
        results.append(failed("G-CLASS", "the profile %s needs a %s run, this one is %s: software OpenGL or a virtual GPU is never hardware evidence" % (profile_name, required, cls), cls, required))
    seconds = report.get("wallSeconds", dig(report, "conditions.durationSeconds"))
    minimum = profile["minSeconds"]
    if isinstance(seconds, (int, float)) and seconds >= minimum * 0.98:
        results.append(passed("G-DURATION", "the run lasted %.0f s" % seconds, seconds, minimum))
    else:
        results.append(failed("G-DURATION", "the run lasted %s s, the profile needs %d s" % (seconds, minimum), seconds, minimum))
    frames = report.get("frames", 0)
    results.append(passed("G-FRAMES", "%d frames were rendered" % frames, frames, 1) if isinstance(frames, int) and frames > 0 else failed("G-FRAMES", "no frame was rendered", frames, 1))
    errors = report.get("glErrors")
    results.append(passed("G-GLERROR", "no OpenGL error", errors, 0) if errors == 0 else failed("G-GLERROR", "OpenGL errors: %s" % errors, errors, 0))
    faults = report.get("playingFaultsMax", 0)
    results.append(passed("G-FAULTS", "no playing clip failed", faults, 0) if faults == 0 else failed("G-FAULTS", "%s playing clips failed at the same time" % faults, faults, 0))
    growth = report.get("growth") or {}
    if profile["judgeGrowth"]:
        judged = report.get("judged") is True
        if not judged:
            results.append(failed("G-GROWTH", "growth was not judged: the run is too short or a series is missing"))
        else:
            leaking = [key for key in GROWTH_KEYS if (growth.get(key) or {}).get("leak")]
            if leaking:
                details = ", ".join("%s +%.1f (limit %.1f)" % (key, growth[key]["growth"], growth[key]["limit"]) for key in leaking)
                results.append(failed("G-GROWTH", "growth above the limit: " + details, leaking))
            else:
                results.append(passed("G-GROWTH", "memory, graphics estimate, threads and handles stay within the limits"))
    else:
        results.append(skipped("G-GROWTH", "the profile %s does not judge growth (too short); the long profiles do" % profile_name))
    work = report.get("workMs") or {}
    interval = report.get("worstIntervalMs")
    fps = report.get("achievedFps")
    p50 = work.get("p50")
    p99 = work.get("p99")
    if profile["frameTimes"] == "sanity":
        limits = gates["sanity"]
        ok = isinstance(interval, (int, float)) and interval <= limits["maxWorstIntervalMs"]
        results.append(passed("G-STALL", "the longest gap between frames was %.0f ms" % interval, interval, limits["maxWorstIntervalMs"]) if ok else failed("G-STALL", "the longest gap between frames was %s ms" % interval, interval, limits["maxWorstIntervalMs"]))
        ratio = (p99 / p50) if isinstance(p50, (int, float)) and p50 > 0 and isinstance(p99, (int, float)) else None
        if ratio is not None and ratio <= limits["maxP99ToP50"]:
            results.append(passed("G-SPREAD", "P99 is %.1f times the median frame time" % ratio, ratio, limits["maxP99ToP50"]))
        else:
            results.append(failed("G-SPREAD", "P99 is %s times the median frame time" % ("%.1f" % ratio if ratio is not None else "unknown"), ratio, limits["maxP99ToP50"]))
        results.append(passed("G-PROGRESS", "%.2f frames a second were achieved" % fps, fps, limits["minAchievedFps"]) if isinstance(fps, (int, float)) and fps >= limits["minAchievedFps"] else failed("G-PROGRESS", "only %s frames a second were achieved" % fps, fps, limits["minAchievedFps"]))
        results.append(skipped("G-FRAMETIME", "performance limits are not applied to %s runs: a software renderer says nothing about a graphics card" % ("software" if CLASSES.get(cls) == "software" else "this class of")))
    else:
        limits = gates["performance"]
        target = report.get("targetFps")
        if not isinstance(target, (int, float)) or target <= 0:
            results.append(failed("G-FRAMETIME", "the target frame rate is unknown"))
        else:
            period = 1000.0 / target
            checks = [
                ("P95 work time", work.get("p95"), period * limits["p95PeriodFactor"], "max"),
                ("P99 work time", work.get("p99"), period * limits["p99PeriodFactor"], "max"),
                ("longest work time", work.get("max"), limits["maxWorkMs"], "max"),
                ("share of frames over one period", report.get("overBudgetFraction"), limits["maxOverBudgetFraction"], "max"),
                ("share of missed frames", (report.get("missedFrames", 0) / frames) if isinstance(frames, int) and frames > 0 else None, limits["maxMissedFraction"], "max"),
                ("achieved frame rate", fps, target * limits["minFpsFactor"], "min"),
            ]
            bad = []
            for name, value, limit, kind in checks:
                if not isinstance(value, (int, float)):
                    bad.append("%s is unknown" % name)
                elif kind == "max" and value > limit:
                    bad.append("%s %.3f above %.3f" % (name, value, limit))
                elif kind == "min" and value < limit:
                    bad.append("%s %.2f below %.2f" % (name, value, limit))
            results.append(failed("G-FRAMETIME", "frame times miss the limits: " + "; ".join(bad)) if bad else passed("G-FRAMETIME", "P95, P99, longest frame, frames over a period, missed frames and frame rate are inside the limits"))
        results.append(skipped("G-STALL", "covered by the performance limits"))
    return results


def evaluate_live(report, gates, profile_name):
    results = []
    profile = gates["profiles"][profile_name]
    if report.get("schema") != "nyrexvj-live-report":
        return [failed("L-FORMAT", "the live report has no schema nyrexvj-live-report")]
    conditions = report.get("conditions") or {}
    missing = [key for key in gates["live"]["requiredConditions"] if not str(conditions.get(key, "")).strip()]
    for key in ("system", "durationSeconds"):
        if not conditions.get(key):
            missing.append(key)
    results.append(failed("L-CONDITIONS", "the live report does not name: " + ", ".join(missing)) if missing else passed("L-CONDITIONS", "the live report names system, build, resolution, clips, effects and duration"))
    seconds = report.get("seconds", 0)
    results.append(passed("L-DURATION", "sampled for %.0f s" % seconds, seconds, profile["minSeconds"]) if seconds >= profile["minSeconds"] * 0.98 else failed("L-DURATION", "sampled for %s s, the profile needs %d s" % (seconds, profile["minSeconds"]), seconds, profile["minSeconds"]))
    for name, item in sorted(report.get("summary", {}).items()):
        if isinstance(item, dict) and "seen_running" in item:
            if not item["seen_running"]:
                results.append(failed("L-RUNNING-" + name, "%s was never seen running" % name))
                continue
            growth = item.get("rss_growth") or {}
            if growth.get("judged") and growth.get("leak"):
                results.append(failed("L-RSS-" + name, "%s memory grew by %.1f MiB (limit %.1f)" % (name, growth["growth"], growth["limit"])))
            elif growth.get("judged"):
                results.append(passed("L-RSS-" + name, "%s memory stays within its limit" % name))
            else:
                results.append(skipped("L-RSS-" + name, "%s memory growth was not judged (too short)" % name))
    cards = report.get("summary", {}).get("gpu_cards", {})
    for index, card in sorted(cards.items()):
        growth = card.get("vram_growth") or {}
        if growth.get("judged") and growth.get("leak"):
            results.append(failed("L-VRAM-" + index, "graphics memory of card %s grew by %.1f MiB (limit %.1f)" % (index, growth["growth"], growth["limit"])))
        elif growth.get("judged"):
            results.append(passed("L-VRAM-" + index, "graphics memory of card %s stays within its limit" % index))
    if report.get("gpu_mode") == "none":
        results.append(skipped("L-GPU", "no graphics card data was available from this system"))
    return results


def render(results, report, profile_name):
    cls = dig(report, "conditions.graphics.validationClass") or "unknown"
    lines = []
    if CLASSES.get(cls) == "software":
        lines.append("Evidence class: %s. Automated evidence only. This is NOT a validation on a real graphics card or a projector." % cls)
    elif cls == "gpu-reported":
        lines.append("Evidence class: gpu-reported. The program saw a renderer that is not a software rasteriser. Hardware evidence also needs a named tester, the driver and the machine, recorded in tests/hardware/results.json.")
    else:
        lines.append("Evidence class: unknown. The report does not say which renderer produced it.")
    lines.append("Profile: %s" % profile_name)
    for item in results:
        lines.append("%-8s %-14s %s" % (item.status.upper(), item.gate, item.text))
    failures = [item for item in results if item.status == "fail"]
    lines.append("Result: %s (%d passed, %d failed, %d skipped)" % ("PASSED" if not failures else "FAILED", sum(1 for i in results if i.status == "pass"), len(failures), sum(1 for i in results if i.status == "skipped")))
    return "\n".join(lines)


def markdown(results, report, profile_name):
    cls = dig(report, "conditions.graphics.validationClass") or "unknown"
    renderer = dig(report, "conditions.graphics.renderer") or "unknown"
    version = dig(report, "conditions.build.version") or "unknown"
    banner = {"software": "Software OpenGL only. Automated evidence, **not** a validation on a real graphics card or projector.", "gpu": "A real renderer was reported. Hardware evidence still needs a recorded manual test."}.get(CLASSES.get(cls), "Unknown renderer.")
    lines = ["### Run gates: %s" % profile_name, "", "> %s" % banner, "", "Build `%s`, renderer `%s`, class `%s`" % (version, renderer, cls), "", "| Gate | Status | Detail |", "| --- | --- | --- |"]
    for item in results:
        lines.append("| %s | %s | %s |" % (item.gate, item.status, item.text.replace("|", "/")))
    return "\n".join(lines) + "\n"


def main(argv=None):
    parser = argparse.ArgumentParser(description="Evaluates a NyrexVJ run report against the release gates")
    parser.add_argument("report")
    parser.add_argument("--profile", choices=["smoke", "soak", "hardware-release"], default="smoke")
    parser.add_argument("--gates", default=GATES)
    parser.add_argument("--exit-code", type=int, default=None)
    parser.add_argument("--live", default="")
    parser.add_argument("--json", default="")
    parser.add_argument("--summary", default="")
    args = parser.parse_args(argv)
    try:
        gates = load_json(args.gates)
        report = load_json(args.report)
    except (OSError, ValueError) as error:
        print("cannot read the input: %s" % error)
        return 2
    results = evaluate(report, gates, args.profile, args.exit_code)
    if args.live:
        try:
            results += evaluate_live(load_json(args.live), gates, args.profile)
        except (OSError, ValueError) as error:
            print("cannot read the live report: %s" % error)
            return 2
    print(render(results, report, args.profile))
    if args.json:
        with open(args.json, "w", encoding="utf-8", newline="\n") as handle:
            json.dump({"profile": args.profile, "class": dig(report, "conditions.graphics.validationClass"), "results": [item.as_dict() for item in results]}, handle, indent=2)
            handle.write("\n")
    if args.summary:
        with open(args.summary, "a", encoding="utf-8", newline="\n") as handle:
            handle.write(markdown(results, report, args.profile))
    return 1 if any(item.status == "fail" for item in results) else 0


if __name__ == "__main__":
    sys.exit(main())
