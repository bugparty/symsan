#!/usr/bin/env bash
set -euo pipefail

symbol="symsan_target_hit"
file="./xor"

if ! nm -gU "$file" | grep -q "$symbol"; then
    echo "symbol not found"
    exit 1
fi

export TAINT_OPTIONS="output_dir=out:taint_file=stdin:debug=1"
../build/bin/fgtest ./xor seed.bin ctwm_index.json xor_traces.json rewards.json