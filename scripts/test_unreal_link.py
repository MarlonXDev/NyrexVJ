import argparse
import json
import os
import shutil
import signal
import socket
import subprocess
import sys
import tempfile
import time


def free_udp_port():
    probe = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    probe.bind(("127.0.0.1", 0))
    port = probe.getsockname()[1]
    probe.close()
    return port


def launch(binary, directory, seconds, log):
    env = dict(os.environ)
    env["NYREXVJ_DATA_DIR"] = directory
    env.setdefault("LIBGL_ALWAYS_SOFTWARE", "1")
    command = [binary, "--fresh", "--no-welcome", "--no-update-check", "--resolution", "0", "--run-seconds", str(seconds)]
    return subprocess.Popen(command, env=env, stdout=open(log, "w"), stderr=subprocess.STDOUT)


def connected_count(path):
    try:
        with open(path) as handle:
            return sum(1 for line in handle if " connected  NyrexVJ" in line)
    except OSError:
        return 0


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("binary", nargs="?", default="build/NyrexVJ")
    parser.add_argument("--output", default="")
    parser.add_argument("--p95-limit-ms", type=float, default=1000.0)
    options = parser.parse_args()

    here = os.path.dirname(os.path.abspath(__file__))
    monitor_script = os.path.join(here, "..", "integrations", "unreal", "examples", "nyrex_monitor.py")
    work = tempfile.mkdtemp(prefix="nvj-unreal-link-")
    data_dir = os.path.join(work, "data")
    os.makedirs(data_dir)
    out_port = free_udp_port()
    in_port = free_udp_port()
    with open(os.path.join(data_dir, "settings.json"), "w") as handle:
        json.dump({"oscEnabled": True, "oscPort": in_port, "oscOutEnabled": True, "oscOutHost": "127.0.0.1", "oscOutPort": out_port, "oscOutRate": 60}, handle)

    results = []

    def check(ok, name, detail=""):
        results.append({"name": name, "ok": bool(ok), "detail": detail})
        print(("PASS " if ok else "FAIL ") + name + ((" (" + detail + ")") if detail else ""))

    monitor_log = os.path.join(work, "monitor.log")
    monitor = subprocess.Popen(
        [sys.executable, "-u", "-B", monitor_script, "--listen-port", str(out_port), "--control-port", str(in_port), "--interval", "0.25", "--duration", "300", "--stop-on", "lost", "--require-pongs", "10"],
        stdout=open(monitor_log, "w"),
        stderr=subprocess.STDOUT,
        text=True,
    )

    run_seconds = 12
    for index in range(2):
        process = launch(options.binary, data_dir, run_seconds, os.path.join(work, "run%d.log" % (index + 1)))
        try:
            process.wait(timeout=run_seconds + 120)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()
        check(process.returncode == 0, "run %d ends normally" % (index + 1), str(process.returncode))
        time.sleep(2.0)

    killed = launch(options.binary, data_dir, 120, os.path.join(work, "run3.log"))
    deadline = time.time() + 120
    while time.time() < deadline and connected_count(monitor_log) < 3 and killed.poll() is None:
        time.sleep(0.5)
    check(connected_count(monitor_log) == 3, "the third run connected", str(connected_count(monitor_log)))
    time.sleep(4.0)
    if killed.poll() is None:
        killed.send_signal(signal.SIGKILL)
        killed.wait()
    try:
        monitor.wait(timeout=60)
    except subprocess.TimeoutExpired:
        monitor.kill()
        monitor.wait()

    summary = {}
    with open(monitor_log) as handle:
        for line in handle:
            if line.startswith("summary: "):
                try:
                    summary = json.loads(line[len("summary: "):])
                except ValueError:
                    summary = {}
    check(bool(summary), "the monitor printed a summary")
    if summary:
        rtt = summary["rttMs"]
        check(summary["connections"] == 3, "the monitor connected three times (two normal runs, one that was killed)", str(summary["connections"]))
        check(summary["losses"] == 1, "exactly one loss: the killed run, the two clean runs ended with bye", str(summary["losses"]))
        check(summary["state"] == "lost", "the final state is lost after the killed run", str(summary["state"]))
        check(summary["schema"] == 1 and bool(summary["version"]), "hello carried schema 1 and a version", str(summary["schema"]) + " " + str(summary["version"]))
        check(summary["inputPort"] == in_port, "hello named the OSC input port", str(summary["inputPort"]))
        in_flight = summary["pingsSent"] - summary["pongs"] - summary["pingsLost"] - summary["ignored"]
        check(summary["pongs"] >= 60, "pongs came back for the pings", str(summary["pongs"]) + " of " + str(summary["pingsSent"]))
        check(0 <= in_flight <= 9, "at most 9 pings (2 seconds at 4 per second, plus one) are still waiting at the end", str(in_flight))
        check(summary["pingsLost"] <= 22, "no more pings are lost than the client sent in the 5 seconds after the killed run (20, plus 2)", str(summary["pingsLost"]))
        check(rtt["p95"] is not None and rtt["p95"] < options.p95_limit_ms, "the 95th percentile of the round trip is below " + str(options.p95_limit_ms) + " ms", str(rtt["p95"]))
        check(rtt["min"] is not None and rtt["min"] >= 0.0, "no negative round trip", str(rtt["min"]))
        check(summary["events"]["audio"] > 100 and summary["events"]["clock"] > 100, "audio and clock data arrived", json.dumps(summary["events"]))

    failed = [row for row in results if not row["ok"]]
    report = {"summary": summary, "checks": results, "failed": len(failed), "passed": len(results) - len(failed)}
    if options.output:
        with open(options.output, "w") as handle:
            json.dump(report, handle, indent=2)
    if failed:
        print("kept the logs in " + work)
        sys.exit(1)
    shutil.rmtree(work, ignore_errors=True)
    print("unreal link test passed: " + str(report["passed"]) + " checks")


if __name__ == "__main__":
    main()
