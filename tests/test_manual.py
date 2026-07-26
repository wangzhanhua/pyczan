"""
pyczan.shmem 手动综合测试
==========================

测试所有功能，结果以表格形式输出。
支持从指定文件夹加载文件（txt/jpg/mp4 等）进行共享内存读写测试。

用法：
    python tests/test_manual.py                          # 只测基础功能
    python tests/test_manual.py --data-dir D:\test_files  # 含文件共享测试
    python tests/test_manual.py --perf-only               # 只跑性能对比
    python tests/test_manual.py --func-only               # 只跑功能测试
"""

import os
import sys
import time
import struct
import tempfile
import argparse
import multiprocessing
from pathlib import Path

#sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "src", "py"))
from pyczan import shmem

# ════════════════════════════════════════════════════════════════
# 工具
# ════════════════════════════════════════════════════════════════

RESET_NAME = "test_manual_reset"


def reset(name):
    """清理指定名称的共享内存"""
    try:
        shmem.Dict.Reset(name)
    except Exception:
        pass


def clean_exit(d):
    """安全关闭"""
    try:
        d.Close()
    except Exception:
        pass


def section(title):
    """打印章节标题"""
    w = 72
    print()
    print("=" * w)
    print(f"  {title}")
    print("=" * w)


def result_table(results):
    """打印功能测试结果表"""
    if not results:
        return
    w = 72
    print(f"  {'测试项':40s} {'结果':>8s} {'耗时':>10s}")
    print(f"  {'-'*40} {'-'*8} {'-'*10}")
    passed = 0
    for name, ok, elapsed in results:
        status = "✅ PASS" if ok else "❌ FAIL"
        elapsed_str = f"{elapsed * 1000:.1f}ms" if elapsed < 1 else f"{elapsed:.2f}s"
        if elapsed < 0.001:
            elapsed_str = "<0.1ms"
        print(f"  {name:40s} {status:>8s} {elapsed_str:>10s}")
        if ok:
            passed += 1
    print(f"  {'-'*40} {'-'*8} {'-'*10}")
    print(f"  合计: {passed}/{len(results)} 通过")
    return passed == len(results)


# ════════════════════════════════════════════════════════════════
# 基础功能测试
# ════════════════════════════════════════════════════════════════


def test_basic_operations():
    """测试 Dict 基本增删改查"""
    section("1. 基础功能测试")
    results = []
    name = "test_manual_basic"
    reset(name)

    # 1.1 Open / Close
    t0 = time.perf_counter()
    d = shmem.Dict()
    try:
        ok = d.OpenOrCreate(name, 4 * 1024 * 1024)
        t = time.perf_counter() - t0
        results.append(("OpenOrCreate（新建）", ok, t))
    except Exception as e:
        results.append(("OpenOrCreate（新建）", False, time.perf_counter() - t0))

    results.append(("IsOpen", d.IsOpen(), 0))
    results.append(("Name", d.Name() == name, 0))

    # 1.2 Set / Get
    t0 = time.perf_counter()
    try:
        d.Set("key_str", "hello world")
        t = time.perf_counter() - t0
        results.append(("Set(str)", True, t))
    except Exception as e:
        results.append(("Set(str)", False, time.perf_counter() - t0))

    t0 = time.perf_counter()
    try:
        v = d.Get("key_str")
        ok = v == "hello world"
        t = time.perf_counter() - t0
        results.append(("Get(str)", ok, t))
    except Exception as e:
        results.append(("Get(str)", False, time.perf_counter() - t0))

    # 1.3 Dict 协议
    try:
        d["key_proto"] = "value_proto"
        v = d["key_proto"]
        del d["key_proto"]
        ok = v == "value_proto" and "key_proto" not in d
        results.append(("Dict 协议（[]/del/in）", ok, 0))
    except Exception as e:
        results.append(("Dict 协议（[]/del/in）", False, 0))

    # 1.4 Has
    results.append(("Has（存在）", d.Has("key_str"), 0))
    results.append(("Has（不存在）", not d.Has("nonexistent"), 0))

    # 1.5 Size
    d["a"] = "1"
    d["b"] = "2"
    results.append(("Size", d.Size() >= 2, 0))

    # 1.6 Keys
    keys = d.Keys()
    results.append(("Keys", "a" in keys and "key_str" in keys, 0))

    # 1.7 Delete
    t0 = time.perf_counter()
    ok = d.Delete("a")
    t = time.perf_counter() - t0
    results.append(("Delete", ok and not d.Has("a"), t))

    # 1.8 Clear
    d.Clear()
    results.append(("Clear", d.Size() == 0, 0))

    # 1.9 覆盖写
    d["overwrite"] = "v1"
    d["overwrite"] = "v2"
    results.append(("覆盖写", d["overwrite"] == "v2", 0))

    # 1.10 大value
    large = "x" * 5000
    d["large"] = large
    results.append(("大value（5KB）", d["large"] == large, 0))

    # 1.11 超大value
    huge = "x" * 100000
    t0 = time.perf_counter()
    try:
        d["huge"] = huge
        t = time.perf_counter() - t0
        ok = d["huge"] == huge
        results.append(("超大value（100KB）", ok, t))
    except Exception as e:
        results.append(("超大value（100KB）", False, time.perf_counter() - t0))

    # 1.12 KeyError
    try:
        _ = d["nonexistent_key"]
        results.append(("KeyError 抛出", False, 0))
    except KeyError:
        results.append(("KeyError 抛出", True, 0))

    # 1.13 Context manager
    try:
        with shmem.Dict() as ctx:
            ctx.OpenOrCreate("test_manual_ctx", 4 * 1024 * 1024)
            ctx["ctx_key"] = "ctx_val"
        # 离开 with 后自动 Close
        with shmem.Dict() as ctx2:
            ctx2.OpenOrCreate("test_manual_ctx", 4 * 1024 * 1024)
            v = ctx2["ctx_key"]
            ok = v == "ctx_val"
            results.append(("Context manager", ok, 0))
    except Exception as e:
        results.append(("Context manager", False, 0))

    reset("test_manual_ctx")
    clean_exit(d)

    all_pass = result_table(results)
    return all_pass


