import argparse
import json
import os
import shutil
import signal
import socket
import struct
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


def osc_message(address, value):
    def pad(data):
        return data + b"\0" * (4 - len(data) % 4)

    return pad(address.encode()) + pad(b",f") + struct.pack(">f", value)


def write_settings(directory, values):
    os.makedirs(directory, exist_ok=True)
    with open(os.path.join(directory, "settings.json"), "w") as handle:
        json.dump(values, handle)


class Node:
    def __init__(self, binary, name, directory, work, seconds, extra=None, osc_port=0):
        self.name = name
        self.directory = directory
        self.report = os.path.join(work, name + ".json")
        self.log = os.path.join(work, name + ".log")
        self.osc_port = osc_port
        env = dict(os.environ)
        env["NYREXVJ_DATA_DIR"] = directory
        env.setdefault("LIBGL_ALWAYS_SOFTWARE", "1")
        command = [binary, "--fresh", "--no-welcome", "--no-update-check", "--output", "--resolution", "0", "--run-seconds", str(seconds), "--failover-report", self.report]
        if osc_port:
            command += ["--osc-port", str(osc_port)]
        command += extra or []
        self.process = subprocess.Popen(command, env=env, stdout=open(self.log, "w"), stderr=subprocess.STDOUT)
        self.started = time.time()

    def read(self):
        try:
            with open(self.report) as handle:
                return json.load(handle)
        except (OSError, ValueError):
            return None

    def alive(self):
        return self.process.poll() is None

    def send(self, address, value):
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sock.sendto(osc_message("/nyrex/" + address, value), ("127.0.0.1", self.osc_port))
        sock.close()


def wait_for(predicate, timeout, step=0.25):
    deadline = time.time() + timeout
    while time.time() < deadline:
        value = predicate()
        if value:
            return value
        time.sleep(step)
    return None


def state_of(node):
    data = node.read()
    return data["state"] if data else None


def ready_for(node, seconds):
    data = node.read()
    if not data:
        return None
    if data["state"] == "following" and data["readiness"]["fails"] == 0 and data["readySeconds"] >= seconds:
        return data
    return None


