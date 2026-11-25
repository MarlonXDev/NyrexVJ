import copy
import json
import os
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import release_gate as g

GATES = g.load_json(g.GATES)


def growth(leak=False):
    return {
        "rss": {"first": 300.0, "last": 420.0 if leak else 305.0, "growth": 120.0 if leak else 5.0, "limit": 64.0, "judged": True, "leak": leak, "unit": "MiB"},
        "gpuEstimate": {"first": 100.0, "last": 100.0, "growth": 0.0, "limit": 16.0, "judged": True, "leak": False, "unit": "MiB"},
        "threads": {"first": 20.0, "last": 20.0, "growth": 0.0, "limit": 2.0, "judged": True, "leak": False, "unit": "count"},
        "handles": {"first": 90.0, "last": 90.0, "growth": 0.0, "limit": 16.0, "judged": True, "leak": False, "unit": "count"},
    }


def report(cls="gpu-reported", seconds=1800.0, fps=60):
    return {
        "schema": "nyrexvj-run-report",
        "schemaVersion": 1,
        "kind": "soak",
        "finished": True,
        "passed": True,
        "wallSeconds": seconds,
        "frames": int(seconds * fps),
        "achievedFps": fps - 0.4,
        "targetFps": fps,
        "workMs": {"p50": 5.0, "p95": 9.0, "p99": 14.0, "max": 40.0, "avg": 5.5},
        "worstIntervalMs": 45.0,
        "overBudgetFraction": 0.004,
        "missedFrames": 20,
        "glErrors": 0,
        "playingFaultsMax": 0,
        "judged": True,
        "growth": growth(),
        "conditions": {
            "system": {"os": "Windows", "osVersion": "10.0.26100", "architecture": "x86-64", "cpu": "Example CPU", "logicalCores": 16, "memoryBytes": 34359738368},
            "build": {"version": "0.50.0", "buildNumber": "1", "configuration": "Release"},
            "graphics": {"vendor": "Example", "renderer": "Example GPU", "driver": "4.6.0 Example 555.00", "glsl": "4.60", "gpuVendor": "example", "validationClass": cls},
            "resolution": {"width": 1920, "height": 1080, "text": "1920x1080"},
            "quality": "High",
            "targetFps": fps,
            "durationSeconds": seconds,
            "scenario": "soak",
            "clips": ["Plasma", "Tunnel"],
            "effects": ["Glow"],
        },
    }


def status(results, gate):
    return next(item.status for item in results if item.gate == gate)


def run(item, profile="hardware-release", code=0):
    return g.evaluate(item, GATES, profile, code)


class HardwareRelease(unittest.TestCase):
    def test_a_clean_gpu_run_passes_every_gate(self):
        results = run(report())
        self.assertEqual([item.gate for item in results if item.status == "fail"], [])
        self.assertEqual(status(results, "G-FRAMETIME"), "pass")
        self.assertEqual(status(results, "G-GROWTH"), "pass")

    def test_software_opengl_can_never_pass_the_hardware_profile(self):
        for cls in ("software-gl", "virtual-gpu"):
            results = run(report(cls))
            self.assertEqual(status(results, "G-CLASS"), "fail", cls)

    def test_a_crash_exit_code_fails(self):
        self.assertEqual(status(run(report(), code=139), "G-EXIT"), "fail")
        self.assertEqual(status(run(report(), code=None), "G-EXIT"), "skipped")

    def test_a_cut_off_report_fails(self):
        item = report()
        item["finished"] = False
        self.assertEqual(status(run(item), "G-FINISHED"), "fail")

    def test_a_short_run_fails_the_duration_gate(self):
        self.assertEqual(status(run(report(seconds=300.0)), "G-DURATION"), "fail")

    def test_growth_above_the_limit_fails_and_names_the_series(self):
        item = report()
        item["growth"] = growth(leak=True)
        results = run(item)
        self.assertEqual(status(results, "G-GROWTH"), "fail")
        self.assertIn("rss", next(r.text for r in results if r.gate == "G-GROWTH"))

    def test_growth_that_was_not_judged_does_not_pass(self):
        item = report()
        item["judged"] = False
        self.assertEqual(status(run(item), "G-GROWTH"), "fail")

    def test_frame_time_percentiles_are_gated(self):
        slow = report()
        slow["workMs"]["p95"] = 20.0
        self.assertEqual(status(run(slow), "G-FRAMETIME"), "fail")
        spike = report()
        spike["workMs"]["max"] = 400.0
        self.assertEqual(status(run(spike), "G-FRAMETIME"), "fail")
        late = report()
        late["overBudgetFraction"] = 0.2
        self.assertEqual(status(run(late), "G-FRAMETIME"), "fail")
        low = report()
        low["achievedFps"] = 40.0
        self.assertEqual(status(run(low), "G-FRAMETIME"), "fail")

    def test_an_unknown_value_fails_instead_of_passing(self):
        item = report()
        del item["workMs"]["p99"]
        self.assertEqual(status(run(item), "G-FRAMETIME"), "fail")

    def test_gl_errors_and_failed_clips_fail(self):
        item = report()
        item["glErrors"] = 3
        self.assertEqual(status(run(item), "G-GLERROR"), "fail")
        item = report()
        item["playingFaultsMax"] = 1
        self.assertEqual(status(run(item), "G-FAULTS"), "fail")


