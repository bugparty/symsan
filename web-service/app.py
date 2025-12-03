"""
FastAPI Web Service for fgtest - 极简版本
提供 REST API 接口来提交任务、查询状态和下载结果
"""
from fastapi import FastAPI, UploadFile, File, Form, HTTPException
from fastapi.responses import FileResponse, JSONResponse
from fastapi.middleware.cors import CORSMiddleware
import os
import uuid
import json
import threading
from pathlib import Path
from typing import Optional
from datetime import datetime

from fgtest_wrapper import run_fgtest_task

app = FastAPI(
    title="fgtest Web Service",
    description="极简版 Web Service for fgtest 符号执行工具",
    version="1.0.0"
)

# 允许跨域（开发环境）
app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)

# 目录配置
UPLOAD_DIR = Path("uploads")
RESULTS_DIR = Path("results")
FGTEST_PATH = os.environ.get("FGTEST_PATH", "../build/bin/fgtest")

# 创建必要的目录
UPLOAD_DIR.mkdir(exist_ok=True)
RESULTS_DIR.mkdir(exist_ok=True)


@app.post("/api/submit")
async def submit_task(
    program: str = Form(..., description="选择程序: dummy 或 xor"),
    seed: Optional[str] = Form("0x0402", description="种子字符串 (hex如0x1a1d或普通字符串，默认0x0402)"),
    branch_meta: Optional[UploadFile] = File(None, description="分支元数据 JSON 文件（可选，默认使用bin/ctwm_index.json）"),
    traces: UploadFile = File(..., description="轨迹 JSON 文件"),
    options: Optional[str] = Form(None, description="可选配置 JSON 字符串")
):
    """
    提交一个新的 fgtest 任务
    
    接收上传的文件和可选的 seed 字符串，启动后台执行任务
    - seed: 十六进制字符串（如 0x1a1d）或普通字符串，默认为 0x0402
    - branch_meta: 可选，不上传则使用 bin/ctwm_index.json
    - traces: 必需的轨迹 JSON 文件
    """
    # 验证程序选择
    if program not in ["dummy", "xor"]:
        raise HTTPException(status_code=400, detail="Program must be 'dummy' or 'xor'")
    
    task_id = str(uuid.uuid4())[:8]
    task_dir = UPLOAD_DIR / task_id
    result_dir = RESULTS_DIR / task_id
    task_dir.mkdir(exist_ok=True)
    result_dir.mkdir(exist_ok=True)
    
    try:
        # 使用本地 bin 目录中的程序
        target_path = Path("bin") / program
        if not target_path.exists():
            raise HTTPException(status_code=500, detail=f"Program '{program}' not found in bin directory")
        
        # seed 参数直接作为字符串传递给 fgtest
        # fgtest 会自动识别是十六进制、普通字符串还是文件路径
        seed_input = seed if seed else "0x0402"
        
        # 处理 branch_meta：如果用户上传了就用上传的，否则使用默认路径
        if branch_meta and branch_meta.filename:
            # 用户上传了 branch_meta 文件
            branch_meta_path = task_dir / "branch_meta.json"
            with open(branch_meta_path, "wb") as f:
                f.write(await branch_meta.read())
        else:
            # 使用默认的 ctwm_index.json
            default_meta_path = Path("bin") / "ctwm_index.json"
            if not default_meta_path.exists():
                raise HTTPException(status_code=500, detail=f"Default branch metadata not found: {default_meta_path}")
            branch_meta_path = default_meta_path
        
        # 保存 traces 文件
        traces_path = task_dir / "traces.json"
        with open(traces_path, "wb") as f:
            f.write(await traces.read())
        
        # 解析 options
        task_options = {}
        if options:
            try:
                task_options = json.loads(options)
            except json.JSONDecodeError:
                raise HTTPException(status_code=400, detail="Invalid JSON in options field")
        
        # 创建初始状态文件
        status = {
            "task_id": task_id,
            "status": "pending",
            "created_at": datetime.now().isoformat(),
            "updated_at": datetime.now().isoformat(),
            "seed": seed_input,
            "program": program,
            "branch_meta_source": "uploaded" if (branch_meta and branch_meta.filename) else "default",
            "error": None,
            "result": None
        }
        status_path = result_dir / "status.json"
        with open(status_path, "w") as f:
            json.dump(status, f, indent=2)
        
        # 启动后台任务
        def task_runner():
            run_fgtest_task(
                task_id=task_id,
                target_path=str(target_path),
                seed_input=seed_input,
                branch_meta_path=str(branch_meta_path),
                traces_path=str(traces_path),
                result_dir=str(result_dir),
                options=task_options,
                fgtest_path=FGTEST_PATH
            )
        
        thread = threading.Thread(target=task_runner, daemon=True)
        thread.start()
        
        return {
            "task_id": task_id,
            "status": "pending",
            "seed": seed_input,
            "program": program,
            "branch_meta_source": "uploaded" if (branch_meta and branch_meta.filename) else "default",
            "message": "Task submitted successfully"
        }
    
    except Exception as e:
        raise HTTPException(status_code=500, detail=f"Failed to submit task: {str(e)}")


@app.get("/api/status/{task_id}")
async def get_status(task_id: str):
    """
    查询任务状态
    
    返回任务的当前状态，包括进度和结果（如果已完成）
    """
    status_path = RESULTS_DIR / task_id / "status.json"
    
    if not status_path.exists():
        raise HTTPException(status_code=404, detail="Task not found")
    
    try:
        with open(status_path, "r") as f:
            status = json.load(f)
        
        # 如果任务已完成，尝试加载结果
        if status.get("status") == "completed":
            rewards_path = RESULTS_DIR / task_id / "rewards.json"
            if rewards_path.exists():
                with open(rewards_path, "r") as f:
                    status["result"] = json.load(f)
        
        return status
    
    except json.JSONDecodeError:
        raise HTTPException(status_code=500, detail="Invalid status file format")
    except Exception as e:
        raise HTTPException(status_code=500, detail=f"Failed to read status: {str(e)}")


@app.get("/api/download/{task_id}")
async def download_result(task_id: str):
    """
    下载任务结果文件 (rewards.json)
    """
    rewards_path = RESULTS_DIR / task_id / "rewards.json"
    
    if not rewards_path.exists():
        raise HTTPException(status_code=404, detail="Result file not found")
    
    return FileResponse(
        path=str(rewards_path),
        filename=f"rewards_{task_id}.json",
        media_type="application/json"
    )


@app.get("/")
async def root():
    """
    根路径 - 返回 API 文档链接
    """
    return {
        "message": "fgtest Web Service",
        "docs": "/docs",
        "api": {
            "submit": "POST /api/submit (program, seed, branch_meta?, traces, options?)",
            "status": "GET /api/status/{task_id}",
            "download": "GET /api/download/{task_id}"
        },
        "defaults": {
            "seed": "0x0402",
            "branch_meta": "bin/ctwm_index.json"
        },
        "note": "seed 和 branch_meta 均可选，使用默认值"
    }


@app.get("/health")
async def health():
    """健康检查端点"""
    return {"status": "healthy"}

