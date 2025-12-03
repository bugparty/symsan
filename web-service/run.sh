#!/bin/bash
# 启动 fgtest Web Service

# 设置 fgtest 路径（如果未设置）
if [ -z "$FGTEST_PATH" ]; then
    export FGTEST_PATH="../build/bin/fgtest"
fi

# 检查 fgtest 是否存在
if [ ! -f "$FGTEST_PATH" ]; then
    echo "Warning: fgtest not found at $FGTEST_PATH"
    echo "Please set FGTEST_PATH environment variable to the correct path"
    echo "Example: export FGTEST_PATH=/path/to/fgtest"
    echo ""
fi

# 创建必要的目录
mkdir -p uploads results

# 启动服务
echo "Starting fgtest Web Service..."
echo "API Documentation: http://localhost:8000/docs"
echo "Press Ctrl+C to stop"
echo ""

uvicorn app:app --reload --host 0.0.0.0 --port 8000

