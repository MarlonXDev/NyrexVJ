#!/usr/bin/env python3
import argparse
import json
import struct
import subprocess
import tempfile
from pathlib import Path

MODELS = [("skin_arm", "edgebreaker"), ("skin_morph", "sequential"), ("morph_cube", "sequential")]


def read_glb(path):
    data = Path(path).read_bytes()
    json_length = struct.unpack("<I", data[12:16])[0]
    doc = json.loads(data[20:20 + json_length])
    bin_length = struct.unpack("<I", data[20 + json_length:24 + json_length])[0]
    binary = data[28 + json_length:28 + json_length + bin_length]
    return doc, binary


def write_glb(path, doc, binary):
    text = json.dumps(doc, separators=(",", ":")).encode("utf-8")
    while len(text) % 4:
        text += b" "
    body = bytes(binary)
    while len(body) % 4:
        body += b"\0"
    total = 12 + 8 + len(text) + 8 + len(body)
    out = struct.pack("<III", 0x46546C67, 2, total)
    out += struct.pack("<II", len(text), 0x4E4F534A) + text
    out += struct.pack("<II", len(body), 0x004E4942) + body
    Path(path).write_bytes(out)


def pack(source, packer, mode, work):
    doc, binary = read_glb(source)
    prefix = work / source.stem
    subprocess.run([str(packer), str(source), str(prefix), mode], check=True)
    blob = prefix.with_suffix(".drc").read_bytes()
    info = json.loads(prefix.with_suffix(".json").read_text())
    primitive = doc["meshes"][0]["primitives"][0]
    stripped = [primitive["indices"]] + [primitive["attributes"][name] for name in info["attributes"]]
    for index in stripped:
        doc["accessors"][index].pop("bufferView")
    doc["accessors"][primitive["indices"]]["count"] = info["faces"] * 3
    used = set()
    for accessor in doc["accessors"]:
        if "bufferView" in accessor:
            used.add(accessor["bufferView"])
        sparse = accessor.get("sparse")
        if sparse:
            used.add(sparse["indices"]["bufferView"])
            used.add(sparse["values"]["bufferView"])
    new_binary = bytearray()
    new_views = []
    remap = {}
    for index, view in enumerate(doc["bufferViews"]):
        if index not in used:
            continue
        while len(new_binary) % 4:
            new_binary.append(0)
        entry = dict(view)
        entry["byteOffset"] = len(new_binary)
        new_binary += binary[view.get("byteOffset", 0):view.get("byteOffset", 0) + view["byteLength"]]
        remap[index] = len(new_views)
        new_views.append(entry)
    while len(new_binary) % 4:
        new_binary.append(0)
    new_views.append({"buffer": 0, "byteOffset": len(new_binary), "byteLength": len(blob)})
    draco_view = len(new_views) - 1
    new_binary += blob
    for accessor in doc["accessors"]:
        if "bufferView" in accessor:
            accessor["bufferView"] = remap[accessor["bufferView"]]
        sparse = accessor.get("sparse")
        if sparse:
            sparse["indices"]["bufferView"] = remap[sparse["indices"]["bufferView"]]
            sparse["values"]["bufferView"] = remap[sparse["values"]["bufferView"]]
    doc["bufferViews"] = new_views
    doc["buffers"][0]["byteLength"] = len(new_binary)
    primitive.setdefault("extensions", {})["KHR_draco_mesh_compression"] = {"bufferView": draco_view, "attributes": info["attributes"]}
    doc["extensionsUsed"] = ["KHR_draco_mesh_compression"]
    doc["extensionsRequired"] = ["KHR_draco_mesh_compression"]
    return doc, new_binary


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("packer", type=Path)
    parser.add_argument("models", type=Path)
    arguments = parser.parse_args()
    with tempfile.TemporaryDirectory() as temp:
        for stem, mode in MODELS:
            doc, binary = pack(arguments.models / (stem + ".glb"), arguments.packer, mode, Path(temp))
            write_glb(arguments.models / ("draco_" + stem + ".glb"), doc, binary)


if __name__ == "__main__":
    main()
