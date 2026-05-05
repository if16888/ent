---
name: task-intake
description: Use this skill when converting a user request into a bounded engineering task card before any repository modification.
---

# Task Intake

将用户请求转换成可执行、可审计、可回滚的 ent 工程任务卡。使用本 skill 时不得修改代码或文档。

## 必读约束

1. 先读取 `AGENTS.md`。
2. 先读取 `docs/ai/AI_GUARDRAILS.md`。
3. 不修改文件，不提交，不创建分支。

## 工作步骤

1. 提炼目标：说明要交付什么可验证结果。
2. 写清非目标：明确本轮不做什么，避免范围膨胀。
3. 列出允许修改范围和禁止修改范围。
4. 判断风险等级：R0 到 R3。
5. 判断授权等级：L0 到 L4，ent 当前禁止 L5。
6. 补出验收命令；缺少验收标准时必须给出建议。
7. 如果请求过大，拆成多个阶段，每阶段只闭环一个主题。
8. 如果涉及高风险模块，建议先执行 `impact-scan`。

## 任务卡模板

```text
任务名称：
目标：
背景：
非目标：
允许修改范围：
禁止修改范围：
风险等级：
授权等级：
具体行动：
验收命令：
提交要求：
失败处理：
未验证项：
复盘点：
```

## 输出要求

- 不要直接实现。
- 不要把建议验收命令写成已执行。
- 明确哪些信息来自用户，哪些是 Codex 补充建议。
- 对不明确或高风险边界给出需要用户确认的问题。