def test_atomic_ops():
    """测试原子计数器"""
    section("2. 原子计数器测试")
    results = []
    name = "test_manual_atomic"
    reset(name)
    d = shmem.Dict()
    d.OpenOrCreate(name, 4 * 1024 * 1024)

    # 2.1 Increment（新key）
    v = d.Increment("counter")
    results.append(("Increment（新key=1）", v == 1, 0))

    # 2.2 Increment（已有）
    v = d.Increment("counter")
    results.append(("Increment（已有=2）", v == 2, 0))

    # 2.3 Decrement
    v = d.Decrement("counter")
    results.append(("Decrement（=1）", v == 1, 0))

    # 2.4 多次操作
    for i in range(10):
        d.Increment("counter")
    results.append(("Increment×10", d["counter"] == 11, 0))

    # 2.5 AtomicAdd float
    v = d.Add("price", 1.5)
    # 新 key=0 → 0+1.5 = 1.5
    results.append(("Add float（新key=1.5）", abs(v - 1.5) < 1e-9, 0))

    v = d.Add("price", 2.5)
    results.append(("Add float（=4.0）", abs(v - 4.0) < 1e-9, 0))

    # 2.6 批量速度测试
    t0 = time.perf_counter()
    for i in range(1000):
        d.Increment("batch")
    t = time.perf_counter() - t0
    results.append((f"Increment×1000", d["batch"] == 1000, t))

    clean_exit(d)
    reset(name)
    return result_table(results)


def test_notify():
    """测试 Wait 变更通知"""
    section("3. 变更通知测试")
    results = []
    name = "test_manual_notify"
    reset(name)
    d = shmem.Dict()
    d.OpenOrCreate(name, 4 * 1024 * 1024)

    # 3.1 Wait 超时
    t0 = time.perf_counter()
    ok = not d.Wait(200)  # 200ms 超时返回 False
    t = time.perf_counter() - t0
    results.append(("Wait 超时（200ms）", ok, t))

    # 3.2 Wait 收到信号

    d.Wait(0)  # 消费残留信号
    p = multiprocessing.Process(target=_child_signal, args=(name,))
    t0 = time.perf_counter()
    p.start()
    ok = d.Wait(3000)  # 等子进程写入
    t = time.perf_counter() - t0
    p.join()
    results.append(("Wait 收到信号", ok and d["signal"] == "done", t))

    clean_exit(d)
    reset(name)
    return result_table(results)


