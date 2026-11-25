import argparse
import os
import sys
import tempfile
import time
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import measure_live as m

PROC_STAT = "4242 (Nyrex VJ) (x) S 1 4242 4242 0 -1 4194560 12345 0 0 0 150 50 0 0 20 0 7 0 100 987654321 6543 18446744073709551615 1 1 0 0 0 0 0 0 0 0 0 0 17 1 0 0 0 0 0\n"
PROC_STATUS = "Name:\tNyrexVJ\nVmPeak:\t  900000 kB\nVmRSS:\t  251904 kB\nThreads:\t7\n"
NVIDIA = "0, 37, 2210, 8192\n1, 0, 310, 12288\n"
POWERSHELL_ONE = '{"Id": 812, "CPU": 93.28125, "WorkingSet64": 268435456}'
POWERSHELL_MANY = '[{"Id": 812, "CPU": 93.5, "WorkingSet64": 268435456}, {"Id": 900, "CPU": null, "WorkingSet64": 1048576}]'
TYPEPERF = (
    '"(PDH-CSV 4.0)","\\\\PC\\GPU Process Memory(pid_812_luid_0x00000000_0x0000A1B2_phys_0)\\Dedicated Usage",'
    '"\\\\PC\\GPU Process Memory(pid_900_luid_0x00000000_0x0000A1B2_phys_0)\\Dedicated Usage",'
    '"\\\\PC\\GPU Engine(pid_812_luid_0x00000000_0x0000A1B2_phys_0_eng_0_engtype_3D)\\Utilization Percentage",'
    '"\\\\PC\\GPU Engine(pid_812_luid_0x00000000_0x0000A1B2_phys_0_eng_1_engtype_Copy)\\Utilization Percentage",'
    '"\\\\PC\\GPU Engine(pid_900_luid_0x00000000_0x0000A1B2_phys_0_eng_0_engtype_3D)\\Utilization Percentage"\n'
    '"11/25/2026 10:00:00.123","1073741824","536870912","12.5","3.0","40.0"\n'
    'Exiting, please wait...  The command completed successfully.\n'
)


class Parsers(unittest.TestCase):
    def test_proc_stat_with_spaces_and_parentheses_in_the_name(self):
        self.assertEqual(m.parse_proc_stat(PROC_STAT), 200)

    def test_proc_stat_rejects_garbage(self):
        self.assertIsNone(m.parse_proc_stat(""))
        self.assertIsNone(m.parse_proc_stat("12 (x) S 1 2"))

    def test_rss(self):
        self.assertEqual(m.parse_proc_rss_bytes(PROC_STATUS), 251904 * 1024)
        self.assertIsNone(m.parse_proc_rss_bytes("Name:\tx\n"))

    def test_nvidia_smi(self):
        cards = m.parse_nvidia_smi(NVIDIA)
        self.assertEqual(len(cards), 2)
        self.assertEqual(cards[0], {"index": 0, "util": 37.0, "vram_used_mib": 2210.0, "vram_total_mib": 8192.0})
        self.assertEqual(m.parse_nvidia_smi(""), [])
        self.assertEqual(m.parse_nvidia_smi("0, [N/A], 100, 200\n"), [])
        self.assertEqual(m.parse_nvidia_smi("No devices were found\n"), [])

    def test_powershell(self):
        one = m.parse_powershell_processes(POWERSHELL_ONE)
        self.assertEqual(one, [{"pid": 812, "cpu_seconds": 93.28125, "rss_bytes": 268435456}])
        many = m.parse_powershell_processes(POWERSHELL_MANY)
        self.assertEqual(len(many), 2)
        self.assertEqual(many[1]["cpu_seconds"], 0.0)
        self.assertEqual(m.parse_powershell_processes(""), [])
        self.assertEqual(m.parse_powershell_processes("  \n"), [])

    def test_typeperf(self):
        counters = m.parse_typeperf(TYPEPERF)
        self.assertEqual(len(counters), 5)
        mine = m.windows_gpu_for_pids(counters, {812})
        self.assertAlmostEqual(mine["vram_mib"], 1024.0)
        self.assertAlmostEqual(mine["util"], 12.5)
        both = m.windows_gpu_for_pids(counters, {812, 900})
        self.assertAlmostEqual(both["vram_mib"], 1536.0)
        self.assertAlmostEqual(both["util"], 52.5)
        none = m.windows_gpu_for_pids(counters, {1})
        self.assertEqual(none, {"vram_mib": 0.0, "util": 0.0})
        self.assertEqual(m.parse_typeperf(""), {})
        self.assertEqual(m.parse_typeperf("Error: no counters\n"), {})

    def test_amdgpu_sysfs(self):
        with tempfile.TemporaryDirectory() as base:
            device = os.path.join(base, "card0", "device")
            os.makedirs(device)
            with open(os.path.join(device, "mem_info_vram_used"), "w") as handle:
                handle.write("536870912\n")
            with open(os.path.join(device, "mem_info_vram_total"), "w") as handle:
                handle.write("8589934592\n")
            with open(os.path.join(device, "gpu_busy_percent"), "w") as handle:
                handle.write("42\n")
            os.makedirs(os.path.join(base, "card0-DP-1"))
            os.makedirs(os.path.join(base, "card1", "device"))
            cards = m.amdgpu_sample(base)
            self.assertEqual(len(cards), 1)
            self.assertEqual(cards[0]["util"], 42.0)
            self.assertAlmostEqual(cards[0]["vram_used_mib"], 512.0)
            self.assertAlmostEqual(cards[0]["vram_total_mib"], 8192.0)
        self.assertEqual(m.amdgpu_sample("/nonexistent/path"), [])


