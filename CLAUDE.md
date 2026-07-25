# CLAUDE.md

本文档为 Claude Code (claude.ai/code) 在此仓库中工作时提供指导。

## 项目概述

`pyczan` 是一个 **C++ 高性能 Python 加速工具集**，为 Python 开发者提供底层 C++ 扩展模块，解决 Windows 平台上 CPU 密集型和 I/O 密集型场景的性能瓶颈。

- **阶段**：v0.1.0 首个模块 `pyczan_shmem`（共享内存字典）开发中。
- **平台**：仅 Windows 10/11 64 位（MSVC + Visual Studio 2022）。
- **Python**：>= 3.8。
- **作者**：zan (13667605889@139.com)
- **许可证**：MIT

## 架构

项目采用 **C++ / Python 目录分离** + **模块化 monorepo** 结构。

### 目录布局

```
pyczan/
├── src/
│   ├── cpp/                          ← C++ 独立项目
│   │   ├── include/pyczan_shmem/     ← 公共 C++ 接口头文件
│   │   ├── pyczan_shmem/             ← 模块源码
│   │   │   ├── shmem_dict.cpp        ← 核心实现（在静态库中）
│   │   │   ├── bindings.cpp          ← nanobind 绑定（在 .pyd 中）
│   │   │   └── test_shmem.cpp        ← C++ 测试（在 .exe 中）
│   │   ├── vc17/                     ← VS2022 项目文件
│   │   │   ├── pyczan_shmem.sln      ← 解决方案（3 个项目）
│   │   │   ├── pyczan_core/          ← 静态库项目
│   │   │   ├── _pyczan_shmem/        ← .pyd 项目
│   │   │   └── test_shmem/           ← 测试程序项目
│   │   ├── lib/                      ← 预编译依赖 + 构建产物
│   │   │   ├── nanobind/             ← 预编译的 nanobind 依赖
│   │   │   ├── pyczan_shmem.lib      ← Release 静态库
│   │   │   └── pyczan_shmemd.lib     ← Debug 静态库
│   │   ├── build_debug.bat
│   │   └── build_release.bat
│   │
│   └── py/                           ← Python 包
│       ├── pyczan/                   ← 命名空间包
│       │   └── __init__.py
│       └── pyczan_shmem/             ← C++ 扩展的 Python 封装
│           ├── __init__.py
│           └── _pyczan_shmem.pyd     ← 编译产物
│
├── tests/test_shmem.py               ← pytest 测试
├── pyproject.toml
├── CLAUDE.md
└── .gitignore
```

### 项目间关系

```
pyczan_core（静态库）                  pyczan_shmem.lib / pyczan_shmemd.lib
  └─ shmem_dict.cpp                    输出到 src/cpp/lib/
       │
       ├──→ _pyczan_shmem（.pyd）       _pyczan_shmem.pyd
       │     └─ bindings.cpp            输出到 src/py/pyczan_shmem/
       │     └─ 链接：pyczan_shmem.lib + nanobind-static.lib
       │
       └──→ test_shmem（.exe）          test_shmem.exe
             └─ test_shmem.cpp          输出到 src/cpp/build/bin/
             └─ 链接：pyczan_shmemd.lib（Debug）/ pyczan_shmem.lib（Release）
```

### 技术栈

| 层级 | 选型 |
|---|---|
| C++ 标准 | C++17 |
| 编译器 | MSVC（Visual Studio 2022，工具集 v143） |
| 构建系统 | MSBuild（.sln/.vcxproj） |
| Python 绑定 | nanobind 1.2.0（预编译静态库） |
| 共享内存 | Win32 API（CreateFileMapping + MapViewOfFile + CreateMutex）|
| Python 打包 | setuptools + build（pyproject.toml，PEP 517/518） |

### 共享内存设计

