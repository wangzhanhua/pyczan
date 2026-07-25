import os
import sys
import pytest

# 确保能找到 pyczan_shmem 包
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "src", "py"))

from pyczan_shmem import SharedMemoryDict


@pytest.fixture(autouse=True)
def cleanup_shmem():
    """每个测试前后清理共享内存"""
    yield
    try:
        from pyczan_shmem._pyczan_shmem import SharedMemoryDict as CppDict
        # 用 C++ 层面的清理（删除共享内存对象由 OS 在进程退出时完成）
        pass
    except Exception:
        pass


class TestSharedMemoryDict:
    """SharedMemoryDict 的 Python 绑定测试"""

    def test_open_close(self):
        d = SharedMemoryDict()
        assert d.OpenOrCreate("test_py_open", 65536)
        assert d.IsOpen()
        assert d.Name() == "test_py_open"
        d.Close()
        assert not d.IsOpen()

    def test_set_get(self):
        d = SharedMemoryDict()
        d.OpenOrCreate("test_py_setget")
        d.Set("hello", "world")
        assert d.Get("hello") == "world"
        d.Close()

    def test_set_get_bytes(self):
        """测试中文字符串"""
        d = SharedMemoryDict()
        d.OpenOrCreate("test_py_unicode")
        d.Set("name", "pyczan")
        d.Set("key中文", "值中文")
        assert d.Get("name") == "pyczan"
        assert d.Get("key中文") == "值中文"
        d.Close()

    def test_overwrite(self):
        d = SharedMemoryDict()
        d.OpenOrCreate("test_py_overwrite")
        d.Set("key", "v1")
        d.Set("key", "v2")
        assert d.Get("key") == "v2"
        d.Close()

    def test_has(self):
        d = SharedMemoryDict()
        d.OpenOrCreate("test_py_has")
        d.Set("exists", "yes")
        assert d.Has("exists")
        assert not d.Has("nonexistent")
        d.Close()

    def test_delete(self):
        d = SharedMemoryDict()
        d.OpenOrCreate("test_py_delete")
        d.Set("temp", "value")
        assert d.Has("temp")
        assert d.Delete("temp")
        assert not d.Has("temp")
        assert not d.Delete("nonexistent")
        d.Close()

    def test_size(self):
        d = SharedMemoryDict()
        d.OpenOrCreate("test_py_size")
        assert d.Size() == 0
        d.Set("a", "1")
        d.Set("b", "2")
        d.Set("c", "3")
        assert d.Size() == 3
        d.Clear()
        assert d.Size() == 0
        d.Close()

    def test_clear(self):
        d = SharedMemoryDict()
        d.OpenOrCreate("test_py_clear")
        d.Set("x", "10")
        d.Set("y", "20")
        assert d.Size() == 2
        d.Clear()
        assert d.Size() == 0
        d.Close()

    def test_keys(self):
        d = SharedMemoryDict()
        d.OpenOrCreate("test_py_keys")
        d.Set("alpha", "1")
        d.Set("beta", "2")
        d.Set("gamma", "3")
        keys = d.Keys()
        assert len(keys) == 3
        assert "alpha" in keys
        assert "beta" in keys
        assert "gamma" in keys
        d.Close()

    def test_not_open_empty(self):
        """未打开时返回空"""
        d = SharedMemoryDict()
        assert d.Size() == 0
        assert not d.Has("anything")
        assert d.Keys() == []
        # Close 后重新打开是新的内存段
        d.OpenOrCreate("test_py_reopen")
        d.Set("a", "1")
        d.Close()
        d2 = SharedMemoryDict()
        d2.OpenOrCreate("test_py_reopen")
        # 重新打开是一个新的共享内存段
        assert d2.Size() == 0
        d2.Close()

    # ======== Python dict 协议测试 ========

    def test_dict_protocol_setitem_getitem(self):
        d = SharedMemoryDict()
        d.OpenOrCreate("test_py_dict")
        d["key1"] = "value1"
        assert d["key1"] == "value1"
        d.Close()

    def test_dict_protocol_contains(self):
        d = SharedMemoryDict()
        d.OpenOrCreate("test_py_contains")
        d["key"] = "val"
        assert "key" in d
        assert "nonexistent" not in d
        d.Close()

    def test_dict_protocol_len(self):
        d = SharedMemoryDict()
        d.OpenOrCreate("test_py_len")
        assert len(d) == 0
        d["a"] = "1"
        d["b"] = "2"
        assert len(d) == 2
        d.Close()

    def test_dict_protocol_delitem(self):
        d = SharedMemoryDict()
        d.OpenOrCreate("test_py_del")
        d["key"] = "val"
        assert "key" in d
        del d["key"]
        assert "key" not in d
        d.Close()

    def test_dict_protocol_delitem_keyerror(self):
        d = SharedMemoryDict()
        d.OpenOrCreate("test_py_del_err")
        with pytest.raises(KeyError):
            del d["nonexistent"]
        d.Close()

    def test_get_keyerror(self):
        d = SharedMemoryDict()
        d.OpenOrCreate("test_py_get_err")
        with pytest.raises(KeyError):
            d.Get("nonexistent")
        d.Close()

    def test_context_manager(self):
        """上下文管理器应自动关闭"""
        with SharedMemoryDict() as d:
            d.OpenOrCreate("test_py_ctx")
            d["key"] = "value"
            assert d["key"] == "value"
        # 退出 with 块后，应自动 Close

    def test_multiple_values(self):
        d = SharedMemoryDict()
        d.OpenOrCreate("test_py_multi")
        for i in range(100):
            d.Set(f"key{i}", f"value{i}")
        assert d.Size() == 100
        assert d.Get("key50") == "value50"
        d.Close()

    def test_not_open_operations(self):
        """未打开时执行操作应抛出异常"""
        d = SharedMemoryDict()
        with pytest.raises(RuntimeError):
            d.Get("any")
        with pytest.raises(RuntimeError):
            d.Set("any", "val")
        with pytest.raises(RuntimeError):
            d.Delete("any")
