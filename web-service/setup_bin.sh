#!/bin/bash
# Setup script for fgtest Web Service
# 准备 bin 目录，包含程序二进制文件和默认的 ctwm_index.json

set -e

echo "=== Setting up bin directory for fgtest Web Service ==="
echo ""

# Create bin directory if it doesn't exist
mkdir -p bin

# Copy build/bin to current directory's bin
echo "1. Copying fgtest and other tools from ../build/bin/..."
cp -r ../build/bin/* bin/

# Execute example build scripts
echo ""
echo "2. Building example programs..."
cd ../examples

echo "   - Building dummy..."
./dummy_build.sh

echo "   - Building xor..."
./xor_build.sh

# Copy built examples to bin directory
echo ""
echo "3. Copying example programs and metadata to bin/..."
cp dummy xor ctwm_index.json ../web-service/bin/

cd ../web-service

echo ""
echo "=== Setup Complete! ==="
echo ""
echo "Files in bin/:"
ls -lh bin/ | grep -E "(dummy|xor|ctwm_index|fgtest)" || ls -lh bin/
echo ""
echo "You can now start the service with:"
echo "  uvicorn app:app --reload --host 0.0.0.0 --port 8000"
