---
name: build-run
description: Build and run pdf2word conversion for testing
---

# 构建和运行

## 构建

```bash
cd e:/gitee/pdf2word

# Debug 构建（自动调用 VS2022 msbuild）
cmd.exe //c "e:\gitee\pdf2word\build_debug.bat"

# Release 构建
cmd.exe //c "e:\gitee\pdf2word\build_release.bat"
```

## 运行测试

```bash
cd e:/gitee/pdf2word

# Debug 模式
cmd.exe //c "e:\gitee\pdf2word\pdf2word-cpp\bin\pdf2wordd.exe e:\gitee\pdf2word\tests\T05_tagged_table.pdf e:\gitee\pdf2word\tests\T05_tagged_table.docx 2>&1"

# Release 模式
cmd.exe //c "e:\gitee\pdf2word\pdf2word-cpp\bin\pdf2word.exe e:\gitee\pdf2word\tests\test.pdf e:\gitee\pdf2word\tests\test.docx 2>&1"
```

## 查看日志

```bash
# 最新日志
tail -50 e:/gitee/pdf2word/pdf2word-cpp/bin/log/pdf2word.log

# 搜索特定内容（表格或标签相关）
grep -E "table|Table|T05|模块链|标签|表格|Cell|MCID|mcid" \
  e:/gitee/pdf2word/pdf2word-cpp/bin/log/pdf2word.log
```
```
