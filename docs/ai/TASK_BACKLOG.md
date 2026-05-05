# AI Task Backlog

本文档是 ent 后续 AI coding agent 任务的第一版 backlog。所有任务执行前必须先生成任务卡，必要时先做 impact-scan。

## P0

### ENT-001：ent_log 返回码统一到 ent.msg

- 目标：扫描并规划 `ent_log` 返回码从裸数字或私有错误码迁移到 `msg/ent.msg` 的影响面。
- 非目标：第一阶段不直接修改 log 业务代码，不调整 API 签名，不改 CI。
- 风险：R3，涉及返回语义、调用方兼容性、测试断言和消息码生成链路。
- 推荐授权等级：L1 impact-scan，后续实现再进入 L2。
- 预期验证命令：`git diff --check`、`rg "return -|return 0|return 1|ENT_LOG|ENT_SYS" comm inc test msg`、后续实现阶段运行 `ctest --test-dir build --output-on-failure`。

### ENT-002：test_ent_log 增加 flush/close 边界测试

- 目标：补齐 log flush / close 的重复调用、closing 状态、失败路径和资源释放测试。
- 非目标：不重构 log 实现，不改变 public API，不统一返回码。
- 风险：R2，可能暴露现有生命周期 bug。
- 推荐授权等级：L2。
- 预期验证命令：`git diff --check`、`cmake -S . -B build -DCMAKE_BUILD_TYPE=Release`、`cmake --build build -j4`、`ctest --test-dir build --output-on-failure`。

### ENT-003：ent_shm API skeleton

- 目标：新增 `ent_shm` public API skeleton、最小实现和编译接入，为后续共享内存能力建立边界。
- 非目标：不实现完整跨进程一致性、不做性能 benchmark、不提供复杂持久化语义。
- 风险：R3，涉及 public header、ABI、Windows / Linux mmap 差异和资源生命周期。
- 推荐授权等级：L1 impact-scan 后 L2 实现。
- 预期验证命令：`git diff --check`、`cmake -S . -B build -DCMAKE_BUILD_TYPE=Release`、`cmake --build build -j4`、`ctest --test-dir build --output-on-failure`。

### ENT-004：Windows CI 增加 test_ent_log 文件路径验证

- 目标：让 Windows CI 覆盖 `test_ent_log` 对文件路径、目录创建、路径分隔符的关键行为。
- 非目标：不重写 CI 矩阵，不调整第三方依赖策略，不改变 log API。
- 风险：R3，涉及 CI workflow、Windows shell、路径语义和 artifact。
- 推荐授权等级：L1 impact-scan 后 L2 / L3。
- 预期验证命令：`git diff --check`、本地 Windows `ctest --test-dir build -C Release --output-on-failure`、GitHub Actions Windows job。

### ENT-005：扫描裸 return -1/0/1 与私有错误码

- 目标：只读扫描库内裸数字返回和私有错误码，分类为 OK、invalid、busy、fatal、nonfatal 等候选语义。
- 非目标：不直接替换代码，不新增消息码，不调整测试。
- 风险：R2，扫描结果会影响后续返回语义治理。
- 推荐授权等级：L1。
- 预期验证命令：`rg "return\\s+[-]?[0-9]+\\s*;" comm inc test`、`rg "ENT_.*_(FAILED|BAD|IN_USE|NON_FATAL|NORMAL)" comm inc test msg`。

## P1

### ENT-101：runtime 多实例测试补齐

- 目标：补齐 runtime 多实例初始化失败、局部关闭、状态隔离和清理顺序测试。
- 非目标：不重构 runtime 架构，不改变 `ENT_RUNTIME` public API。
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
- 非目标：不直接修改实现，不扩大到 runtime / db / log。
- 风险：R2，可能发现跨模块返回语义不一致。
- 推荐授权等级：L1。
- 预期验证命令：`rg "return\\s+[-]?[0-9]+\\s*;" comm inc test`、`rg "UTL_|ENT_THRD|ENT_TPL|ENT_TMR" comm inc test msg`。

### ENT-104：README 增加 API 返回语义速查表

- 目标：在 README 或 docs 中整理 public API 返回语义速查表。
- 非目标：不改变代码、不新增消息码、不修改测试。
- 风险：R1，文档可能暴露现有不一致。
- 推荐授权等级：L2。
- 预期验证命令：`git diff --check`、文档链接检查、人工 review。

### ENT-105：CI artifact 上传失败日志

- 目标：让 CI 在失败时上传关键构建和测试日志，方便 triage。
- 非目标：不改构建逻辑，不改变测试断言。
- 风险：R3，涉及 workflow 和 artifact 权限。
- 推荐授权等级：L1 impact-scan 后 L2 / L3。
- 预期验证命令：`git diff --check`、GitHub Actions dry review、实际失败 job artifact 验证。

## P2

### ENT-201：docs/architecture/runtime.md

- 目标：补充 runtime 架构文档，说明单实例与多实例边界、资源归属和关闭顺序。
- 非目标：不改代码，不调整测试。
- 风险：R1。
- 推荐授权等级：L2。
- 预期验证命令：`git diff --check`、人工 review。

### ENT-202：docs/architecture/log-lifecycle.md

- 目标：补充 log 生命周期文档，说明 service-level 和 handle-level close / flush 语义。
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
