# AI Engineering Decisions

本文档记录 ent 仓库 AI Engineering Control Loop 的长期决策。格式参考 ADR，但保持轻量。

## 0001：建立 AI Engineering Control Loop

- 日期：2026-05-05
- 背景：ent 后续会持续演进 runtime、log、msg、thread、db、shm 和 CI。AI coding agent 参与开发时，如果缺少任务边界、验证命令和 review gate，容易产生不可审计的大范围修改。
- 决策：建立 `AGENTS.md`、`docs/ai/*` 和 `.codex/skills/*`，要求每轮任务具备目标、非目标、允许范围、禁止范围、风险等级、授权等级、验收命令、未验证项和输出模板。
- 后果：后续 AI 修改必须以小闭环推进，文档和 skills 会成为默认治理入口。
- 后续复盘点：观察 ENT-001 到 ENT-005 执行时，现有模板是否足够约束影响面和验证范围。

## 0002：ent 当前禁止 AI 自动合并

- 日期：2026-05-05
- 背景：ent 是 C 公共库，API / ABI、生命周期、跨平台和 CI 都存在高风险。自动合并会绕过人工判断和 release 风险控制。
- 决策：L5 自动合并级在 ent 当前禁止使用。AI 可以在明确授权下提交分支或创建 PR，但不能 merge。
- 后果：合并必须由人类维护者执行，AI 最终输出必须给出 Go / No-Go / Go with conditions 建议和未验证项。
- 后续复盘点：当测试覆盖、CI 矩阵和 release gate 足够成熟后，再评估是否允许更高自动化等级。

## 0003：高风险模块必须先 impact-scan

- 日期：2026-05-05
- 背景：runtime、log、`ent.msg`、db handle、thread / tpool / timer、shm、public header 和 CI 具有跨模块影响，直接改实现容易遗漏调用方和测试。
- 决策：高风险模块任务进入实现前必须执行只读 impact-scan，输出影响面表格、调用链、风险等级、分阶段计划和验证命令。
- 后果：高风险任务会增加前置分析成本，但能降低回滚和 CI 失败成本。
- 后续复盘点：如果 impact-scan 输出过重或过轻，根据实际任务调整表格字段和扫描命令。

## 0004：CI 与测试结果优先于 AI 自述

- 日期：2026-05-05
- 背景：AI 可能根据静态推断给出“应该通过”的判断，但 C 跨平台构建、链接和运行时行为必须由实际命令验证。
- 决策：CI、CTest、本地构建、`git diff --check` 等可复现证据优先于 AI 自述。未运行的命令必须列为未验证项。
- 后果：最终输出需要包含执行过的命令、结果、失败项和未验证项，不允许伪造通过。
- 后续复盘点：根据 CI 失败案例补充 `docs/ai/CI_TRIAGE.md` 和 `.codex/skills/ci-triage`。
