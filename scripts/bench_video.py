#!/usr/bin/env python3
import argparse
import json
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

SCENARIOS = [
    ("1x H.264 1080p", ["h264_1080"]),
    ("4x H.264 1080p", ["h264_1080", "h264_1080#2", "h264_1080#3", "h264_1080#4"]),
    ("1x H.264 4K", ["h264_4k"]),
    ("1x HEVC 1080p", ["hevc_1080"]),
    ("1x AV1 1080p", ["av1_1080"]),
    ("1x HAP 1080p", ["hap_1080"]),
    ("1x HAP Alpha 1080p", ["hapalpha_1080"]),
    ("1x HAP Q 1080p", ["hapq_1080"]),
    ("1x HAP 4K", ["hap_4k"]),
    ("4x HAP 1080p", ["hap_1080", "hap_1080#2", "hap_1080#3", "hap_1080#4"]),
    ("2x H.264 1080p + 1x H.264 4K", ["h264_1080", "h264_1080#2", "h264_4k"]),
]


def prepare(videos: Path, data: Path, needed):
    media = data / "media"
    media.mkdir(parents=True, exist_ok=True)
    needed = [n for n in needed if not n.startswith("dxv_")]
    for name in needed:
        stem, _, copy = name.partition("#")
        source = next((p for p in videos.iterdir() if p.stem == stem), None)
        if source is None:
            raise SystemExit(f"missing test video {stem} in {videos}")
        target = media / f"{stem}{('_' + copy) if copy else ''}{source.suffix}"
        if not target.exists():
            target.symlink_to(source.resolve())


def prepare_dxv(exe: Path, dxv: Path, data: Path, runs: int, extra, report: Path):
    media = data / "media"
    media.mkdir(parents=True, exist_ok=True)
    original = media / ("dxv_original" + dxv.suffix)
    if not original.exists():
        original.symlink_to(dxv.resolve())
    staging = data / "convert_input"
    staging.mkdir(parents=True, exist_ok=True)
    source = staging / "dxv_source.mov"
    if not source.exists():
        source.symlink_to(dxv.resolve())
    env = dict(os.environ)
    env["NYREXVJ_DATA_DIR"] = str(data)
    command = [str(exe), "--bench-convert", f"{runs}:{source}", "--convert-keep", str(media), "--bench-out", str(report)] + extra
    subprocess.run(command, env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=3600, check=False)
    if not report.exists():
        raise SystemExit("the conversion measurement failed (FFmpeg with DXV decoder and HAP encoder needed)")
    entry = json.loads(report.read_text())["files"][0]
    if entry.get("failed"):
        raise SystemExit("the conversion measurement failed: " + entry["failed"])
    return entry


def clip_name(name: str) -> str:
    stem, _, copy = name.partition("#")
    return f"{stem}{('_' + copy) if copy else ''}"


def run(exe: Path, data: Path, names, seconds: float, resolution: int, extra, out: Path):
    spec = f"{seconds}:" + ",".join(clip_name(n) for n in names)
    env = dict(os.environ)
    env["NYREXVJ_DATA_DIR"] = str(data)
    env.setdefault("DISPLAY", ":99")
    env.setdefault("LIBGL_ALWAYS_SOFTWARE", "1")
    env.setdefault("XDG_RUNTIME_DIR", "/tmp")
    command = [str(exe), "--fresh", "--no-update-check", "--no-welcome", "--resolution", str(resolution), "--bench-video", spec, "--bench-out", str(out)] + extra
    subprocess.run(command, env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=int(seconds * 4 + 180), check=False)
    if not out.exists():
        return None
    return json.loads(out.read_text())


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--exe", required=True)
    parser.add_argument("--videos", required=True)
    parser.add_argument("--seconds", type=float, default=10.0)
    parser.add_argument("--resolution", type=int, default=0)
    parser.add_argument("--only", default="")
    parser.add_argument("--json", default="")
    parser.add_argument("--arg", action="append", default=[])
    parser.add_argument("--dxv", default="")
    parser.add_argument("--convert-runs", type=int, default=3)
    args = parser.parse_args()

    videos = Path(args.videos)
    chosen = [s for s in SCENARIOS if not args.only or args.only.lower() in s[0].lower()]
    if args.dxv:
        chosen = [("1x DXV original", ["dxv_original"]), ("1x DXV as HAP copy", ["dxv_source_hap"])] if args.only == "dxv" else chosen + [("1x DXV original", ["dxv_original"]), ("1x DXV as HAP copy", ["dxv_source_hap"])]
    needed = sorted({n for _, names in chosen for n in names})
    results = []
    conversion = None
    with tempfile.TemporaryDirectory(prefix="nvj_bench_") as scratch:
        data = Path(scratch) / "data"
        prepare(videos, data, needed)
        if args.dxv:
            conversion = prepare_dxv(Path(args.exe), Path(args.dxv), data, args.convert_runs, args.arg, Path(scratch) / "convert.json")
        for title, names in chosen:
            out = Path(scratch) / "result.json"
            if out.exists():
                out.unlink()
            report = run(Path(args.exe), data, names, args.seconds, args.resolution, args.arg, out)
            if report is None:
                print(f"{title}: failed", file=sys.stderr)
                results.append({"scenario": title, "failed": True})
                continue
            report["scenario"] = title
            results.append(report)
            print(f"{title}: cpu without rasteriser {report.get('cpuWithoutRasterizerPercentOfOneCore', 0):.1f}% of one core, total {report['processCpuPercentOfOneCore']:.1f}%", file=sys.stderr)

    print("| Scenario | CPU without software rasteriser (% of one core) | Process CPU total (%) | Video memory (MB) | Resident memory (MB) |")
    print("|---|---:|---:|---:|---:|")
    for r in results:
        if r.get("failed"):
            print(f"| {r['scenario']} | failed | | | |")
            continue
        media = r.get("memory", {}).get("mediaBytes", 0) / 1048576.0
        resident = r.get("memory", {}).get("processResidentBytes", 0) / 1048576.0
        print(f"| {r['scenario']} | {r.get('cpuWithoutRasterizerPercentOfOneCore', 0):.1f} | {r['processCpuPercentOfOneCore']:.1f} | {media:.1f} | {resident:.0f} |")
    if conversion is not None:
        print()
        print("| DXV source | Variant | Target | Size | Frames | Conversion (s) | Decode (s) | Encode (s) | Frames per second | Faster than playback | Size ratio |")
        print("|---|---|---|---|---:|---:|---:|---:|---:|---:|---:|")
        print(f"| {Path(args.dxv).name} | {conversion['variant']} | {conversion['target']} | {conversion['width']}x{conversion['height']} | {conversion['frames']} | {conversion['medianWallSeconds']:.3f} | {conversion['medianDecodeSeconds']:.3f} | {conversion['medianEncodeSeconds']:.3f} | {conversion['framesPerSecond']:.0f} | {conversion['speedVersusPlayback']:.1f}x | {conversion['sizeRatio']:.2f} |")
    if args.json:
        Path(args.json).write_text(json.dumps({"scenarios": results, "conversion": conversion}, indent=2))


if __name__ == "__main__":
    main()