def test_zero_copy():
    """测试零拷贝缓冲区"""
    section("4. 零拷贝缓冲区测试")
    results = []
    name = "test_manual_zerocopy"
    reset(name)
    d = shmem.Dict()
    d.OpenOrCreate(name, 4 * 1024 * 1024)

    # 4.1 Alloc / Free
    buf = d.Alloc(1024)
    results.append(("Alloc（1KB）", buf is not None and len(buf) >= 1024, 0))

    if buf:
        # 4.2 写入 memoryview
        buf[:8] = b"TESTDATA"
        results.append(("写入 memoryview", bytes(buf[:8]) == b"TESTDATA", 0))

        # 4.3 Free
        try:
            d.Free(buf)
            results.append(("Free", True, 0))
        except Exception:
            results.append(("Free", False, 0))

    # 4.4 大块分配（需要 64MB 总空间才能分配 4MB）
    big = d.Alloc(2 * 1024 * 1024)
    results.append(("Alloc（2MB）", big is not None, 0))
    if big:
        d.Free(big)

    # 4.5 分配失败（请求超过剩余空间）
    try:
        huge = d.Alloc(500 * 1024 * 1024)  # 500MB（远超 4MB 剩余空间）
        results.append(("Alloc 超量返回 None", huge is None, 0))
    except Exception:
        results.append(("Alloc 超量返回 None", False, 0))

    clean_exit(d)
    reset(name)
    return result_table(results)


def test_status():
    """测试 Status 状态监控"""
    section("5. 状态监控测试")
    results = []
    name = "test_manual_status"
    reset(name)
    d = shmem.Dict()
    d.OpenOrCreate(name, 4 * 1024 * 1024)

    # 5.1 基本字段
    s = d.Status()
    results.append(("Status 有 entries 字段", "entries" in s, 0))
    results.append(("Status 有 total_blocks 字段", "total_blocks" in s, 0))
    results.append(("Status 有 used_blocks 字段", "used_blocks" in s, 0))
    results.append(("Status 有 generation 字段", "generation" in s, 0))
    results.append(("Status 有 was_crashed 字段", "was_crashed" in s, 0))

    # 5.2 写入后状态变化
    d["a"] = "1"
    s2 = d.Status()
    results.append(("写入后 entries > 0", s2["entries"] > 0, 0))
    results.append(("写入后 used_blocks > 0", s2["used_blocks"] > 0, 0))

    # 5.3 Generation 递增
    g1 = d.Status()["generation"]
    d["b"] = "2"
    g2 = d.Status()["generation"]
    results.append(("Generation 递增", g2 > g1, 0))

    clean_exit(d)
    reset(name)
    return result_table(results)