def settings_for(role, port, name, leader_port, host="127.0.0.1", follow=True):
    values = {"netSyncRole": role, "netSyncPort": port, "netSyncName": name, "syncMode": "manual", "failoverEnabled": True, "failoverAutoTakeover": True, "failoverLostAfter": 3.0, "failoverHoldOutputs": True, "failoverLeaderPort": leader_port}
    if role == "follower":
        values["netSyncHost"] = host
    return values


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("binary", nargs="?", default="build/NyrexVJ")
    parser.add_argument("--output", default="")
    options = parser.parse_args()

    results = []

    def check(ok, name, detail=""):
        results.append({"name": name, "ok": bool(ok), "detail": detail})
        print(("PASS " if ok else "FAIL ") + name + ((" (" + detail + ")") if detail else ""))

    summary = {}
    work = tempfile.mkdtemp(prefix="nvj-failover-")
    nodes = []

    def launch(*args, **kwargs):
        node = Node(options.binary, *args, **kwargs)
        nodes.append(node)
        return node

    try:
        primary_port = free_udp_port()
        leader_port = free_udp_port()
        osc_primary = free_udp_port()
        osc_standby = free_udp_port()
        primary_dir = os.path.join(work, "primary")
        standby_dir = os.path.join(work, "standby")
        write_settings(primary_dir, settings_for("master", primary_port, "pc-one", leader_port))
        write_settings(standby_dir, settings_for("follower", primary_port, "pc-two", leader_port))

        one = launch("pc-one", primary_dir, work, 220, osc_port=osc_primary)
        key_file = os.path.join(primary_dir, "netsync_key")
        check(wait_for(lambda: os.path.exists(key_file) and os.path.getsize(key_file) > 0, 90) is not None, "the first PC creates the key on its first start")
        shutil.copyfile(key_file, os.path.join(standby_dir, "netsync_key"))
        os.chmod(os.path.join(standby_dir, "netsync_key"), 0o600)
        two = launch("pc-two", standby_dir, work, 200, osc_port=osc_standby)

        ready = wait_for(lambda: ready_for(two, 11.0), 120)
        check(ready is not None, "the standby is ready for eleven seconds", json.dumps((two.read() or {}).get("readiness", {}).get("overall")))
        data = two.read() or {}
        check(data.get("outputHeld") is True and data.get("role") == "follower", "the standby holds its output while it follows")
        one_data = one.read() or {}
        check(one_data.get("standby", {}).get("present") is True and one_data.get("standby", {}).get("reports") is True, "the primary sees the standby and its readiness report", json.dumps(one_data.get("standby")))
        check(one_data.get("outputHeld") is False and one_data.get("role") == "master", "the primary does not hold its output")

        frozen_at = time.time()
        one.process.send_signal(signal.SIGSTOP)
        leader = wait_for(lambda: (two.read() or {}).get("state") == "leader" and (two.read() or {}).get("role") == "master", 30, 0.1)
        took_at = time.time()
        check(leader, "the frozen primary (as a network partition) is replaced by the standby")
        data = two.read() or {}
        measure = data.get("measure", {})
        check(measure.get("taken") and measure.get("automatic"), "the takeover was automatic")
        check(3.0 <= measure.get("detectionSeconds", 0.0) <= 4.5, "the loss was detected after the set time", str(measure.get("detectionSeconds")))
        check(data.get("epoch") == 2 and data.get("outputHeld") is False, "the new leader has epoch 2 and releases its output", str(data.get("epoch")))
        check(data.get("netsync", {}).get("port") == leader_port, "the leader listens on its own port", str(data.get("netsync", {}).get("port")))
        summary["partition"] = {"observedFrozenToLeaderSeconds": took_at - frozen_at, "detectionSeconds": measure.get("detectionSeconds"), "stateAgeSeconds": measure.get("stateAgeAtTakeover"), "servingAfterDecisionSeconds": data.get("switchSeconds")}

        one.process.send_signal(signal.SIGCONT)
        stepped = wait_for(lambda: (one.read() or {}).get("role") == "follower" and (one.read() or {}).get("epoch") == 2, 40, 0.25)
        check(stepped, "the old primary steps down to follower when it is reachable again")
        one_data = one.read() or {}
        check(one_data.get("outputHeld") is True, "the old primary holds its output after it stepped down")
        check(any(event["kind"] == "stepped-down" for event in one_data.get("events", [])), "the old primary logs the step down")
        joined = wait_for(lambda: any(event["kind"] == "old-primary-joined" for event in (two.read() or {}).get("events", [])), 30)
        check(joined, "the leader sees the old primary join as a follower")
        check(wait_for(lambda: (two.read() or {}).get("announcing") is False, 15), "the leader stops telling the old primary to step down")
        summary["partition"]["servingAfterDecisionSeconds"] = (two.read() or {}).get("switchSeconds")
        both = [(n.read() or {}).get("role") for n in (one, two)]
        check(sorted(both) == ["follower", "master"], "exactly one PC is master at the end", json.dumps(both))

        ready_again = wait_for(lambda: ready_for(one, 11.0), 120)
        check(ready_again is not None, "the former primary is ready as standby behind the new leader")

        for step in range(50):
            two.send("layer/2/opacity", 0.1 + 0.8 * step / 49.0)
            time.sleep(0.04)
        sent_at = time.time()
        time.sleep(0.15)
        killed_at = time.time()
        two.process.send_signal(signal.SIGKILL)
        two.process.wait()
        taken = wait_for(lambda: (one.read() or {}).get("state") == "leader" and (one.read() or {}).get("role") == "master", 30, 0.1)
        leader_at = time.time()
        check(taken, "after the leader was killed the former primary takes over by itself")
        data = one.read() or {}
        measure = data.get("measure", {})
        check(measure.get("taken") and measure.get("automatic") and data.get("epoch") == 3, "the second takeover is automatic and has epoch 3", str(data.get("epoch")))
        check(3.0 <= measure.get("detectionSeconds", 0.0) <= 4.5, "the loss of the killed PC was detected after the set time", str(measure.get("detectionSeconds")))
        value = data.get("controls", {}).get("layer/2/opacity", -1.0)
        index = max(0, min(49, round((value - 0.1) / 0.8 * 49.0)))
        lost_seconds = (killed_at - (sent_at - (49 - index) * 0.04))
        check(value > 0.85, "the last values sent before the kill are on the new leader", str(value))
        summary["crash"] = {"observedKillToLeaderSeconds": leader_at - killed_at, "detectionSeconds": measure.get("detectionSeconds"), "stateAgeSeconds": measure.get("stateAgeAtTakeover"), "servingAfterDecisionSeconds": data.get("switchSeconds"), "lastValueAgeAtKillSeconds": lost_seconds, "lastValue": value}
        check(leader_at - killed_at < 3.0 + 5.0, "the observed time from the kill to the new leader is within the loss time plus five seconds", str(leader_at - killed_at))
        check(lost_seconds < 1.5, "no control change older than one and a half seconds before the kill was lost", str(lost_seconds))
        check(not any(event["kind"] == "takeover-manual" for event in data.get("events", [])), "no takeover by hand was involved")

        for node in (one, two):
            if node.alive():
                node.process.kill()
            node.process.wait()

        closing_primary = os.path.join(work, "closing-primary")
        closing_standby = os.path.join(work, "closing-standby")
        port2 = free_udp_port()
        leader2 = free_udp_port()
        write_settings(closing_primary, settings_for("master", port2, "pc-three", leader2))
        write_settings(closing_standby, settings_for("follower", port2, "pc-four", leader2))
        three = launch("pc-three", closing_primary, work, 60)
        key_file = os.path.join(closing_primary, "netsync_key")
        wait_for(lambda: os.path.exists(key_file) and os.path.getsize(key_file) > 0, 90)
        shutil.copyfile(key_file, os.path.join(closing_standby, "netsync_key"))
        os.chmod(os.path.join(closing_standby, "netsync_key"), 0o600)
        four = launch("pc-four", closing_standby, work, 100, extra=["--failover-takeover-at", "75"])
        ready = wait_for(lambda: ready_for(four, 11.0), 100)
        check(ready is not None, "the second standby is ready before the primary is closed")
        three.process.wait(timeout=120)
        closed_at = time.time()
        check(three.process.returncode == 0, "the second primary ends normally", str(three.process.returncode))
        ended = wait_for(lambda: (four.read() or {}).get("state") == "primary-ended", 10, 0.1)
        check(ended, "the standby sees that the primary was closed on purpose")
        time.sleep(max(0.0, 8.0 - (time.time() - closed_at)))
        data = four.read() or {}
        check(data.get("state") == "primary-ended" and data.get("role") == "follower" and data.get("autoBlockedByEnd") is True, "there is no automatic takeover after a clean close")
        check(not any(event["kind"].startswith("takeover") for event in data.get("events", [])), "no takeover event exists before the one by hand")
        manual = wait_for(lambda: (four.read() or {}).get("state") == "leader", 60, 0.25)
        check(manual, "the takeover by hand at the set time makes the standby the leader")
        data = four.read() or {}
        measure = data.get("measure", {})
        check(measure.get("taken") and not measure.get("automatic") and data.get("epoch") == 2, "the takeover by hand is recorded as manual", json.dumps({"automatic": measure.get("automatic"), "epoch": data.get("epoch")}))
        check(any(event["kind"] == "takeover-manual" for event in data.get("events", [])), "the event log has the takeover by hand")
        summary["cleanClose"] = {"stateAgeSeconds": measure.get("stateAgeAtTakeover"), "servingAfterDecisionSeconds": data.get("switchSeconds")}

        for node in nodes:
            if node.alive():
                node.process.terminate()
        for node in nodes:
            try:
                node.process.wait(timeout=30)
            except subprocess.TimeoutExpired:
                node.process.kill()
    finally:
        for node in nodes:
            if node.alive():
                node.process.kill()

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
    print("two instance failover test passed: " + str(summary["passed"]) + " checks")


if __name__ == "__main__":
    main()
