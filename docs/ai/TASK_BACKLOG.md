# AI Task Backlog

本文档是 ent 后续 AI coding agent 任务的第一版 backlog。任何任务执行前都必须先生成任务卡；高风险模块先做 impact-scan。

## 已完成 / 已关闭

### ENT-001：ent_log 返回码统一到 ent.msg

- 状态：已完成（PR #9）
- 说明：`ent_log` 成功返回路径已统一到 `ENT_SYS_NORMAL`，并完成对应 impact-scan、实现、验证和合并。

### ENT-002：test_ent_log 增加 flush/close 边界测试

- 状态：已完成（PR #10）
- 说明：`test_ent_log` 已补齐 flush / close / ctx / interval=0 边界测试。

### ENT-003：ent_shm 生命周期边界硬化

- 状态：已完成（已收口）
- 说明：`ent_shm` 的生命周期边界测试已补齐，当前不再作为待实现任务。

### ENT-009：重审 ENT_Init / ENT_RuntimeInit 多实例收口边界

- 状态：已完成（impact-scan）
- 说明：已完成只读扫描，为后续 handle-based 收口提供了影响面与风险边界。

### ENT-010：收口 ENT_Init 为多实例入口并压缩函数长度

- 状态：已完成（PR #19）
- 说明：已删除对外 `ENT_Runtime*` public API，`ENT_Init` / `ENT_Close` / `ENT_Run` / `ENT_SetRtAttributes` 成为 handle-based 对外入口。

### ENT-011：清理旧 Runtime API 并同步文档与测试

- 状态：已完成（PR #19）
- 说明：README、example、test/downstream_consumer 与 public header 已同步到 handle-based API。

### ENT-012：ENT_HANDLE Init / Run / Stop / Close 闭环

- 状态：已完成（PR #20, PR #21）
- 说明：`ENT_HANDLE` 的 `Init / Run / Stop / Close` 生命周期闭环已完成并合并，包含重复初始化拒绝、stop 唤醒、close 收口和并发边界修复。

### ENT-102：db handle close / reinit 并发边界测试补齐

- 状态：已完成（已落地）
- 说明：`test_ent_db` 已补齐 close 进行中对 active operation、reinit、重复 close、service close 的并发边界测试。

### ENT-103：thread/tpool/timer 返回语义 review

- 状态：已完成（review 完成，无立即实现缺口）
- 说明：已完成对 thread / tpool / timer public wrapper 返回码、失败路径和生命周期语义的只读 review，当前未发现必须立即修改实现的 public contract 缺口。

### ENT-201：docs/architecture/handle-lifecycle.md

- 状态：已完成（已落地）
- 说明：已补齐 `ENT_HANDLE` 生命周期架构文档，并与 README 的 handle-based 入口互相链接。

### ENT-104：README 增加 handle-based API 返回语义速查表

- 状态：已完成（已落地）
- 说明：README 已补齐 handle-based public API 的返回语义速查表，并与 `docs/log-return-codes.md` 互相引用。

### ENT-202：docs/architecture/log-lifecycle.md

- 状态：已完成（已落地）
- 说明：已补齐 log 生命周期文档，说明 service-level 和 handle-level close / flush 语义，以及当前 handle-based API 的收口边界。

### ENT-101：ENT_HANDLE 多实例并发/隔离边界强化

- 状态：已完成（基础覆盖已存在，暂无立即实现缺口）
- 说明：`ENT_HANDLE` 的多实例初始化、独立运行、独立关闭、失败隔离、stop / close 闭环和 stale handle 拒绝已在代码、测试和 README 中收口；当前剩余更偏向更细并发边界强化，不再作为阻塞性待办。

### ENT-004：Windows CI 路径回归可见性

- 状态：已完成（已合并到 `master`，见 PR #22 / PR #23）
- 说明：Windows CI 已补齐 `test_ent_log` 路径相关失败的日志收集与 artifact 上传，路径回归的可见性和定位能力已收口。

### ENT-005：清点 public API 与内部 helper 的裸数字返回

- 状态：已完成（review 完成，无立即实现缺口）
- 说明：已完成对库内裸数字返回和私有错误码的只读扫描，当前未发现必须立即修改实现的 public contract 缺口。

### ENT-015：全库句柄生命周期审计与 runtime 所有权设计

- 状态：已完成（文档 + 设计）
- 说明：已完成对 runtime / log / db / timer / thread / tpool / shm / socket / lock / cv 的句柄生命周期审计，并输出 runtime 所有权迁移设计。

### ENT-017：ENT_HANDLE close concurrency contract hardening

- 状态：已完成（PR #28，contract 冻结；无 registry）
- 说明：已冻结 `ENT_Close()` 与 `ENT_Run` / `ENT_Stop` / `ENT_SetRtAttributes` 的并发契约；只承诺等待已进入运行路径的调用退出，不承诺 close 开始后的新入口任意并发安全。

### ENT-022：ent_shm lifecycle contract

