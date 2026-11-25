import argparse
import bisect
import heapq
import json
import math
import os
import random
import shutil
import socket
import struct
import subprocess
import sys
import tempfile
import threading
import time


def free_udp_port():
    probe = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    probe.bind(("127.0.0.1", 0))
    port = probe.getsockname()[1]
    probe.close()
    return port


def osc_message(address, value):
    def pad(data):
        return data + b"\0" * (4 - len(data) % 4)

    return pad(address.encode()) + pad(b",f") + struct.pack(">f", value)


class Relay(threading.Thread):
    def __init__(self, master_port, seed):
        super().__init__(daemon=True)
        self.front = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.front.bind(("127.0.0.1", 0))
        self.back = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.back.bind(("127.0.0.1", 0))
        self.front.setblocking(False)
        self.back.setblocking(False)
        self.port = self.front.getsockname()[1]
        self.master = ("127.0.0.1", master_port)
        self.follower = None
        self.loss = 0.0
        self.jitter = 0.0
        self.paused = False
        self.stop = False
        self.random = random.Random(seed)
        self.queue = []
        self.counter = 0
        self.passed = 0
        self.dropped = 0

    def admit(self, data, target, sock):
        if self.paused or self.random.random() < self.loss:
            self.dropped += 1
            return
        delay = self.random.uniform(0.0, self.jitter) if self.jitter > 0.0 else 0.0
        self.counter += 1
        heapq.heappush(self.queue, (time.monotonic() + delay, self.counter, sock, target, data))

    def run(self):
        import select

        while not self.stop:
            ready, _, _ = select.select([self.front, self.back], [], [], 0.002)
            for sock in ready:
                try:
                    data, address = sock.recvfrom(4096)
                except OSError:
                    continue
                if sock is self.front:
                    self.follower = address
                    self.admit(data, self.master, self.back)
                elif self.follower is not None:
                    self.admit(data, self.follower, self.front)
                else:
                    self.dropped += 1
            now = time.monotonic()
            while self.queue and self.queue[0][0] <= now:
                _, _, sock, target, data = heapq.heappop(self.queue)
                if self.paused:
                    self.dropped += 1
                    continue
                try:
                    sock.sendto(data, target)
                    self.passed += 1
                except OSError:
                    self.dropped += 1


def write_settings(directory, values):
    os.makedirs(directory, exist_ok=True)
    with open(os.path.join(directory, "settings.json"), "w") as handle:
        json.dump(values, handle)


def launch(binary, directory, osc_port, seconds, report, log):
    env = dict(os.environ)
    env["NYREXVJ_DATA_DIR"] = directory
    env.setdefault("LIBGL_ALWAYS_SOFTWARE", "1")
    command = [binary, "--fresh", "--no-welcome", "--no-update-check", "--resolution", "0", "--run-seconds", str(seconds), "--netsync-report", report]
    if osc_port:
        command += ["--osc-port", str(osc_port)]
    return subprocess.Popen(command, env=env, stdout=open(log, "w"), stderr=subprocess.STDOUT)


def interpolate(samples, times, wall):
    index = bisect.bisect_left(times, wall)
    if index <= 0 or index >= len(samples):
        return None
    a = samples[index - 1]
    b = samples[index]
    if b[0] - a[0] > 0.5 or a[3] != 0 or b[3] != 0:
        return None
    span = b[0] - a[0]
    if span <= 0.0:
        return None
    return a[1] + (b[1] - a[1]) * (wall - a[0]) / span


def beat_error(follower, master, start, end):
    times = [row[0] for row in master]
    errors = []
    for row in follower:
        if row[0] < start or row[0] > end or row[3] != 0:
            continue
        expected = interpolate(master, times, row[0])
        if expected is None:
            continue
        errors.append(abs(row[1] - expected) * 60.0 / max(row[2], 1.0) * 1000.0)
    if not errors:
        return None
    return {"samples": len(errors), "meanMs": sum(errors) / len(errors), "worstMs": max(errors)}