def test_edge_cases():
    """测试边界情况"""
    section("6. 边界情况测试")
    results = []
    name = "test_manual_edge"
    reset(name)
    d = shmem.Dict()
    d.OpenOrCreate(name, 4 * 1024 * 1024)

    # 6.1 空 key
    try:
        d[""] = "empty"
        results.append(("空 key", d[""] == "empty", 0))
    except Exception:
        results.append(("空 key", False, 0))

    # 6.2 空 value
    try:
        d["empty_val"] = ""
        results.append(("空 value", d["empty_val"] == "", 0))
    except Exception:
        results.append(("空 value", False, 0))

    # 6.3 特殊字符 key
    special = "!@#$%^&*()_+{}|:<>?~`你好"
    try:
        d[special] = "special"
        results.append(("特殊字符 key", d[special] == "special", 0))
    except Exception:
        results.append(("特殊字符 key", False, 0))

    # 6.4 二进制数据
    binary = bytes(range(256))
    try:
        d["binary"] = binary
        results.append(("二进制数据（\\x00-\\xff）", d["binary"] == binary, 0))
    except Exception:
        results.append(("二进制数据（\\x00-\\xff）", False, 0))

    # 6.5 大量小 key
    t0 = time.perf_counter()
    try:
        for i in range(500):
            d[f"tiny_{i}"] = str(i)
        ok = d.Size() >= 500
        t = time.perf_counter() - t0
        results.append((f"500 个小 key 写入", ok, t))
    except Exception as e:
        results.append(("500 个小 key 写入", False, time.perf_counter() - t0))

    # 6.6 大量小 key 读取
    t0 = time.perf_counter()
    try:
        all_ok = True
        for i in range(500):
            if d[f"tiny_{i}"] != str(i):
                all_ok = False
                break
        t = time.perf_counter() - t0
        results.append(("500 个小 key 读取", all_ok, t))
    except Exception:
        results.append(("500 个小 key 读取", False, time.perf_counter() - t0))

    # 6.7 删除所有 500 个 key
    t0 = time.perf_counter()
    try:
        for i in range(500):
            d.Delete(f"tiny_{i}")
        ok = d.Size() < 500
        t = time.perf_counter() - t0
        results.append(("删除 500 个 key", ok, t))
    except Exception:
        results.append(("删除 500 个 key", False, time.perf_counter() - t0))

    # 6.8 写删交替（碎片测试）
    t0 = time.perf_counter()
    try:
        for i in range(200):
            d[f"temp_{i}"] = "x" * (50 + (i % 10) * 10)
        for i in range(200):
            d.Delete(f"temp_{i}")
        t = time.perf_counter() - t0
        results.append(("写删交替 200 次", True, t))
    except Exception:
        results.append(("写删交替 200 次", False, time.perf_counter() - t0))

    # 6.9 重复 OpenOrCreate（幂等）
    try:
        ok = d.OpenOrCreate(name)
        results.append(("重复 OpenOrCreate（幂等）", ok, 0))
    except Exception:
        results.append(("重复 OpenOrCreate（幂等）", False, 0))

    clean_exit(d)
    reset(name)
    return result_table(results)


# ════════════════════════════════════════════════════════════════
# 多进程测试
# ════════════════════════════════════════════════════════════════


def _proc_write(name, start, count):
    d = shmem.Dict()
    d.OpenOrCreate(name, 4 * 1024 * 1024)
    for i in range(count):
        d[f"proc_{start + i}"] = str(start + i)
    d.Close()


def _proc_increment(name, key, count):
    d = shmem.Dict()
    d.OpenOrCreate(name, 4 * 1024 * 1024)
    for _ in range(count):
        d.Increment(key)
    d.Close()


def _proc_big_write(name, data_size):
    d = shmem.Dict()
    d.OpenOrCreate(name, 4 * 1024 * 1024)
    d["big"] = "x" * data_size
    d.Close()

def _child_signal(name):
    import time
    d2 = shmem.Dict()
    d2.OpenOrCreate(name, 4 * 1024 * 1024)
    d2["signal"] = "done"
    d2.Close()


def _proc_rw(name):
    d2 = shmem.Dict()
    d2.OpenOrCreate(name, 4 * 1024 * 1024)
    for _ in range(50):
        _ = d2["shared"]
        d2["shared"] = "x" * 100
    d2.Close()


def test_multiprocess():
    """多进程并发测试"""
    section("7. 多进程并发测试")
    results = []
    name = "test_manual_mp"
    reset(name)
    d = shmem.Dict()
    d.OpenOrCreate(name, 4 * 1024 * 1024)

    # 7.1 多进程写入
    N = 4
    PER = 50
    t0 = time.perf_counter()
    procs = []
    for i in range(N):
        p = multiprocessing.Process(target=_proc_write, args=(name, i * PER, PER))
        p.start()
        procs.append(p)
    for p in procs:
        p.join()
    t = time.perf_counter() - t0
    all_ok = all(p.exitcode == 0 for p in procs)
    results.append((f"{N} 进程并发写入 {PER} 条/进程", all_ok and d.Size() == N * PER, t))

    # 7.2 多进程原子递增
    key = "mp_counter"
    d[key] = "0"
    N2 = 4
    PER2 = 200
    t0 = time.perf_counter()
    procs2 = []
    for _ in range(N2):
        p = multiprocessing.Process(target=_proc_increment, args=(name, key, PER2))
        p.start()
        procs2.append(p)
    for p in procs2:
        p.join()
    t = time.perf_counter() - t0
    expected = N2 * PER2
    actual = int(d[key])
    results.append((f"{N2} 进程并发递增 {PER2} 次/进程", actual == expected, t))

    # 7.3 多进程读写混合
    d.Clear()
    d["shared"] = "init"
    N3 = 4
    procs3 = []
    t0 = time.perf_counter()


    for _ in range(N3):
        p = multiprocessing.Process(target=_proc_rw, args=(name,))
        p.start()
        procs3.append(p)
    for p in procs3:
        p.join()
    t = time.perf_counter() - t0
    all_ok = all(p.exitcode == 0 for p in procs3)
    results.append((f"{N3} 进程读写混合 50 次", all_ok, t))

    # 7.4 多进程大数据并发
    data_size = 50000
    t0 = time.perf_counter()
    procs4 = []
    for i in range(3):
        p = multiprocessing.Process(target=_proc_big_write, args=(name, data_size))
        p.start()
        procs4.append(p)
    for p in procs4:
        p.join()
    t = time.perf_counter() - t0
    all_ok = all(p.exitcode == 0 for p in procs4)
    results.append((f"3 进程并发写 50KB", all_ok, t))

    clean_exit(d)
    reset(name)
    return result_table(results)


