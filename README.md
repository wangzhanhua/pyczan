# pyczan

**Windows 多进程共享内存，dict 即用，零配置。**

```python
from pyczan import shmem

d = shmem.Dict()
d.OpenOrCreate("config")

# 和普通 dict 一样用
d["host"] = "localhost"
d["port"] = 8080          # 自动类型
d["ratio"] = 0.95         # int/float/bytes/dict 都支持
d["cfg"] = {"key": "val"} # 自动 pickle

print(d["host"])           # "localhost"
print(len(d))              # 3
del d["port"]
print("host" in d)         # True
d.Close()
```

---

## 为什么用 pyczan？

### 跨进程共享，不需要 Redis

```
两个 Python 进程要共享数据：
  Redis      → 要装服务、配置、占端口
  Manager()  → 慢 10-35x（序列化+pipe）
  文件        → 没同步、要轮询
  raw shmem  → 只有 bytes，自己管索引

  pyczan     → pip install，dict 即用
```

### 性能

```
                写入 10 条 × 10B          写入 100 条 × 10B         写入 10 条 × 100KB
pyczan          0.0000s                  0.0001s                   0.0001s
Manager()       0.0006s  (35x 慢)        0.0036s  (36x 慢)         0.0017s  (17x 慢)
raw shmem       0.0000s  (同层)          0.0001s  (同层)           0.0006s  (慢 6x)
```

### numpy 零拷贝

```python
buf = d.Alloc(24 * 1024 * 1024)     # 24MB 连续内存
arr = np.frombuffer(buf, dtype=np.uint8)  # 零拷贝
```

---

## 安装

```bash
pip install pyczan
```

需要 Windows 10/11 64 位 + Python 3.8+。

## 使用

```python
from pyczan import shmem

d = shmem.Dict()
d.OpenOrCreate("config", total_size=1024*1024*1024)

# dict 协议：自动类型
d["count"] = 42                # int
d["ratio"] = 3.14              # float
d["data"] = b"\x00\x01\xff"   # bytes
d["user"] = {"name": "zan"}    # dict → pickle

# 原子操作
d.Increment("counter")
d.Add("price", 1.5)

# 零拷贝缓冲区
buf = d.Alloc(1024 * 1024)
arr = np.frombuffer(buf, dtype=np.uint8)
# ... 写入数据 ...
d.Free(buf)

# 状态监控
print(d.Status())
# → {"entries": 5, "total_blocks": 61440, "used_blocks": 12, ...}

# 崩溃后恢复
if d.Status()["was_crashed"]:
    print("检测到上次异常退出，数据已自动修复")

d.Close()
```

## 开发

从源码构建：

```bash
git clone https://gitee.com/weiyunnote/pyczan.git
cd pyczan
src\cpp\build_release.bat
pytest tests\test_shmem.py -v
```

需要 VS2022 BuildTools（MSVC v143）。

## 许可证

MIT
