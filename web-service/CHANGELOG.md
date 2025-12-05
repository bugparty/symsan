# fgtest 更新说明

## 更新内容

### 1. fgtest 支持字符串 seed 输入

**文件**: `driver/fgtest.cpp`

现在 `fgtest` 的 `input` 参数支持三种格式：

#### 格式1：十六进制字符串
```bash
# 带 0x 前缀
./fgtest ./xor 0x1a1d ctwm_index.json traces.json rewards.json

# 不带前缀（偶数位十六进制数字）
./fgtest ./xor 1a1d ctwm_index.json traces.json rewards.json
```

#### 格式2：普通字符串
```bash
./fgtest ./xor "hello" ctwm_index.json traces.json rewards.json
./fgtest ./xor "test123" ctwm_index.json traces.json rewards.json
```

#### 格式3：文件路径（原有功能）
```bash
./fgtest ./xor seed.bin ctwm_index.json traces.json rewards.json
```

**自动识别规则**：
1. 如果以 `0x` 开头 → 解析为十六进制
2. 如果全是十六进制字符且偶数位 → 解析为十六进制
3. 如果能成功打开为文件 → 从文件读取
4. 否则 → 当作普通字符串使用

**示例输出**：
```
Loaded hex seed: 0x1a1d (2 bytes)
Loaded hex seed: 3132 (2 bytes)
Loaded string seed: hello (5 bytes)
Loaded seed from file: seed.bin (2 bytes)
```

### 2. Web Service API 更新

**文件**: `web-service/app.py`, `web-service/fgtest_wrapper.py`

#### API 变更

**POST /api/submit** 参数更新：
- ❌ 移除 `input_file` 文件上传参数
- ✅ 新增 `seed` 字符串参数（可选，默认 `0x0402`）
- ✅ `branch_meta` 改为可选（默认使用 `bin/ctwm_index.json`）

**新接口定义**：
```
POST /api/submit
  - program (必需): "dummy" 或 "xor"
  - seed (可选): 种子字符串，默认 "0x0402"
  - branch_meta (可选): 分支元数据 JSON 文件，默认 "bin/ctwm_index.json"
  - traces (必需): 轨迹 JSON 文件
  - options (可选): JSON 配置字符串
```

#### 使用示例

**curl 命令**：
```bash
# 最简单：使用所有默认值
curl -X POST http://localhost:8000/api/submit \
  -F "program=xor" \
  -F "traces=@xor_traces.json"

# 自定义 seed，使用默认 branch_meta
curl -X POST http://localhost:8000/api/submit \
  -F "program=xor" \
  -F "seed=0x1a1d" \
  -F "traces=@xor_traces.json"

# 同时自定义 seed 和 branch_meta
curl -X POST http://localhost:8000/api/submit \
  -F "program=xor" \
  -F "seed=0x1a1d" \
  -F "branch_meta=@custom_meta.json" \
  -F "traces=@xor_traces.json"
```

**Python 示例**：
```python
import requests

response = requests.post(
    "http://localhost:8000/api/submit",
    files={
        'branch_meta': open('ctwm_index.json', 'rb'),
        'traces': open('xor_traces.json', 'rb')
    },
    data={
        'program': 'xor',
        'seed': '0x1a1d'
    }
)

result = response.json()
task_id = result['task_id']
```

## 测试脚本

### 1. 测试 fgtest 命令行
```bash
cd examples
./test_seed_formats.sh
```

### 2. 测试 Web Service API
```bash
cd web-service
# 启动服务
uvicorn app:app --reload

# 在另一个终端运行测试
./test_api.sh
```

### 3. Python 客户端示例
```bash
cd web-service
python3 example_client.py
```

## 升级指南

### 现有代码迁移

如果你之前使用文件上传方式：

**之前**：
```python
files = {
    'target': open('program', 'rb'),
    'input_file': open('seed.bin', 'rb'),
    'branch_meta': open('meta.json', 'rb'),
    'traces': open('traces.json', 'rb')
}
response = requests.post(url, files=files)
```

**现在（最简单）**：
```python
files = {
    'traces': open('traces.json', 'rb')
}
data = {
    'program': 'xor'  # 其他参数都用默认值
}
response = requests.post(url, files=files, data=data)
```

**现在（自定义参数）**：
```python
files = {
    'traces': open('traces.json', 'rb')
    # branch_meta 可选，不传就用默认的 bin/ctwm_index.json
}
data = {
    'program': 'xor',
    'seed': '0x1a1d'  # 可选，默认 0x0402
}
response = requests.post(url, files=files, data=data)
```

## 优势

1. **更简单**：不需要创建临时 seed 文件
2. **更灵活**：支持十六进制和字符串格式
3. **更高效**：减少文件 I/O 操作
4. **更清晰**：seed 值在 API 响应中直接可见
5. **更便捷**：branch_meta 可选，大多数情况下使用默认值即可
6. **更快速**：最少只需上传一个 traces 文件即可提交任务

## 兼容性

- ✅ 保持向后兼容：文件路径仍然支持
- ✅ 现有测试脚本仍可正常工作
- ✅ 默认值 `0x0402` 确保无 seed 参数时也能运行
