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

实际基准数据（Windows 10 + Python 3.9 + MSVC 2022）：

```
100条 × 10B 写入/读取（越快越好）：
  pyczan.shmem      0.0001s / 0.0000s  ← 基准
  Manager().dict()  0.0035s / 0.0034s  慢 33x / 慢 251x
  raw shared_memory 0.0001s / 0.0000s  持平（无 dict API）
  file pickle       0.0002s / 0.0001s  慢 2x
  file JSON         0.0002s / 0.0001s  慢 2x

1000条 × 10B 写入/读取：
  pyczan.shmem      0.0005s / 0.0001s  ← 基准
  Manager().dict()  0.0278s / 0.0251s  慢 56x / 慢 205x
  raw shared_memory 0.0004s / 0.0002s  持平
  file pickle       0.0002s / 0.0001s  慢 2x
  file JSON         0.0005s / 0.0002s  慢 2x

1条 × 1MB 写入/读取：
  pyczan.shmem      0.0008s / 0.0005s  ← 基准
  Manager().dict()  0.0029s / 0.0019s  慢 4x / 慢 4x
  file pickle       0.0016s / 0.0005s  慢 2x / 持平
  file JSON         0.0021s / 0.0020s  慢 3x / 慢 4x
```

pyczan 大幅快于 Manager().dict()，与原始共享内存性能相当（但提供完整 dict API）。
小数据时比文件 IPC 快 2-3 倍，大数据时持平。

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
    print("注意：检测到上次异常退出，数据已被保留")
    print("如有需要可调用 d.Clear() 手动清空")

d.Close()
```

## 文档

| 文档 | 说明 |
|------|------|
| [API 参考](doc/api.md) | 完整 API 文档，含参数、返回值、异常 |
| [使用教程](doc/tutorial.md) | 从入门到生产，含多进程实战、性能调优 |
| [设计文档](doc/pyczan_shmem%20设计文档.md) | 架构设计、算法、数据布局（旧版，仅供参考） |

## 开发

从源码构建：

```bash
git clone https://gitee.com/weiyunnote/pyczan.git
cd pyczan
src\cpp\build_release.bat
pytest tests\test_shmem.py -v
```

需要 VS2022（MSVC v143）。

## 许可证

MIT
