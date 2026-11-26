#!/usr/bin/env python3
import argparse
import json
import os
import random
import re
import shutil
import socket
import struct
import subprocess
import sys
import tempfile
import threading
import time

PHASE = re.compile(r"STREAM-PHASE (\w+)")


def free_port(kind):
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM if kind == "udp" else socket.SOCK_STREAM)
    sock.bind(("127.0.0.1", 0))
    port = sock.getsockname()[1]
    sock.close()
    return port


class FfmpegSender:
    def __init__(self, ffmpeg, source, codec_args, url, env):
        self.command = [ffmpeg, "-hide_banner", "-loglevel", "error", "-re", "-f", "lavfi", "-i", source] + codec_args + ["-f", "mpegts", url]
        self.env = env
        self.wanted = False
        self.alive = True
        self.process = None
        self.lock = threading.Lock()
        self.thread = threading.Thread(target=self.watch, daemon=True)
        self.thread.start()

    def watch(self):
        while self.alive:
            with self.lock:
                if self.wanted and (self.process is None or self.process.poll() is not None):
                    self.process = subprocess.Popen(self.command, env=self.env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            time.sleep(0.25)

    def start(self):
        with self.lock:
            self.wanted = True

    def stop(self):
        with self.lock:
            self.wanted = False
            if self.process is not None and self.process.poll() is None:
                self.process.kill()
                self.process.wait()
            self.process = None

    def close(self):
        self.stop()
        self.alive = False
        self.thread.join(timeout=2)


def read_jpeg(data):
    pos = 2
    tables = {}
    width = height = 0
    sampling = None
    scan = None
    while pos < len(data):
        if data[pos] != 0xFF:
            pos += 1
            continue
        marker = data[pos + 1]
        if marker == 0xD8 or marker == 0x01 or 0xD0 <= marker <= 0xD7:
            pos += 2
            continue
        if marker == 0xD9:
            break
        length = struct.unpack(">H", data[pos + 2:pos + 4])[0]
        segment = data[pos + 4:pos + 2 + length]
        if marker == 0xDB:
            i = 0
            while i < len(segment):
                precision = segment[i] >> 4
                table_id = segment[i] & 15
                i += 1
                size = 128 if precision else 64
                tables[table_id] = segment[i:i + size]
                i += size
        elif marker == 0xC0:
            height, width = struct.unpack(">HH", segment[1:5])
            sampling = segment[7]
        elif marker == 0xDA:
            end = data.rfind(b"\xff\xd9")
            scan = data[pos + 2 + length:end]
            break
        pos += 2 + length
    kind = {0x21: 0, 0x22: 1}[sampling]
    return {"width": width, "height": height, "type": kind, "tables": tables[0] + tables.get(1, tables[0]), "scan": scan}


def jpeg_packets(frame, sequence, stamp, ssrc, mtu=1400):
    packets = []
    data = frame["scan"]
    offset = 0
    while offset < len(data):
        header = bytes([0]) + offset.to_bytes(3, "big") + bytes([frame["type"], 255, frame["width"] // 8, frame["height"] // 8])
        extra = b""
        if offset == 0:
            extra = struct.pack(">BBH", 0, 0, len(frame["tables"])) + frame["tables"]
        room = mtu - 12 - len(header) - len(extra)
        chunk = data[offset:offset + room]
        last = offset + len(chunk) >= len(data)
        rtp = struct.pack(">BBHII", 0x80, (0x80 if last else 0) | 26, sequence & 0xFFFF, stamp & 0xFFFFFFFF, ssrc)
        packets.append(rtp + header + extra + chunk)
        sequence += 1
        offset += len(chunk)
    return packets, sequence


class RtspServer:
    def __init__(self, port, jpeg, fps=25):
        self.port = port
        self.frame = read_jpeg(jpeg)
        self.fps = fps
        self.running = False
        self.listener = None
        self.clients = []
        self.lock = threading.Lock()
        self.thread = None

    def start(self):
        listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        listener.bind(("127.0.0.1", self.port))
        listener.listen(4)
        listener.settimeout(0.5)
        self.listener = listener
        self.running = True
        self.thread = threading.Thread(target=self.accept, daemon=True)
        self.thread.start()

    def stop(self):
        self.running = False
        if self.listener is not None:
            self.listener.close()
            self.listener = None
        with self.lock:
            clients = list(self.clients)
            self.clients = []
        for client in clients:
            try:
                client.shutdown(socket.SHUT_RDWR)
            except OSError:
                pass
            client.close()
        if self.thread is not None:
            self.thread.join(timeout=2)
            self.thread = None

    def accept(self):
        while self.running:
            try:
                client, _ = self.listener.accept()
            except socket.timeout:
                continue
            except OSError:
                return
            with self.lock:
                self.clients.append(client)
            threading.Thread(target=self.serve, args=(client,), daemon=True).start()

    def reply(self, client, write_lock, cseq, headers=None, body=b""):
        lines = ["RTSP/1.0 200 OK", "CSeq: " + cseq, "Server: nyrexvj-test"]
        for key, value in (headers or {}).items():
            lines.append(key + ": " + value)
        if body:
            lines.append("Content-Length: " + str(len(body)))
        data = ("\r\n".join(lines) + "\r\n\r\n").encode() + body
        with write_lock:
            client.sendall(data)

    def serve(self, client):
        write_lock = threading.Lock()
        buffer = b""
        playing = threading.Event()
        client.settimeout(0.5)
        try:
            while self.running:
                try:
                    chunk = client.recv(4096)
                except socket.timeout:
                    continue
                if not chunk:
                    return
                buffer += chunk
                while True:
                    if buffer.startswith(b"$"):
                        if len(buffer) < 4:
                            break
                        size = struct.unpack(">H", buffer[2:4])[0]
                        if len(buffer) < 4 + size:
                            break
                        buffer = buffer[4 + size:]
                        continue
                    end = buffer.find(b"\r\n\r\n")
                    if end < 0:
                        break
                    head = buffer[:end].decode("latin-1")
                    buffer = buffer[end + 4:]
                    lines = head.split("\r\n")
                    method = lines[0].split(" ")[0]
                    fields = {}
                    for line in lines[1:]:
                        if ":" in line:
                            key, value = line.split(":", 1)
                            fields[key.strip().lower()] = value.strip()
                    cseq = fields.get("cseq", "0")
                    if method == "OPTIONS":
                        self.reply(client, write_lock, cseq, {"Public": "OPTIONS, DESCRIBE, SETUP, PLAY, PAUSE, TEARDOWN, GET_PARAMETER"})
                    elif method == "DESCRIBE":
                        sdp = "\r\n".join([
                            "v=0",
                            "o=- 0 0 IN IP4 127.0.0.1",
                            "s=NyrexVJ test stream",
                            "c=IN IP4 0.0.0.0",
                            "t=0 0",
                            "a=control:*",
                            "m=video 0 RTP/AVP 26",
                            "a=rtpmap:26 JPEG/90000",
                            "a=control:streamid=0",
                            "",
                        ]).encode()
                        self.reply(client, write_lock, cseq, {"Content-Type": "application/sdp", "Content-Base": "rtsp://127.0.0.1:%d/live/" % self.port}, sdp)
                    elif method == "SETUP":
                        self.reply(client, write_lock, cseq, {"Transport": "RTP/AVP/TCP;unicast;interleaved=0-1", "Session": "12345678;timeout=60"})
                    elif method == "PLAY":
                        self.reply(client, write_lock, cseq, {"Session": "12345678", "Range": "npt=0.000-"})
                        playing.set()
                        threading.Thread(target=self.stream, args=(client, write_lock), daemon=True).start()
                    elif method == "TEARDOWN":
                        self.reply(client, write_lock, cseq, {"Session": "12345678"})
                        return
                    else:
                        self.reply(client, write_lock, cseq, {"Session": "12345678"})
        except OSError:
            return
        finally:
            client.close()

    def stream(self, client, write_lock):
        sequence = random.randint(0, 60000)
        started = time.monotonic()
        count = 0
        while self.running:
            due = started + count / self.fps
            delay = due - time.monotonic()
            if delay > 0:
                time.sleep(delay)
            packets, sequence = jpeg_packets(self.frame, sequence, int(count * 90000 / self.fps), 0x4E595831)
            try:
                with write_lock:
                    for packet in packets:
                        client.sendall(b"$\x00" + struct.pack(">H", len(packet)) + packet)
            except OSError:
                return
            count += 1


class Display:
    def __init__(self):
        self.number = None
        self.process = None
        for number in range(90, 140):
            if os.path.exists("/tmp/.X%d-lock" % number) or os.path.exists("/tmp/.X11-unix/X%d" % number):
                continue
            process = subprocess.Popen(["Xvfb", ":%d" % number, "-screen", "0", "1280x720x24", "-nolisten", "tcp"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            deadline = time.time() + 15
            while time.time() < deadline and process.poll() is None and not os.path.exists("/tmp/.X11-unix/X%d" % number):
                time.sleep(0.1)
            if process.poll() is None and os.path.exists("/tmp/.X11-unix/X%d" % number):
                self.number = number
                self.process = process
                return
            if process.poll() is None:
                process.kill()
                process.wait()
        raise RuntimeError("no virtual display could be started")

    def close(self):
        if self.process is not None and self.process.poll() is None:
            self.process.terminate()
            try:
                self.process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self.process.kill()
                self.process.wait()


class Run:
    def __init__(self, binary, spec, out_json, env, timeout):
        command = [binary, "--fresh", "--no-update-check", "--no-welcome", "--stream-selftest", spec, "--bench-out", out_json]
        self.process = subprocess.Popen(command, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, errors="replace")
        self.lines = []
        self.events = {}
        self.timeout = timeout
        self.reader = threading.Thread(target=self.read, daemon=True)
        self.reader.start()

    def event(self, name):
        if name not in self.events:
            self.events[name] = threading.Event()
        return self.events[name]

    def read(self):
        for line in self.process.stdout:
            self.lines.append(line.rstrip("\n"))
            match = PHASE.search(line)
            if match:
                self.event(match.group(1)).set()
        self.process.stdout.close()

    def wait_for(self, name, seconds):
        return self.event(name).wait(seconds)

    def diagnose(self):
        pid = self.process.pid
        print("the program did not end in time, pid %d" % pid, flush=True)
        try:
            for tid in sorted(os.listdir("/proc/%d/task" % pid), key=int):
                base = "/proc/%d/task/%s/" % (pid, tid)
                parts = []
                for name in ("comm", "wchan", "syscall"):
                    try:
                        with open(base + name, "r", errors="replace") as handle:
                            parts.append(handle.read().strip().split(" ")[0])
                    except OSError:
                        parts.append("?")
                print("    thread %s %s" % (tid, " ".join(parts)), flush=True)
        except OSError:
            pass
        if shutil.which("gdb"):
            command = ["gdb", "-p", str(pid), "-batch", "-ex", "thread apply all bt 14"]
            for prefix in ([], ["sudo", "-n"]):
                try:
                    result = subprocess.run(prefix + command, capture_output=True, text=True, errors="replace", timeout=90)
                except (OSError, subprocess.TimeoutExpired):
                    continue
                lines = [line for line in result.stdout.splitlines() if line.startswith("#") or line.startswith("Thread")]
                if lines:
                    for line in lines[-160:]:
                        print("    gdb " + line[:220], flush=True)
                    break
        print("    last lines of the program:", flush=True)
        for line in self.lines[-30:]:
            print("    | " + line[:200], flush=True)

    def finish(self, seconds):
        try:
            self.process.wait(timeout=seconds)
        except subprocess.TimeoutExpired:
            self.diagnose()
            self.process.kill()
            self.process.wait()
        self.reader.join(timeout=3)
        return self.process.returncode

    def kill(self):
        if self.process.poll() is None:
            self.process.kill()
            self.process.wait()


def reddish(pixel):
    return pixel is not None and pixel[0] >= 200 and pixel[1] <= 60 and pixel[2] <= 60


def load_json(path):
    try:
        with open(path, "r", encoding="utf-8") as handle:
            return json.load(handle)
    except (OSError, ValueError):
        return None


def sender_args(codec):
    table = {
        "mpeg2": ["-c:v", "mpeg2video", "-b:v", "2M", "-g", "25", "-pix_fmt", "yuv420p"],
        "mpeg4": ["-c:v", "mpeg4", "-b:v", "1M", "-g", "25"],
        "h264": ["-c:v", "libopenh264", "-b:v", "2M", "-g", "25", "-pix_fmt", "yuv420p"],
        "mjpeg": ["-c:v", "mjpeg", "-q:v", "4", "-pix_fmt", "yuvj420p"],
    }
    return table[codec]


class Context:
    def __init__(self, args):
        self.args = args
        self.work = args.work
        self.env = dict(os.environ)
        self.env["LIBGL_ALWAYS_SOFTWARE"] = "1"
        if not self.env.get("NYREXVJ_DATA_DIR"):
            data = os.path.join(self.work, "data")
            os.makedirs(data, exist_ok=True)
            self.env["NYREXVJ_DATA_DIR"] = data
        self.display = None
        if not self.env.get("DISPLAY"):
            self.display = Display()
            self.env["DISPLAY"] = ":%d" % self.display.number
        self.results = []

    def close(self):
        if self.display is not None:
            self.display.close()

    def record(self, name, passed, details):
        self.results.append({"scenario": name, "passed": bool(passed), **details})
        print("%-18s %s" % (name, "passed" if passed else "FAILED"), flush=True)
        for key, value in details.items():
            if key != "problems":
                print("    %s: %s" % (key, value), flush=True)
        for problem in details.get("problems", []):
            print("    problem: %s" % problem, flush=True)


def phases_scenario(ctx, name, spec_for, start_sender, stop_sender, profile="balanced", size="640x360", live_limit=120):
    out_json = os.path.join(ctx.work, name + ".json")
    problems = []
    details = {}
    run = Run(ctx.args.binary, "%s@%s" % (profile, spec_for), out_json, ctx.env, 400)
    try:
        start_sender()
        if not run.wait_for("live", live_limit):
            problems.append("no picture arrived")
        else:
            time.sleep(1.0)
            stop_sender()
            if not run.wait_for("lost", 60):
                problems.append("the lost signal was not reported")
            else:
                start_sender()
                if not run.wait_for("recovered", 120):
                    problems.append("the stream did not come back")
        code = run.finish(90)
    finally:
        stop_sender()
        run.kill()
    report = load_json(out_json)
    if code != 0:
        problems.append("exit code %s" % code)
    if report is None:
        problems.append("no result file")
    else:
        live = report.get("live", {})
        loss = report.get("loss", {})
        recovery = report.get("recovery", {})
        if not reddish(live.get("centerPixel")):
            problems.append("the picture is not red: %s" % live.get("centerPixel"))
        if live.get("size") != size:
            problems.append("size %s" % live.get("size"))
        if not report.get("releasedWhenUnused"):
            problems.append("the input was not released")
        if report.get("threadsAfter", 0) > report.get("threadsBefore", 0):
            problems.append("threads grew from %s to %s" % (report.get("threadsBefore"), report.get("threadsAfter")))
        details = {
            "profile": report.get("profile"),
            "codec": live.get("codec"),
            "size": live.get("size"),
            "firstPictureSeconds": round(live.get("secondsToFirstPicture", 0.0), 2),
            "pictureRate": round(live.get("fps", 0.0), 1),
            "jitterMs": round(live.get("jitterMs", 0.0), 2),
            "lossDetectedSeconds": round(loss.get("detectedAfterLastPictureSeconds", 0.0), 2),
            "recoveredSeconds": round(recovery.get("secondsAfterLoss", 0.0), 2),
        }
    details["problems"] = problems
    ctx.record(name, not problems, details)


def scenario_srt_caller(ctx, codec="mpeg2", profile="balanced"):
    port = free_port("tcp")
    url = "srt://127.0.0.1:%d?mode=listener&latency=120000" % port
    sender = FfmpegSender(ctx.args.ffmpeg, "color=c=red:size=640x360:rate=25", sender_args(codec), url, ctx.env)
    try:
        phases_scenario(ctx, "srt-caller-%s-%s" % (codec, profile), "srt://127.0.0.1:%d?mode=caller" % port, sender.start, sender.stop, profile)
    finally:
        sender.close()


def scenario_srt_listener(ctx):
    port = free_port("udp")
    url = "srt://127.0.0.1:%d?mode=caller&latency=120000" % port
    sender = FfmpegSender(ctx.args.ffmpeg, "color=c=red:size=640x360:rate=25", sender_args("mpeg2"), url, ctx.env)
    try:
        phases_scenario(ctx, "srt-listener", "srt://:%d?mode=listener" % port, sender.start, sender.stop)
    finally:
        sender.close()


def scenario_udp(ctx):
    port = free_port("udp")
    url = "udp://127.0.0.1:%d?pkt_size=1316" % port
    sender = FfmpegSender(ctx.args.ffmpeg, "color=c=red:size=640x360:rate=25", sender_args("mpeg2"), url, ctx.env)
    try:
        phases_scenario(ctx, "udp-mpegts", "udp://@:%d" % port, sender.start, sender.stop)
    finally:
        sender.close()


def make_jpeg(ctx):
    path = os.path.join(ctx.work, "red.jpg")
    subprocess.run([ctx.args.ffmpeg, "-hide_banner", "-loglevel", "error", "-y", "-f", "lavfi", "-i", "color=c=red:size=640x360", "-frames:v", "1", "-pix_fmt", "yuvj420p", "-c:v", "mjpeg", "-huffman", "default", "-q:v", "3", "-f", "image2", path], check=True, env=ctx.env)
    with open(path, "rb") as handle:
        return handle.read()


def scenario_rtsp(ctx):
    port = free_port("tcp")
    server = RtspServer(port, make_jpeg(ctx))
    try:
        phases_scenario(ctx, "rtsp-tcp-mjpeg", "rtsp://127.0.0.1:%d/live" % port, server.start, server.stop)
    finally:
        server.stop()


def reject_scenario(ctx, name, source, codec_args, expect_text):
    port = free_port("udp")
    url = "udp://127.0.0.1:%d?pkt_size=1316" % port
    sender = FfmpegSender(ctx.args.ffmpeg, source, codec_args, url, ctx.env)
    out_json = os.path.join(ctx.work, name + ".json")
    problems = []
    details = {}
    run = Run(ctx.args.binary, "reject@udp://@:%d" % port, out_json, ctx.env, 120)
    try:
        sender.start()
        code = run.finish(120)
    finally:
        sender.close()
        run.kill()
    report = load_json(out_json)
    if code != 0:
        problems.append("exit code %s" % code)
    if report is None:
        problems.append("no result file")
    else:
        message = report.get("message", "")
        details["message"] = message
        if expect_text not in message:
            problems.append("the message does not mention %r" % expect_text)
        if report.get("uploaded", 1) != 0:
            problems.append("a picture was shown")
    details["problems"] = problems
    ctx.record(name, not problems, details)


def scenario_reject_codec(ctx):
    reject_scenario(ctx, "reject-codec", "color=c=red:size=640x360:rate=25", sender_args("mpeg4"), "codec")


def scenario_reject_size(ctx):
    reject_scenario(ctx, "reject-size", "color=c=red:size=8400x64:rate=10", sender_args("mpeg2"), "picture size")


def survive_scenario(ctx, name, feeder):
    port = free_port("udp")
    out_json = os.path.join(ctx.work, name + ".json")
    problems = []
    run = Run(ctx.args.binary, "reject@udp://@:%d" % port, out_json, ctx.env, 120)
    stop = threading.Event()
    thread = threading.Thread(target=feeder, args=(port, stop), daemon=True)
    thread.start()
    try:
        code = run.finish(150)
    finally:
        stop.set()
        thread.join(timeout=3)
        run.kill()
    if code is None or code < 0:
        problems.append("the program was killed by signal %s" % code)
    if code not in (0, 1):
        problems.append("exit code %s" % code)
    crashed = [line for line in run.lines if "Segmentation" in line or "AddressSanitizer" in line or "terminate called" in line]
    if crashed:
        problems.append(crashed[0][:160])
    if run.event("live").is_set():
        problems.append("garbage was shown as a picture")
    details = {"exitCode": code, "logLines": len(run.lines), "problems": problems}
    if problems:
        details["lastLines"] = run.lines[-30:]
    ctx.record(name, not problems, details)


def feed_random(port, stop):
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    rng = random.Random(7)
    while not stop.is_set():
        size = rng.choice([188, 376, 1316, 7 * 188, 1400, 64])
        data = bytearray(rng.getrandbits(8) for _ in range(size))
        if rng.random() < 0.7:
            for i in range(0, size - 187, 188):
                data[i] = 0x47
        sock.sendto(bytes(data), ("127.0.0.1", port))
        time.sleep(0.002)


def make_corrupt_feeder(ctx):
    path = os.path.join(ctx.work, "clean.ts")
    subprocess.run([ctx.args.ffmpeg, "-hide_banner", "-loglevel", "error", "-y", "-f", "lavfi", "-i", "testsrc=size=320x180:rate=25", "-t", "4"] + sender_args("mpeg2") + ["-f", "mpegts", path], check=True, env=ctx.env)
    with open(path, "rb") as handle:
        clean = handle.read()

    def feed(port, stop):
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        rng = random.Random(11)
        while not stop.is_set():
            data = bytearray(clean)
            for _ in range(len(data) // 40):
                data[rng.randrange(len(data))] = rng.getrandbits(8)
            for i in range(0, len(data), 1316):
                if stop.is_set():
                    return
                sock.sendto(bytes(data[i:i + 1316]), ("127.0.0.1", port))
                time.sleep(0.004)

    return feed


def scenario_garbage(ctx):
    survive_scenario(ctx, "survive-garbage", feed_random)


def scenario_corrupt(ctx):
    survive_scenario(ctx, "survive-corruption", make_corrupt_feeder(ctx))


def scenario_profiles(ctx):
    rows = []
    for profile in ("low", "balanced", "smooth"):
        port = free_port("tcp")
        url = "srt://127.0.0.1:%d?mode=listener&latency=120000" % port
        sender = FfmpegSender(ctx.args.ffmpeg, "color=c=red:size=640x360:rate=25", sender_args("mpeg2"), url, ctx.env)
        out_json = os.path.join(ctx.work, "profile-%s.json" % profile)
        run = Run(ctx.args.binary, "%s,once@srt://127.0.0.1:%d?mode=caller" % (profile, port), out_json, ctx.env, 120)
        try:
            sender.start()
            code = run.finish(120)
        finally:
            sender.close()
            run.kill()
        report = load_json(out_json) or {}
        live = report.get("live", {})
        rows.append({"profile": profile, "exitCode": code, "firstPictureSeconds": round(live.get("secondsToFirstPicture", 0.0), 2), "latencyMs": round(live.get("latencyMs", 0.0), 1), "jitterMs": round(live.get("jitterMs", 0.0), 2), "live": live.get("live", False)})
    passed = all(row["live"] and row["exitCode"] == 0 for row in rows)
    ctx.record("profiles", passed, {"rows": rows, "problems": [] if passed else ["a profile did not reach live"]})


SCENARIOS = {
    "srt-caller": scenario_srt_caller,
    "srt-listener": scenario_srt_listener,
    "udp": scenario_udp,
    "rtsp": scenario_rtsp,
    "srt-h264": lambda ctx: scenario_srt_caller(ctx, "h264"),
    "srt-low": lambda ctx: scenario_srt_caller(ctx, "mpeg2", "low"),
    "srt-smooth": lambda ctx: scenario_srt_caller(ctx, "mpeg2", "smooth"),
    "reject-codec": scenario_reject_codec,
    "reject-size": scenario_reject_size,
    "garbage": scenario_garbage,
    "corruption": scenario_corrupt,
    "profiles": scenario_profiles,
}

DEFAULT = ["srt-caller", "srt-listener", "udp", "rtsp", "reject-codec", "reject-size", "garbage", "corruption", "profiles"]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", default="build/NyrexVJ")
    parser.add_argument("--ffmpeg", default="ffmpeg")
    parser.add_argument("--scenario", action="append")
    parser.add_argument("--out")
    parser.add_argument("--work")
    args = parser.parse_args()
    if not os.path.exists(args.binary):
        print("the program was not found: %s" % args.binary, file=sys.stderr)
        return 2
    args.binary = os.path.abspath(args.binary)
    cleanup = args.work is None
    args.work = args.work or tempfile.mkdtemp(prefix="nvj-stream-")
    os.makedirs(args.work, exist_ok=True)
    ctx = Context(args)
    names = args.scenario or DEFAULT
    try:
        for name in names:
            if name not in SCENARIOS:
                print("unknown scenario %s" % name, file=sys.stderr)
                return 2
            SCENARIOS[name](ctx)
    finally:
        ctx.close()
        if cleanup:
            shutil.rmtree(args.work, ignore_errors=True)
    if args.out:
        with open(args.out, "w", encoding="utf-8") as handle:
            json.dump(ctx.results, handle, indent=2)
            handle.write("\n")
    failed = [item["scenario"] for item in ctx.results if not item["passed"]]
    print("%d of %d scenarios passed" % (len(ctx.results) - len(failed), len(ctx.results)), flush=True)
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
