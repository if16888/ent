# Codex Workflow

本文档定义 ent 仓库中 Codex / AI coding agent 的标准工作流。目标是让每次修改都有明确边界、验证命令、review gate 和可回滚证据。

## 0. 前置原则

- 没有验收命令的任务不应执行。
- 没有禁止范围的任务容易失控。
- 没有未验证项说明的结果不可信。
- Codex 不能自己定义完成标准后自证完成。
- 任务越高风险，越要拆分成多个小闭环。

## 1. Task Intake

将用户请求转换成任务卡。任务卡必须明确目标、非目标、范围、风险、授权等级和验收命令。

输出物：

- 任务卡。
- 建议风险等级。
- 是否需要先 impact-scan。
- 是否需要人工确认。

## 2. Impact Scan

对高风险模块或不确定影响面执行只读扫描。不得修改文件，不得提交。

扫描内容：

- 相关 public header。
- 相关实现函数。
- 相关测试。
- CMake / CI 入口。
- `msg/ent.msg` 与返回码使用点。
- 下游调用方或 example。

输出物：

- 影响面表格。
- 调用链。
- 风险点。
- 推荐分阶段计划。
- 验证命令。

## 3. Implementation

仅实现任务卡允许范围内的一个小变更。

要求：

- 不做无关 cleanup。
- 不扩大模块范围。
- 不修改 public API，除非任务明确允许。
- 不删除测试。
- 修改生命周期代码必须补测试。
- 修改错误码必须同步 `msg/ent.msg`、调用点、测试和文档。

## 4. Local Validation

执行任务卡中的验收命令。命令不能运行时，必须说明原因和环境限制。

默认业务改动验证：

```bash
git diff --check
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j4
ctest --test-dir build --output-on-failure
git status --short
```

文档与 skills 变更可使用轻量验证，但必须说明未运行业务构建和测试。

## 5. Diff Summary

总结 diff，而不是复述意图。

最小内容：

- changed files。
- behavior changes。
- tests added / changed。
- commands run。
- pass / fail / not run。
- known gaps。

## 6. Review Gate

对 branch 或 diff 做独立 review。默认只读，不改文件。

关注重点：

- API / ABI 稳定。
- 生命周期释放和失败路径。
- 返回码语义。
- 并发 close / flush / free。
- Windows / Linux 差异。
- 测试是否覆盖边界和失败路径。

## 7. CI Gate

CI 失败必须先 triage，不允许直接大改。

处理顺序：

1. 找失败 job。
2. 找第一个真实错误。
3. 区分 warning 和 error。
4. 区分编译、链接、测试、环境失败。
5. 找最小复现命令。
6. 提出最小修复。

## 8. Decision Log

影响长期工程治理、API 语义、CI 策略、错误码体系的决策必须记录到 `docs/ai/DECISIONS.md`。

记录内容：

- 日期。
- 背景。
- 决策。
- 后果。
- 后续复盘点。

## 9. Retrospective

每个较大任务结束后进行简短复盘：

- 哪个验证命令最有效。
- 哪个风险点被低估。
- 是否需要新增 guardrail。
- 是否需要拆分 backlog。
- 是否需要更新 `.codex/skills`。

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

## 结果输出模板

```text
当前分支：
修改文件清单：
行为变化：
测试变化：
执行过的命令：
命令结果：
通过项：
失败项：
未验证项：
是否已提交：
commit hash：
下一步建议：
```
