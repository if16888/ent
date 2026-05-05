# ent AI Engineering Control Loop

本文档是 ent 仓库的 AI 工作宪法。任何 Codex 或 AI coding agent 在本仓库内执行任务时，必须先理解本文件，再结合 `docs/ai/*` 与 `.codex/skills/*` 执行。

## 项目定位

`ent` 是一个 C 语言公共库，面向跨平台基础能力复用。当前重点模块包括 runtime / 多实例运行时、log 日志生命周期、`msg/ent.msg` 返回码与消息码体系、thread / tpool / timer、db handle 生命周期，以及后续计划中的 `ent_shm` 共享内存公共库。

优先级顺序：

1. API 语义清晰且可迁移。
2. ABI 稳定，不无意破坏下游二进制兼容。
3. Windows / Linux 跨平台行为一致或差异可解释。
4. 资源生命周期可证明，init / close / flush / free 路径可审计。
5. 错误码和返回语义统一，避免裸数字和私有错误码扩散。
6. 测试完整，尤其覆盖失败路径、重复调用、边界条件和并发关闭。

## AI 工作总规则

- 默认不直接修改 `master` / `main`；需要在主分支提交时，必须有用户明确授权。
- 默认不 force push。
- 默认不删除、跳过或弱化测试来换取通过。
- 默认不扩大任务范围，不做跨模块顺手优化。
- 默认不把环境失败说成代码通过。
- 每轮任务必须明确目标、非目标、允许修改范围、禁止修改范围、风险等级、授权等级和验收命令。
- 每轮只做一个可审计、可验证、可回滚的小闭环。
- 修改 public API 必须同步 `inc/` public header、测试和文档。
- 修改生命周期相关代码必须补测试，至少覆盖成功路径、失败路径、重复 close / flush / free 和资源释放。
- 修改 Windows / Linux 差异路径必须说明已验证平台和未验证平台。
- 修改 `msg/ent.msg` 或生成消息码使用点时，必须同步本文档中的 Message Code Rules。
- 高风险模块实施前必须先做只读 impact-scan。
- review 与实现尽量分离；AI 自述不是事实，diff、测试和 CI 才是证据。
- CI 是最终事实裁判；本地验证不能替代失败 CI 的 triage。

## 默认验证命令

业务代码改动的默认验证命令：

```bash
git diff --check
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j4
ctest --test-dir build --output-on-failure
git status --short
```

文档、skills 或治理文件改动可以使用任务卡指定的轻量验收命令，但最终输出必须明确说明未运行业务构建与测试的原因。

如果本地环境缺少 `cmake`、编译器、`nmake`、shell、数据库开发库或 Windows toolchain，必须明确说明环境限制，不得伪造通过，不得把“未执行”写成“通过”。

## Windows Compiler Environment

Windows native build 必须保持 toolchain 环境 shell-local：

- 不永久把 Visual Studio toolchain 目录加入 `PATH`。
- x64 构建使用 `VsDevCmd.bat -arch=amd64 -host_arch=amd64` 或 `vcvars64.bat`。
- x86 构建使用 `VsDevCmd.bat -arch=x86 -host_arch=x86` 或 `vcvars32.bat`。
- 如果当前 shell 中没有 `cl` 或 `nmake`，说明对应架构 toolchain 未初始化。
- 优先使用仓库本地 helper scripts，但不得污染全局环境。

## 高风险模块

以下改动必须先做 impact-scan，并在最终输出中单独说明风险和验证范围：

- runtime 生命周期、多实例状态隔离、全局状态拆分。
- log 初始化、关闭、flush、buffer thread 和默认 handle 生命周期。
- `ent.msg` 返回码语义、生成码、调用方返回判断。
- db handle 生命周期、close 并发、active operation 计数。
- thread / tpool / timer 并发、锁、条件变量、实时调度。
- shm / mmap / file mapping / 持久化映射和跨进程资源释放。
- Windows / Linux 文件路径、权限、文件句柄、映射句柄和换行差异。
- CI 矩阵、平台依赖、artifact、缓存和 release packaging。
- public header、导出符号、API / ABI 兼容性。

