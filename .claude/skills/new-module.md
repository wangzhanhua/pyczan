---
name: new-module
description: 添加一个新的 C++ 扩展模块的步骤
---

# 添加新 C++ 扩展模块

## 步骤

1. 在 `src/cpp/` 下创建模块源码目录 `src/cpp/<模块名>/`，放入 `.cpp` 文件
2. 在 `src/cpp/include/<模块名>/` 下放入公共接口头文件 `.hpp`
3. 在 `src/py/<模块名>/` 下创建 Python 包，包含 `__init__.py`
4. 在 `src/cpp/vc17/` 下创建 `.vcxproj` 项目文件：
   - 若为核心库（供其他模块链接），配置类型为 `StaticLibrary`
   - 若为 Python 扩展，配置类型为 `DynamicLibrary`，扩展名 `.pyd`
   - 若为测试程序，配置类型为 `Application`
5. 在 `src/cpp/vc17/pyczan_shmem.sln` 中添加新项目
6. 更新 `pyproject.toml` 中 `[tool.setuptools.packages.find]` 的 `include` 模式（如需要）

## 静态库命名

- Release：`<模块名>.lib`
- Debug：`<模块名>d.lib`（加 `d` 后缀）

## 参考

见 `src/cpp/vc17/pyczan_core/` 的配置。
