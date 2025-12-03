"""
fgtest 程序包装器
负责执行 fgtest 命令并管理任务状态
"""
import subprocess
import json
import os
from pathlib import Path
from datetime import datetime
from typing import Dict, Optional


def update_status(result_dir: str, status: str, error: Optional[str] = None, result: Optional[Dict] = None):
    """更新任务状态文件"""
    status_path = Path(result_dir) / "status.json"
    
    # 读取现有状态
    if status_path.exists():
        with open(status_path, "r") as f:
            status_data = json.load(f)
    else:
        status_data = {}
    
    # 更新状态
    status_data["status"] = status
    status_data["updated_at"] = datetime.now().isoformat()
    
    if error:
        status_data["error"] = error
    
    if result:
        status_data["result"] = result
    
    # 如果状态是运行中，设置开始时间
    if status == "running" and "started_at" not in status_data:
        status_data["started_at"] = datetime.now().isoformat()
    
    # 如果是完成或失败状态，设置完成时间
    if status in ["completed", "failed"]:
        status_data["completed_at"] = datetime.now().isoformat()
    
    # 写入状态文件
    with open(status_path, "w") as f:
        json.dump(status_data, f, indent=2)


def build_taint_options(options: Dict) -> str:
    """构建 TAINT_OPTIONS 环境变量字符串"""
    parts = []
    
    if "output_dir" in options:
        parts.append(f"output_dir={options['output_dir']}")
    
    if options.get("stdin"):
        parts.append("taint_file=stdin")
    elif "taint_file" in options:
        parts.append(f"taint_file={options['taint_file']}")
    
    if options.get("debug"):
        parts.append("debug=1")
    
    if options.get("solve_ub"):
        parts.append("solve_ub=1")
    
    return ":".join(parts) if parts else ""


def run_fgtest_task(
    task_id: str,
    target_path: str,
    seed_input: str,
    branch_meta_path: str,
    traces_path: str,
    result_dir: str,
    options: Dict,
    fgtest_path: str
):
    """
    执行 fgtest 任务
    
    Args:
        task_id: 任务 ID
        target_path: 目标程序路径
        seed_input: 种子输入字符串（支持十六进制如0x1a1d或普通字符串）
        branch_meta_path: 分支元数据 JSON 路径
        traces_path: 轨迹 JSON 路径
        result_dir: 结果目录
        options: 配置选项字典
        fgtest_path: fgtest 可执行文件路径
    """
    try:
        # 更新状态为运行中
        update_status(result_dir, "running")
        
        # 准备输出文件路径
        rewards_path = os.path.join(result_dir, "rewards.json")
        
        # 设置输出目录（如果未指定）
        if "output_dir" not in options:
            output_dir = os.path.join(result_dir, "output")
            os.makedirs(output_dir, exist_ok=True)
            options["output_dir"] = output_dir
        
        # 构建 TAINT_OPTIONS 环境变量
        env = os.environ.copy()
        taint_options = build_taint_options(options)
        if taint_options:
            env["TAINT_OPTIONS"] = taint_options
        
        # 构建 fgtest 命令
        # seed_input 直接作为字符串参数传递给 fgtest
        # fgtest 会自动识别是十六进制字符串、普通字符串还是文件路径
        cmd = [
            fgtest_path,
            target_path,
            seed_input,
            branch_meta_path,
            traces_path,
            rewards_path
        ]
        
        # 记录命令到日志（可选）
        log_path = Path(result_dir) / "execution.log"
        with open(log_path, "w") as log_file:
            log_file.write(f"Command: {' '.join(cmd)}\n")
            log_file.write(f"Seed input: {seed_input}\n")
            log_file.write(f"TAINT_OPTIONS: {taint_options}\n\n")
            log_file.flush()
            
            # 执行命令
            try:
                process = subprocess.run(
                    cmd,
                    env=env,
                    stdout=log_file,
                    stderr=subprocess.STDOUT,
                    timeout=3600,  # 1 小时超时
                    cwd=result_dir
                )
                
                log_file.write(f"\n\nExit code: {process.returncode}\n")
                
            except subprocess.TimeoutExpired:
                log_file.write("\n\nERROR: Task timeout (1 hour)\n")
                update_status(result_dir, "failed", error="Task execution timeout")
                return
            
            except Exception as e:
                log_file.write(f"\n\nERROR: {str(e)}\n")
                update_status(result_dir, "failed", error=f"Execution error: {str(e)}")
                return
        
        # 检查执行结果
        if process.returncode != 0:
            error_msg = f"fgtest exited with code {process.returncode}"
            update_status(result_dir, "failed", error=error_msg)
            return
        
        # 检查结果文件是否存在
        if not os.path.exists(rewards_path):
            update_status(result_dir, "failed", error="Result file not generated")
            return
        
        # 读取结果文件
        try:
            with open(rewards_path, "r") as f:
                result_data = json.load(f)
            
            # 更新状态为完成
            update_status(result_dir, "completed", result=result_data)
            
        except json.JSONDecodeError as e:
            update_status(result_dir, "failed", error=f"Invalid JSON in result file: {str(e)}")
        except Exception as e:
            update_status(result_dir, "failed", error=f"Failed to read result file: {str(e)}")
    
    except Exception as e:
        # 捕获所有其他异常
        error_msg = f"Unexpected error: {str(e)}"
        update_status(result_dir, "failed", error=error_msg)