- 使用 Windows 原生 `CreateFileMapping` + `MapViewOfFile` 创建共享内存段
- 使用 `CreateMutex` 实现进程间同步
- 内部采用紧凑二进制序列化存储 key-value 对
- **无需任何第三方依赖**（nanobind 除外）
- **命名约定**：共享内存 `pyczan_shmem_<name>`，互斥体 `pyczan_shmem_mtx_<name>`

### 设计原则

- **C++ / Python 分离** —— C++ 源码在 `src/cpp/`，Python 在 `src/py/`，各自独立
- **功能单一** —— 每个模块只做一件事，做到极致性能
- **零多余依赖** —— 优先使用操作系统原生 API
- **易用性** —— Python API 与原生 Python 习惯保持一致

## 构建命令

### Release 构建

```bash
cd src/cpp
build_release.bat
```

或从项目根目录：

```bash
cmd /c "src\cpp\build_release.bat"
```

### Debug 构建

```bash
cd src/cpp
build_debug.bat
```

### 架构说明

构建时使用 `/p:BuildInParallel=false` 确保按顺序编译：先编译 `pyczan_core`（静态库），再编译 `_pyczan_shmem`（.pyd）和 `test_shmem`（.exe）。

Debug 模式下 `_pyczan_shmem.pyd` 使用 `/MD`（Release CRT）以匹配 Python 和 nanobind 的运行时，而 `pyczan_core` 和 `test_shmem` 使用 `/MDd`（Debug CRT）。`.pyd` 在 Debug 模式下链接 Release 静态库（`pyczan_shmem.lib`），其他项目链接 Debug 静态库（`pyczan_shmemd.lib`）。

### 输出产物

| 产物 | Release | Debug |
|------|---------|-------|
| 静态库 | `src/cpp/lib/pyczan_shmem.lib` | `src/cpp/lib/pyczan_shmemd.lib` |
| Python 扩展 | `src/py/pyczan_shmem/_pyczan_shmem.pyd` | 同上 |
| 测试程序 | `src/cpp/build/bin/Release/test_shmem.exe` | `src/cpp/build/bin/Debug/test_shmem.exe` |

## 测试

### C++ 测试

```bash
src\cpp\build\bin\Release\test_shmem.exe
```

### Python 测试

```bash
pytest tests/test_shmem.py -v
pytest tests/test_shmem.py::test_set_get -v          # 单个用例
```

### Python 手动验证

```python
from pyczan_shmem import SharedMemoryDict
d = SharedMemoryDict()
d.OpenOrCreate("test")
d["key1"] = "value1"
assert d["key1"] == "value1"
assert len(d) == 1
assert "key1" in d
del d["key1"]
assert "key1" not in d
d.Close()
```

## 发布到 PyPI

```bash
pip install build twine
rm -rf dist build *.egg-info
python -m build
twine upload dist/* --verbose
```

## 关键文档

- **`C++编码规范.md`** —— C++ 编码规范，生成 C++ 代码时**必须严格遵守**。
- **`项目开发说明书.md`** —— 完整项目说明书。
- **`上传到pypi步骤.txt`** —— PyPI 上传步骤。

## 重要约定

- **C++ 代码**须遵循 `C++编码规范.md`：PascalCase 类名/函数名、camelCase 变量/参数、`m_` 前缀成员、BSD/Allman 大括号风格。
- **Python 代码**遵循 PEP 8。
- **模块命名**：每个 C++ 扩展模块在 `src/cpp/` 下独立一个子目录，对应 Python 包在 `src/py/` 下。
- **nanobind** 用于 C++/Python 绑定（预编译静态库，位于 `src/cpp/lib/nanobind/`）。
- **公共接口头文件**放在 `src/cpp/include/<模块名>/` 下，供其他模块链接。
- **静态库命名**：Release 为 `<模块名>.lib`，Debug 为 `<模块名>d.lib`。
- **源码布局**：Python 包位于 `src/py/`（见 `pyproject.toml` 中 `where = ["src/py"]`）。
- **依赖关系**：`_pyczan_shmem` 和 `test_shmem` 均依赖 `pyczan_core` 静态库。
