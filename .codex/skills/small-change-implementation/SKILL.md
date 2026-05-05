---
name: small-change-implementation
description: Use this skill when implementing exactly one bounded change with strict scope control, validation, and diff summary.
---

# Small Change Implementation

只实现一个有明确任务卡的小范围变更。目标是最小 diff、明确验证、可回滚。

## 执行前

1. 读取 `AGENTS.md`。
2. 读取 `docs/ai/AI_GUARDRAILS.md`。
3. 确认任务卡包含目标、非目标、允许范围、禁止范围、风险等级、授权等级和验收命令。
4. 执行 `git status --short`。如果存在用户未提交修改，停止并说明。

## 范围控制

- 严格遵守允许修改范围。
- 不做无关 cleanup。
- 不删除测试。
- 不修改 public API，除非任务明确允许。
- 不修改 CI，除非任务明确允许。
- 不把环境失败写成代码通过。

## 实现后验证

1. 运行任务卡验收命令。
2. 至少运行 `git diff --check`。
3. 无法运行的命令必须写明原因。
4. 文档-only 任务不得声称业务构建或测试通过。

## 输出模板

```text
changed files：
behavior changes：
tests：
commands：
results：
known gaps：
commit：
```

## 停止条件

- 工作区出现未知用户修改。
- 需要越过禁止范围。
- 需要更高授权等级。
- 验收命令缺失且用户未确认替代命令。