# ════════════════════════════════════════════════════════════════
# 文件共享测试
# ════════════════════════════════════════════════════════════════


def file_sharing_test(data_dir):
    """文件共享测试：从指定目录读文件写到共享内存"""
    section("8. 文件共享测试")
    results = []
    name = "test_manual_files"
    reset(name)
    d = shmem.Dict()
    d.OpenOrCreate(name, 32 * 1024 * 1024)

    if not data_dir or not os.path.isdir(data_dir):
        results.append(("数据目录不存在，跳过文件测试", True, 0))
        result_table(results)
        clean_exit(d)
        return True

    # 支持的扩展名
    exts = {".txt", ".jpg", ".jpeg", ".png", ".gif", ".bmp",
            ".mp4", ".avi", ".mov", ".mkv",
            ".mp3", ".wav", ".flac",
            ".pdf", ".zip", ".json", ".xml", ".yaml", ".csv"}

    files = []
    for ext in exts:
        files.extend(Path(data_dir).rglob(f"*{ext}"))

    if not files:
        results.append((f"{data_dir} 中没有找到支持的测试文件", True, 0))
        result_table(results)
        clean_exit(d)
        return True

    # 限制最多 20 个文件
    files = files[:20]
    total_bytes = 0

    for f in files:
        fname = f.name
        try:
            data = f.read_bytes()
            total_bytes += len(data)
            # 存到共享内存
            d[fname] = data
            # 读回来验证
            readback = d[fname]
            if readback == data:
                size_str = f"{len(data):,}B" if len(data) < 1024 else f"{len(data)/1024:.1f}KB" if len(data) < 1024*1024 else f"{len(data)/1024/1024:.1f}MB"
                results.append((f"  {fname[:32]:32s} ✅ {size_str}", True, 0))
            else:
                results.append((f"  {fname[:32]:32s} ❌ 数据不匹配", False, 0))
        except Exception as e:
            results.append((f"  {fname[:32]:32s} ❌ {str(e)[:20]}", False, 0))

    results.insert(0, (f"文件总数: {len(files)}，总大小: {total_bytes/1024:.1f}KB", True, 0))

    clean_exit(d)
    reset(name)
    result_table(results)
    return True


# ════════════════════════════════════════════════════════════════
# 性能对比测试
# ════════════════════════════════════════════════════════════════


def _warmup():
    """预热：加载 DLL"""
    d = shmem.Dict()
    d.OpenOrCreate("_warmup", 4 * 1024 * 1024)
    d["x"] = "1"
    d.Close()


def bench_pyczan(count, value_size, read_only=False):
    d = shmem.Dict()
    d.OpenOrCreate("bench_pyczan", 32 * 1024 * 1024)

    value = "x" * value_size

    if read_only:
        # 先写入
        for i in range(count):
            d[f"k{i}"] = value

    t0 = time.perf_counter()
    if read_only:
        for i in range(count):
            _ = d[f"k{i}"]
    else:
        for i in range(count):
            d[f"k{i}"] = value
    elapsed = time.perf_counter() - t0

    d.Close()
    return elapsed