class Statistics(unittest.TestCase):
    def test_percentile_and_summary(self):
        self.assertEqual(m.percentile([], 0.95), 0.0)
        self.assertEqual(m.percentile([5.0], 0.95), 5.0)
        self.assertEqual(m.percentile(list(range(1, 101)), 0.95), 95)
        self.assertEqual(m.summarize([]), {"count": 0})
        summary = m.summarize([1.0, 2.0, 3.0])
        self.assertEqual((summary["min"], summary["avg"], summary["max"]), (1.0, 2.0, 3.0))

    def test_growth(self):
        flat = m.growth([500.0 + (i % 3) for i in range(60)], 0.25, 16.0, 0.1)
        self.assertTrue(flat["judged"])
        self.assertFalse(flat["leak"])
        leak = m.growth([500.0 + 5.0 * i for i in range(60)], 0.25, 16.0, 0.1)
        self.assertTrue(leak["leak"])
        self.assertFalse(m.growth([1.0] * 5)["judged"])
        warm = [100.0 + 40.0 * i for i in range(15)] + [700.0] * 45
        self.assertFalse(m.growth(warm, 0.25, 16.0, 0.1)["leak"])
        spike = [500.0] * 60
        spike[58] = spike[59] = 9000.0
        self.assertFalse(m.growth(spike, 0.25, 16.0, 0.1)["leak"])
        shrinking = m.growth([900.0 - 4.0 * i for i in range(60)], 0.25, 16.0, 0.1)
        self.assertFalse(shrinking["leak"])


@unittest.skipUnless(sys.platform.startswith("linux"), "needs /proc")
class LinuxLive(unittest.TestCase):
    def test_own_process_matches_the_python_clock(self):
        ticks = os.sysconf("SC_CLK_TCK")
        before = time.process_time()
        end = time.time() + 0.4
        while time.time() < end:
            sum(range(1000))
        stat = m.parse_proc_stat(m.read_text("/proc/self/stat"))
        after = time.process_time()
        measured = stat / ticks
        self.assertGreater(measured, before)
        self.assertLess(abs(measured - after), 0.25)

    def test_own_rss_is_found(self):
        rss = m.parse_proc_rss_bytes(m.read_text("/proc/self/status"))
        self.assertGreater(rss, 1024 * 1024)

    def test_process_found_by_name_and_missing_one(self):
        name = m.read_text("/proc/self/comm").strip()
        found = m.linux_process_sample(name, os.sysconf("SC_CLK_TCK"))
        self.assertIn(os.getpid(), found["pids"])
        self.assertGreater(found["rss_bytes"], 0)
        missing = m.linux_process_sample("no-such-program", os.sysconf("SC_CLK_TCK"))
        self.assertEqual(missing["pids"], [])

    def test_collect_and_report(self):
        name = m.read_text("/proc/self/comm").strip()
        args = argparse.Namespace(seconds=2.2, interval=0.5, process=[name, "no-such-program"], gpu="none", out="")
        mode, samples = m.collect(args)
        self.assertEqual(mode, "none")
        self.assertGreaterEqual(len(samples), 3)
        report = m.build_report(args, mode, samples)
        mine = report["summary"][name]
        self.assertTrue(mine["seen_running"])
        self.assertGreater(mine["rss_mib"]["max"], 1.0)
        self.assertGreaterEqual(mine["cpu_percent_of_one_core"]["count"], 2)
        gone = report["summary"]["no-such-program"]
        self.assertFalse(gone["seen_running"])
        self.assertEqual(gone["cpu_percent_of_one_core"], {"count": 0})
        self.assertEqual(report["summary"]["gpu_cards"], {})
        self.assertEqual(report["schema"], "nyrexvj-live-report")
        self.assertEqual(report["conditions"]["durationSeconds"], 2.2)
        self.assertEqual(report["conditions"]["clips"], "")

    def test_conditions_are_carried_into_the_report(self):
        args = argparse.Namespace(seconds=5.0, interval=1.0, process=["x"], gpu="none", out="", system="Windows 11", driver="555.00", build="0.50.0", resolution="1920x1080", clips="Plasma", effects="Glow")
        conditions = m.live_conditions(args)
        self.assertEqual(conditions["system"], "Windows 11")
        self.assertEqual(conditions["driver"], "555.00")
        self.assertEqual(conditions["resolution"], "1920x1080")
        self.assertEqual(conditions["effects"], "Glow")


if __name__ == "__main__":
    unittest.main()