class Conditions(unittest.TestCase):
    def test_every_required_condition_is_checked(self):
        for path in ("conditions.system.os", "conditions.build.version", "conditions.graphics.driver", "conditions.resolution.width", "conditions.durationSeconds", "conditions.clips", "conditions.effects"):
            item = report()
            node = item
            parts = path.split(".")
            for part in parts[:-1]:
                node = node[part]
            del node[parts[-1]]
            self.assertEqual(status(run(item), "G-FORMAT"), "fail", path)

    def test_empty_clip_list_and_blank_text_fail(self):
        item = report()
        item["conditions"]["clips"] = []
        self.assertEqual(status(run(item), "G-FORMAT"), "fail")
        item = report()
        item["conditions"]["graphics"]["driver"] = "  "
        self.assertEqual(status(run(item), "G-FORMAT"), "fail")

    def test_unknown_class_and_wrong_schema_fail(self):
        item = report()
        item["conditions"]["graphics"]["validationClass"] = "nvidia"
        self.assertEqual(status(run(item), "G-FORMAT"), "fail")
        item = report()
        item["schema"] = "something-else"
        self.assertEqual(status(run(item), "G-FORMAT"), "fail")


class SoftwareProfiles(unittest.TestCase):
    def test_smoke_on_software_passes_without_performance_limits(self):
        item = report("software-gl", seconds=6.0, fps=5)
        item["workMs"] = {"p50": 180.0, "p95": 214.0, "p99": 215.0, "max": 217.0, "avg": 185.0}
        item["worstIntervalMs"] = 228.0
        item["overBudgetFraction"] = 1.0
        item["judged"] = False
        results = run(item, "smoke")
        self.assertEqual([r.gate for r in results if r.status == "fail"], [])
        self.assertEqual(status(results, "G-FRAMETIME"), "skipped")
        self.assertEqual(status(results, "G-GROWTH"), "skipped")

    def test_smoke_still_catches_a_stall(self):
        item = report("software-gl", seconds=6.0, fps=5)
        item["worstIntervalMs"] = 30000.0
        self.assertEqual(status(run(item, "smoke"), "G-STALL"), "fail")

    def test_smoke_still_catches_gl_errors_and_a_dead_run(self):
        item = report("software-gl", seconds=6.0, fps=5)
        item["glErrors"] = 1
        item["frames"] = 0
        results = run(item, "smoke")
        self.assertEqual(status(results, "G-GLERROR"), "fail")
        self.assertEqual(status(results, "G-FRAMES"), "fail")

    def test_soak_profile_judges_growth_on_software(self):
        item = report("software-gl", seconds=600.0, fps=5)
        item["growth"] = growth(leak=True)
        self.assertEqual(status(run(item, "soak"), "G-GROWTH"), "fail")


