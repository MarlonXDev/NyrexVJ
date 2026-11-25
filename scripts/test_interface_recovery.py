import argparse
import json
import os
import shutil
import subprocess
import sys
import tempfile

GOOD_LAYOUT = "[Window][Probe]\nPos=10,10\nSize=300,200\nCollapsed=0\n\n"


def run_app(binary, data_dir, extra=None, frames=40):
    env = dict(os.environ)
    env["NYREXVJ_DATA_DIR"] = data_dir
    env.setdefault("LIBGL_ALWAYS_SOFTWARE", "1")
    command = [binary, "--fresh", "--no-update-check", "--no-welcome", "--audio-demo", "--frames", str(frames)]
    command += extra or []
    try:
        done = subprocess.run(command, env=env, capture_output=True, text=True, timeout=180)
    except subprocess.TimeoutExpired:
        return 124, "timeout"
    return done.returncode, (done.stdout or "") + (done.stderr or "")


def write(path, text):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8", newline="\n") as handle:
        handle.write(text)


def read(path):
    with open(path, "r", encoding="utf-8", errors="replace") as handle:
        return handle.read()


def load_json(path):
    try:
        with open(path, "r", encoding="utf-8") as handle:
            return json.load(handle)
    except (OSError, ValueError):
        return None


class Suite:
    def __init__(self, binary):
        self.binary = binary
        self.results = []
        self.base = tempfile.mkdtemp(prefix="nvj-recovery-")
        self.counter = 0

    def folder(self):
        self.counter += 1
        path = os.path.join(self.base, "case%d" % self.counter)
        os.makedirs(path)
        return path

    def record(self, name, passed, detail):
        self.results.append({"name": name, "passed": bool(passed), "detail": detail})
        print("%s: %s (%s)" % ("PASS" if passed else "FAIL", name, detail))

    def start(self, folder, extra=None):
        code, output = run_app(self.binary, folder, extra)
        return code, output

    def case_first_run(self):
        folder = self.folder()
        code, _ = self.start(folder)
        layout = os.path.join(folder, "layout_v6.ini")
        ok = code == 0 and os.path.isfile(layout) and "[Window]" in read(layout)
        self.record("a first start exits cleanly and writes a layout", ok, "exit %d" % code)
        return folder

    def case_damaged_layout(self):
        folder = self.folder()
        garbage = "this is not a layout\n\x01\x02 = = =\n"
        write(os.path.join(folder, "layout_v6.ini"), garbage)
        code, _ = self.start(folder)
        bad = os.path.join(folder, "layout_v6.ini.bad")
        fresh = os.path.join(folder, "layout_v6.ini")
        self.record("a damaged layout is set aside and the start still works", code == 0 and os.path.isfile(bad) and read(bad) == garbage, "exit %d" % code)
        self.record("a new usable layout is written after a damaged one", os.path.isfile(fresh) and "[Window]" in read(fresh) and read(fresh) != garbage, "layout present: %s" % os.path.isfile(fresh))

    def case_wild_layout(self):
        folder = self.folder()
        wild = "[Window][Deck]\nPos=99999999,99999999\nSize=10,10\n\n"
        write(os.path.join(folder, "layout_v6.ini"), wild)
        code, _ = self.start(folder)
        self.record("a layout with positions far outside any screen is set aside", code == 0 and os.path.isfile(os.path.join(folder, "layout_v6.ini.bad")), "exit %d" % code)

    def case_good_layout_kept(self):
        folder = self.folder()
        write(os.path.join(folder, "layout_v6.ini"), GOOD_LAYOUT)
        code, _ = self.start(folder)
        self.record("a usable layout is not set aside", code == 0 and not os.path.exists(os.path.join(folder, "layout_v6.ini.bad")), "exit %d" % code)

    def case_damaged_settings(self):
        folder = self.folder()
        broken = "{ this is not json"
        write(os.path.join(folder, "settings.json"), broken)
        code, _ = self.start(folder)
        bad = os.path.join(folder, "settings.json.bad")
        after = load_json(os.path.join(folder, "settings.json"))
        self.record("damaged settings are set aside and the start still works", code == 0 and os.path.isfile(bad) and read(bad) == broken, "exit %d" % code)
        self.record("usable settings are written after damaged ones", isinstance(after, dict), "settings parsed: %s" % isinstance(after, dict))

    def case_scale_limits(self):
        folder = self.folder()
        write(os.path.join(folder, "settings.json"), json.dumps({"uiScale": 99.0, "uiContrast": "banana", "uiAutoScale": "yes", "uiKeyboardNav": 7}))
        code, _ = self.start(folder)
        after = load_json(os.path.join(folder, "settings.json")) or {}
        scale = after.get("uiScale", 1.0)
        self.record("an interface size far out of range is limited", code == 0 and 0.699 <= scale <= 2.501, "exit %d, uiScale %s" % (code, scale))
        self.record("unknown contrast and wrong value types fall back to the defaults", after.get("uiContrast", "standard") == "standard" and not after.get("uiAutoScale", False) and not after.get("uiKeyboardNav", False), json.dumps({k: after.get(k) for k in ("uiContrast", "uiAutoScale", "uiKeyboardNav")}))
        folder = self.folder()
        write(os.path.join(folder, "settings.json"), json.dumps({"uiScale": -4.0}))
        code, _ = self.start(folder)
        after = load_json(os.path.join(folder, "settings.json")) or {}
        scale = after.get("uiScale", 1.0)
        self.record("a negative interface size is limited", code == 0 and 0.699 <= scale <= 2.501, "exit %d, uiScale %s" % (code, scale))

    def case_damaged_shortcuts(self):
        folder = self.folder()
        broken = "{ \"version\": 1, \"bindings\": ["
        write(os.path.join(folder, "shortcuts.json"), broken)
        code, _ = self.start(folder)
        bad = os.path.join(folder, "shortcuts.json.bad")
        self.record("damaged shortcuts are set aside and the start still works", code == 0 and os.path.isfile(bad) and read(bad) == broken, "exit %d" % code)
        folder = self.folder()
        no_version = json.dumps({"bindings": {"blackout": "K"}})
        write(os.path.join(folder, "shortcuts.json"), no_version)
        code, _ = self.start(folder)
        self.record("shortcuts without a version number count as damaged", code == 0 and os.path.isfile(os.path.join(folder, "shortcuts.json.bad")), "exit %d" % code)

    def case_newer_shortcuts(self):
        folder = self.folder()
        newer = json.dumps({"version": 999, "bindings": {"blackout": "K"}, "future": True}, indent=2) + "\n"
        write(os.path.join(folder, "shortcuts.json"), newer)
        code, _ = self.start(folder)
        path = os.path.join(folder, "shortcuts.json")
        self.record("shortcuts from a newer version are left untouched", code == 0 and os.path.isfile(path) and read(path) == newer and not os.path.exists(path + ".bad"), "exit %d" % code)

    def case_valid_shortcuts_kept(self):
        folder = self.folder()
        good = json.dumps({"version": 1, "bindings": {"blackout": "Ctrl+K"}}, indent=2) + "\n"
        write(os.path.join(folder, "shortcuts.json"), good)
        code, _ = self.start(folder)
        path = os.path.join(folder, "shortcuts.json")
        self.record("usable shortcuts are neither set aside nor rewritten", code == 0 and read(path) == good and not os.path.exists(path + ".bad"), "exit %d" % code)

    def case_unknown_shortcut_entries(self):
        folder = self.folder()
        text = json.dumps({"version": 1, "bindings": {"blackout": "Ctrl+K", "does.not.exist": "Q", "tap": "NotAKey"}}, indent=2) + "\n"
        write(os.path.join(folder, "shortcuts.json"), text)
        code, _ = self.start(folder)
        self.record("unknown actions and unknown keys in a shortcut file do not stop the start", code == 0, "exit %d" % code)

    def case_reset_layout(self):
        folder = self.folder()
        good = json.dumps({"version": 1, "bindings": {"blackout": "Ctrl+K"}}, indent=2) + "\n"
        write(os.path.join(folder, "shortcuts.json"), good)
        write(os.path.join(folder, "settings.json"), json.dumps({"uiScale": 1.5}))
        write(os.path.join(folder, "layout_v6.ini"), GOOD_LAYOUT)
        code, _ = self.start(folder, ["--reset-layout"])
        bad = os.path.join(folder, "layout_v6.ini.bad")
        after = load_json(os.path.join(folder, "settings.json")) or {}
        self.record("--reset-layout sets the layout aside", code == 0 and os.path.isfile(bad) and read(bad) == GOOD_LAYOUT, "exit %d" % code)
        self.record("--reset-layout keeps shortcuts and interface size", read(os.path.join(folder, "shortcuts.json")) == good and abs(after.get("uiScale", 1.0) - 1.5) < 0.001, "uiScale %s" % after.get("uiScale"))

    def case_reset_interface(self):
        folder = self.folder()
        write(os.path.join(folder, "shortcuts.json"), json.dumps({"version": 1, "bindings": {"blackout": "Ctrl+K"}}))
        write(os.path.join(folder, "settings.json"), json.dumps({"uiScale": 2.0, "uiContrast": "high", "uiAutoScale": True, "uiKeyboardNav": True, "theme": "Blue"}))
        write(os.path.join(folder, "layout_v6.ini"), GOOD_LAYOUT)
        code, _ = self.start(folder, ["--reset-interface"])
        after = load_json(os.path.join(folder, "settings.json")) or {}
        self.record("--reset-interface sets layout and shortcuts aside", code == 0 and os.path.isfile(os.path.join(folder, "layout_v6.ini.bad")) and os.path.isfile(os.path.join(folder, "shortcuts.json.bad")), "exit %d" % code)
        self.record("--reset-interface restores size, contrast and keyboard options", abs(after.get("uiScale", 1.0) - 1.0) < 0.001 and after.get("uiContrast", "standard") == "standard" and not after.get("uiAutoScale", False) and not after.get("uiKeyboardNav", False), json.dumps({k: after.get(k) for k in ("uiScale", "uiContrast", "uiAutoScale", "uiKeyboardNav")}))
        self.record("--reset-interface keeps the other settings", after.get("theme") == "Blue", "theme %s" % after.get("theme"))

    def case_damaged_workspaces(self):
        folder = self.folder()
        write(os.path.join(folder, "workspaces", "broken.ini"), "nonsense\n")
        write(os.path.join(folder, "workspaces", "huge.ini"), "[Window][A]\n" + "Pos=1,1\n" * 80000)
        write(os.path.join(folder, "workspaces", "notes.txt"), "not a workspace")
        code, _ = self.start(folder)
        self.record("damaged and oversized workspace files do not stop the start", code == 0, "exit %d" % code)
        self.record("workspace files are never deleted or changed by the start", read(os.path.join(folder, "workspaces", "broken.ini")) == "nonsense\n", "broken.ini kept")

    def case_binary_files(self):
        folder = self.folder()
        for name in ("layout_v6.ini", "settings.json", "shortcuts.json"):
            with open(os.path.join(folder, name), "wb") as handle:
                handle.write(bytes(range(256)) * 8)
        code, _ = self.start(folder)
        names = sorted(os.listdir(folder))
        self.record("binary garbage in all three files does not stop the start", code == 0 and all(os.path.exists(os.path.join(folder, n + ".bad")) for n in ("layout_v6.ini", "settings.json", "shortcuts.json")), "exit %d, files %s" % (code, ",".join(names)))

    def case_empty_files(self):
        folder = self.folder()
        for name in ("layout_v6.ini", "settings.json", "shortcuts.json"):
            write(os.path.join(folder, name), "")
        code, _ = self.start(folder)
        self.record("empty files do not stop the start", code == 0, "exit %d" % code)

    def run(self):
        self.case_first_run()
        self.case_damaged_layout()
        self.case_wild_layout()
        self.case_good_layout_kept()
        self.case_damaged_settings()
        self.case_scale_limits()
        self.case_damaged_shortcuts()
        self.case_newer_shortcuts()
        self.case_valid_shortcuts_kept()
        self.case_unknown_shortcut_entries()
        self.case_reset_layout()
        self.case_reset_interface()
        self.case_damaged_workspaces()
        self.case_binary_files()
        self.case_empty_files()

    def finish(self, output):
        failed = [r for r in self.results if not r["passed"]]
        report = {"checks": self.results, "failed": len(failed), "passed": not failed and bool(self.results)}
        if output:
            with open(output, "w", encoding="utf-8", newline="\n") as handle:
                json.dump(report, handle, indent=2)
                handle.write("\n")
        shutil.rmtree(self.base, ignore_errors=True)
        print("%d checks, %d failed" % (len(self.results), len(failed)))
        return 0 if report["passed"] else 1


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("binary")
    parser.add_argument("--output", default="")
    args = parser.parse_args()
    suite = Suite(os.path.abspath(args.binary))
    suite.run()
    return suite.finish(args.output)


if __name__ == "__main__":
    sys.exit(main())
