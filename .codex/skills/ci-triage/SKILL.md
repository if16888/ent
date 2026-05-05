---
name: ci-triage
description: Use this skill when analyzing build, test, or GitHub Actions failures. It must classify the failure and propose the smallest safe fix.
---

# CI Triage

分析构建、测试或 GitHub Actions 失败。目标是定位第一处关键错误并提出最小安全修复。

## 规则

- 不先重构。
- 先定位第一处关键错误。
- 区分环境问题和代码问题。
- 区分 warning 和 error。
- 区分 configure、build、link、test、install、package 阶段。
- 给出最小复现命令。
- 不伪造 CI 通过。

## 分类

- 代码问题。
- 测试假设问题。
- 环境依赖问题。
- 平台差异问题。
- flaky 问题。
- 超时问题。
- 权限 / 路径问题。

## 输出模板

```text
CI 链接：
失败平台：
失败阶段：
第一处关键错误：
判断类型：
最小复现命令：
最小修复建议：
需要补充的测试：
未验证项：
```

## 修复原则

- 优先修根因，不扩大范围。
- 如果是环境问题，修 CI 环境或文档，不改业务逻辑。
- 如果是测试假设问题，收紧测试前提，不弱化核心断言。
- 如果是平台差异，明确 Windows / Linux 验证范围。