def bench_fast_pyczan(count, value_size):
    """小数据快测"""
    d = shmem.Dict()
    d.OpenOrCreate("bench_fast", 32 * 1024 * 1024)
    value = "x" * value_size

    t0 = time.perf_counter()
    for i in range(count):
        d[f"k{i}"] = value
    write_t = time.perf_counter() - t0

    t0 = time.perf_counter()
    for i in range(count):
        _ = d[f"k{i}"]
    read_t = time.perf_counter() - t0

    d.Close()
    return write_t, read_t


def bench_manager(count, value_size):
    from multiprocessing import Manager
    mgr = Manager()
    d = mgr.dict()
    value = "x" * value_size

    t0 = time.perf_counter()
    for i in range(count):
        d[f"k{i}"] = value
    write_t = time.perf_counter() - t0

    t0 = time.perf_counter()
    for i in range(count):
        _ = d[f"k{i}"]
    read_t = time.perf_counter() - t0

    return write_t, read_t


def bench_raw_shmem(count, value_size):
    from multiprocessing import shared_memory
    slot_size = 32 + value_size
    total = count * slot_size
    name = "bench_raw"

    for retry in range(3):
        try:
            shm = shared_memory.SharedMemory(name=name, create=True, size=total)
            break
        except FileExistsError:
            try:
                old = shared_memory.SharedMemory(name=name, create=False)
                old.close()
                old.unlink()
            except Exception:
                pass
            time.sleep(0.1)
    else:
        return None, None

    data = bytearray(count * (32 + value_size))
    t0 = time.perf_counter()
    for i in range(count):
        offset = i * slot_size
        key_bytes = f"k{i}".encode()
        val_bytes = b"x" * value_size
        data[offset:offset + len(key_bytes)] = key_bytes
        data[offset + 32:offset + 32 + value_size] = val_bytes

    # 模拟写入
    write_t = time.perf_counter() - t0

    t0 = time.perf_counter()
    for i in range(count):
        offset = i * slot_size
        _ = bytes(shm.buf[offset:offset + slot_size])
    read_t = time.perf_counter() - t0

    shm.close()
    shm.unlink()
    return write_t, read_t


def bench_pickle_file(count, value_size):
    import pickle
    import tempfile
    fd, path = tempfile.mkstemp(suffix=".pkl")
    os.close(fd)

    value = "x" * value_size
    data = {f"k{i}": value for i in range(count)}

    t0 = time.perf_counter()
    with open(path, "wb") as f:
        pickle.dump(data, f)
    write_t = time.perf_counter() - t0

    t0 = time.perf_counter()
    with open(path, "rb") as f:
        _ = pickle.load(f)
    read_t = time.perf_counter() - t0

    os.unlink(path)
    return write_t, read_t


def bench_json_file(count, value_size):
    import json
    import tempfile
    fd, path = tempfile.mkstemp(suffix=".json")
    os.close(fd)

    value = "x" * value_size
    data = {f"k{i}": value for i in range(count)}

    t0 = time.perf_counter()
    with open(path, "w", encoding="utf-8") as f:
        json.dump(data, f)
    write_t = time.perf_counter() - t0

    t0 = time.perf_counter()
    with open(path, "r", encoding="utf-8") as f:
        _ = json.load(f)
    read_t = time.perf_counter() - t0

    os.unlink(path)
    return write_t, read_t


def perf_compare_table(results, title):
    """打印性能对比表"""
    w = 80
    print()
    print(f"  ╔═ {title} {'═' * (w - 6)}")
    print(f"  ║ {'方案':30s} {'写入':>12s} {'读取':>12s} {'写入/op':>10s} {'读取/op':>10s}")
    print(f"  ║ {'-'*30} {'-'*12} {'-'*12} {'-'*10} {'-'*10}")

    for label, write_t, read_t in results:
        if write_t is None:
            print(f"  ║ {label:30s} {'FAILED':>12s} {'FAILED':>12s}")
        else:
            w_per_op = write_t / max(count, 1) if 'count' in dir() else write_t / 1
            r_per_op = read_t / max(count, 1) if 'count' in dir() else read_t / 1
            print(f"  ║ {label:30s} {write_t:12.4f}s {read_t:12.4f}s {w_per_op*1e6:9.1f}us {r_per_op*1e6:9.1f}us")

    print(f"  ╚{'═' * (w - 2)}")


