---
name: build-run
description: 构建和测试 pyczan C++ 扩展模块
---

# 构建和运行 pyczan

## Release 构建

```bash
src\cpp\build_release.bat
```

## Debug 构建

```bash
src\cpp\build_debug.bat
```

## C++ 测试

```bash
src\cpp\build\bin\Release\test_shmem.exe
```

## Python 测试

```bash
pytest tests\test_shmem.py -v
```
