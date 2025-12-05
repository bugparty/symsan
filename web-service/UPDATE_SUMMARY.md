# Web Service 更新总结

## 主要改动

### 1. seed 参数改为字符串输入
- **之前**：需要上传 seed 文件
- **现在**：直接传递字符串参数，默认值 `0x0402`
- **支持格式**：
  - 十六进制：`0x1a1d`, `1a1d`
  - 普通字符串：`hello`, `test123`
  - 文件路径（兼容旧方式）

### 2. branch_meta 改为可选参数
- **之前**：必须上传 branch_meta 文件
- **现在**：可选，默认使用 `bin/ctwm_index.json`
- **好处**：大多数情况下使用默认配置，减少上传文件数量

## 新的 API 接口

```
POST /api/submit
  ✅ program (必需)      - 程序名称: "dummy" | "xor"
  ⭕ seed (可选)         - 种子字符串，默认 "0x0402"
  ⭕ branch_meta (可选)  - 元数据文件，默认使用 bin/ctwm_index.json
  ✅ traces (必需)       - 轨迹 JSON 文件
  ⭕ options (可选)      - JSON 配置字符串
```

## 使用对比

### 最简单的调用方式

**之前（需要3个文件）**：
```bash
curl -X POST http://localhost:8000/api/submit \
  -F "program=xor" \
  -F "input_file=@seed.bin" \
  -F "branch_meta=@ctwm_index.json" \
  -F "traces=@traces.json"
```

**现在（只需1个文件）**：
```bash
curl -X POST http://localhost:8000/api/submit \
  -F "program=xor" \
  -F "traces=@traces.json"
```

## 文件结构

```
web-service/
├── app.py                    # FastAPI 应用（已更新）
├── fgtest_wrapper.py         # 任务包装器（已更新）
├── example_client.py         # Python 客户端示例（已更新）
├── test_api.sh              # API 测试脚本（已更新）
├── setup_bin.sh             # 环境准备脚本（已更新）
├── README.md                # 主文档（已更新）
├── CHANGELOG.md             # 变更日志（新增）
├── QUICKSTART.md            # 快速参考（新增）
└── bin/                     # 运行时文件目录
    ├── fgtest               # fgtest 可执行文件
    ├── dummy                # dummy 程序
    ├── xor                  # xor 程序
    └── ctwm_index.json      # 默认元数据（重要！）
```

## 环境准备

### 方法1：使用 setup 脚本（推荐）
```bash
cd web-service
./setup_bin.sh
```

### 方法2：手动准备
```bash
cd web-service
mkdir -p bin

# 构建示例程序
cd ../examples
./dummy_build.sh
./xor_build.sh

# 复制文件
cp dummy xor ctwm_index.json ../web-service/bin/
cp ../build/bin/fgtest ../web-service/bin/
```

## 启动服务

```bash
cd web-service
uvicorn app:app --reload --host 0.0.0.0 --port 8000
```

访问文档：http://localhost:8000/docs

## 测试

### 测试 fgtest 命令行
```bash
cd examples
./test_seed_formats.sh
```

### 测试 Web Service API
```bash
cd web-service
./test_api.sh
```

### Python 客户端测试
```bash
cd web-service
python3 example_client.py
```

## 响应示例

### 提交任务（使用默认值）
```bash
curl -X POST http://localhost:8000/api/submit \
  -F "program=xor" \
  -F "traces=@traces.json"
```

响应：
```json
{
  "task_id": "a1b2c3d4",
  "status": "pending",
  "seed": "0x0402",
  "program": "xor",
  "branch_meta_source": "default",
  "message": "Task submitted successfully"
}
```

### 提交任务（自定义参数）
```bash
curl -X POST http://localhost:8000/api/submit \
  -F "program=xor" \
  -F "seed=0x1a1d" \
  -F "branch_meta=@custom_meta.json" \
  -F "traces=@traces.json"
```

响应：
```json
{
  "task_id": "b2c3d4e5",
  "status": "pending",
  "seed": "0x1a1d",
  "program": "xor",
  "branch_meta_source": "uploaded",
  "message": "Task submitted successfully"
}
```

## 兼容性说明

✅ **向后兼容**
- fgtest 命令行工具仍支持文件路径作为 seed
- 现有的测试脚本无需修改即可运行
- 可以选择性地上传 branch_meta 文件

✅ **渐进式迁移**
- 可以逐步从文件上传方式迁移到字符串参数
- 两种方式可以共存

## 优势总结

1. ✨ **更简单**：最少只需上传1个文件（traces）
2. 🚀 **更快速**：减少文件上传时间和服务器 I/O
3. 🎯 **更灵活**：seed 支持多种格式
4. 📦 **更清晰**：参数值直接在响应中可见
5. 🔧 **更易用**：大多数场景使用默认配置即可

## 注意事项

⚠️ **重要**：确保 `bin/ctwm_index.json` 文件存在且正确
- 这是默认的 branch metadata 文件
- 如果文件不存在，使用默认值会失败
- 运行 `./setup_bin.sh` 会自动创建

⚠️ **程序文件**：确保 `bin/dummy` 和 `bin/xor` 存在
- 这两个是示例程序
- 需要先编译才能使用
- 运行 `./setup_bin.sh` 会自动构建和复制
