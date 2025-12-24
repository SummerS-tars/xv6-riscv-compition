#!/usr/bin/env python3
import subprocess
import time
import sys
import os

def run_test():
    print(">>> Starting Experiment A Grading...")
    
    # 确保当前目录下有 Makefile
    if not os.path.exists("Makefile"):
        print("Makefile not found. Please run this script from the xv6-riscv directory.")
        return

    # 1. 启动子进程: 执行 "make qemu"
    try:
        process = subprocess.Popen(
            ["make", "qemu"],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            encoding='utf-8',
            # [修复] 文本模式下 bufsize 不能为 0，移除该参数或设为 1 (行缓冲)
            bufsize=1 
        )
    except FileNotFoundError:
        print(" 'make' command not found. Ensure you have build tools installed.")
        return

    # 2. 交互逻辑
    try:
        output_buffer = ""
        test_started = False
        
        # 循环读取输出
        while True:
            # 读取一个字符
            char = process.stdout.read(1)
            
            # 如果进程退出，char 为空
            if not char and process.poll() is not None:
                break
            
            if not char:
                continue

            output_buffer += char
            sys.stdout.write(char) # 实时打印到屏幕
            sys.stdout.flush()
            
            # 检测 Shell 启动完成
            # 注意：xv6 的提示符可能是 "$ "
            if "$ " in output_buffer and not test_started:
                print("\n[Controller] xv6 shell detected. Sending 'mmaptest' command...")
                # 发送命令
                process.stdin.write("mmaptest\n")
                process.stdin.flush()
                test_started = True
                output_buffer = "" # 清空 buffer

            # 检测测试结束标志
            if "ALL TESTS PASSED" in output_buffer:
                print("\n\n>>> All tests passed! Score: 100/100")
                terminate_process(process)
                return
            
            # 检测失败标志
            if "panic:" in output_buffer or "TEST FAILED" in output_buffer:
                print("\n\n>>> [FAIL] Test failed or Kernel panic detected.")
                terminate_process(process)
                return

    except KeyboardInterrupt:
        print("\nInterrupted by user.")
        terminate_process(process)
    except Exception as e:
        print(f"\n Exception occurred: {e}")
        terminate_process(process)

def terminate_process(process):
    # 尝试优雅退出 qemu (Ctrl-A X)
    try:
        # 某些版本的 QEMU 需要特殊字符退出，
        # 但直接 terminate 对自动化测试通常足够
        process.terminate()
        process.wait(timeout=2)
    except:
        process.kill()

if __name__ == "__main__":
    run_test()