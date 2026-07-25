---
name: docx-inspect
description: Inspect generated DOCX XML parts using unzip + grep
---

# DOCX 内部结构检查

检查生成的 `.docx` 文件中的 OOXML 部件内容。

## 常用命令

```bash
# 检查文档结构：sectPr 和 footer 引用数
cd E:/gitee/pdf2word/tests
unzip -p test.docx word/document.xml | grep -o "sectPr" | wc -l
unzip -p test.docx word/document.xml | grep -o "footerReference" | wc -l

# 统计段落数
unzip -p test.docx word/document.xml | grep -o "w:p>" | wc -l

# 查看页脚 XML
unzip -p test.docx word/footer1.xml

# 查看文档关系（页脚引用等）
unzip -p test.docx word/_rels/document.xml.rels

# 查看文档末尾（sectPr 区域）
unzip -p test.docx word/document.xml | tail -c 2000

# 查看文档开头（设置、字体等）
unzip -p test.docx word/document.xml | head -c 2000

# 搜索特定内容
unzip -p test.docx word/document.xml | grep -i "footer\|sectPr"

# 查看 DOCX 文件列表
unzip -l test.docx | grep -i footer

# 查看整个 document.xml（提取为 XML 文本再查看）
unzip -p test.docx word/document.xml > /tmp/document.xml