- 状态：已完成（PR #29，contract 冻结；无 registry）
- 说明：已冻结 `ENT_SharedMap` caller-synchronized 生命周期契约；只承诺顺序关闭和置空，不承诺 `Ptr` / `Size` / `Flush` 与 `Close` 并发安全。

### ENT-026：fgn / protocol-analyzer reuse validation

- 状态：已完成（计划 + 只读扫描）
- 说明：已完成 fgn / protocol-analyzer 复用验证计划和 fgn 只读扫描；结论是 `ent_log` 是 fgn 第一原型候选，`ent_msg` 是规约分析工具第一候选，`ent_shm` 是次级候选。

### ENT-027：fgn ent_log minimal prototype plan

- 状态：已完成（原型前计划）
- 说明：已完成 fgn `ent_log` 最小原型前评估，输出候选方案、推荐方案、构建影响、验收标准和停止条件。

## 进行中

## P1

### ENT-016：Runtime resource manager design review

- 目标：把 `ENT_CTX` 作为 runtime owner 的资源注册器设计成可实现的最小模型。
- 非目标：不实现 registry，不修改 public API。
- 风险：R2。
- 推荐授权等级：L1。
- 预期验证命令：`git diff --check`、人工 review。

### ENT-018：ent_log ctx lifecycle hardening

- 目标：审计并收口 `ENT_LOG_CTX` 的 owner-context 生命周期、close/free 边界和 writer/callback 竞争。
- 非目标：不统一 log 返回码，不重构日志格式化路径。
- 风险：R3。
- 推荐授权等级：L1 / L2。
- 预期验证命令：`git diff --check`、`ctest --test-dir build --output-on-failure -R "test_ent_log"`.

### ENT-019：timer lifecycle hardening

- 目标：收紧 `UTL_TimerDelete` / `UTL_TimerClose` / callback worker / RT worker 的生命周期边界。
- 非目标：不改 timer 调度策略，不改公开 timer 类型。
- 风险：R3。
- 推荐授权等级：L1 / L2。
- 预期验证命令：`git diff --check`、`ctest --test-dir build --output-on-failure -R "test_utl_timer"`.

### ENT-020：db init/close edge cleanup

- 目标：继续清理 DB init/close / reinit / service-close 边界，并为 runtime owner 迁移保留接口形状。
- 非目标：不改 backend 连接策略，不新增数据库依赖。
- 风险：R3。
- 推荐授权等级：L1 / L2。
- 预期验证命令：`git diff --check`、`ctest --test-dir build --output-on-failure -R "test_ent_db"`.

### ENT-021：ent_thread lifecycle contract

- 目标：明确 `ENT_THREAD` 的 join/close 语义、owner-thread 责任和并发边界。
- 非目标：不把线程服务改成 runtime owner。
- 风险：R2。
- 推荐授权等级：L1。
- 预期验证命令：`git diff --check`、`ctest --test-dir build --output-on-failure -R "test_ent_thread"`.

## P2

### ENT-023：runtime-aware API migration plan

- 目标：为 `ENT_DbInitHandleEx` / `ENT_LogInitHandleEx` / `UTL_TimerCreateEx` / `UTL_TPoolInitEx` / `ENT_SharedMapOpenEx` 设计迁移路线。
- 非目标：不立即实现所有 Ex API。
- 风险：R2。
- 推荐授权等级：L1。
- 预期验证命令：`git diff --check`、人工 review。

### ENT-024：resource registry implementation prototype

- 目标：基于 `ENT_CTX` 做一个最小的资源注册/析构原型。
- 非目标：不做全量 API 迁移，不把所有句柄改成 `ENT_HANDLE`。
- 风险：R3。

### FGN-ENT-001：fgn ent_log minimal prototype

- 目标：基于 `ENT-027` 推荐方案，在 `fgn` 中实现一个最小 `ent_log` 复用原型。
- 非目标：不改 ent public API，不引入 runtime registry，不迁移 `ent_shm` / `ent_msg`，不全量重构 logging。
- 风险：R3。
- 推荐授权等级：L1 / L2。
- 预期验证命令：fgn Windows build、fgn smoke、日志输出检查、回滚检查。

### ENT-203：benchmark/report 结构预研

- 目标：设计 ent runtime / log / thread / shm benchmark 报告结构，包括 CSV、markdown、环境元数据和复现命令。
- 非目标：不引入 benchmark 框架，不提交性能结论。
- 风险：R1。
- 推荐授权等级：L1 / L2。
- 预期验证命令：`git diff --check`、样例报告人工 review。

### ENT-204：fgn 复用 ent_shm 的边界分析

- 目标：分析 fgn 未来复用 ent_shm / log / msg / runtime 思想的边界、依赖方向和不可复用点。
- 非目标：不修改 fgn，不迁移代码，不改变 ent public API。
- 风险：R2，涉及跨仓库架构判断。
- 推荐授权等级：L1。
- 预期验证命令：只读分析命令、引用证据清单、人工 review。
