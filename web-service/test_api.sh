#!/usr/bin/env bash
# 测试 fgtest Web Service API

set -euo pipefail

API_URL="${API_URL:-http://localhost:11000}"

echo "=== Testing fgtest Web Service API ==="
echo ""

# 测试健康检查
echo "1. Health check:"
curl -s "$API_URL/health" | jq .
echo ""

# 测试根路径
echo "2. Root endpoint:"
curl -s "$API_URL/" | jq .
echo ""

# 测试提交任务 - 使用所有默认值 (默认 seed 和默认 branch_meta)
echo "3. Submit task with all defaults (seed=0402, branch_meta=bin/ctwm_index.json, program=dummy):"
TASK_ID=$(curl -s -X POST "$API_URL/api/submit" \
  -F "program=dummy" \
  -F "traces=@../examples/dummy_traces.json" \
  | jq -r '.task_id')

echo "Task ID: $TASK_ID"
echo ""

# 等待一下
sleep 2

# 查询任务状态
echo "4. Query task status:"
curl -s "$API_URL/api/status/$TASK_ID" | jq .
echo ""

# 测试提交任务 - 使用自定义 seed 字符串，默认 branch_meta
echo "5. Submit task with custom seed string (\"0x1a1d\"), default branch_meta (program=dummy):"
TASK_ID2=$(curl -s -X POST "$API_URL/api/submit" \
  -F "program=dummy" \
  -F "seed=0x1a1d" \
  -F "traces=@../examples/dummy_traces.json" \
  | jq -r '.task_id')

echo "Task ID: $TASK_ID2"
echo ""

# 测试提交任务 - 使用普通字符串 seed 和上传的 branch_meta
echo "6. Submit task with string seed (hello) and uploaded branch_meta (program=dummy):"
TASK_ID3=$(curl -s -X POST "$API_URL/api/submit" \
  -F "program=dummy" \
  -F "seed=hello" \
  -F "branch_meta=@../examples/ctwm_index.json" \
  -F "traces=@../examples/dummy_traces.json" \
  | jq -r '.task_id')

echo "Task ID: $TASK_ID3"
echo ""

# 测试提交任务 - 使用 control_temp 程序
echo "7. Submit task with control_temp program (seed=\"1 35 2 0\"):"
TASK_ID4=$(curl -s -X POST "$API_URL/api/submit" \
  -F "program=control_temp" \
  -F "seed=1 35 2 0" \
  -F "traces=@../examples/control_temp_traces.json" \
  | jq -r '.task_id')

echo "Task ID: $TASK_ID4"
echo ""

# 等待一下
sleep 2

# 查询 control_temp 任务状态
echo "8. Query control_temp task status:"
curl -s "$API_URL/api/status/$TASK_ID4" | jq .
echo ""

echo "=== All API tests completed ==="
echo "Use 'curl $API_URL/api/status/{TASK_ID}' to check task status"
echo "Use 'curl $API_URL/api/download/{TASK_ID}' to download results"
