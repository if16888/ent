# Public Core Task Backlog

本文档只记录 `ent` 公开 core 的已完成基线、已知限制和可公开维护任务。功能边界以 [`OPEN_SOURCE_SCOPE.md`](../../OPEN_SOURCE_SCOPE.md) 为准。

本文件不是内部产品路线图，也不得记录主备、高可用、跨节点复制、高级共享内存、分布式同步、event-loop 集成、RPC 或其他闭源扩展的设计与计划。

## Backlog 使用规则

每个任务在实施前必须形成任务卡，至少包含：

- 目标与用户可见行为；
- 非目标与 scope check；
- 允许和禁止修改范围；
- API / ABI / 平台影响；
- 风险等级与失败回滚；
- 本地验证、CI 和 release 验收命令。

不符合公开范围的需求不进入本 backlog，也不以 placeholder API、stub、TODO 或“预研”形式保留。

## 已完成基线

### Runtime 与生命周期

- handle-based `ENT_Init` / `ENT_Run` / `ENT_Stop` / `ENT_Close` 生命周期已经形成闭环。
- 多实例初始化、失败隔离、独立停止和关闭已有测试覆盖。
- close 只等待已经进入运行路径的调用；close 开始后的新入口由调用方 owner lock / lifecycle lock 互斥。

### Log

- public 成功返回语义已统一到 `msg/ent.msg`。
- flush、close、writer 和 context ownership 的主要生命周期边界已有测试。
- service-level close 与 handle-level close 的责任已文档化。

### Database

- SQLite、MariaDB/MySQL、PostgreSQL 后端可以独立启用或关闭。
- DB handle 的 active / closing / closed 生命周期和 close/reinit 边界已有并发测试。
- 参数化 read/write API 是处理外部输入的推荐路径。

### Thread、timer 与 thread pool

- 基础 thread、lock、condition variable、thread pool 和 timer API 已接入跨平台构建与测试。
- 主要 init/close/join 和失败路径已有回归测试。

### Basic shared memory

- `ENT_SharedMap` 的公开契约已冻结为本机、caller-synchronized 的基础 mapping API。
- 顺序 close、handle invalidation、flush 和主要失败路径已有基础测试。
- 公开模块不承诺高级多写者、无锁、跨节点同步或复制能力。

### Build、CI 与发布

- Linux normal、ASan/UBSan、TSan 和 Windows x86/x64、x64 ASan 已形成标准 CI。
- no-db 与 SQLite-only 配置已有独立 workflow。
- CMake install、downstream `find_package` 和 release runtime/devel packaging 已接入验证。
- Apache-2.0、NOTICE 和 THIRD_PARTY_NOTICES 已进入发布包。

## 进行中

### PUB-001：Release SDK relocatability

- 状态：PR 验证中。
- 目标：使实际解压后的 devel SDK 可以分别构建并运行 shared/static consumer。
- 验收：导出 CMake metadata 不含 build tree、runner 或 `vcpkg_installed` 绝对路径；真实 archive consumer 必须通过。
- 非目标：不修改 public C API，不改变 DB 行为。

### PUB-002：Open-source scope closure

- 状态：进行中。
- 目标：让公开范围在 scope 文档、AI 规则、贡献入口和 backlog 中保持一致。
- 验收：公开仓库不包含闭源功能的实现、public API 占位、roadmap 或设计文档。
- 非目标：不删除已经属于 core 且有测试的基础模块。

## P1：公开 core 可靠性

### CORE-101：异步日志队列容量与过载语义

- 目标：为异步日志队列建立可配置上限、明确的拒绝/丢弃策略和可观测计数。
- 非目标：不改变日志格式，不引入外部 event loop。
- 风险：R3。
- 验收：容量边界、关闭期间提交、过载返回语义和资源释放测试。

### CORE-102：线程池任务队列容量

- 目标：为 thread pool queue 建立容量上限和确定的 backpressure / reject 语义。
- 非目标：不实现分布式调度、RPC worker 或跨节点任务复制。
- 风险：R3。
- 验收：满队列、关闭竞争、重复 close、worker failure 和无泄漏测试。

### CORE-103：基础 shared-memory 安全加固

- 目标：强化本机 mapping 的名称、路径、权限、大小校验和异常关闭行为。
- 非目标：不增加 multi-writer、RCU、version chain、transaction、replication 或 distributed synchronization。
- 风险：R3。
- 验收：Windows/Linux 基础 mapping、无效输入、权限失败、重复关闭和清理测试。

### CORE-104：数据库结果与输入资源上限

- 目标：对可控的结果 materialization、参数数量、字段长度和错误返回建立明确上限。
- 非目标：不实现 ORM、连接池集群、故障转移或在线复制。
- 风险：R3。
- 验收：超限拒绝、边界值、不同 backend 一致性和释放路径测试。

### CORE-105：公开 API 文档校准

- 目标：确保 public headers、README、architecture docs 和 tests 对生命周期、线程安全和可选后端描述一致。
- 非目标：不提前承诺未实现 API。
- 风险：R1。
- 验收：文档链接、示例构建和 public symbol 清单 review。

## P2：工程质量增强

### ENG-201：编译器 warning baseline

- 目标：逐步接入 GCC/Clang `-Wall -Wextra` 和 MSVC `/W4`，形成可维护的 warning baseline。
- 非目标：不在一个 PR 中全库重写风格。

### ENG-202：静态分析

- 目标：评估并接入适合 C 项目的 CodeQL 或等价静态分析。
- 非目标：不把第三方扫描结果直接等同于有效漏洞。

### ENG-203：覆盖率报告

- 目标：建立 Linux 测试覆盖率基线，识别未覆盖的失败和释放路径。
- 非目标：不以覆盖率数字替代行为测试质量。

### ENG-204：ABI 变化检查

- 目标：在进入更稳定版本前评估 public symbol / ABI diff 检查。
- 非目标：0.x 阶段不承诺 minor version ABI 不变。

### ENG-205：输入解析 fuzzing

- 目标：选择消息生成、配置解析或其他边界明确的公开输入面进行最小 fuzzing。
- 非目标：不引入私有协议或产品数据格式。

## 明确不进入公开 backlog 的事项

以下事项不在 public core 中立项、预研或预留 API：

- active/standby、primary/backup 和自动 failover；
- leader election、quorum、fencing、split-brain prevention；
- snapshot、journal、incremental 或 cross-node replication；
- advanced multi-writer / lock-free / RCU shared-memory engine；
- distributed synchronization 和工业实时状态复制；
- libevent、libev 或其他 event-loop integration；
- RPC、XDR、gRPC、service discovery、retry、flow control 或 streaming；
- 私有产品运维、许可、部署控制或内部项目复用计划。

这类需求应在独立私有环境中管理，依赖方向只能是私有扩展依赖公开 `ent` core。
