import os
import sys
import time
import multiprocessing
import pytest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "src", "py"))

from pyczan import shmem


class TestDict:
    """pyczan.shmem.Dict 的测试"""

    def test_open_close(self):
        d = shmem.Dict()
        d.OpenOrCreate("test_py_open", 4*1024*1024)
        assert d.IsOpen()
        assert d.Name() == "test_py_open"
        d.Close()

    def test_set_get_str(self):
        d = shmem.Dict()
        d.OpenOrCreate("test_py_str")
        d.Set("hello", "world")
        assert d.Get("hello") == "world"
        d.Close()

    def test_dict_protocol(self):
        d = shmem.Dict()
        d.OpenOrCreate("test_py_dict")
        d["key1"] = "value1"
        assert d["key1"] == "value1"
        assert "key1" in d
        assert len(d) == 1
        del d["key1"]
        assert "key1" not in d
        d.Close()

    def test_overwrite(self):
        d = shmem.Dict()
        d.OpenOrCreate("test_py_ow")
        d["key"] = "v1"
        d["key"] = "v2"
        assert d["key"] == "v2"
        d.Close()

    def test_large_value(self):
        """跨块链的大值测试"""
        d = shmem.Dict()
        d.OpenOrCreate("test_py_large")
        d["large"] = "x" * 2000
        assert d["large"] == "x" * 2000
        d.Close()

    def test_cross_zone_update(self):
        """从小区域切换到大区域"""
        d = shmem.Dict()
        d.OpenOrCreate("test_py_cross")
        d["key"] = "small_value"
        assert d["key"] == "small_value"
        d["key"] = "x" * 2000
        assert d["key"] == "x" * 2000
        d.Close()

    def test_has(self):
        d = shmem.Dict()
        d.OpenOrCreate("test_py_has")
        d["exists"] = "yes"
        assert d.Has("exists")
        assert not d.Has("nonexistent")
        d.Close()

    def test_delete(self):
        d = shmem.Dict()
        d.OpenOrCreate("test_py_del")
        d["temp"] = "value"
        assert d.Has("temp")
        assert d.Delete("temp")
        assert not d.Has("temp")
        assert not d.Delete("nonexistent")
        d.Close()

    def test_size(self):
        d = shmem.Dict()
        d.OpenOrCreate("test_py_size")
        assert len(d) == 0
        d["a"] = "1"
        d["b"] = "2"
        d["c"] = "3"
        assert len(d) == 3
        d.Clear()
        assert len(d) == 0
        d.Close()

    def test_clear(self):
        d = shmem.Dict()
        d.OpenOrCreate("test_py_clear")
        d["x"] = "10"
        d["y"] = "20"
        assert len(d) == 2
        d.Clear()
        assert len(d) == 0
        d.Close()

    def test_keys(self):
        d = shmem.Dict()
        d.OpenOrCreate("test_py_keys")
        d["alpha"] = "1"
        d["beta"] = "2"
        d["gamma"] = "3"
        keys = d.Keys()
        assert len(keys) == 3
        assert "alpha" in keys
        assert "beta" in keys
        assert "gamma" in keys
        d.Close()

    def test_mixed_small_large(self):
        """小数据和大数据混合"""
        d = shmem.Dict()
        d.OpenOrCreate("test_py_mix")
        for i in range(10):
            d[f"small{i}"] = f"val{i}"
        d["big"] = "x" * 2000
        assert d["big"] == "x" * 2000
        for i in range(10):
            assert d[f"small{i}"] == f"val{i}"
        assert len(d.Keys()) == 11
        d.Close()

    def test_context_manager(self):
        with shmem.Dict() as d:
            d.OpenOrCreate("test_py_ctx")
            d["key"] = "value"
            assert d["key"] == "value"

    def test_get_keyerror(self):
        d = shmem.Dict()
        d.OpenOrCreate("test_py_get_err")
        with pytest.raises(KeyError):
            _ = d["nonexistent"]
        d.Close()

    def test_del_keyerror(self):
        d = shmem.Dict()
        d.OpenOrCreate("test_py_del_err")
        with pytest.raises(KeyError):
            del d["nonexistent"]
        d.Close()

    def test_not_open_operations(self):
        d = shmem.Dict()
        with pytest.raises(RuntimeError):
            d.Get("any")
        with pytest.raises(RuntimeError):
            d.Set("any", "val")


# ============================================================
# 多进程并发测试（核心生产环境验证）
# ============================================================


def _child_write(name, key, value):
    d = shmem.Dict()
    d.OpenOrCreate(name, 4*1024*1024)
    d[key] = value
    d.Close()


def _child_increment(name, key, count):
    d = shmem.Dict()
    d.OpenOrCreate(name, 4*1024*1024)
    for _ in range(count):
        d.Increment(key)
    d.Close()


def _child_wait_signal(name, sleep_sec):
    time.sleep(sleep_sec)
    d = shmem.Dict()
    d.OpenOrCreate(name, 4*1024*1024)
    d["signal"] = "done"
    d.Close()


