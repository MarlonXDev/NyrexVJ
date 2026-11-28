import argparse
import hashlib
import json
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import zlib


def read_png(path):
    with open(path, "rb") as handle:
        data = handle.read()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("not a PNG file")
    position = 8
    header = None
    idat = bytearray()
    while position < len(data):
        length, kind = struct.unpack(">I4s", data[position:position + 8])
        body = data[position + 8:position + 8 + length]
        position += 12 + length
        if kind == b"IHDR":
            header = struct.unpack(">IIBBBBB", body)
        elif kind == b"IDAT":
            idat += body
        elif kind == b"IEND":
            break
    width, height, depth, color, _, _, interlace = header
    if depth != 8 or color not in (2, 6) or interlace != 0:
        raise ValueError("unsupported PNG layout")
    channels = 3 if color == 2 else 4
    stride = width * channels
    raw = zlib.decompress(bytes(idat))
    rows = []
    previous = bytearray(stride)
    offset = 0
    for _ in range(height):
        filter_type = raw[offset]
        line = bytearray(raw[offset + 1:offset + 1 + stride])
        offset += 1 + stride
        if filter_type == 1:
            for i in range(channels, stride):
                line[i] = (line[i] + line[i - channels]) & 255
        elif filter_type == 2:
            for i in range(stride):
                line[i] = (line[i] + previous[i]) & 255
        elif filter_type == 3:
            for i in range(stride):
                left = line[i - channels] if i >= channels else 0
                line[i] = (line[i] + ((left + previous[i]) >> 1)) & 255
        elif filter_type == 4:
            for i in range(stride):
                a = line[i - channels] if i >= channels else 0
                b = previous[i]
                c = previous[i - channels] if i >= channels else 0
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                predictor = a if pa <= pb and pa <= pc else (b if pb <= pc else c)
                line[i] = (line[i] + predictor) & 255
        rows.append(line)
        previous = line
    return width, height, channels, rows


def count_colors(path):
    width, height, channels, rows = read_png(path)
    salmon = 0
    blue = 0
    yellow = 0
    lit = 0
    for line in rows:
        for x in range(0, width * channels, channels * 2):
            r, g, b = line[x], line[x + 1], line[x + 2]
            if r > 12 or g > 12 or b > 12:
                lit += 1
            if r > 150 and r - g > 25 and r - b > 20 and g > 90:
                salmon += 1
            elif b > r + 40 and b > 150:
                blue += 1
            elif r > 180 and g > 170 and b < 120:
                yellow += 1
    return {"lit": lit, "salmon": salmon, "blue": blue, "yellow": yellow}


def digest(path):
    with open(path, "rb") as handle:
        return hashlib.sha256(handle.read()).hexdigest()


def run_app(binary, data_dir, image, log):
    env = dict(os.environ)
    env["NYREXVJ_DATA_DIR"] = data_dir
    env.setdefault("LIBGL_ALWAYS_SOFTWARE", "1")
    command = [binary, "--no-welcome", "--no-update-check", "--preset", "Shared 3D World", "--frames", "90", "--dump-output", image]
    with open(log, "w") as handle:
        result = subprocess.run(command, env=env, stdout=handle, stderr=subprocess.STDOUT, timeout=300)
    return result.returncode == 0 and os.path.exists(image)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("binary", nargs="?", default="build/NyrexVJ")
    parser.add_argument("world_tests", nargs="?", default="build/nvj_world_tests")
    parser.add_argument("--output", default="")
    options = parser.parse_args()

    work = tempfile.mkdtemp(prefix="nvj-shared-world-")
    checks = []

    def expect(name, condition, detail=""):
        checks.append({"name": name, "passed": bool(condition), "detail": detail})
        print(("passed  " if condition else "FAILED  ") + name + (" (" + detail + ")" if detail else ""), flush=True)

    try:
        demo = os.path.join(work, "demo")
        written = subprocess.run([options.world_tests, "--write-demo", demo], capture_output=True, text=True)
        expect("the demo files are written by the test program", written.returncode == 0, written.stderr.strip())

        folders = {}
        for name in ("on", "off", "legacy"):
            folders[name] = os.path.join(work, name)
            shutil.copytree(demo, folders[name])
        with open(os.path.join(folders["off"], "settings.json"), "w") as handle:
            json.dump({"sceneWorlds": False}, handle)
        preset_path = os.path.join(folders["legacy"], "presets", "Shared 3D World.json")
        with open(preset_path) as handle:
            preset = json.load(handle)
        for layer in preset["layers"]:
            for key in ("world", "worldX", "worldY", "worldZ", "worldYaw", "worldScale"):
                layer.pop(key, None)
        with open(preset_path, "w") as handle:
            json.dump(preset, handle)

        images = {}
        for name in ("on", "off", "legacy"):
            images[name] = os.path.join(work, name + ".png")
            ok = run_app(options.binary, folders[name], images[name], os.path.join(work, name + ".log"))
            expect("the program renders the demo preset (" + name + ")", ok)
        if all(os.path.exists(path) for path in images.values()):
            on = count_colors(images["on"])
            off = count_colors(images["off"])
            expect("in a shared world both layers are visible", on["salmon"] > 1000 and on["blue"] > 1000, str(on))
            expect("without a world the upper layer covers the lower one as before", off["salmon"] < 150 and off["blue"] > 1000, str(off))
            expect("the picture with a shared world differs from the one without", digest(images["on"]) != digest(images["off"]))
            expect("switching shared worlds off gives the same picture as layers without a world", digest(images["off"]) == digest(images["legacy"]))
            expect("the layer placed in front covers part of the other in the world", on["blue"] > 20000 and on["lit"] > off["lit"], str(on))
    finally:
        keep = os.environ.get("NYREXVJ_KEEP_TEST_FILES")
        if keep:
            print("files kept in " + work)
        else:
            shutil.rmtree(work, ignore_errors=True)

    passed = all(item["passed"] for item in checks)
    if options.output:
        with open(options.output, "w") as handle:
            json.dump({"passed": passed, "checks": checks}, handle, indent=2)
    return 0 if passed else 1


if __name__ == "__main__":
    sys.exit(main())
