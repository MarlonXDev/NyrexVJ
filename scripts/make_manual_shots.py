#!/usr/bin/env python3
import argparse
import ctypes
import json
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
SEED = ROOT / "scripts" / "manual_shots" / "seed"
SEED_GENERATORS = ROOT / "scripts" / "manual_shots" / "seed_generators"
OUT = ROOT / "site" / "assets" / "manual"

COMMON = ["--fresh", "--no-welcome", "--no-update-check", "--audio-demo", "--frames", "60"]

WIN = (0, 0, 1600, 900)
DECK = (0, 20, 1290, 392)
BROWSER = (1250, 463, 350, 417)
MASTER = (497, 463, 388, 417)
CLIP = (885, 463, 364, 417)
AUDIO = (1250, 463, 350, 417)
TRANSPORT = (0, 408, 730, 52)
STRIP = (1250, 463, 350, 26)
SETTINGS = (358, 158, 874, 584)
CREATOR = (138, 58, 1324, 784)
NODES = (178, 68, 1244, 764)
TIMELINE = (208, 278, 1184, 562)
MANUAL = (280, 98, 1042, 704)
SHOWPKG = (416, 124, 770, 420)
PACKS = (406, 118, 788, 672)
GENERATORS = (310, 90, 980, 720)
CHAINS = (370, 100, 860, 700)

RUNS = {
    "main": ["--column", "1"],
    "library": ["--tab", "1", "--library-mode", "0", "--library-kind", "0"],
    "effects": ["--tab", "1", "--library-mode", "1"],
    "presets": ["--tab", "0"],
    "audio": ["--tab", "2"],
    "master": ["--tab", "3"],
    "set0": ["--settings", "0"],
    "set2": ["--settings", "2"],
    "set3": ["--settings", "3"],
    "set5": ["--settings", "5"],
    "set6": ["--settings", "6"],
    "set7": ["--settings", "7"],
    "set10": ["--settings", "10"],
    "creator": ["--creator"],
    "nodes": ["--node-editor", "Wavy Rainbow"],
    "timeline": ["--timeline"],
    "manual": ["--manual", "first-show"],
    "shows": ["--show-packages", "0"],
    "packs": ["--community-packages", "1"],
    "generators": ["--generators", "0"],
    "gpuparticles": ["--generators", "1", "--generator-select", "GPU Nebula", "--gpu-particles", "1"],
    "chains": ["--chains", "0", "--chain-select", "Neon Glow"],
}

SEEDED = {"nodes", "timeline"}
PACKED = {"packs"}
GENERATED = {"generators", "gpuparticles", "chains"}

FIGURES = [
    ("main-window", "main", WIN),
    ("clip-grid", "main", DECK),
    ("transport-bar", "main", TRANSPORT),
    ("bottom-strip", "main", STRIP),
    ("master-window", "master", MASTER),
    ("clip-window", "main", CLIP),
    ("library", "library", BROWSER),
    ("effects-library", "effects", BROWSER),
    ("presets", "presets", BROWSER),
    ("audio-tab", "audio", AUDIO),
    ("settings-general", "set0", SETTINGS),
    ("settings-audio", "set2", SETTINGS),
    ("settings-output", "set3", SETTINGS),
    ("settings-control", "set5", SETTINGS),
    ("settings-stage", "set6", SETTINGS),
    ("settings-sync", "set7", SETTINGS),
    ("settings-performance", "set10", SETTINGS),
    ("creator", "creator", CREATOR),
    ("node-editor", "nodes", NODES),
    ("timeline", "timeline", TIMELINE),
    ("manual-window", "manual", MANUAL),
    ("show-packages", "shows", SHOWPKG),
    ("community-packages", "packs", PACKS),
    ("generators", "generators", GENERATORS),
    ("gpu-particles", "gpuparticles", GENERATORS),
    ("effect-chains", "chains", CHAINS),
]

CARDS = [
    ("card-clips", "main", (0, 20, 800, 450)),
    ("card-creator", "creator", (150, 70, 1300, 731)),
    ("card-nodes", "nodes", (180, 70, 1240, 690)),
    ("card-audio", "audio", (1000, 440, 600, 440)),
    ("card-output", "set3", (358, 158, 874, 492)),
    ("card-timeline", "timeline", (208, 278, 996, 560)),
]


def move_pointer_away(display):
    try:
        x11 = ctypes.CDLL("libX11.so.6")
        x11.XOpenDisplay.restype = ctypes.c_void_p
        handle = x11.XOpenDisplay(display.encode())
        if not handle:
            return
        root = x11.XDefaultRootWindow(ctypes.c_void_p(handle))
        screen_w = x11.XDisplayWidth(ctypes.c_void_p(handle), 0)
        screen_h = x11.XDisplayHeight(ctypes.c_void_p(handle), 0)
        x11.XWarpPointer(ctypes.c_void_p(handle), 0, ctypes.c_ulong(root), 0, 0, 0, 0, screen_w - 2, screen_h - 2)
        x11.XFlush(ctypes.c_void_p(handle))
    except OSError:
        pass


