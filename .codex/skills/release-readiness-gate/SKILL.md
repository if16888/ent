---
name: release-readiness-gate
description: Use this skill when checking whether a branch, PR, or version is ready to merge or release.
---

# Release Readiness Gate

检查分支、PR 或版本是否具备 merge / release 条件。输出必须是 Go、No-Go 或 Go with conditions。

## 检查项

1. 任务目标是否完成。
2. 非目标是否保持未触碰。
3. 禁止范围是否被违反。
4. public API / ABI 是否有变化。
5. 测试是否覆盖行为变化。
6. 本地验证是否执行。
7. CI 是否通过。
8. 文档是否同步。
9. 未验证项是否明确。
10. rollback 路径是否清楚。

## 输出模板

```text
结论：Go / No-Go / Go with conditions
目标完成情况：
禁止范围检查：
API / ABI：
测试与 CI：
文档：
未验证项：
rollback 路径：
必须处理项：
可后续处理项：
```

## 判定规则

- 有 P0 问题时输出 No-Go。
- CI 失败且未明确为无关环境问题时输出 No-Go。
- 未验证项影响核心行为时输出 Go with conditions 或 No-Go。
- 只有文档或非行为变更时，也必须检查 diff 和未验证项。
