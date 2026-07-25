---
name: debug-pipeline
description: Add temporary logging to debug data flow in the conversion pipeline
---

# 调试日志添加

在转换流水线中临时添加日志来跟踪数据流。

## 在 GenerateDocumentXml 中打印 totalPages

```cpp
// 在 docx_writer.cpp 中，找到这一行：
int totalPages = static_cast<int>(model.pageMargins.size());
// 在后面添加：
ZH_LOG_INFO("GENDBG: totalPages=%d, pageMargins.size=%zu, blocks=%zu, pageFooters=%zu",
            totalPages, model.pageMargins.size(), model.blocks.size(),
            model.pageFooters.size());
```

## 在 WriteDocx 中添加日志

```cpp
// 在 pipeline_scheduler.cpp WriteDocx 函数中
ZH_LOG_INFO("WriteDocx: pageCount=%d, margins=%zu, footers=%zu",
            model->pageCount, model->pageMargins.size(),
            model->pageFooters.size());
```

## 检查模型 JSON

Debug 模式会输出 `test_model.json`，包含完整的 DocumentModel 数据：

```bash
# 查看页脚数据
grep "pageFooters" E:/gitee/pdf2word/tests/test_model.json | head -c 200

# 查看页边距数据
grep "pageMargins" E:/gitee/pdf2word/tests/test_model.json | head -c 300

# 查看完整 JSON
cat E:/gitee/pdf2word/tests/test_model.json | python3 -m json.tool 2>/dev/null || cat E:/gitee/pdf2word/tests/test_model.json
```

## 检查 PageContent JSON

Debug 模式会在 AnalyzeDocument 前调用 PageDeconstructor，输出 `test_content.json`：

```bash
# 查看页面内容概要
grep -o '"pageIndex":[0-9]*' E:/gitee/pdf2word/tests/test_content.json | sort | uniq -c
```
