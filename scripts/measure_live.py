import argparse
import csv
import io
import json
import math
import os
import re
import subprocess
import sys
import time

MIB = 1024.0 * 1024.0


def percentile(values, fraction):
    if not values:
        return 0.0
    ordered = sorted(values)
    index = min(len(ordered) - 1, max(0, int(math.ceil(fraction * len(ordered))) - 1))
    return ordered[index]


def summarize(values):
    if not values:
        return {"count": 0}
    return {
        "count": len(values),
        "min": min(values),
        "avg": sum(values) / len(values),
        "p95": percentile(values, 0.95),
        "max": max(values),
    }


def median(values):
    ordered = sorted(values)
    if not ordered:
        return 0.0
    middle = len(ordered) // 2
    if len(ordered) % 2:
        return ordered[middle]
    return (ordered[middle - 1] + ordered[middle]) / 2.0


def growth(values, warmup=0.25, absolute=0.0, relative=0.0):
    skip = int(math.floor(len(values) * min(max(warmup, 0.0), 0.9)))
    window = values[skip:]
    if len(window) < 6:
        return {"judged": False}
    third = len(window) // 3
    first = median(window[:third])
    last = median(window[-third:])
    limit = max(absolute, relative * abs(first))
    return {"judged": True, "first": first, "last": last, "growth": last - first, "limit": limit, "leak": (last - first) > limit}


def parse_proc_stat(text):
    close = text.rfind(")")
    if close < 0:
        return None
    fields = text[close + 2:].split()
    if len(fields) < 13:
        return None
    return int(fields[11]) + int(fields[12])


def parse_proc_rss_bytes(text):
    match = re.search(r"^VmRSS:\s+(\d+)\s+kB", text, re.MULTILINE)
    return int(match.group(1)) * 1024 if match else None


def parse_nvidia_smi(text):
    cards = []
    for row in csv.reader(io.StringIO(text.strip())):
        values = [item.strip() for item in row]
        if len(values) < 4:
            continue
        try:
            cards.append({"index": int(values[0]), "util": float(values[1]), "vram_used_mib": float(values[2]), "vram_total_mib": float(values[3])})
        except ValueError:
            continue
    return cards


def parse_powershell_processes(text):
    text = text.strip()
    if not text:
        return []
    data = json.loads(text)
    if isinstance(data, dict):
        data = [data]
    found = []
    for item in data:
        found.append({"pid": int(item["Id"]), "cpu_seconds": float(item.get("CPU") or 0.0), "rss_bytes": int(item.get("WorkingSet64") or 0)})
    return found


def parse_typeperf(text):
    rows = [row for row in csv.reader(io.StringIO(text.strip())) if row]
    header = None
    last = None
    for row in rows:
        if row and row[0].startswith("(PDH-CSV"):
            header = row
            last = None
        elif header is not None and len(row) == len(header):
            last = row
    if header is None or last is None:
        return {}
    values = {}
    for name, value in zip(header[1:], last[1:]):
        try:
            values[name] = float(value)
        except ValueError:
            continue
    return values


def windows_gpu_for_pids(counters, pids):
    memory = 0.0
    utilization = 0.0
    for name, value in counters.items():
        match = re.search(r"\((?:pid_(\d+))_", name)
        if not match or int(match.group(1)) not in pids:
            continue
        if "GPU Process Memory" in name and "Dedicated Usage" in name:
            memory += value
        elif "GPU Engine" in name and "Utilization Percentage" in name and "engtype_3D" in name:
            utilization += value
    return {"vram_mib": memory / MIB, "util": utilization}


def read_text(path):
    with open(path, encoding="utf-8", errors="replace") as handle:
        return handle.read()


def run(command, timeout=10):
    try:
        done = subprocess.run(command, capture_output=True, text=True, timeout=timeout)
    except (OSError, subprocess.TimeoutExpired):
        return None
    return done.stdout if done.returncode == 0 else None


