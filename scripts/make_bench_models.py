#!/usr/bin/env python3
import argparse
import math
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from make_test_models import Builder, bounds_of, material, write_glb


def tube(rings, segments, height, radius):
    positions = []
    normals = []
    for r in range(rings):
        y = height * r / (rings - 1)
        for s in range(segments):
            angle = 2.0 * math.pi * s / segments
            x = math.cos(angle)
            z = math.sin(angle)
            positions.append((radius * x, y, radius * z))
            normals.append((x, 0.0, z))
    indices = []
    for r in range(rings - 1):
        for s in range(segments):
            a = r * segments + s
            b = r * segments + (s + 1) % segments
            c = (r + 1) * segments + (s + 1) % segments
            d = (r + 1) * segments + s
            indices += [a, c, b, a, d, c]
    return positions, normals, indices


def model(rings, segments, skinned, morphed, blended):
    builder = Builder()
    height = 2.0
    positions, normals, indices = tube(rings, segments, height, 0.15)
    attributes = {
        "POSITION": builder.floats(positions, "VEC3", bounds=bounds_of(positions)),
        "NORMAL": builder.floats(normals, "VEC3"),
    }
    primitive = {"attributes": attributes, "indices": builder.indices(indices), "material": 0, "mode": 4}
    mesh = {"primitives": [primitive], "name": "tube"}
    nodes = [{"name": "tube", "mesh": 0}]
    doc = {
        "asset": {"version": "2.0", "generator": "NyrexVJ scripts/make_bench_models.py"},
        "scene": 0,
        "scenes": [{"nodes": [0]}],
        "meshes": [mesh],
        "materials": [material((0.8, 0.5, 0.2, 0.6 if blended else 1.0), "BLEND" if blended else None)],
    }
    times = builder.floats([0.0, 1.0, 2.0], "SCALAR", target=None, bounds=([0.0], [2.0]))
    samplers = []
    channels = []
    if skinned:
        joints = []
        weights = []
        for r in range(rings):
            w0 = 1.0 - r / (rings - 1)
            for _ in range(segments):
                if w0 >= 1.0:
                    joints.append((0, 0, 0, 0))
                    weights.append((1.0, 0.0, 0.0, 0.0))
                elif w0 <= 0.0:
                    joints.append((1, 0, 0, 0))
                    weights.append((1.0, 0.0, 0.0, 0.0))
                else:
                    joints.append((0, 1, 0, 0))
                    weights.append((w0, 1.0 - w0, 0.0, 0.0))
        attributes["JOINTS_0"] = builder.accessor(b"".join(struct.pack("<4H", *j) for j in joints), 5123, len(joints), "VEC4", 34962)
        attributes["WEIGHTS_0"] = builder.floats(weights, "VEC4")
        identity = [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1]
        shifted = [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, -1, 0, 1]
        nodes[0]["skin"] = 0
        nodes += [{"name": "shoulder", "children": [2]}, {"name": "elbow", "translation": [0.0, 1.0, 0.0]}]
        doc["scenes"][0]["nodes"] = [0, 1]
        doc["skins"] = [{"joints": [1, 2], "inverseBindMatrices": builder.floats([identity, shifted], "MAT4", target=None)}]
        half = math.sqrt(0.5)
        rotations = builder.floats([(0, 0, 0, 1), (0, 0, half, half), (0, 0, 0, 1)], "VEC4", target=None)
        samplers.append({"input": times, "output": rotations, "interpolation": "LINEAR"})
        channels.append({"sampler": len(samplers) - 1, "target": {"node": 2, "path": "rotation"}})
    if morphed:
        targets = []
        for k in range(8):
            deltas = [(0.3 * math.cos(k) * (p[1] / height), 0.0, 0.3 * math.sin(k) * (p[1] / height)) for p in positions]
            targets.append({"POSITION": builder.floats(deltas, "VEC3", bounds=bounds_of(deltas))})
        primitive["targets"] = targets
        mesh["weights"] = [0.0] * 8
        values = [0.0] * 8 + [1.0] * 8 + [0.0] * 8
        samplers.append({"input": times, "output": builder.floats(values, "SCALAR", target=None), "interpolation": "LINEAR"})
        channels.append({"sampler": len(samplers) - 1, "target": {"node": 0, "path": "weights"}})
    doc["nodes"] = nodes
    if samplers:
        doc["animations"] = [{"name": "move", "samplers": samplers, "channels": channels}]
    return builder.finish(doc), builder.bin


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("output", type=Path)
    parser.add_argument("--rings", type=int, default=250)
    parser.add_argument("--segments", type=int, default=256)
    arguments = parser.parse_args()
    arguments.output.mkdir(parents=True, exist_ok=True)
    variants = {
        "bench_static.glb": (False, False, False),
        "bench_skin.glb": (True, False, False),
        "bench_morph.glb": (False, True, False),
        "bench_skin_morph.glb": (True, True, False),
        "bench_blend.glb": (False, False, True),
    }
    for name, (skinned, morphed, blended) in variants.items():
        doc, binary = model(arguments.rings, arguments.segments, skinned, morphed, blended)
        write_glb(arguments.output / name, doc, binary)


if __name__ == "__main__":
    main()