## Repository Structure

- `comm/`：核心库实现，包括初始化、日志、线程、数据库、socket、timer、thread pool 等。
- `inc/`：public headers，使用 `ent_*.h` 命名并暴露库 API。
- `example/`：示例消费者程序。
- `test/`：测试目标，新增测试应按模块命名，例如 `test_ent_log.c`。
- `msg/`：消息码定义，`msg/ent.msg` 是返回码语义的源头。
- `3rd/`：第三方依赖或源码，Windows 历史依赖较多，改动需说明平台影响。
- `docs/ai/`：AI 工程治理文档。
- `.codex/skills/`：repo-local Codex skills，只服务本仓库闭环。
- `scripts/ai/`：后续 AI 辅助脚本入口，当前不引入额外依赖。

## Coding Style

- C 代码保持 4 空格缩进。
- 函数和控制块大括号独占一行。
- 保持邻近代码的 `if(...)` / `for(...)` 紧凑风格。
- public API 使用 `ENT_` 或 `UTL_` 前缀。
- internal helper 使用 `i` 前缀，例如 `iENT_CTXFree`。
- 类型使用 upper snake case，例如 `MSG_ID_T`。
- source / header 配对保持 `ent_*` 或 `utl_*` 命名。
- 仓库无统一 formatter，修改时以相邻文件风格为准。

## Message Code Rules

编辑 `msg/ent.msg` 或生成消息码使用点时必须遵守：

- `module` 和 `submodule` 必须大写，长度不得超过 4。
- `module` 优先使用 3 个字符，便于长期一致性。
- message symbol 保持 `ENT_<SUBMODULE>_<NAME>` 约定。
- 当前 ENT submodules：`SYS`、`INIT`、`RT`、`THRD`、`UTHD`、`SOCK`、`TMR`、`TPL`、`DLL`、`SCR`、`DBS`、`LOG`。
- `SCR` 预留给 script runtime 生命周期与执行错误。
- `DBS` 预留给 database service 与 backend 生命周期错误。
- 参数化 DB API 也使用 `DBS` submodule；扩展 DB surface 时保持 `BAD_PARAMS`、`PARAM_COUNT`、`PREPARE_FAILED`、`BIND_FAILED`、`EXEC_FAILED` 与 `msg/ent.msg` 对齐。
- `LOG` 预留给 log service 与 handle 生命周期错误。
- 任何 message-code 变更，包括新增、删除、重命名 submodule、code 或 text，都必须在同一变更集中同步本文档。

## Testing Guidelines

- 不允许删除测试来规避失败。
- 修改生命周期、错误码、并发或跨平台路径时必须新增或更新测试。
- 没有 active test suite 的区域，至少使用现有 example 或相邻测试建立 smoke test 证据。
- 新增测试优先放在 `test/`，通过 CMake / CTest 接入。
- 如果测试因环境缺失无法运行，必须输出环境限制、未验证项和建议的 CI 验证范围。

## Commit And PR Rules

- 提交必须聚焦单一闭环。
- commit message 使用简洁祈使句，例如 `fix ent_log close return code`。
- 不 amend 用户已有 commit，除非用户明确要求。
- 不 merge PR，除非用户明确授权且治理文档允许。
- PR 描述必须包含行为变化、验证命令、平台影响、未验证项和 rollback 路径。

## Codex 输出要求

每个任务最终输出必须包含：

- 修改文件清单。
- 行为变化。
- 测试变化。
- 执行过的命令。
- 通过项。
- 失败项。
- 未验证项。
- 是否已提交以及 commit hash。
- 下一步建议。

不得使用“应该通过”“看起来没问题”替代证据。没有运行的命令必须列为未验证项。
