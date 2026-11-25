import json
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MATRIX = os.path.join(ROOT, "tests", "hardware", "matrix.json")
RESULTS = os.path.join(ROOT, "tests", "hardware", "results.json")
DOC = os.path.join(ROOT, "docs", "TESTING.md")
START = "<!-- matrix:start -->"
END = "<!-- matrix:end -->"
VENDOR_DOC = os.path.join(ROOT, "docs", "COMPATIBILITY.md")
VENDOR_START = "<!-- vendors:start -->"
VENDOR_END = "<!-- vendors:end -->"
CONDITION_KEYS = ["system", "driver", "resolution", "clips", "effects"]
SOFTWARE_MARKERS = re.compile(r"llvmpipe|softpipe|swrast|lavapipe|warp|software|virtual|xvfb|basic render", re.IGNORECASE)
DATE = re.compile(r"^\d{4}-\d{2}-\d{2}$")
RANK = {"n/a": 0, "implemented": 1, "automated": 2, "hardware": 3}


def load(path):
    with open(path, encoding="utf-8") as handle:
        return json.load(handle)


def read(path):
    with open(os.path.join(ROOT, path), encoding="utf-8") as handle:
        return handle.read()


def ctest_names():
    return set(re.findall(r"add_test\(NAME (\w+)", read("CMakeLists.txt")))


def cli_options():
    return set(re.findall(r'arg == "(--[a-z0-9-]+)"', read("src/main.cpp")))


def check_matrix(matrix):
    problems = []
    seen = set()
    names = ctest_names()
    options = cli_options()
    platforms = set(matrix.get("platforms", []))
    for area in matrix.get("areas", []):
        label = area.get("id", "?")
        if not area.get("id") or area["id"] in seen:
            problems.append("area id missing or repeated: " + label)
        seen.add(area.get("id"))
        if not area.get("title"):
            problems.append(label + ": no title")
        if set(area.get("platforms", {})) != platforms:
            problems.append(label + ": platforms must list " + ", ".join(sorted(platforms)))
        for key in ("docs", "sources"):
            if not area.get(key):
                problems.append(label + ": no " + key)
            for path in area.get(key, []):
                if not os.path.exists(os.path.join(ROOT, path)):
                    problems.append(label + ": " + key + " path does not exist: " + path)
        for test in area.get("automated", []):
            tid = test.get("id", "")
            if not tid.startswith("A-") or tid in seen:
                problems.append(label + ": automated id missing, wrong or repeated: " + tid)
            seen.add(tid)
            if test.get("kind") == "ctest":
                if test.get("name") not in names:
                    problems.append(tid + ": ctest name not found in CMakeLists.txt: " + str(test.get("name")))
            elif test.get("kind") == "cli":
                used = re.findall(r"(--[a-z0-9-]+)", test.get("command", ""))
                if not used:
                    problems.append(tid + ": command has no option")
                for option in used:
                    if option not in options:
                        problems.append(tid + ": option not found in src/main.cpp: " + option)
                if not test.get("note"):
                    problems.append(tid + ": no note")
            elif test.get("kind") == "script":
                parts = test.get("command", "").split()
                path = next((part for part in parts if part.startswith("scripts/")), "")
                if not path or not os.path.exists(os.path.join(ROOT, path)):
                    problems.append(tid + ": script not found: " + path)
                if not test.get("note"):
                    problems.append(tid + ": no note")
            else:
                problems.append(tid + ": kind must be ctest, cli or script")
        for test in area.get("manual", []):
            tid = test.get("id", "")
            if not tid.startswith("M-") or tid in seen:
                problems.append(label + ": manual id missing, wrong or repeated: " + tid)
            seen.add(tid)
            for key in ("title", "needs", "measure", "criteria"):
                if not test.get(key):
                    problems.append(tid + ": no " + key)
            if len(test.get("steps", [])) < 2:
                problems.append(tid + ": needs at least two steps")
    return problems


