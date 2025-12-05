#!/usr/bin/env bash
# Test script demonstrating different seed input formats for fgtest

set -euo pipefail

echo "=== Testing fgtest seed input formats ==="
echo ""

export TAINT_OPTIONS="output_dir=out:taint_file=stdin:debug=0"

echo "1. Hex string with 0x prefix (0x1a1d):"
../build/bin/fgtest ./xor 0x1a1d ctwm_index.json xor_traces.json /tmp/test1.json 2>&1 | grep "Loaded"
echo ""

echo "2. Hex string without prefix (3132):"
../build/bin/fgtest ./xor 3132 ctwm_index.json xor_traces.json /tmp/test2.json 2>&1 | grep "Loaded"
echo ""

echo "3. Plain string (hello):"
../build/bin/fgtest ./xor "hello" ctwm_index.json xor_traces.json /tmp/test3.json 2>&1 | grep "Loaded"
echo ""

echo "4. File path (seed.bin):"
../build/bin/fgtest ./xor seed.bin ctwm_index.json xor_traces.json /tmp/test4.json 2>&1 | grep "Loaded"
echo ""

echo "5. Complex hex (0xdeadbeef):"
../build/bin/fgtest ./xor 0xdeadbeef ctwm_index.json xor_traces.json /tmp/test5.json 2>&1 | grep "Loaded"
echo ""

echo "All tests passed!"
