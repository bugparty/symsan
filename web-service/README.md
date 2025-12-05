# fgtest Web Service

极简版 FastAPI Web Service，用于将 `fgtest` 命令行工具包装成 REST API。

## 功能特性

- ✅ 文件上传（multipart/form-data）
- ✅ 异步任务执行（后台线程）
- ✅ 任务状态查询
- ✅ 结果文件下载
- ✅ 自动生成 API 文档

## 快速开始

### 1. 安装依赖

```bash
cd web-service
pip install -r requirements.txt
```

### 2. 配置 fgtest 路径

默认路径是 `../build/bin/fgtest`，可以通过环境变量设置：

```bash
export FGTEST_PATH=/path/to/fgtest
```

### 3. 启动服务

开发模式：
```bash
uvicorn app:app --reload --host 0.0.0.0 --port 8000
```

生产模式：
```bash
uvicorn app:app --host 0.0.0.0 --port 8000 --workers 4
```

### 4. 访问 API 文档

打开浏览器访问：http://localhost:8000/docs

## API 端点

### POST `/api/submit`

提交一个新的 fgtest 任务。

**参数（multipart/form-data）：**
- `program` (必需): 选择程序，可选值 `dummy` 或 `xor`
- `seed` (可选): 写入目标 stdin 的种子字符串（原样写入，不再解析为 hex 或文件路径），默认为 `"0402"`
- `branch_meta` (可选): 分支元数据 JSON 文件，默认使用 `bin/ctwm_index.json`
- `traces` (必需): 轨迹 JSON 文件
- `options` (可选): JSON 字符串配置

**示例 curl 命令**:
```bash
# 使用所有默认值（seed=0402, branch_meta=bin/ctwm_index.json）
curl -X POST http://localhost:8000/api/submit \
  -F "program=xor" \
  -F "traces=@examples/xor_traces.json"

# 使用自定义 seed（按字符串写入 stdin），默认 branch_meta
curl -X POST http://localhost:8000/api/submit \
  -F "program=xor" \
  -F "seed=0x1a1d" \
  -F "traces=@examples/xor_traces.json"

# 使用自定义 seed 和上传的 branch_meta
curl -X POST http://localhost:8000/api/submit \
  -F "program=xor" \
  -F "seed=0x1a1d" \
  -F "branch_meta=@examples/ctwm_index.json" \
  -F "traces=@examples/xor_traces.json"

# 使用字符串 seed，默认 branch_meta
curl -X POST http://localhost:8000/api/submit \
  -F "program=dummy" \
  -F "seed=hello" \
  -F "traces=@examples/dummy_traces.json"
```

**响应示例**:
```json
{
  "task_id": "abc12345",
  "status": "pending",
  "seed": "0x1a1d",
  "program": "xor",
  "branch_meta_source": "default",
  "message": "Task submitted successfully"
}
```

### GET `/api/status/{task_id}`

查询任务状态。

**响应示例**:
```json
{
  "task_id": "abc12345",
  "status": "completed",
  "created_at": "2024-01-01T12:00:00",
  "updated_at": "2024-01-01T12:05:00",
  "completed_at": "2024-01-01T12:05:00",
  "error": null,
  "result": {
    "rewards": [...]
  }
}
```

状态值：
- `pending`: 任务已提交，等待执行
- `running`: 任务正在执行
- `completed`: 任务完成
- `failed`: 任务失败

### GET `/api/download/{task_id}`

下载任务结果文件 (rewards.json)。

## 目录结构

```
web-service/
├── app.py                 # FastAPI 主应用
├── fgtest_wrapper.py      # fgtest 包装器
├── requirements.txt       # Python 依赖
├── README.md             # 本文件
├── uploads/              # 上传文件存储（自动创建）
│   └── {task_id}/
└── results/              # 结果文件存储（自动创建）
    └── {task_id}/
        ├── status.json   # 任务状态
        ├── rewards.json  # 奖励结果
        ├── execution.log # 执行日志
        └── output/       # 输出文件（如果指定了 output_dir）
```

## 使用示例

### Python 客户端示例

```python
import requests

# 提交任务
files = {
    'target': open('target_program', 'rb'),
    'input_file': open('input.bin', 'rb'),
    'branch_meta': open('branch_meta.json', 'rb'),
    'traces': open('traces.json', 'rb'),
}
data = {
    'options': '{"debug": true}'
}

response = requests.post('http://localhost:8000/api/submit', files=files, data=data)
task_id = response.json()['task_id']
print(f"Task ID: {task_id}")

# 查询状态
status = requests.get(f'http://localhost:8000/api/status/{task_id}').json()
print(f"Status: {status['status']}")

# 下载结果
if status['status'] == 'completed':
    result = requests.get(f'http://localhost:8000/api/download/{task_id}')
    with open('rewards.json', 'wb') as f:
        f.write(result.content)
```

### curl 示例

```bash
# 提交任务
curl -X POST "http://localhost:8000/api/submit" \
  -F "target=@target_program" \
  -F "input_file=@input.bin" \
  -F "branch_meta=@branch_meta.json" \
  -F "traces=@traces.json" \
  -F 'options={"debug": true}'

# 查询状态
curl "http://localhost:8000/api/status/{task_id}"

# 下载结果
curl "http://localhost:8000/api/download/{task_id}" -o rewards.json
```

## 注意事项

⚠️ **这是极简版本，不考虑以下特性**：
- 安全性（无认证、无授权）
- 安全隔离（无 Docker/沙箱）
- 分布式（单机运行）
- 任务取消机制
- 并发控制
- 文件清理策略

## 故障排查

### fgtest 路径错误

如果遇到 "fgtest not found" 错误，请检查：
1. 确保 fgtest 已编译：`ls ../build/bin/fgtest`
2. 设置环境变量：`export FGTEST_PATH=/absolute/path/to/fgtest`

### 权限问题

确保目标程序文件有执行权限：
```bash
chmod +x uploads/{task_id}/target
```

### 任务执行失败

查看执行日志：
```bash
cat results/{task_id}/execution.log
```

## 许可证

与主项目相同。
