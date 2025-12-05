# 部署检查清单

## 修改完成的文件

### 核心代码
- [x] `driver/fgtest.cpp` - 添加字符串 seed 支持
- [x] `web-service/app.py` - 更新 API，seed 和 branch_meta 改为可选
- [x] `web-service/fgtest_wrapper.py` - 更新任务包装器

### 文档
- [x] `web-service/README.md` - 更新主文档
- [x] `web-service/CHANGELOG.md` - 添加变更日志
- [x] `web-service/QUICKSTART.md` - 添加快速参考
- [x] `web-service/UPDATE_SUMMARY.md` - 添加更新总结

### 示例和测试
- [x] `web-service/example_client.py` - 更新 Python 客户端示例
- [x] `web-service/test_api.sh` - 更新 API 测试脚本
- [x] `web-service/setup_bin.sh` - 更新环境准备脚本
- [x] `examples/test_seed_formats.sh` - 添加 seed 格式测试脚本

## 部署前检查

### 1. 编译 fgtest
```bash
cd /workspaces/symsan/build
CC=clang-14 CXX=clang++-14 cmake --build .
CC=clang-14 CXX=clang++-14 cmake --install .
```

### 2. 准备 Web Service 环境
```bash
cd /workspaces/symsan/web-service
./setup_bin.sh
```

验证文件：
```bash
ls -lh bin/
# 应该看到：
# - fgtest
# - dummy
# - xor  
# - ctwm_index.json
```

### 3. 安装 Python 依赖
```bash
cd /workspaces/symsan/web-service
pip install -r requirements.txt
```

### 4. 启动服务
```bash
uvicorn app:app --reload --host 0.0.0.0 --port 8000
```

### 5. 测试 API
在另一个终端：
```bash
cd /workspaces/symsan/web-service

# 健康检查
curl http://localhost:8000/health

# 测试 API
./test_api.sh
```

## 功能验证

### 测试点 1：使用默认参数
```bash
curl -X POST http://localhost:8000/api/submit \
  -F "program=xor" \
  -F "traces=@../examples/xor_traces.json"
```

预期结果：
- ✓ `seed` 为 `0x0402`
- ✓ `branch_meta_source` 为 `default`
- ✓ 返回 task_id

### 测试点 2：自定义 seed（十六进制）
```bash
curl -X POST http://localhost:8000/api/submit \
  -F "program=xor" \
  -F "seed=0x1a1d" \
  -F "traces=@../examples/xor_traces.json"
```

预期结果：
- ✓ `seed` 为 `0x1a1d`
- ✓ `branch_meta_source` 为 `default`

### 测试点 3：自定义 seed（字符串）
```bash
curl -X POST http://localhost:8000/api/submit \
  -F "program=dummy" \
  -F "seed=hello" \
  -F "traces=@../examples/dummy_traces.json"
```

预期结果：
- ✓ `seed` 为 `hello`
- ✓ `branch_meta_source` 为 `default`

### 测试点 4：上传自定义 branch_meta
```bash
curl -X POST http://localhost:8000/api/submit \
  -F "program=xor" \
  -F "seed=0x1a1d" \
  -F "branch_meta=@../examples/ctwm_index.json" \
  -F "traces=@../examples/xor_traces.json"
```

预期结果：
- ✓ `seed` 为 `0x1a1d`
- ✓ `branch_meta_source` 为 `uploaded`

### 测试点 5：查询任务状态
```bash
TASK_ID="<从上面获取的 task_id>"
curl http://localhost:8000/api/status/$TASK_ID
```

预期结果：
- ✓ 返回任务状态（pending/running/completed/failed）
- ✓ 包含 seed 和 branch_meta_source 信息

## 常见问题

### Q1: "Default branch metadata not found"
**原因**：bin/ctwm_index.json 不存在
**解决**：运行 `./setup_bin.sh`

### Q2: "Program 'xor' not found in bin directory"
**原因**：bin/xor 不存在
**解决**：运行 `./setup_bin.sh` 或手动编译并复制

### Q3: 任务一直停留在 pending
**原因**：fgtest 可能未正确安装或权限问题
**排查**：
```bash
# 检查 fgtest 是否可执行
ls -lh bin/fgtest
./bin/fgtest --help

# 检查日志
cat results/<task_id>/execution.log
```

### Q4: Python 导入错误
**原因**：依赖未安装
**解决**：
```bash
pip install -r requirements.txt
```

## 性能建议

1. **生产部署**：使用 gunicorn + uvicorn workers
   ```bash
   gunicorn app:app -w 4 -k uvicorn.workers.UvicornWorker
   ```

2. **资源限制**：设置任务超时和并发限制
   - 当前超时：3600秒（1小时）
   - 修改：在 fgtest_wrapper.py 中调整 timeout 参数

3. **存储清理**：定期清理旧的任务结果
   ```bash
   find uploads -type d -mtime +7 -exec rm -rf {} \;
   find results -type d -mtime +7 -exec rm -rf {} \;
   ```

## 安全建议

1. 添加认证中间件
2. 实现速率限制
3. 验证上传文件大小和类型
4. 使用 HTTPS
5. 限制允许的程序列表

## 下一步

- [ ] 添加任务队列（Redis + Celery）
- [ ] 添加用户认证
- [ ] 添加任务优先级
- [ ] 实现结果缓存
- [ ] 添加监控和日志
- [ ] Docker 容器化部署