def linux_pids(name):
    pids = []
    for entry in os.listdir("/proc"):
        if not entry.isdigit():
            continue
        try:
            if read_text("/proc/%s/comm" % entry).strip()[:15] == name[:15]:
                pids.append(int(entry))
        except OSError:
            continue
    return pids


def linux_process_sample(name, ticks):
    cpu = 0.0
    rss = 0
    pids = linux_pids(name)
    for pid in pids:
        try:
            used = parse_proc_stat(read_text("/proc/%d/stat" % pid))
            size = parse_proc_rss_bytes(read_text("/proc/%d/status" % pid))
        except OSError:
            continue
        if used is not None:
            cpu += used / ticks
        if size is not None:
            rss += size
    return {"pids": pids, "cpu_seconds": cpu, "rss_bytes": rss}


def windows_process_sample(name):
    out = run(["powershell", "-NoProfile", "-Command", "Get-Process -Name %s -ErrorAction SilentlyContinue | Select-Object Id,CPU,WorkingSet64 | ConvertTo-Json" % name])
    items = parse_powershell_processes(out) if out else []
    return {"pids": [item["pid"] for item in items], "cpu_seconds": sum(item["cpu_seconds"] for item in items), "rss_bytes": sum(item["rss_bytes"] for item in items)}


def amdgpu_sample(base="/sys/class/drm"):
    cards = []
    if not os.path.isdir(base):
        return cards
    for card in sorted(os.listdir(base)):
        device = os.path.join(base, card, "device")
        if not re.fullmatch(r"card\d+", card) or not os.path.exists(os.path.join(device, "mem_info_vram_used")):
            continue
        try:
            used = int(read_text(os.path.join(device, "mem_info_vram_used")))
            total = int(read_text(os.path.join(device, "mem_info_vram_total")))
            busy_path = os.path.join(device, "gpu_busy_percent")
            busy = float(read_text(busy_path)) if os.path.exists(busy_path) else None
        except (OSError, ValueError):
            continue
        cards.append({"card": card, "util": busy, "vram_used_mib": used / MIB, "vram_total_mib": total / MIB})
    return cards


def choose_gpu(mode):
    if mode != "auto":
        return mode
    if sys.platform == "win32":
        return "windows"
    if run(["nvidia-smi", "-L"]):
        return "nvidia"
    if amdgpu_sample():
        return "amd"
    return "none"


def gpu_sample(mode, pids):
    if mode == "nvidia":
        out = run(["nvidia-smi", "--query-gpu=index,utilization.gpu,memory.used,memory.total", "--format=csv,noheader,nounits"])
        return {"cards": parse_nvidia_smi(out)} if out else {}
    if mode == "amd":
        return {"cards": amdgpu_sample()}
    if mode == "windows":
        counters = ["\\GPU Process Memory(*)\\Dedicated Usage", "\\GPU Engine(*)\\Utilization Percentage"]
        out = run(["typeperf"] + counters + ["-sc", "1"], timeout=20)
        if not out:
            return {}
        return {"processes": windows_gpu_for_pids(parse_typeperf(out), set(pids))}
    return {}


def collect(args):
    ticks = os.sysconf("SC_CLK_TCK") if hasattr(os, "sysconf") else 100
    mode = choose_gpu(args.gpu)
    samples = []
    previous = {}
    start = time.monotonic()
    last = start
    while True:
        now = time.monotonic()
        elapsed = now - start
        if elapsed >= args.seconds:
            break
        entry = {"t": elapsed, "processes": {}}
        all_pids = []
        for name in args.process:
            sample = windows_process_sample(name) if sys.platform == "win32" else linux_process_sample(name, ticks)
            all_pids.extend(sample["pids"])
            wall = now - last
            before = previous.get(name)
            percent = None
            if before is not None and wall > 0.0 and sample["pids"]:
                percent = max(0.0, (sample["cpu_seconds"] - before) / wall * 100.0)
            previous[name] = sample["cpu_seconds"]
            entry["processes"][name] = {"running": bool(sample["pids"]), "cpu_percent_of_one_core": percent, "rss_mib": sample["rss_bytes"] / MIB}
        entry["gpu"] = gpu_sample(mode, all_pids)
        samples.append(entry)
        last = now
        time.sleep(max(0.0, args.interval - (time.monotonic() - now)))
    return mode, samples