def frame_ms(samples, start, end):
    gaps = [b[0] - a[0] for a, b in zip(samples, samples[1:]) if start <= a[0] <= end]
    return sum(gaps) / len(gaps) * 1000.0 if gaps else 0.0


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("binary", nargs="?", default="build/NyrexVJ")
    parser.add_argument("--output", default="")
    parser.add_argument("--loss", type=float, default=0.25)
    parser.add_argument("--jitter-ms", type=float, default=25.0)
    parser.add_argument("--seed", type=int, default=7)
    options = parser.parse_args()

    work = tempfile.mkdtemp(prefix="nvj-netsync-")
    master_dir = os.path.join(work, "master")
    follower_dir = os.path.join(work, "follower")
    master_port = free_udp_port()
    osc_port = free_udp_port()
    relay = Relay(master_port, options.seed)
    relay.start()
    write_settings(master_dir, {"netSyncRole": "master", "netSyncPort": master_port, "netSyncName": "master-pc", "syncMode": "manual"})
    write_settings(follower_dir, {"netSyncRole": "follower", "netSyncHost": "127.0.0.1", "netSyncPort": relay.port, "netSyncName": "follower-pc", "syncMode": "manual"})
    master_report = os.path.join(work, "master.json")
    follower_report = os.path.join(work, "follower.json")
    master_seconds = 66
    follower_seconds = 54

    results = []

    def check(ok, name, detail=""):
        results.append({"name": name, "ok": bool(ok), "detail": detail})
        print(("PASS " if ok else "FAIL ") + name + ((" (" + detail + ")") if detail else ""))

    master = launch(options.binary, master_dir, osc_port, master_seconds, master_report, os.path.join(work, "master.log"))
    key_file = os.path.join(master_dir, "netsync_key")
    deadline = time.time() + 90
    while time.time() < deadline and not (os.path.exists(key_file) and os.path.getsize(key_file) > 0):
        if master.poll() is not None:
            break
        time.sleep(0.25)
    check(os.path.exists(key_file), "the master creates its key file on first start")
    shutil.copyfile(key_file, os.path.join(follower_dir, "netsync_key"))
    os.chmod(os.path.join(follower_dir, "netsync_key"), 0o600)

    launched = time.time()
    follower = launch(options.binary, follower_dir, 0, follower_seconds, follower_report, os.path.join(work, "follower.log"))
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

    def send(address, value):
        sock.sendto(osc_message("/nyrex/" + address, value), ("127.0.0.1", osc_port))

    def wait_until(offset):
        remaining = launched + offset - time.time()
        if remaining > 0:
            time.sleep(remaining)

    wait_until(14)
    send("layer/2/opacity", 0.3)
    send("master/level", 0.6)
    send("macro/3", 0.8)
    send("layer/1/fx/2/param/3", 0.15)
    wait_until(16)
    send("master/bpm", 0.55)
    wait_until(18)
    send("layer/1/clip/2", 1.0)
    wait_until(20)
    relay.loss = options.loss
    relay.jitter = options.jitter_ms / 1000.0
    sweep_end = launched + 27
    while time.time() < sweep_end:
        phase = (time.time() - launched) * 4.0
        send("layer/3/opacity", 0.5 + 0.4 * math.sin(phase))
        time.sleep(0.033)
    send("layer/3/opacity", 0.7)
    wait_until(32)
    relay.loss = 0.0
    relay.jitter = 0.0
    relay.paused = True
    wait_until(34)
    send("master/level", 0.2)
    send("layer/4/opacity", 0.45)
    wait_until(38)
    relay.paused = False

    follower.wait(timeout=follower_seconds + 120)
    master.wait(timeout=master_seconds + 120)
    relay.stop = True
    sock.close()

    check(follower.returncode == 0, "the follower process ends normally", str(follower.returncode))
    check(master.returncode == 0, "the master process ends normally", str(master.returncode))
    summary = {"relay": {"passed": relay.passed, "dropped": relay.dropped}}
    if os.path.exists(master_report) and os.path.exists(follower_report):
        with open(master_report) as handle:
            m = json.load(handle)
        with open(follower_report) as handle:
            f = json.load(handle)
        check(f.get("role") == "follower" and f.get("phase") == "live" and f.get("hasState"), "the follower ends live with a full show state", str(f.get("phase")))
        check(m.get("role") == "master" and m["stats"]["received"] > 20 and m["stats"]["sent"] > 100, "the master served the follower", str(m["stats"]["received"]) + " in, " + str(m["stats"]["sent"]) + " out")
        check(relay.dropped > 50, "the relay really dropped packets", str(relay.dropped))

        expected = {"layer/2/opacity": 0.3, "master/level": 0.2, "macro/3": 0.8, "layer/1/fx/2/param/3": 0.15, "layer/3/opacity": 0.7, "layer/4/opacity": 0.45}
        for name, value in expected.items():
            check(abs(m["controls"][name] - value) < 0.003, "the master keeps the value of " + name, str(m["controls"][name]))
            check(abs(f["controls"][name] - value) < 0.003, "the follower has the value of " + name, str(f["controls"][name]))
        different = [name for name in m["controls"] if abs(m["controls"][name] - f["controls"].get(name, -9.0)) > 0.004]
        check(not different, "all " + str(len(m["controls"])) + " synced controls match on both sides", ", ".join(different[:6]))
        check(m["activeColumns"] == f["activeColumns"] and m["activeDeck"] == f["activeDeck"], "active clips and deck match", str(m["activeColumns"]) + " vs " + str(f["activeColumns"]))
        check(m["activeColumns"][0] == 1 and f["triggerCounts"][0] >= 1, "the clip started by OSC on the master was started on the follower")
        check(abs(f["clock"]["bpm"] - 150.0) < 0.3 and abs(m["samples"][-1][2] - 150.0) < 0.3, "the tempo of 150 BPM is followed", str(f["clock"]["bpm"]))
        check(f["applied"]["controls"] > 10, "the follower applied the control changes", str(f["applied"]["controls"]))
        check(m["stats"]["badAuth"] == 0 and f["stats"]["badAuth"] == 0, "no packet failed the signature check")

        first = f["samples"][0][0]
        windows = {
            "calm": (launched + 11, launched + 15.5, 50.0, 150.0),
            "lossy": (launched + 23, launched + 31, 50.0, 150.0),
            "afterOutage": (launched + 44, launched + 80, 50.0, 150.0),
        }
        summary["beatError"] = {}
        for name, (start, end, mean_limit, worst_limit) in windows.items():
            result = beat_error(f["samples"], m["samples"], start, end)
            frame = frame_ms(f["samples"], start, end)
            mean_limit = max(mean_limit, 1.5 * frame)
            worst_limit = max(worst_limit, 4.0 * frame)
            if result is not None:
                result["frameMs"] = frame
            summary["beatError"][name] = result
            ok = result is not None and result["samples"] > 20 and result["meanMs"] < mean_limit and result["worstMs"] < worst_limit
            check(ok, "the beat stays close to the master (" + name + ")", json.dumps(result))
        summary["followerFirstSampleAfterLaunch"] = first - launched
        summary["followerReport"] = {k: f[k] for k in ("stats", "clock", "applied", "mismatches", "transport") if k in f}
        summary["masterReport"] = {k: m[k] for k in ("stats", "followers", "transport") if k in m}
    else:
        check(False, "both processes wrote their report")

    failed = [row for row in results if not row["ok"]]
    summary["checks"] = results
    summary["failed"] = len(failed)
    summary["passed"] = len(results) - len(failed)
    if options.output:
        with open(options.output, "w") as handle:
            json.dump(summary, handle, indent=2)
    if failed:
        print("kept the logs in " + work)
        sys.exit(1)
    shutil.rmtree(work, ignore_errors=True)
    print("two instance test passed: " + str(summary["passed"]) + " checks")


if __name__ == "__main__":
    main()
