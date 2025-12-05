#!/usr/bin/env bash
set -euo pipefail

symbol="symsan_target_hit"
file="./dummy"

if ! nm -gU "$file" | grep -q "$symbol"; then
    echo "symbol not found"
    exit 1
fi
mkdir -p out
export TAINT_OPTIONS="output_dir=out:taint_file=stdin:debug=1"
../build/bin/fgtest ./dummy seed.bin dummy_ctwm_index.json dummy_traces.json rewards.json