def tests_of(matrix):
    found = {}
    for area in matrix["areas"]:
        for kind in ("automated", "manual"):
            for test in area.get(kind, []):
                found[test["id"]] = (area["id"], kind)
    return found


def check_results(matrix, results):
    problems = []
    areas = {area["id"]: area for area in matrix["areas"]}
    tests = tests_of(matrix)
    for index, item in enumerate(results.get("results", [])):
        label = "result " + str(index + 1)
        area = areas.get(item.get("area"))
        if area is None:
            problems.append(label + ": unknown area " + str(item.get("area")))
            continue
        if item.get("platform") not in matrix["platforms"]:
            problems.append(label + ": unknown platform")
        elif not area["platforms"].get(item["platform"]):
            problems.append(label + ": area does not exist on " + item["platform"])
        if item.get("gpu") not in matrix["gpus"]:
            problems.append(label + ": unknown gpu")
        if item.get("outcome") not in ("passed", "failed"):
            problems.append(label + ": outcome must be passed or failed")
        test = tests.get(item.get("test"))
        if test is None or test[0] != item.get("area"):
            problems.append(label + ": test does not belong to the area")
            continue
        state = item.get("state")
        if state not in ("automated", "hardware"):
            problems.append(label + ": state must be automated or hardware")
            continue
        if (state == "hardware") != (test[1] == "manual"):
            problems.append(label + ": hardware results belong to manual tests and automated results to automated tests")
        if not DATE.match(str(item.get("date", ""))):
            problems.append(label + ": date must look like 2026-11-25")
        for key in ("build", "environment", "notes"):
            if not str(item.get(key, "")).strip():
                problems.append(label + ": no " + key)
        if state == "hardware":
            if item.get("gpu") == "software":
                problems.append(label + ": a hardware result cannot use the software gpu")
            if SOFTWARE_MARKERS.search(str(item.get("environment", ""))):
                problems.append(label + ": environment names a software renderer or virtual machine, so this is not a hardware result")
            for key in ("tester", "evidence"):
                if not str(item.get(key, "")).strip():
                    problems.append(label + ": a hardware result needs " + key)
            conditions = item.get("conditions")
            if not isinstance(conditions, dict):
                problems.append(label + ": a hardware result needs conditions with " + ", ".join(CONDITION_KEYS) + " and durationSeconds")
            else:
                for key in CONDITION_KEYS:
                    if not str(conditions.get(key, "")).strip():
                        problems.append(label + ": conditions has no " + key)
                seconds = conditions.get("durationSeconds")
                if isinstance(seconds, bool) or not isinstance(seconds, (int, float)) or seconds <= 0:
                    problems.append(label + ": conditions.durationSeconds must be a number above zero")
                for key in ("system", "driver"):
                    if SOFTWARE_MARKERS.search(str(conditions.get(key, ""))):
                        problems.append(label + ": conditions." + key + " names a software renderer or virtual machine, so this is not a hardware result")
        else:
            if not str(item.get("command", "")).strip():
                problems.append(label + ": an automated result needs the command")
    return problems


def cell(matrix, results, area, platform):
    if not area["platforms"].get(platform):
        return "n/a"
    passed = {"automated": set(), "hardware": set()}
    failed = False
    for item in results.get("results", []):
        if item.get("area") == area["id"] and item.get("platform") == platform:
            if item.get("outcome") == "passed":
                passed[item["state"]].add(item["test"])
            else:
                failed = True
    text = "implemented"
    if passed["hardware"]:
        text = "hardware %d/%d" % (len(passed["hardware"]), len(area["manual"]))
    elif passed["automated"]:
        text = "automated %d/%d" % (len(passed["automated"]), len(area["automated"]))
    if failed:
        text += ", failures recorded"
    return text


def manual_column(results, area):
    if not area["manual"]:
        return "none"
    parts = []
    for test in area["manual"]:
        done = [r["gpu"] for r in results.get("results", []) if r.get("test") == test["id"] and r.get("outcome") == "passed"]
        parts.append(test["id"] + (" done on " + ", ".join(sorted(set(done))) if done else " open"))
    return "; ".join(parts)


