import os
import sys
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
