"""
性能对比：pyczan.shmem vs multiprocessing.shared_memory vs Manager().dict()
"""
import time
import os
import sys
import struct
from multiprocessing import shared_memory, Manager

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "src", "py"))
from pyczan import shmem


# ============================================================
# pyczan.shmem
# ============================================================

def bench_pyczan_set(count, value_size):
    d = shmem.Dict()
    d.OpenOrCreate("bench_pyczan", 32 * 1024 * 1024)
    value = "x" * value_size
    start = time.perf_counter()
    for i in range(count):
        d[f"key{i}"] = value
    elapsed = time.perf_counter() - start
    d.Close()
    return elapsed


def bench_pyczan_get(count, value_size):
    d = shmem.Dict()
    d.OpenOrCreate("bench_pyczan", 32 * 1024 * 1024)
    # populate
    value = "x" * value_size
    for i in range(count):
        d[f"key{i}"] = value
    start = time.perf_counter()
    for i in range(count):
        _ = d[f"key{i}"]
    elapsed = time.perf_counter() - start
    d.Close()
    return elapsed


# ============================================================
# multiprocessing.shared_memory (raw bytes, no dict API)
# ============================================================

def bench_raw_shmem_set(count, value_size):
    # Layout: fixed-size slots, each slot = 32B header + value
    slot_size = 32 + value_size
    total = count * slot_size
    name = "bench_raw"
    try:
        shm = shared_memory.SharedMemory(name=name, create=True, size=total)
    except FileExistsError:
        shm = shared_memory.SharedMemory(name=name, create=False)
        shm.unlink()
        shm = shared_memory.SharedMemory(name=name, create=True, size=total)

    start = time.perf_counter()
    for i in range(count):
        offset = i * slot_size
        key_bytes = f"key{i}".encode()
        val_bytes = b"x" * value_size
        data = struct.pack("II", len(key_bytes), len(val_bytes)) + key_bytes + val_bytes
        shm.buf[offset:offset + len(data)] = data
    write_time = time.perf_counter() - start

    # read back
    start = time.perf_counter()
    for i in range(count):
        offset = i * slot_size
        _ = bytes(shm.buf[offset:offset + slot_size])
    read_time = time.perf_counter() - start

    shm.close()
    shm.unlink()
    return write_time, read_time


# ============================================================
# multiprocessing.Manager().dict()
# ============================================================

def bench_manager_set(count, value_size):
    mgr = Manager()
    d = mgr.dict()
    value = "x" * value_size
    start = time.perf_counter()
    for i in range(count):
        d[f"key{i}"] = value
    elapsed = time.perf_counter() - start
    return elapsed


def bench_manager_get(count, value_size):
    mgr = Manager()
    d = mgr.dict()
    value = "x" * value_size
    for i in range(count):
        d[f"key{i}"] = value
    start = time.perf_counter()
    for i in range(count):
        _ = d[f"key{i}"]
    elapsed = time.perf_counter() - start
    return elapsed


# ============================================================
# 文件 IPC（JSON / pickle）
# ============================================================

import json
import pickle
import tempfile

def bench_file_json_set(count, value_size):
    value = "x" * value_size
    data = {f"key{i}": value for i in range(count)}
    fd, path = tempfile.mkstemp(suffix=".json")
    os.close(fd)
    start = time.perf_counter()
    with open(path, "w", encoding="utf-8") as f:
        json.dump(data, f)
    elapsed = time.perf_counter() - start
    os.unlink(path)
    return elapsed

def bench_file_json_get(count, value_size):
    value = "x" * value_size
    data = {f"key{i}": value for i in range(count)}
    fd, path = tempfile.mkstemp(suffix=".json")
    os.close(fd)
    with open(path, "w", encoding="utf-8") as f:
        json.dump(data, f)
    start = time.perf_counter()
    with open(path, "r", encoding="utf-8") as f:
        _ = json.load(f)
    elapsed = time.perf_counter() - start
    os.unlink(path)
    return elapsed

def bench_file_pickle_set(count, value_size):
    value = "x" * value_size
    data = {f"key{i}": value for i in range(count)}
    fd, path = tempfile.mkstemp(suffix=".pkl")
    os.close(fd)
    start = time.perf_counter()
    with open(path, "wb") as f:
        pickle.dump(data, f)
    elapsed = time.perf_counter() - start
    os.unlink(path)
    return elapsed

def bench_file_pickle_get(count, value_size):
    value = "x" * value_size
    data = {f"key{i}": value for i in range(count)}
    fd, path = tempfile.mkstemp(suffix=".pkl")
    os.close(fd)
    with open(path, "wb") as f:
        pickle.dump(data, f)
    start = time.perf_counter()
    with open(path, "rb") as f:
        _ = pickle.load(f)
    elapsed = time.perf_counter() - start
    os.unlink(path)
    return elapsed


# ============================================================
# 运行
# ============================================================

if __name__ == "__main__":
    # 只测小数据（pyczan 走小区域）和中等数据
    scenarios = [
        ("10条 × 10B", 10, 10),
        ("100条 × 10B", 100, 10),
        ("10条 × 1KB", 10, 1024),
        ("10条 × 100KB", 10, 102400),
    ]

    print("=" * 72)
    print("  pyczan.shmem vs multiprocessing.shared_memory vs Manager().dict()")
    print("  环境: Windows + Python 3.9 + MSVC 2022")
    print("=" * 72)

    for label, count, vsize in scenarios:
        print(f"\n▶ {label}")
        print(f"  {'方案':30s} {'写入耗时':>12s} {'读取耗时':>12s}")

        # pyczan
        try:
            pt = bench_pyczan_set(count, vsize)
            pg = bench_pyczan_get(count, vsize) if count <= 100 else float('inf')
            print(f"  {'pyczan.shmem':30s} {pt:12.4f}s {pg:12.4f}s")
        except Exception as e:
            print(f"  {'pyczan.shmem':30s} FAILED: {e}")

        # Manager
        try:
            mt = bench_manager_set(count, vsize)
            mg = bench_manager_get(count, vsize) if count <= 100 else float('inf')
            print(f"  {'Manager().dict()':30s} {mt:12.4f}s {mg:12.4f}s")
        except Exception as e:
            print(f"  {'Manager().dict()':30s} FAILED: {e}")

        # raw shared_memory
        if vsize <= 102400:
            try:
                rt, rr = bench_raw_shmem_set(count, vsize)
                print(f"  {'raw shared_memory':30s} {rt:12.4f}s {rr:12.4f}s")
            except Exception as e:
                print(f"  {'raw shared_memory':30s} FAILED: {e}")

        # file IPC
        try:
            ft = bench_file_json_set(count, vsize)
            fg = bench_file_json_get(count, vsize)
            print(f"  {'file JSON (串行)':30s} {ft:12.4f}s {fg:12.4f}s")
        except Exception as e:
            print(f"  {'file JSON (串行)':30s} FAILED: {e}")

        try:
            pt = bench_file_pickle_set(count, vsize)
            pg = bench_file_pickle_get(count, vsize)
            print(f"  {'file pickle (串行)':30s} {pt:12.4f}s {pg:12.4f}s")
        except Exception as e:
            print(f"  {'file pickle (串行)':30s} FAILED: {e}")