def build_demo_pack(binary, work, env):
    folder = work / "pack-demo"
    if folder.exists():
        shutil.rmtree(folder)
    (folder / "presets").mkdir(parents=True)
    (folder / "shaders").mkdir()
    manifest = {
        "id": "neon-starter",
        "version": "1.2",
        "name": "Neon Starter Pack",
        "author": "Marlon",
        "description": "A few presets and a glow shader to get started.",
        "license": "MIT",
        "homepage": "https://example.invalid/neon",
        "nyrexvj": {"min": "0.40", "max": ""},
        "dependencies": [],
    }
    (folder / "pack.json").write_text(json.dumps(manifest, indent=2), encoding="utf-8")
    (folder / "LICENSE.txt").write_text("MIT License\n\nA demonstration package.\n", encoding="utf-8")
    (folder / "presets" / "Neon Pulse.json").write_text(json.dumps({"name": "Neon Pulse"}), encoding="utf-8")
    (folder / "shaders" / "glow.frag").write_text("void main() {}\n", encoding="utf-8")
    preview = Image.new("RGB", (260, 146))
    pixels = preview.load()
    for y in range(146):
        for x in range(260):
            pixels[x, y] = (40 + x // 3, 20 + y, 120 + (x + y) // 4)
    preview.save(folder / "preview.png")
    target = work / "neon-starter.nyxpack"
    result = subprocess.run([str(binary), "--pack-build", str(folder), str(target), "--pack-overwrite"], env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=120)
    if result.returncode != 0 or not target.is_file():
        print(result.stdout.decode(errors="replace")[-600:], file=sys.stderr)
        raise SystemExit("the demonstration package could not be built")
    return target


def run_app(binary, lang, key, work):
    data = work / f"data-{lang}-{key}"
    if data.exists():
        shutil.rmtree(data)
    data.mkdir(parents=True)
    if key in SEEDED:
        shutil.copytree(SEED, data, dirs_exist_ok=True)
    if key in GENERATED:
        shutil.copytree(SEED_GENERATORS, data, dirs_exist_ok=True)
    target = work / f"{lang}-{key}.png"
    env = dict(os.environ)
    env["NYREXVJ_DATA_DIR"] = str(data)
    env["LIBGL_ALWAYS_SOFTWARE"] = "1"
    move_pointer_away(env.get("DISPLAY", ":0"))
    extra = []
    if key in PACKED:
        extra = ["--pack-file", str(build_demo_pack(binary, work, env))]
    command = [str(binary), *COMMON, "--lang", lang, *RUNS[key], *extra, "--screenshot", str(target)]
    result = subprocess.run(command, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=300)
    if result.returncode != 0 or not target.is_file():
        print(result.stdout.decode(errors="replace")[-600:], file=sys.stderr)
        raise SystemExit(f"{lang}/{key}: the program did not write a screenshot")
    return target


def save(image, path, size=None, quality=80):
    if size is not None and image.size != size:
        image = image.resize(size, Image.LANCZOS)
    path.parent.mkdir(parents=True, exist_ok=True)
    image.convert("RGB").save(path, "WEBP", quality=quality, method=6)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", required=True)
    parser.add_argument("--langs", nargs="*", default=["en", "de"])
    parser.add_argument("--only", nargs="*", default=[])
    args = parser.parse_args()
    binary = Path(args.binary)
    if not binary.is_file():
        raise SystemExit(f"{binary} not found")
    if not os.environ.get("DISPLAY"):
        raise SystemExit("DISPLAY is not set, start the program under Xvfb with a screen larger than 1600x900")
    wanted_names = set(args.only)
    with tempfile.TemporaryDirectory() as tmp:
        work = Path(tmp)
        for lang in args.langs:
            shots = {}
            for name, key, crop in FIGURES + CARDS:
                if wanted_names and name not in wanted_names:
                    continue
                if key not in shots:
                    shots[key] = Image.open(run_app(binary, lang, key, work)).convert("RGB")
                    if shots[key].size != (1600, 900):
                        raise SystemExit(f"{lang}/{key}: unexpected screenshot size {shots[key].size}")
                x, y, w, h = crop
                image = shots[key].crop((x, y, x + w, y + h))
                if name.startswith("card-"):
                    save(image, OUT / lang / f"{name}.webp", size=(640, 360), quality=78)
                else:
                    save(image, OUT / lang / f"{name}.webp", quality=82)
                print(f"{lang}/{name}.webp")


if __name__ == "__main__":
    main()
