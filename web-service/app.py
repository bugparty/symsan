"""
FastAPI Web Service for fgtest - 极简版本
提供 REST API 接口来提交任务、查询状态和下载结果
"""
from fastapi import FastAPI, UploadFile, File, Form, HTTPException, Request
from fastapi.responses import FileResponse, JSONResponse
from fastapi.middleware.cors import CORSMiddleware
from fastapi.exceptions import RequestValidationError
from starlette.exceptions import HTTPException as StarletteHTTPException
import os
import uuid
import json
import threading
import traceback
import logging
from pathlib import Path
from typing import Optional
from datetime import datetime

from fgtest_wrapper import run_fgtest_task

# 配置日志
logging.basicConfig(
    level=logging.DEBUG,
    format='%(asctime)s - %(name)s - %(levelname)s - %(message)s'
)
logger = logging.getLogger(__name__)

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

# 基准目录
BASE_DIR = Path(__file__).resolve().parent
# 路径配置（可通过环境变量覆盖），统一为绝对路径
BIN_DIR = Path(os.environ.get("FGTEST_BIN_DIR", "/appdata/bin")).resolve()
UPLOAD_DIR = Path(os.environ.get("FGTEST_UPLOAD_DIR", BASE_DIR / "uploads")).resolve()
RESULTS_DIR = Path(os.environ.get("FGTEST_RESULTS_DIR", BASE_DIR / "results")).resolve()
FGTEST_PATH = os.environ.get("FGTEST_PATH", str((BIN_DIR / "fgtest").resolve()))

# 创建必要的目录
UPLOAD_DIR.mkdir(exist_ok=True)
RESULTS_DIR.mkdir(exist_ok=True)


# 请求验证错误处理器 - 捕获参数验证失败
@app.exception_handler(RequestValidationError)
async def validation_exception_handler(request: Request, exc: RequestValidationError):
    """捕获请求验证错误，输出详细信息到console"""
    error_details = exc.errors()
    body = await request.body()
    logger.error(f"Validation error for {request.method} {request.url}")
    logger.error(f"Request body: {body[:1000] if body else 'empty'}")  # 限制日志长度
    logger.error(f"Validation errors: {json.dumps(error_details, indent=2, default=str)}")
    return JSONResponse(
        status_code=422,
        content={
            "detail": error_details,
            "body": body.decode('utf-8', errors='replace')[:500] if body else None
        }
    )


# HTTP 异常处理器 - 捕获 HTTPException
@app.exception_handler(StarletteHTTPException)
async def http_exception_handler(request: Request, exc: StarletteHTTPException):
    """捕获 HTTP 异常，输出详细信息到console"""
    logger.error(f"HTTP {exc.status_code} for {request.method} {request.url}: {exc.detail}")
    return JSONResponse(
        status_code=exc.status_code,
        content={"detail": exc.detail}
    )


# 全局异常处理器 - 捕获所有未处理的异常
@app.exception_handler(Exception)
async def global_exception_handler(request: Request, exc: Exception):
    """捕获所有未处理的异常，输出详细信息到console"""
    error_detail = traceback.format_exc()
    logger.error(f"Unhandled exception for {request.method} {request.url}:\n{error_detail}")
    return JSONResponse(
        status_code=500,
        content={
            "detail": str(exc),
            "type": type(exc).__name__,
            "traceback": error_detail
        }
    )


@app.post("/api/submit")
async def submit_task(
    traces: UploadFile = File(..., description="轨迹 JSON 文件"),
    program: str = Form("dummy", description="选择程序: dummy, xor 或 control_temp"),
    seed: str = Form("0402", description="写入目标 stdin 的种子字符串（原样写入，默认 \"0402\"）"),
    branch_meta: Optional[UploadFile] = File(None, description="分支元数据 JSON 文件（可选，默认使用bin/ctwm_index.json）"),
    options: Optional[str] = Form(None, description="可选配置 JSON 字符串")
):
    """
    提交一个新的 fgtest 任务
    
    接收上传的文件和 seed 字符串（直接写入目标程序 stdin），启动后台执行任务
    - seed: 任何字符串，原样写入 stdin（如 "0x1a1d" 会按字符写入），默认 "0402"
    - branch_meta: 可选，不上传则使用 bin/{program}_ctwm_index.json（若不存在则回退到 bin/ctwm_index.json）
    - traces: 必需的轨迹 JSON 文件
    """
    # 验证程序选择
    if program not in ["dummy", "xor", "control_temp"]:
        raise HTTPException(status_code=400, detail="Program must be 'dummy', 'xor' or 'control_temp'")
    
    task_id = str(uuid.uuid4())[:8]
    task_dir = UPLOAD_DIR / task_id
    result_dir = RESULTS_DIR / task_id
    task_dir.mkdir(exist_ok=True)
    result_dir.mkdir(exist_ok=True)
    
    try:
        # 使用本地 bin 目录中的程序
        target_path = (BIN_DIR / program).resolve()
        if not target_path.exists():
            raise HTTPException(status_code=500, detail=f"Program '{program}' not found in bin directory ({target_path})")
        
        # seed 参数直接作为字符串传递，fgtest 会将其写入目标 stdin
        seed_input = seed
        
        # 处理 branch_meta：如果用户上传了就用上传的，否则优先使用 {program}_ctwm_index.json，不存在则回退到通用 ctwm_index.json
        if branch_meta and branch_meta.filename:
            # 用户上传了 branch_meta 文件
            branch_meta_path = (task_dir / "branch_meta.json").resolve()
            with open(branch_meta_path, "wb") as f:
                f.write(await branch_meta.read())
        else:
            # 先尝试 program 专用的 {program}_ctwm_index.json，不存在则回退
            program_meta_path = (BIN_DIR / f"{program}_ctwm_index.json").resolve()
            default_meta_path = (BIN_DIR / "ctwm_index.json").resolve()
            if program_meta_path.exists():
                branch_meta_path = program_meta_path
            elif default_meta_path.exists():
                branch_meta_path = default_meta_path
            else:
                raise HTTPException(status_code=500, detail=f"Default branch metadata not found: {program_meta_path} or {default_meta_path}")
            if not default_meta_path.exists():
                # 记录提示，仍然继续使用 fallback
                pass
        
        # 保存 traces 文件
        traces_path = (task_dir / "traces.json").resolve()
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
    
    except HTTPException:
        raise
    except Exception as e:
        import traceback
        error_detail = traceback.format_exc()
        logger.error(f"Failed to submit task: {error_detail}")
        raise HTTPException(status_code=500, detail=f"Failed to submit task: {type(e).__name__}: {str(e) or 'No message'}\n{error_detail}")


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
            "seed": "0402",
            "branch_meta": str(BIN_DIR / "{program}_ctwm_index.json"),
            "bin_dir": str(BIN_DIR)
        },
        "note": "seed 和 branch_meta 均可选，使用默认值"
    }


@app.get("/health")
async def health():
    """健康检查端点"""
    return {"status": "healthy"}
