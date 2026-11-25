import datetime
import glob
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
REQUIRED_DOCS = [
    "docs/RELEASE.md",
    "docs/COMPATIBILITY.md",
    "docs/KNOWN_ISSUES.md",
    "docs/templates/measurement-report.json",
    "docs/templates/test-log.md",
    "tests/hardware/gates.json",
    "scripts/release_gate.py",
]


def read(root, path):
    with open(os.path.join(root, path), encoding="utf-8") as handle:
        return handle.read()


def exists(root, path):
    return os.path.exists(os.path.join(root, path))


def cmake_version(root):
    match = re.search(r"project\(NyrexVJ VERSION (\d+\.\d+\.\d+)", read(root, "CMakeLists.txt"))
    return match.group(1) if match else None


def changelog_top(root, path):
    match = re.search(r"^## (\d+\.\d+\.\d+) - (\d{4}-\d{2}-\d{2})\s*$", read(root, path), re.MULTILINE)
    return (match.group(1), match.group(2)) if match else (None, None)


def version_tuple(text):
    return tuple(int(part) for part in text.split("."))


def check_versions(root):
    problems = []
    version = cmake_version(root)
    if version is None:
        return ["CMakeLists.txt has no project(NyrexVJ VERSION x.y.z"], None
    for path in ("CHANGELOG.md", "CHANGELOG.de.md"):
        top, date = changelog_top(root, path)
        if top is None:
            problems.append(path + ": no entry like '## 0.1.0 - 2026-01-01' found")
            continue
        if top != version:
            problems.append("%s: newest entry is %s but CMakeLists.txt says %s" % (path, top, version))
        try:
            datetime.date.fromisoformat(date)
        except ValueError:
            problems.append(path + ": the date of the newest entry is not a date: " + date)
        headings = re.findall(r"^## (\d+\.\d+\.\d+) - ", read(root, path), re.MULTILINE)
        if len(set(headings)) != len(headings):
            problems.append(path + ": a version appears twice")
        ordered = [version_tuple(item) for item in headings]
        if ordered != sorted(ordered, reverse=True):
            problems.append(path + ": the entries are not in descending order")
    english = re.findall(r"^## (\d+\.\d+\.\d+) - ", read(root, "CHANGELOG.md"), re.MULTILINE)
    german = re.findall(r"^## (\d+\.\d+\.\d+) - ", read(root, "CHANGELOG.de.md"), re.MULTILINE)
    if english != german:
        only = sorted(set(english) ^ set(german), key=version_tuple)
        problems.append("CHANGELOG.md and CHANGELOG.de.md list different versions: " + ", ".join(only))
    status = re.search(r"^> Status: v(\d+\.\d+\.\d+)", read(root, "README.md"), re.MULTILINE)
    if not status or status.group(1) != version:
        problems.append("README.md says status %s but CMakeLists.txt says %s" % (status.group(1) if status else "nothing", version))
    stand = re.search(r"^> Stand: v(\d+\.\d+\.\d+)", read(root, "README.de.md"), re.MULTILINE)
    if not stand or stand.group(1) != version:
        problems.append("README.de.md says Stand %s but CMakeLists.txt says %s" % (stand.group(1) if stand else "nothing", version))
    return problems, version


def check_decisions(root):
    problems = []
    files = sorted(os.path.basename(path) for path in glob.glob(os.path.join(root, "docs", "adr", "[0-9][0-9][0-9][0-9]-*.md")))
    numbers = [name[:4] for name in files]
    if len(set(numbers)) != len(numbers):
        problems.append("two decision files share a number")
    table = read(root, "docs/DECISIONS.md")
    listed = re.findall(r"\| \[(\d{4})\]\(adr/([^)]+)\)", table)
    listed_names = {name for _, name in listed}
    for name in files:
        if name not in listed_names:
            problems.append("docs/DECISIONS.md has no row for docs/adr/" + name)
    for number, name in listed:
        if not name.startswith(number + "-"):
            problems.append("docs/DECISIONS.md: row %s links to %s" % (number, name))
        if not exists(root, "docs/adr/" + name):
            problems.append("docs/DECISIONS.md links to a missing file: docs/adr/" + name)
    if len({number for number, _ in listed}) != len(listed):
        problems.append("docs/DECISIONS.md lists a number twice")
    return problems


