# fgtest Web Service - 快速参考

## 最简单的用法

只需提供程序名和 traces 文件，其他参数都使用默认值：

```bash
curl -X POST http://localhost:8000/api/submit \
  -F "program=xor" \
  -F "traces=@xor_traces.json"
```

**默认值**：
- `seed`: `0402`
- `branch_meta`: `bin/ctwm_index.json`

## API 参数说明

### POST /api/submit

| 参数 | 类型 | 必需 | 默认值 | 说明 |
|------|------|------|--------|------|
| `program` | string | ✅ | - | 程序名称：`dummy` 或 `xor` |
| `seed` | string | ❌ | `0402` | 写入目标 stdin 的字符串（原样写入） |
| `branch_meta` | file | ❌ | `bin/ctwm_index.json` | 分支元数据 JSON 文件 |
| `traces` | file | ✅ | - | 轨迹 JSON 文件 |
| `options` | string | ❌ | `null` | JSON 配置字符串 |

## 使用场景

### 场景1：快速测试（使用所有默认值）
```bash
curl -X POST http://localhost:8000/api/submit \
  -F "program=xor" \
  -F "traces=@traces.json"
```

### 场景2：自定义 seed（按字符串写入 stdin）
```bash
curl -X POST http://localhost:8000/api/submit \
  -F "program=xor" \
  -F "seed=0x1a1d" \
  -F "traces=@traces.json"
```

### 场景3：使用字符串 seed
```bash
curl -X POST http://localhost:8000/api/submit \
  -F "program=dummy" \
  -F "seed=hello123" \
  -F "traces=@traces.json"
```

### 场景4：完全自定义
```bash
curl -X POST http://localhost:8000/api/submit \
  -F "program=xor" \
  -F "seed=0xabcd" \
  -F "branch_meta=@custom_meta.json" \
  -F "traces=@traces.json" \
  -F 'options={"debug":true}'
```

## Python 示例

```python
import requests

# 最简单的方式
response = requests.post(
    "http://localhost:8000/api/submit",
    files={'traces': open('traces.json', 'rb')},
    data={'program': 'xor'}
)
result = response.json()
task_id = result['task_id']

# 查询状态
status = requests.get(f"http://localhost:8000/api/status/{task_id}").json()
print(status['status'])  # pending, running, completed, failed

# 下载结果
if status['status'] == 'completed':
    result_file = requests.get(f"http://localhost:8000/api/download/{task_id}")
    with open('result.json', 'wb') as f:
        f.write(result_file.content)
```

## 响应格式

### 提交成功
```json
{
  "task_id": "a1b2c3d4",
  "status": "pending",
  "seed": "0402",
  "program": "xor",
  "branch_meta_source": "default",
  "message": "Task submitted successfully"
}
```

### 任务状态
```json
{
  "task_id": "a1b2c3d4",
  "status": "completed",
  "seed": "0402",
  "program": "xor",
  "branch_meta_source": "default",
  "created_at": "2025-12-03T01:30:00",
  "updated_at": "2025-12-03T01:32:15",
  "completed_at": "2025-12-03T01:32:15",
  "error": null,
  "result": {
    "rewards": [...]
  }
}
```

## 错误处理

### 程序不存在
```json
{
  "detail": "Program must be 'dummy' or 'xor'"
}
```

### 任务不存在
```json
{
  "detail": "Task not found"
}
```

### 默认 branch_meta 不存在
```json
{
  "detail": "Default branch metadata not found: bin/ctwm_index.json"
}
```

## 准备工作

在启动服务前，确保 `bin/` 目录下有：
1. 程序二进制文件：`bin/dummy`, `bin/xor`
2. 默认元数据：`bin/ctwm_index.json`

```bash
# 构建程序并复制到 bin 目录
cd web-service
mkdir -p bin
cp ../examples/dummy bin/
cp ../examples/xor bin/
cp ../examples/ctwm_index.json bin/

# 启动服务
uvicorn app:app --reload --host 0.0.0.0 --port 8000
```

## 开发建议

1. **生产环境**：建议使用容器化部署，确保 bin 目录正确挂载
2. **性能优化**：对于高并发场景，考虑使用消息队列替代线程
3. **安全性**：添加认证和速率限制
4. **监控**：记录任务执行时间和成功率
