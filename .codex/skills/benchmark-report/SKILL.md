---
name: benchmark-report
description: Use this skill when adding or reviewing benchmark evidence, performance reports, CSV outputs, and reproducibility metadata.
---

# Benchmark Report

用于 ent 未来 runtime、log、thread、shm 等性能相关任务，也便于后续迁移到 fgn。

## 必须记录

- commit hash。
- OS、内核或 Windows 版本。
- CPU 型号和核心数。
- 内存容量。
- 磁盘类型和测试路径。
- 编译器、CMake、构建类型。
- benchmark 参数。
- 样本规模。
- warm / cold 条件。
- invalid samples 和剔除原因。

## 输出要求

- 输出 CSV，便于后续对比。
- 输出 markdown report，便于 review。
- 记录复现命令。
- 记录数据文件路径。
- 记录统计方法，例如 min / max / avg / median / p95。

## 结论规则

- 不用单次结果夸大结论。
- 不把不同机器结果直接比较成性能结论。
- 不隐藏 invalid samples。
- 性能结论必须可复现。
- 如果只有 smoke benchmark，只能写“冒烟结果”，不能写“性能提升”。

## Markdown 报告模板

```text
标题：
commit：
环境：
构建参数：
benchmark 参数：
CSV 路径：
样本数量：
invalid samples：
统计摘要：
结论：
未验证项：
复现命令：
```