def check_options(root):
    problems = []
    options = sorted(set(re.findall(r'arg == "(--[a-z0-9-]+)"', read(root, "src/main.cpp"))))
    texts = [read(root, "README.md"), read(root, "CONTRIBUTING.md"), read(root, "AGENTS.md")]
    for path in glob.glob(os.path.join(root, "docs", "**", "*.md"), recursive=True):
        with open(path, encoding="utf-8") as handle:
            texts.append(handle.read())
    text = "\n".join(texts)
    for option in options:
        if not re.search(re.escape(option) + r"(?![a-z0-9-])", text):
            problems.append("the option %s in src/main.cpp is not named in any document" % option)
    readme = read(root, "README.md")
    german = read(root, "README.de.md")
    for option in options:
        pattern = "`" + option
        if (pattern in readme) != (pattern in german):
            problems.append("the option %s is in the README table of only one language" % option)
    return problems


def check_workflows(root):
    problems = []
    for path in sorted(glob.glob(os.path.join(root, ".github", "workflows", "*.yml"))):
        with open(path, encoding="utf-8") as handle:
            text = handle.read()
        for script in sorted(set(re.findall(r"scripts/[A-Za-z0-9_./-]+\.(?:py|sh|mjs)", text))):
            if not os.path.exists(os.path.join(root, script)):
                problems.append("%s names %s, which does not exist" % (os.path.basename(path), script))
        for gate in sorted(set(re.findall(r"tests/hardware/[A-Za-z0-9_./-]+\.json", text))):
            if not os.path.exists(os.path.join(root, gate)):
                problems.append("%s names %s, which does not exist" % (os.path.basename(path), gate))
    return problems


def check_documents(root):
    problems = []
    for path in REQUIRED_DOCS:
        if not exists(root, path):
            problems.append("a release document is missing: " + path)
    return problems


def check_known_issues(root, version):
    path = "docs/KNOWN_ISSUES.md"
    if not exists(root, path):
        return []
    match = re.search(r"^Checked at version: (\d+\.\d+\.\d+)\s*$", read(root, path), re.MULTILINE)
    if not match:
        return [path + ": no line 'Checked at version: x.y.z'"]
    if version_tuple(match.group(1)) > version_tuple(version):
        return [path + ": checked at a newer version than the build (%s > %s)" % (match.group(1), version)]
    return []


def check_release_mode(root, version):
    problems = []
    path = "docs/KNOWN_ISSUES.md"
    match = re.search(r"^Checked at version: (\d+\.\d+\.\d+)\s*$", read(root, path), re.MULTILINE) if exists(root, path) else None
    if not match or match.group(1) != version:
        problems.append("release check: %s must say 'Checked at version: %s' after the issues were walked through again" % (path, version))
    top, date = changelog_top(root, "CHANGELOG.md")
    if date and datetime.date.fromisoformat(date) > datetime.date.today():
        problems.append("release check: the changelog date %s lies in the future" % date)
    return problems


def run(root, release=False):
    problems, version = check_versions(root)
    problems += check_decisions(root)
    problems += check_options(root)
    problems += check_workflows(root)
    problems += check_documents(root)
    if version:
        problems += check_known_issues(root, version)
        if release:
            problems += check_release_mode(root, version)
    return problems, version


def main(argv=None):
    argv = sys.argv[1:] if argv is None else argv
    root = ROOT
    release = "--release" in argv
    if "--root" in argv:
        root = argv[argv.index("--root") + 1]
    problems, version = run(root, release)
    for problem in problems:
        print(problem)
    if problems:
        return 1
    print("release consistency ok: version %s%s" % (version, ", release mode" if release else ""))
    return 0


if __name__ == "__main__":
    sys.exit(main())
