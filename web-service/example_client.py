#!/usr/bin/env python3
"""
示例：使用 Python requests 库调用 fgtest Web Service API
"""
import requests
import time
import json

# API 基础 URL
BASE_URL = "http://localhost:8000"

def submit_task(program, traces_path, seed=None, branch_meta_path=None):
    """
    提交 fgtest 任务
    
    Args:
        program: 程序名称 ('dummy', 'xor' 或 'control_temp')
        traces_path: 轨迹 JSON 文件路径
        seed: 可选，写入 stdin 的种子字符串，默认为 "0402"
        branch_meta_path: 可选，branch metadata 文件路径，默认使用 bin/ctwm_index.json
    """
    url = f"{BASE_URL}/api/submit"
    
    files = {
        'traces': open(traces_path, 'rb')
    }
    
    data = {
        'program': program
    }
    
    if seed:
        data['seed'] = seed
    
    if branch_meta_path:
        files['branch_meta'] = open(branch_meta_path, 'rb')
    
    response = requests.post(url, files=files, data=data)
    response.raise_for_status()
    
    # 关闭文件
    for f in files.values():
        if hasattr(f, 'close'):
            f.close()
    
    return response.json()

def get_status(task_id):
    """查询任务状态"""
    url = f"{BASE_URL}/api/status/{task_id}"
    response = requests.get(url)
    response.raise_for_status()
    return response.json()

def download_result(task_id, output_path):
    """下载任务结果"""
    url = f"{BASE_URL}/api/download/{task_id}"
    response = requests.get(url)
    response.raise_for_status()
    
    with open(output_path, 'wb') as f:
        f.write(response.content)
    print(f"Result saved to: {output_path}")

def main():
    print("=== fgtest Web Service API 使用示例 ===\n")
    
    # 1. 提交任务（使用 control_temp 程序）
    print("1. 提交任务（control_temp 程序）...")
    result = submit_task(
        program='control_temp',
        traces_path='../examples/control_temp_traces.json',
        seed='1 35 2 0'
    )
    
    task_id = result['task_id']
    print(f"   Task ID: {task_id}")
    print(f"   Seed: {result['seed']}")
    print(f"   Program: {result['program']}")
    print(f"   Branch Meta Source: {result['branch_meta_source']}\n")
    
    # 2. 轮询任务状态
    print("2. 等待任务完成...")
    while True:
        status = get_status(task_id)
        print(f"   Status: {status['status']}")
        
        if status['status'] in ['completed', 'failed']:
            break
        
        time.sleep(2)
    
    print()
    
    # 3. 下载结果
    if status['status'] == 'completed':
        print("3. 下载结果...")
        download_result(task_id, f'result_{task_id}.json')
        
        if 'result' in status:
            print("\n4. 结果摘要:")
            print(json.dumps(status['result'], indent=2))
    else:
        print(f"3. 任务失败: {status.get('error', 'Unknown error')}")

if __name__ == '__main__':
    main()
