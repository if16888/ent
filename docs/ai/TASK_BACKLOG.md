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

### ENT-201：docs/architecture/handle-lifecycle.md

- 状态：已完成（已落地）
- 说明：已补齐 `ENT_HANDLE` 生命周期架构文档，并与 README 的 handle-based 入口互相链接。

## 进行中

### ENT-012：ENT_HANDLE Init / Run / Stop / Close 闭环

- 状态：进行中（PR #20）
- 目标：让 `ENT_HANDLE` 的 `Init / Run / Stop / Close` 形成完整生命周期闭环，避免重复初始化，支持 stop 唤醒和 close 收口。
- 非目标：不改 DB、timer、SHM、socket 主逻辑，不恢复 `ENT_Runtime*`，不做旧 API 兼容。
- 风险：R3，涉及 public API、状态机、资源释放和并发关闭。
- 推荐授权等级：L2。
- 预期验证命令：
  - `git diff --check`
  - `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release`
  - `cmake --build build -j4`
  - `ctest --test-dir build --output-on-failure -R "test_ent_init|test_ent_msg|test_ent_log|test_ent_log_flush_deadline|test_ent_db|test_security|test_utl_thread|test_utl_tpool_integration|test_utl_timer|test_ent_shm"`

## P0

### ENT-004：Windows CI 路径回归可见性

- 目标：让 Windows CI 在 `test_ent_log` 路径相关失败时更容易定位问题，而不是新增重复的路径测试。
- 非目标：不重构 CI 矩阵，不改 log API，不重复拆分已有 `test_ent_log` 路径覆盖。
- 风险：R3，涉及 CI workflow、Windows shell、路径语义和失败诊断。
- 推荐授权等级：L1 impact-scan 后 L2 / L3。
- 预期验证命令：`git diff --check`、Windows `ctest --test-dir build -C Release --output-on-failure -R "test_ent_log|test_ent_log_flush_deadline"`、GitHub Actions Windows job。

### ENT-005：清点 public API 与内部 helper 的裸数字返回

- 目标：只读扫描库内裸数字返回和私有错误码，区分 public API、内部 helper、test/perf stub 三类返回语义，并标出真正需要收口的 public contract 缺口。
- 非目标：不直接替换代码，不新增消息码，不调整测试，不把测试桩或 perf harness 的哨兵返回当成产品缺陷。
- 风险：R2，扫描结果会影响后续返回语义治理，但主要是分类与收口优先级问题。
- 推荐授权等级：L1。
- 预期验证命令：`rg "return\\s+[-]?[0-9]+\\s*;" comm inc test`、`rg "ENT_.*_(FAILED|BAD|IN_USE|NON_FATAL|NORMAL)" comm inc test msg`。

## P1

### ENT-101：ENT_HANDLE 多实例并发/隔离边界测试补齐

- 目标：补齐 `ENT_HANDLE` 多实例并发运行、交错 stop / close、初始化失败、局部关闭、状态隔离和清理顺序测试。
- 非目标：不重构 handle 架构，不恢复 `ENT_Runtime*` public API。
- 风险：R3，涉及全局状态、生命周期和下游行为。
- 推荐授权等级：L1 impact-scan 后 L2。
- 预期验证命令：`git diff --check`、`cmake -S . -B build -DCMAKE_BUILD_TYPE=Release`、`cmake --build build -j4`、`ctest --test-dir build --output-on-failure`。

### ENT-102：db handle close 并发测试

- 目标：补齐 DB handle active operation 与 close 竞争、重复 close、service close 时 live handle 的并发测试。
- 非目标：不改 DB backend 连接策略，不引入新数据库依赖。
- 风险：R3，涉及并发、条件变量、数据库 backend 和资源释放。
- 推荐授权等级：L1 impact-scan 后 L2。
- 预期验证命令：`git diff --check`、`cmake -S . -B build -DCMAKE_BUILD_TYPE=Release`、`cmake --build build -j4`、`ctest --test-dir build --output-on-failure`。

### ENT-103：thread/tpool/timer 返回语义 review

- 目标：review thread / tpool / timer 的返回码、失败路径和生命周期语义，输出问题清单。
- 非目标：不直接修改实现，不扩展到 runtime / db / log。
- 风险：R2，可能发现跨模块返回语义不一致。
- 推荐授权等级：L1。
- 预期验证命令：`rg "return\\s+[-]?[0-9]+\\s*;" comm inc test`、`rg "UTL_|ENT_THRD|ENT_TPL|ENT_TMR" comm inc test msg`。

### ENT-104：README 增加 handle-based API 返回语义速查表

- 目标：在 README 中整理 handle-based public API 返回语义速查表。
- 非目标：不改代码、不新增消息码、不调整测试。
- 风险：R1，文档可能和实现不同步。
- 推荐授权等级：L2。
- 预期验证命令：`git diff --check`、文档链路 review。

### ENT-105：CI failure log artifact 可见性

- 目标：让 CI 在失败时上传关键构建、测试和安装日志，便于快速 triage。
- 非目标：不改构建逻辑，不改变失败判定。
- 风险：R3，涉及 workflow 和 artifact 权限。
- 推荐授权等级：L1 impact-scan 后 L2 / L3。
- 预期验证命令：`git diff --check`、GitHub Actions dry review、失败 job artifact 验证。

## P2

### ENT-202：docs/architecture/log-lifecycle.md

- 目标：补齐 log 生命周期文档，说明 service-level 和 handle-level close / flush 语义，以及当前 handle-based API 的收口边界。
- 非目标：不统一返回码，不修改 log 实现。
- 风险：R1。
- 推荐授权等级：L2。
- 预期验证命令：`git diff --check`、人工 review。

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