def run_benchmarks():
    """运行性能测试"""
    section("9. 性能对比测试")
    reset("bench_pyczan")
    reset("bench_fast")

    # 预热
    _warmup()

    scenarios = [
        ("10条 × 10B", 10, 10),
        ("100条 × 10B", 100, 10),
        ("10条 × 1KB", 10, 1024),
        ("100条 × 1KB", 100, 1024),
        ("1000条 × 10B", 1000, 10),
        ("10条 × 100KB", 10, 102400),
        ("1条 × 1MB", 1, 1024 * 1024),
    ]

    for label, cnt, vsize in scenarios:
        global count
        count = cnt

        results = []

        # pyczan
        try:
            wt, rt = bench_fast_pyczan(cnt, vsize)
            results.append(("pyczan.shmem", wt, rt))
        except Exception as e:
            results.append((f"pyczan.shmem ({e})", None, None))

        # Manager
        try:
            wt, rt = bench_manager(cnt, vsize)
            results.append(("Manager().dict()", wt, rt))
        except Exception as e:
            results.append(("Manager().dict()", None, None))

        # raw shared_memory
        if vsize <= 102400:
            try:
                wt, rt = bench_raw_shmem(cnt, vsize)
                if wt is not None:
                    results.append(("raw shared_memory", wt, rt))
            except Exception:
                pass

        # file pickle
        try:
            wt, rt = bench_pickle_file(cnt, vsize)
            results.append(("file pickle", wt, rt))
        except Exception:
            pass

        # file JSON
        try:
            wt, rt = bench_json_file(cnt, vsize)
            results.append(("file JSON", wt, rt))
        except Exception:
            pass

        perf_compare_table(results, label)

    # 清理
    reset("bench_pyczan")
    reset("bench_fast")


# ════════════════════════════════════════════════════════════════
# pytest 支持
# ════════════════════════════════════════════════════════════════


def pytest_addoption(parser):
    """pytest 命令行参数：--data-dir"""
    parser.addoption("--data-dir", default=None, help="测试文件目录（.txt/.jpg/.mp4 等）")


def pytest_configure(config):
    """pytest 配置：有 --data-dir 时跑文件共享测试"""
    data_dir = config.getoption("--data-dir", default=None)
    if data_dir:
        file_sharing_test(data_dir)


# ════════════════════════════════════════════════════════════════
# 主入口
# ════════════════════════════════════════════════════════════════


def parse_args():
    parser = argparse.ArgumentParser(
        description="pyczan.shmem 手动综合测试",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__,
    )
    parser.add_argument("--data-dir", default=None,
                        help="测试文件目录（.txt/.jpg/.mp4 等）")
    parser.add_argument("--func-only", action="store_true",
                        help="只跑功能测试")
    parser.add_argument("--perf-only", action="store_true",
                        help="只跑性能测试")
    return parser.parse_args()


if __name__ == "__main__":
    args = parse_args()

    print()
    print("╔══════════════════════════════════════════════════════════════╗")
    print("║           pyczan.shmem 手动综合测试                         ║")
    print("║           Windows + Python 3.8+                             ║")
    print("╚══════════════════════════════════════════════════════════════╝")
    print(f"  Python: {sys.version}")
    print(f"  时间: {time.strftime('%Y-%m-%d %H:%M:%S')}")

    all_pass = True

    if not args.perf_only:
        # 基础功能
        if not test_basic_operations():
            all_pass = False
        if not test_atomic_ops():
            all_pass = False
        if not test_notify():
            all_pass = False
        if not test_zero_copy():
            all_pass = False
        if not test_status():
            all_pass = False
        if not test_edge_cases():
            all_pass = False

        # 多进程
        if not test_multiprocess():
            all_pass = False

        # 文件共享
        if args.data_dir:
            file_sharing_test(args.data_dir)
        else:
            section("8. 文件共享测试")
            print("  ⚠ 跳过（使用 --data-dir D:\\test_files 指定文件目录）")

    if not args.func_only:
        run_benchmarks()

    # 最终结果
    print()
    w = 72
    print("=" * w)
    if all_pass:
        print("  ✅ 所有功能测试通过！")
    else:
        print("  ⚠ 部分测试失败，请检查上方的 ❌ 行")
    print(f"  {time.strftime('%Y-%m-%d %H:%M:%S')}")
    print("=" * w)
    print()

    sys.exit(0 if all_pass else 1)
