#!/usr/bin/env bash
set -euo pipefail

symbol="symsan_target_hit"
file="./control_temp"

if ! nm -gU "$file" | grep -q "$symbol"; then
    echo "symbol not found"
    exit 1
fi
mkdir -p out
export TAINT_OPTIONS="output_dir=out:taint_file=stdin:debug=1"
../build/bin/fgtest ./control_temp "1 1 11 1" ctwm_index.json control_temp_traces.json rewards.json