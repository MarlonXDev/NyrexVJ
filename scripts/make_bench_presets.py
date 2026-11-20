import json
import os
import sys


def effect(name, params=None, enabled=True):
    entry = {"effect": name, "enabled": enabled}
    if params is not None:
        entry["params"] = params
    return entry


def layer(clip, opacity=1.0, blend="Normal", effects=None):
    return {
        "cells": [clip] + [None] * 7,
        "active": 0,
        "opacity": opacity,
        "blend": blend,
        "fx": effects or [],
    }


def preset(name, layers, master_effects=None, resolution=(960, 540)):
    padded = list(layers)
    while len(padded) < 4:
        padded.append({"cells": [None] * 8, "active": -1})
    return {
        "format": 2,
        "app": "NyrexVJ",
        "name": name,
        "bpm": 128.0,
        "beatsPerBar": 4,
        "resolution": list(resolution),
        "master": {"level": 1.0, "fx": master_effects or []},
        "layers": padded,
    }


CLIPS = ["Plasma", "Tunnel", "Metaballs", "Kaleido"]
BLENDS = ["Normal", "Add", "Screen", "Add"]


def four_layers(opacities, effects_per_layer):
    layers = []
    for index in range(4):
        layers.append(layer(CLIPS[index], opacities[index], BLENDS[index], effects_per_layer(index)))
    return layers


def glow_blur(index):
    return [effect("Glow"), effect("Blur")]


def trails_glow(index):
    return [effect("Trails"), effect("Glow")]


def build():
    presets = []
    presets.append(preset("Bench Four Visible", four_layers([1.0, 1.0, 1.0, 1.0], glow_blur)))
    presets.append(preset("Bench Three Hidden", four_layers([1.0, 0.0, 0.0, 0.0], glow_blur)))
    presets.append(preset("Bench Hidden Feedback", four_layers([1.0, 0.0, 0.0, 0.0], trails_glow)))
    presets.append(
        preset(
            "Bench Heavy Chain",
            [
                layer(
                    "Plasma",
                    1.0,
                    "Normal",
                    [effect("Star Burst"), effect("Light Rays"), effect("Glow"), effect("Anamorphic Streak")],
                )
            ],
            master_effects=[effect("Radial Blur")],
        )
    )
    presets.append(
        preset(
            "Bench Fade Mix",
            four_layers([1.0, 0.5, 0.25, 0.0], glow_blur),
        )
    )
    return presets


def main():
    if len(sys.argv) < 2:
        print("usage: make_bench_presets.py <presets directory> [effect names...]")
        return 1
    target = sys.argv[1]
    os.makedirs(target, exist_ok=True)
    presets = build()
    for name in sys.argv[2:]:
        presets.append(preset("Bench Fx " + name, [layer("Plasma", 1.0, "Normal", [effect(name)])]))
    for item in presets:
        path = os.path.join(target, item["name"] + ".json")
        with open(path, "w") as handle:
            json.dump(item, handle, indent=2)
    print("wrote", len(presets), "presets to", target)
    return 0


if __name__ == "__main__":
    sys.exit(main())