def series(samples, getter):
    values = []
    for entry in samples:
        value = getter(entry)
        if value is not None:
            values.append(value)
    return values


def live_conditions(args):
    return {
        "system": getattr(args, "system", "") or "",
        "driver": getattr(args, "driver", "") or "",
        "build": getattr(args, "build", "") or "",
        "resolution": getattr(args, "resolution", "") or "",
        "clips": getattr(args, "clips", "") or "",
        "effects": getattr(args, "effects", "") or "",
        "durationSeconds": args.seconds,
    }


def build_report(args, mode, samples):
    report = {"schema": "nyrexvj-live-report", "tool": "measure_live.py", "platform": sys.platform, "gpu_mode": mode, "seconds": args.seconds, "interval": args.interval, "conditions": live_conditions(args), "samples": samples, "summary": {}}
    for name in args.process:
        cpu = series(samples, lambda e: e["processes"][name]["cpu_percent_of_one_core"])
        rss = series(samples, lambda e: e["processes"][name]["rss_mib"])
        report["summary"][name] = {"cpu_percent_of_one_core": summarize(cpu), "rss_mib": summarize(rss), "rss_growth": growth(rss, 0.25, 64.0, 0.10), "seen_running": any(e["processes"][name]["running"] for e in samples)}
    cards = {}
    for entry in samples:
        for index, card in enumerate(entry["gpu"].get("cards", [])):
            cards.setdefault(index, {"util": [], "vram_used_mib": []})
            if card.get("util") is not None:
                cards[index]["util"].append(card["util"])
            cards[index]["vram_used_mib"].append(card["vram_used_mib"])
    report["summary"]["gpu_cards"] = {str(index): {"util": summarize(item["util"]), "vram_used_mib": summarize(item["vram_used_mib"]), "vram_growth": growth(item["vram_used_mib"], 0.25, 16.0, 0.05)} for index, item in cards.items()}
    processes = series(samples, lambda e: e["gpu"].get("processes", {}).get("vram_mib"))
    if processes:
        report["summary"]["gpu_processes"] = {"vram_mib": summarize(processes), "vram_growth": growth(processes, 0.25, 16.0, 0.05), "util": summarize(series(samples, lambda e: e["gpu"].get("processes", {}).get("util")))}
    return report


def main():
    parser = argparse.ArgumentParser(description="Samples CPU, memory and graphics card use of NyrexVJ (and VRChat) while a show runs")
    parser.add_argument("--seconds", type=float, default=60.0)
    parser.add_argument("--interval", type=float, default=1.0)
    parser.add_argument("--process", action="append", default=None)
    parser.add_argument("--gpu", choices=["auto", "nvidia", "amd", "windows", "none"], default="auto")
    parser.add_argument("--out", default="")
    parser.add_argument("--system", default="")
    parser.add_argument("--driver", default="")
    parser.add_argument("--build", default="")
    parser.add_argument("--resolution", default="")
    parser.add_argument("--clips", default="")
    parser.add_argument("--effects", default="")
    args = parser.parse_args()
    if args.process is None:
        args.process = ["NyrexVJ"]
    if args.seconds < 1.0 or args.interval < 0.2:
        print("--seconds must be at least 1 and --interval at least 0.2")
        return 1
    mode, samples = collect(args)
    report = build_report(args, mode, samples)
    text = json.dumps(report, indent=2)
    if args.out:
        with open(args.out, "w", encoding="utf-8", newline="\n") as handle:
            handle.write(text + "\n")
    for name in args.process:
        item = report["summary"][name]
        print("%s: seen running %s, CPU avg %.1f %%, RSS max %.0f MiB" % (name, item["seen_running"], item["cpu_percent_of_one_core"].get("avg", 0.0), item["rss_mib"].get("max", 0.0)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
