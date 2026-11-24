#!/usr/bin/env python3
import argparse
import json
import math
import struct
from pathlib import Path


class Builder:
    def __init__(self):
        self.bin = bytearray()
        self.views = []
        self.accessors = []

    def view(self, data, target=None):
        while len(self.bin) % 4:
            self.bin.append(0)
        entry = {"buffer": 0, "byteOffset": len(self.bin), "byteLength": len(data)}
        if target:
            entry["target"] = target
        self.bin += data
        self.views.append(entry)
        return len(self.views) - 1

    def accessor(self, data, component, count, kind, target=None, bounds=None):
        entry = {"bufferView": self.view(data, target), "componentType": component, "count": count, "type": kind}
        if bounds:
            entry["min"], entry["max"] = bounds
        self.accessors.append(entry)
        return len(self.accessors) - 1

    def floats(self, values, kind, target=34962, bounds=None):
        width = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4, "MAT4": 16}[kind]
        flat = [component for item in values for component in (item if isinstance(item, (list, tuple)) else (item,))]
        return self.accessor(struct.pack("<%df" % len(flat), *flat), 5126, len(flat) // width, kind, target, bounds)

    def indices(self, values):
        return self.accessor(struct.pack("<%dI" % len(values), *values), 5125, len(values), "SCALAR", 34963)

    def finish(self, doc):
        while len(self.bin) % 4:
            self.bin.append(0)
        doc["buffers"] = [{"byteLength": len(self.bin)}]
        doc["bufferViews"] = self.views
        doc["accessors"] = self.accessors
        return doc


def write_glb(path, doc, binary):
    text = json.dumps(doc, separators=(",", ":")).encode("utf-8")
    while len(text) % 4:
        text += b" "
    body = bytes(binary)
    total = 12 + 8 + len(text) + 8 + len(body)
    out = struct.pack("<III", 0x46546C67, 2, total)
    out += struct.pack("<II", len(text), 0x4E4F534A) + text
    out += struct.pack("<II", len(body), 0x004E4942) + body
    Path(path).write_bytes(out)


def bounds_of(points):
    return [min(p[i] for p in points) for i in range(3)], [max(p[i] for p in points) for i in range(3)]


def column():
    heights = [0.0, 0.5, 1.0, 1.5, 2.0]
    corners = [(0.1, 0.1), (-0.1, 0.1), (-0.1, -0.1), (0.1, -0.1)]
    positions = []
    normals = []
    for y in heights:
        for x, z in corners:
            positions.append((x, y, z))
            length = math.hypot(x, z)
            normals.append((x / length, 0.0, z / length))
    indices = []
    for k in range(4):
        for c in range(4):
            a = k * 4 + c
            b = k * 4 + (c + 1) % 4
            cc = (k + 1) * 4 + (c + 1) % 4
            d = (k + 1) * 4 + c
            indices += [a, cc, b, a, d, cc]
    return positions, normals, indices


def cube():
    faces = [
        ((1, 0, 0), [(0.5, -0.5, -0.5), (0.5, 0.5, -0.5), (0.5, 0.5, 0.5), (0.5, -0.5, 0.5)]),
        ((-1, 0, 0), [(-0.5, -0.5, 0.5), (-0.5, 0.5, 0.5), (-0.5, 0.5, -0.5), (-0.5, -0.5, -0.5)]),
        ((0, 1, 0), [(-0.5, 0.5, -0.5), (-0.5, 0.5, 0.5), (0.5, 0.5, 0.5), (0.5, 0.5, -0.5)]),
        ((0, -1, 0), [(-0.5, -0.5, 0.5), (-0.5, -0.5, -0.5), (0.5, -0.5, -0.5), (0.5, -0.5, 0.5)]),
        ((0, 0, 1), [(-0.5, -0.5, 0.5), (0.5, -0.5, 0.5), (0.5, 0.5, 0.5), (-0.5, 0.5, 0.5)]),
        ((0, 0, -1), [(0.5, -0.5, -0.5), (-0.5, -0.5, -0.5), (-0.5, 0.5, -0.5), (0.5, 0.5, -0.5)]),
    ]
    positions = []
    normals = []
    indices = []
    for normal, quad in faces:
        base = len(positions)
        positions += quad
        normals += [normal] * 4
        indices += [base, base + 1, base + 2, base, base + 2, base + 3]
    return positions, normals, indices


def quad(half, z):
    positions = [(-half, -half, z), (half, -half, z), (half, half, z), (-half, half, z)]
    normals = [(0.0, 0.0, 1.0)] * 4
    return positions, normals, [0, 1, 2, 0, 2, 3]


def material(color, alpha_mode=None, double_sided=False, metallic=0.0, roughness=0.6):
    entry = {"pbrMetallicRoughness": {"baseColorFactor": list(color), "metallicFactor": metallic, "roughnessFactor": roughness}}
    if alpha_mode:
        entry["alphaMode"] = alpha_mode
    if double_sided:
        entry["doubleSided"] = True
    return entry


def mesh_attributes(builder, positions, normals):
    return {
        "POSITION": builder.floats(positions, "VEC3", bounds=bounds_of(positions)),
        "NORMAL": builder.floats(normals, "VEC3"),
    }


def skin_data(builder, ring_weights):
    joints = []
    weights = []
    for w0 in ring_weights:
        for _ in range(4):
            if w0 >= 1.0:
                joints.append((0, 0, 0, 0))
                weights.append((1.0, 0.0, 0.0, 0.0))
            elif w0 <= 0.0:
                joints.append((1, 0, 0, 0))
                weights.append((1.0, 0.0, 0.0, 0.0))
            else:
                joints.append((0, 1, 0, 0))
                weights.append((w0, 1.0 - w0, 0.0, 0.0))
    joint_bytes = b"".join(struct.pack("<4H", *j) for j in joints)
    return (
        builder.accessor(joint_bytes, 5123, len(joints), "VEC4", 34962),
        builder.floats(weights, "VEC4"),
    )


def skin_arm(with_morph):
    builder = Builder()
    positions, normals, indices = column()
    attributes = mesh_attributes(builder, positions, normals)
    attributes["JOINTS_0"], attributes["WEIGHTS_0"] = skin_data(builder, [1.0, 0.75, 0.5, 0.25, 0.0])
    primitive = {"attributes": attributes, "indices": builder.indices(indices), "material": 0, "mode": 4}
    mesh = {"primitives": [primitive], "name": "arm"}
    if with_morph:
        widen = [(x * 2.0, 0.0, z * 2.0) for x, _, z in positions]
        primitive["targets"] = [{"POSITION": builder.floats(widen, "VEC3", bounds=bounds_of(widen))}]
        mesh["weights"] = [0.0]
    identity = [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1]
    shifted = [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, -1, 0, 1]
    skin = {"joints": [1, 2], "inverseBindMatrices": builder.floats([identity, shifted], "MAT4", target=None), "name": "arm skin"}
    half = math.sqrt(0.5)
    times = builder.floats([0.0, 1.0, 2.0], "SCALAR", target=None, bounds=([0.0], [2.0]))
    rotations = builder.floats([(0, 0, 0, 1), (0, 0, half, half), (0, 0, 0, 1)], "VEC4", target=None)
    samplers = [{"input": times, "output": rotations, "interpolation": "LINEAR"}]
    channels = [{"sampler": 0, "target": {"node": 2, "path": "rotation"}}]
    if with_morph:
        morph_values = builder.floats([0.0, 1.0, 0.0], "SCALAR", target=None)
        samplers.append({"input": times, "output": morph_values, "interpolation": "LINEAR"})
        channels.append({"sampler": 1, "target": {"node": 0, "path": "weights"}})
    doc = {
        "asset": {"version": "2.0", "generator": "NyrexVJ scripts/make_test_models.py"},
        "scene": 0,
        "scenes": [{"nodes": [0, 1]}],
        "nodes": [
            {"name": "skinned arm", "mesh": 0, "skin": 0, "translation": [5.0, 0.0, 0.0]},
            {"name": "shoulder", "children": [2]},
            {"name": "elbow", "translation": [0.0, 1.0, 0.0]},
        ],
        "meshes": [mesh],
        "skins": [skin],
        "materials": [material((0.8, 0.3, 0.2, 1.0))],
        "animations": [{"name": "bend", "samplers": samplers, "channels": channels}],
    }
    return builder.finish(doc), builder.bin


def morph_cube(sparse):
    builder = Builder()
    positions, normals, indices = cube()
    attributes = mesh_attributes(builder, positions, normals)
    top = [i for i, p in enumerate(positions) if p[1] > 0.0]
    lift = [(0.0, 1.0, 0.0) if i in top else (0.0, 0.0, 0.0) for i in range(len(positions))]
    puff = [(n[0] * 0.25, n[1] * 0.25, n[2] * 0.25) for n in normals]
    puff_normals = [(0.3, 0.0, 0.0)] * len(positions)
    if sparse:
        index_view = builder.view(struct.pack("<%dH" % len(top), *top))
        value_view = builder.view(struct.pack("<%df" % (3 * len(top)), *[c for _ in top for c in (0.0, 1.0, 0.0)]))
        builder.accessors.append(
            {
                "componentType": 5126,
                "count": len(positions),
                "type": "VEC3",
                "min": [0.0, 0.0, 0.0],
                "max": [0.0, 1.0, 0.0],
                "sparse": {"count": len(top), "indices": {"bufferView": index_view, "componentType": 5123}, "values": {"bufferView": value_view}},
            }
        )
        lift_accessor = len(builder.accessors) - 1
    else:
        lift_accessor = builder.floats(lift, "VEC3", bounds=bounds_of(lift))
    targets = [
        {"POSITION": lift_accessor},
        {"POSITION": builder.floats(puff, "VEC3", bounds=bounds_of(puff)), "NORMAL": builder.floats(puff_normals, "VEC3")},
    ]
    primitive = {"attributes": attributes, "indices": builder.indices(indices), "material": 0, "mode": 4, "targets": targets}
    times = builder.floats([0.0, 1.0, 2.0], "SCALAR", target=None, bounds=([0.0], [2.0]))
    values = builder.floats([0.0, 0.0, 1.0, 0.5, 0.0, 0.0], "SCALAR", target=None)
    doc = {
        "asset": {"version": "2.0", "generator": "NyrexVJ scripts/make_test_models.py"},
        "scene": 0,
        "scenes": [{"nodes": [0]}],
        "nodes": [{"name": "cube", "mesh": 0}],
        "meshes": [{"primitives": [primitive], "weights": [0.25, 0.0], "name": "cube", "extras": {"targetNames": ["lift", "puff"]}}],
        "materials": [material((0.2, 0.6, 0.9, 1.0))],
        "animations": [{"name": "pulse", "samplers": [{"input": times, "output": values, "interpolation": "LINEAR"}], "channels": [{"sampler": 0, "target": {"node": 0, "path": "weights"}}]}],
    }
    return builder.finish(doc), builder.bin


def morph_many():
    builder = Builder()
    positions, normals, indices = cube()
    attributes = mesh_attributes(builder, positions, normals)
    targets = []
    for index in range(12):
        height = 0.1 if index < 8 else 4.0
        deltas = [(0.0, height, 0.0) if p[1] > 0.0 else (0.0, 0.0, 0.0) for p in positions]
        targets.append({"POSITION": builder.floats(deltas, "VEC3", bounds=bounds_of(deltas))})
    primitive = {"attributes": attributes, "indices": builder.indices(indices), "material": 0, "mode": 4, "targets": targets}
    weights = [1.0] * 8 + [0.05] * 4
    doc = {
        "asset": {"version": "2.0", "generator": "NyrexVJ scripts/make_test_models.py"},
        "scene": 0,
        "scenes": [{"nodes": [0]}],
        "nodes": [{"name": "cube", "mesh": 0}],
        "meshes": [{"primitives": [primitive], "weights": weights, "name": "many"}],
        "materials": [material((0.9, 0.7, 0.2, 1.0))],
    }
    return builder.finish(doc), builder.bin


def blend_planes(near_first):
    builder = Builder()
    specs = [
        (0.5, (1.0, 0.1, 0.1, 0.5), "BLEND"),
        (0.0, (0.1, 1.0, 0.1, 0.5), "BLEND"),
        (-0.5, (0.1, 0.1, 1.0, 0.5), "BLEND"),
    ]
    if not near_first:
        specs.reverse()
    nodes = []
    meshes = []
    materials = []
    for index, (z, color, mode) in enumerate(specs):
        positions, normals, indices = quad(0.5, z)
        attributes = mesh_attributes(builder, positions, normals)
        meshes.append({"primitives": [{"attributes": attributes, "indices": builder.indices(indices), "material": index}]})
        materials.append(material(color, mode, double_sided=(index == 1)))
        nodes.append({"mesh": index, "name": "plane %d" % index})
    positions, normals, indices = quad(1.0, -1.0)
    attributes = mesh_attributes(builder, positions, normals)
    meshes.append({"primitives": [{"attributes": attributes, "indices": builder.indices(indices), "material": len(materials)}]})
    materials.append(material((0.9, 0.9, 0.9, 1.0)))
    nodes.append({"mesh": len(meshes) - 1, "name": "wall"})
    doc = {
        "asset": {"version": "2.0", "generator": "NyrexVJ scripts/make_test_models.py"},
        "scene": 0,
        "scenes": [{"nodes": list(range(len(nodes)))}],
        "nodes": nodes,
        "meshes": meshes,
        "materials": materials,
    }
    return builder.finish(doc), builder.bin


def lights_and_cameras(required):
    builder = Builder()
    positions = [(0.0, 0.0, 0.0), (1.0, 0.0, 0.0), (0.0, 1.0, 0.0)]
    normals = [(0.0, 0.0, 1.0)] * 3
    attributes = mesh_attributes(builder, positions, normals)
    primitive = {"attributes": attributes, "indices": builder.indices([0, 1, 2]), "material": 0}
    half = math.sqrt(0.5)
    doc = {
        "asset": {"version": "2.0", "generator": "NyrexVJ scripts/make_test_models.py"},
        "extensionsUsed": ["KHR_lights_punctual"],
        "scene": 0,
        "scenes": [{"nodes": [0, 1, 2, 3, 4, 5]}],
        "nodes": [
            {"name": "triangle", "mesh": 0},
            {"name": "sun", "rotation": [-half, 0.0, 0.0, half], "extensions": {"KHR_lights_punctual": {"light": 0}}},
            {"name": "lamp", "translation": [1.0, 2.0, 3.0], "extensions": {"KHR_lights_punctual": {"light": 1}}},
            {"name": "spot", "translation": [-1.0, 1.0, 0.0], "extensions": {"KHR_lights_punctual": {"light": 2}}},
            {"name": "main camera", "translation": [0.0, 1.0, 5.0], "camera": 0},
            {"name": "top camera", "translation": [0.0, 10.0, 0.0], "rotation": [-half, 0.0, 0.0, half], "camera": 1},
        ],
        "meshes": [{"primitives": [primitive]}],
        "materials": [material((0.7, 0.7, 0.7, 1.0))],
        "cameras": [
            {"type": "perspective", "name": "main", "perspective": {"yfov": 0.6, "znear": 0.1, "zfar": 50.0, "aspectRatio": 1.7778}},
            {"type": "orthographic", "name": "top", "orthographic": {"xmag": 5.0, "ymag": 3.0, "znear": 0.1, "zfar": 40.0}},
        ],
        "extensions": {
            "KHR_lights_punctual": {
                "lights": [
                    {"type": "directional", "name": "sun", "color": [1.0, 0.95, 0.9], "intensity": 3.0},
                    {"type": "point", "name": "lamp", "color": [1.0, 0.5, 0.25], "intensity": 5.0},
                    {"type": "spot", "name": "spot", "color": [0.2, 0.4, 1.0], "intensity": 2.0, "spot": {"innerConeAngle": 0.1, "outerConeAngle": 0.5}},
                ]
            }
        },
    }
    if required:
        doc["extensionsRequired"] = ["KHR_lights_punctual"]
    return builder.finish(doc), builder.bin


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("output", type=Path)
    arguments = parser.parse_args()
    arguments.output.mkdir(parents=True, exist_ok=True)
    files = {
        "skin_arm.glb": skin_arm(False),
        "skin_morph.glb": skin_arm(True),
        "morph_cube.glb": morph_cube(False),
        "morph_sparse.glb": morph_cube(True),
        "morph_many.glb": morph_many(),
        "blend_near_first.glb": blend_planes(True),
        "blend_far_first.glb": blend_planes(False),
        "lights_cameras.glb": lights_and_cameras(False),
        "lights_required.glb": lights_and_cameras(True),
    }
    for name, (doc, binary) in files.items():
        write_glb(arguments.output / name, doc, binary)


if __name__ == "__main__":
    main()
