# pyczan

**高性能 Python 加速工具集** — 为 Python 开发者提供底层 C++ 扩展模块，解决 CPU 密集型和 I/O 密集型场景下的性能瓶颈。

## 模块

| 模块 | 版本 | 说明 |
|------|------|------|
| pyczan_shmem | v0.1.0 | 基于 Windows 共享内存的进程间字典（SharedMemoryDict） |

### pyczan_shmem

多进程共享内存字典，提供 dict-like 接口：

```python
from pyczan_shmem import SharedMemoryDict

d = SharedMemoryDict()
d.OpenOrCreate("config")
d["host"] = "127.0.0.1"
d["port"] = "8080"
print(d["host"])       # "127.0.0.1"
print(len(d))          # 2
print("host" in d)     # True
del d["port"]
d.Close()
```

## 环境要求

- Windows 10/11 64 位
- Visual Studio 2022 BuildTools（MSVC v143）
- Python >= 3.8
- nanobind 1.2.0（已预编译，见 `src/cpp/lib/nanobind-static.lib`）

## 快速开始

```bash
# 克隆仓库
git clone https://gitee.com/weiyunnote/pyczan.git
cd pyczan

# 构建 C++ 扩展（Release）
src\cpp\build_release.bat

# 运行测试
pytest tests\test_shmem.py -v
```

## 构建

```bash
# Release 构建
src\cpp\build_release.bat

# Debug 构建
src\cpp\build_debug.bat
```

构建产物：

| 产物 | 路径 |
|------|------|
| Python 扩展 | `src/py/pyczan_shmem/_pyczan_shmem.pyd` |
| 静态库（Release） | `src/cpp/lib/pyczan_shmem.lib` |
| 静态库（Debug） | `src/cpp/lib/pyczan_shmemd.lib` |
| 测试程序 | `src/cpp/build/bin/Release/test_shmem.exe` |

## 测试

```bash
# C++ 测试
src\cpp\build\bin\Release\test_shmem.exe

# Python 测试
pytest tests\test_shmem.py -v
```

## 许可证

MIT
