#!/bin/bash
set -euo pipefail
tool="${1:?usage: make_meshopt_models.sh PATH_TO_GLTFPACK [MODEL_DIRECTORY]}"
dir="${2:-tests/data/models}"
for name in skin_arm skin_morph morph_cube; do
    "$tool" -i "$dir/$name.glb" -o "$dir/meshopt_exact_$name.glb" -c -noq -kn -ke
    "$tool" -i "$dir/$name.glb" -o "$dir/meshopt_quant_$name.glb" -c -kn -ke
done
rm -f "$dir/meshopt_fallback_skin_morph.glb" "$dir/meshopt_fallback_skin_morph.fallback.bin"
"$tool" -i "$dir/skin_morph.glb" -o "$dir/meshopt_fallback_skin_morph.glb" -c -cf -noq -kn -ke