class TestMultiProcess:

    def test_child_write_parent_read(self):
        """子进程写入，父进程读取"""
        name = "test_mp_write"
        shmem.Dict.Reset(name)
        d = shmem.Dict()
        d.OpenOrCreate(name, 4*1024*1024)
        d["parent"] = "hello"
        p = multiprocessing.Process(target=_child_write, args=(name, "child", "world"))
        p.start()
        p.join()
        assert p.exitcode == 0
        assert d["child"] == "world"
        assert d["parent"] == "hello"
        d.Close()

    def test_concurrent_increment(self):
        """多进程并发原子递增，最终值正确"""
        name = "test_mp_inc"
        shmem.Dict.Reset(name)
        d = shmem.Dict()
        d.OpenOrCreate(name, 4*1024*1024)
        d["counter"] = "0"
        N = 5       # 进程数
        PER = 100   # 每个进程递增次数
        procs = []
        for _ in range(N):
            p = multiprocessing.Process(target=_child_increment, args=(name, "counter", PER))
            p.start()
            procs.append(p)
        for p in procs:
            p.join()
        for p in procs:
            assert p.exitcode == 0
        assert int(d["counter"]) == N * PER
        d.Close()


class TestWait:

    def test_wait_timeout(self):
        """Wait 超时返回 False"""
        name = "test_wait_timeout"
        shmem.Dict.Reset(name)
        d = shmem.Dict()
        d.OpenOrCreate(name, 4*1024*1024)
        result = d.Wait(100)  # 100ms 超时
        assert result is False
        d.Close()

    def test_wait_receives_signal(self):
        """子进程写入后，父进程 Wait 返回 True"""
        name = "test_wait_signal"
        shmem.Dict.Reset(name)
        d = shmem.Dict()
        d.OpenOrCreate(name, 4*1024*1024)
        d.Wait(0)  # 消费启动时的残留信号
        p = multiprocessing.Process(target=_child_wait_signal, args=(name, 0.3))
        p.start()
        result = d.Wait(5000)
        assert result is True
        assert d["signal"] == "done"
        p.join()
        d.Close()


class TestAtomicOps:

    def test_increment_new_key(self):
        """对不存在的 key Increment → 值为 1"""
        name = "test_inc_new"
        shmem.Dict.Reset(name)
        d = shmem.Dict()
        d.OpenOrCreate(name, 4*1024*1024)
        r = d.Increment("counter")
        assert r == 1
        assert d["counter"] == 1
        d.Close()

    def test_increment_existing(self):
        """对已存在的 int key Increment"""
        name = "test_inc_exist"
        shmem.Dict.Reset(name)
        d = shmem.Dict()
        d.OpenOrCreate(name, 4*1024*1024)
        d["counter"] = 10
        r = d.Increment("counter")
        assert r == 11
        assert d["counter"] == 11
        d.Close()

    def test_decrement(self):
        name = "test_dec"
        shmem.Dict.Reset(name)
        d = shmem.Dict()
        d.OpenOrCreate(name, 4*1024*1024)
        d["counter"] = 5
        r = d.Decrement("counter")
        assert r == 4
        assert d["counter"] == 4
        d.Close()

    def test_atomic_add_float(self):
        name = "test_add_float"
        shmem.Dict.Reset(name)
        d = shmem.Dict()
        d.OpenOrCreate(name, 4*1024*1024)
        d["price"] = 10.5
        r = d.Add("price", 3.2)
        assert abs(r - 13.7) < 1e-9
        assert abs(float(d["price"]) - 13.7) < 1e-9
        d.Close()

    def test_atomic_add_new_key(self):
        """对不存在的 key Add → 初始 0 + delta"""
        name = "test_add_new"
        shmem.Dict.Reset(name)
        d = shmem.Dict()
        d.OpenOrCreate(name, 4*1024*1024)
        r = d.Add("price", 2.5)
        assert abs(r - 2.5) < 1e-9
        d.Close()


class TestDictProtocolExtras:

    def test_get_with_default(self):
        """get(key, default) 当 key 不存在时返回默认值"""
        name = "test_get_def"
        shmem.Dict.Reset(name)
        d = shmem.Dict()
        d.OpenOrCreate(name, 4*1024*1024)
        assert d.get("nonexistent") is None
        assert d.get("nonexistent", 42) == 42
        d["exists"] = "hello"
        assert d.get("exists") == "hello"
        assert d.get("exists", "fallback") == "hello"
        d.Close()

    def test_iter_keys(self):
        """__iter__ 能遍历所有 key"""
        name = "test_iter"
        shmem.Dict.Reset(name)
        d = shmem.Dict()
        d.OpenOrCreate(name, 4*1024*1024)
        keys = {"a", "b", "c"}
        for k in keys:
            d[k] = k.upper()
        iterated = set(k for k in d)
        assert iterated == keys
        d.Close()