class Output(unittest.TestCase):
    def test_software_banner_never_claims_hardware(self):
        text = g.render(run(report("software-gl"), "smoke"), report("software-gl"), "smoke")
        self.assertIn("NOT a validation on a real graphics card", text.splitlines()[0])
        md = g.markdown(run(report("software-gl"), "smoke"), report("software-gl"), "smoke")
        self.assertIn("not** a validation on a real graphics card or projector", md)

    def test_a_gpu_report_still_asks_for_a_recorded_test(self):
        text = g.render(run(report()), report(), "hardware-release")
        self.assertIn("also needs a named tester", text)

    def test_command_line_exit_codes(self):
        with tempfile.TemporaryDirectory() as folder:
            good = os.path.join(folder, "good.json")
            bad = os.path.join(folder, "bad.json")
            out = os.path.join(folder, "out.json")
            summary = os.path.join(folder, "summary.md")
            with open(good, "w") as handle:
                json.dump({**report("software-gl", seconds=6.0, fps=5), "judged": False}, handle)
            broken = report("software-gl", seconds=6.0, fps=5)
            broken["glErrors"] = 2
            with open(bad, "w") as handle:
                json.dump(broken, handle)
            self.assertEqual(g.main([good, "--profile", "smoke", "--exit-code", "0", "--json", out, "--summary", summary]), 0)
            self.assertEqual(g.main([bad, "--profile", "smoke", "--exit-code", "0"]), 1)
            self.assertEqual(g.main([good, "--profile", "smoke", "--exit-code", "1"]), 1)
            self.assertEqual(g.main([os.path.join(folder, "missing.json")]), 2)
            self.assertEqual(g.load_json(out)["class"], "software-gl")
            with open(summary, encoding="utf-8") as handle:
                self.assertIn("Software OpenGL only", handle.read())


def live(**changes):
    item = {
        "schema": "nyrexvj-live-report",
        "seconds": 1800.0,
        "gpu_mode": "nvidia",
        "conditions": {"system": "Windows 11, Example CPU", "build": "0.50.0", "resolution": "1920x1080", "clips": "Plasma, Tunnel", "effects": "Glow", "durationSeconds": 1800},
        "summary": {
            "NyrexVJ": {"seen_running": True, "rss_growth": {"judged": True, "leak": False, "growth": 3.0, "limit": 64.0}},
            "VRChat": {"seen_running": True, "rss_growth": {"judged": True, "leak": False, "growth": 20.0, "limit": 64.0}},
            "gpu_cards": {"0": {"vram_growth": {"judged": True, "leak": False, "growth": 2.0, "limit": 16.0}}},
        },
    }
    item.update(changes)
    return item


class LiveReport(unittest.TestCase):
    def test_a_good_live_report_passes(self):
        results = g.evaluate_live(live(), GATES, "hardware-release")
        self.assertEqual([r.gate for r in results if r.status == "fail"], [])

    def test_missing_conditions_fail(self):
        item = live()
        del item["conditions"]["clips"]
        results = g.evaluate_live(item, GATES, "hardware-release")
        self.assertEqual(status(results, "L-CONDITIONS"), "fail")

    def test_a_process_that_never_ran_and_growth_fail(self):
        item = live()
        item["summary"]["VRChat"]["seen_running"] = False
        item["summary"]["NyrexVJ"]["rss_growth"]["leak"] = True
        item["summary"]["gpu_cards"]["0"]["vram_growth"]["leak"] = True
        results = g.evaluate_live(item, GATES, "hardware-release")
        self.assertEqual(status(results, "L-RUNNING-VRChat"), "fail")
        self.assertEqual(status(results, "L-RSS-NyrexVJ"), "fail")
        self.assertEqual(status(results, "L-VRAM-0"), "fail")

    def test_a_short_sample_fails_and_a_wrong_schema_fails(self):
        self.assertEqual(status(g.evaluate_live(live(seconds=60.0), GATES, "hardware-release"), "L-DURATION"), "fail")
        self.assertEqual(g.evaluate_live({"schema": "x"}, GATES, "smoke")[0].status, "fail")


class Gates(unittest.TestCase):
    def test_the_limit_file_is_complete(self):
        for name in ("smoke", "soak", "hardware-release"):
            self.assertIn(name, GATES["profiles"])
        self.assertEqual(GATES["profiles"]["hardware-release"]["requireClass"], "gpu-reported")
        self.assertIsNone(GATES["profiles"]["smoke"]["requireClass"])
        for key in ("p95PeriodFactor", "p99PeriodFactor", "maxOverBudgetFraction", "maxMissedFraction", "maxWorkMs", "minFpsFactor"):
            self.assertIn(key, GATES["performance"])

    def test_the_gate_does_not_modify_its_input(self):
        item = report()
        before = copy.deepcopy(item)
        run(item)
        self.assertEqual(item, before)


if __name__ == "__main__":
    unittest.main()