def table(matrix, results):
    lines = ["| Area | Windows | Linux | Automatic checks | Manual tests |", "| --- | --- | --- | --- | --- |"]
    for area in matrix["areas"]:
        lines.append("| %s | %s | %s | %d | %s |" % (area["title"], cell(matrix, results, area, "windows"), cell(matrix, results, area, "linux"), len(area["automated"]), manual_column(results, area)))
    return "\n".join(lines)


def vendor_cell(matrix, results, gpu, platform):
    items = [r for r in results.get("results", []) if r.get("gpu") == gpu and r.get("platform") == platform]
    manual = sum(len(a["manual"]) for a in matrix["areas"] if a["platforms"].get(platform))
    automated = sum(len(a["automated"]) for a in matrix["areas"] if a["platforms"].get(platform))
    if gpu == "software":
        done = {r["test"] for r in items if r.get("outcome") == "passed" and r.get("state") == "automated"}
        text = "software OpenGL, automated %d of %d checks, not hardware" % (len(done), automated)
    else:
        done = {r["test"] for r in items if r.get("outcome") == "passed" and r.get("state") == "hardware"}
        text = "hardware %d of %d manual tests" % (len(done), manual) if done else "no hardware run recorded"
    if any(r.get("outcome") == "failed" for r in items):
        text += ", failures recorded"
    return text


def vendor_table(matrix, results):
    lines = ["| Graphics card | Windows | Linux |", "| --- | --- | --- |"]
    for gpu in matrix["gpus"]:
        label = {"nvidia": "NVIDIA", "amd": "AMD", "intel": "Intel", "software": "Software OpenGL (CI, containers, virtual machines)"}.get(gpu, gpu)
        lines.append("| %s | %s | %s |" % (label, vendor_cell(matrix, results, gpu, "windows"), vendor_cell(matrix, results, gpu, "linux")))
    return "\n".join(lines)


def documents(matrix, results):
    return [
        (DOC, "docs/TESTING.md", START, END, table(matrix, results)),
        (VENDOR_DOC, "docs/COMPATIBILITY.md", VENDOR_START, VENDOR_END, vendor_table(matrix, results)),
    ]


def check_doc(matrix, results):
    problems = []
    for path, label, start, end, content in documents(matrix, results):
        if not os.path.exists(path):
            problems.append(label + " does not exist")
            continue
        with open(path, encoding="utf-8") as handle:
            text = handle.read()
        if start not in text or end not in text:
            problems.append(label + " has no matrix markers")
            continue
        inside = text.split(start, 1)[1].split(end, 1)[0].strip("\n")
        if inside != content:
            problems.append("the table in " + label + " is out of date; run python3 scripts/check_test_matrix.py --write")
    return problems


def write_doc(matrix, results):
    for path, _, start, end, content in documents(matrix, results):
        if not os.path.exists(path):
            continue
        with open(path, encoding="utf-8") as handle:
            text = handle.read()
        if start not in text or end not in text:
            continue
        head, rest = text.split(start, 1)
        tail = rest.split(end, 1)[1]
        with open(path, "w", encoding="utf-8", newline="\n") as handle:
            handle.write(head + start + "\n" + content + "\n" + end + tail)


def main():
    matrix = load(MATRIX)
    results = load(RESULTS)
    problems = check_matrix(matrix) + check_results(matrix, results)
    if "--write" in sys.argv and not problems:
        write_doc(matrix, results)
    if not problems:
        problems += check_doc(matrix, results)
    for problem in problems:
        print(problem)
    if problems:
        return 1
    print("test matrix ok: %d areas, %d automated, %d manual, %d results" % (len(matrix["areas"]), sum(len(a["automated"]) for a in matrix["areas"]), sum(len(a["manual"]) for a in matrix["areas"]), len(results["results"])))
    return 0


if __name__ == "__main__":
    sys.exit(main())
