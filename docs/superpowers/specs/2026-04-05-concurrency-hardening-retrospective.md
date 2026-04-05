# Concurrency Hardening Retrospective

## Scope

本轮连续开发主要覆盖 3 个并发相关阶段：

1. `ent_log` 日志句柄生命周期并发安全
2. `ent_thread` 线程等待与关闭语义收口
3. `utl_thread` 锁/CV 原语语义修正

目标不是做大规模重构，而是在保持现有公开 API 基本不变的前提下，优先收掉高风险并发缺陷，并用回归测试把行为固定下来。

## Technical Outcomes

### 1. Log Lifecycle

落点文件：

- `comm/ent_log.c`
- `test/test_ent_log.c`

已完成的收口：

- `ENT_LOG_CTX` 增加 `closing`、`activeWriters` 和 `closeCv`
- 写日志入口统一走 writer acquire/release
- `ENT_LogCloseHandle` 先拒绝新 writer，再等待活跃 writer 清零，最后销毁资源
- 补齐了 `ENT_LogError` 等早返回分支的 release 对称性
- 增加并发关闭/写日志回归测试

解决的问题：

- 写线程与关闭线程并发时的 use-after-free 风险
- 关闭路径与写路径之间的销毁竞争

### 2. Thread Lifecycle

落点文件：

- `comm/ent_thread.c`
- `test/test_ent_thread.c`

已完成的收口：

- POSIX `THREAD_DB` 从 `volatile isDone` 改成 `doneMutex + doneCv + finished`
- `ENT_ThreadWaitById` 改为真正等待线程完成，而不是 `UTL_Sleep(ms)` 轮询
- 当线程在超时窗口内先结束时，`ENT_ThreadWaitById(..., ms > 0)` 会直接成功返回
- `ENT_ThreadClose` 去掉默认 `pthread_cancel`，改为正常 `pthread_join`
- 补充“超时前完成直接成功”和“关闭不调用 `pthread_cancel`”的回归测试

解决的问题：

- 非同步完成标记带来的数据竞争
- 超时等待的近似轮询语义
- 默认强杀线程破坏清理路径的问题

### 3. Lock/CV Semantics

落点文件：

- `comm/utl_thread.c`
- `test/test_utl_thread.c`

已完成的收口：

- POSIX `UTL_CVInit` 增加条件变量属性初始化
- Linux 下条件变量显式使用 `CLOCK_MONOTONIC`
- `UTL_CVWait` 的 timed wait 改为合法 `timespec` 归一化
- `UTL_LockEnter/UTL_LockLeave` 对 `LOCK_RW_E` 不再默认按写锁处理，而是要求显式使用 `UTL_LockEnterEx/UTL_LockLeaveEx`
- 增加 CV timed wait deadline 归一化测试
- 增加 RW lock 必须显式声明读写模式的测试

解决的问题：

- timed wait 受 wall clock 跳变影响
- `tv_nsec` 溢出生成非法 deadline
- 泛型 lock API 暗含 RW 写锁默认语义

## Verification Status

本轮结束时，工作区验证结果为：

- `ctest --output-on-failure` 通过
- 重点子集 `test_ent_log`、`test_ent_thread`、`test_utl_thread` 通过

## Where Time Was Lost

### 1. Over-trusting intermediate implementation reports

子代理报告“完成”后，如果没有立刻做关键控制流抽查，就会把后面的验证成本放大。

最典型的是 `ent_log` 阶段：汇报里说 `CloseHandle` 已经等待 writer 清零，但本地查看源码后发现关键等待逻辑实际上没有真正落地。

结论：

- 子代理结果不能直接视为完成
- 必须先看关键 diff，再接受测试结果

### 2. Editing legacy C files without normalizing file-format risks first

`ent_log.c` 这类历史 C 文件对编码、换行风格、局部注释格式比较敏感。一次不受控的文本改写会把真正的逻辑修复淹没在大量 churn 里。

结论：

- 先确认文件编码和换行风格
- 优先做最小 patch
- 不对老文件做整段重排或整文件风格修复

### 3. Running verification commands in parallel

并行读文件、查 diff 问题不大，但并行跑 `ctest` 和单测会产生误导，尤其是测试二进制共享构建输出、共享临时文件或共享全局状态时。

结论：

- 验证阶段应顺序执行
- “单目标测试通过”之后再跑一次全量回归

### 4. Tests aimed too directly at platform internals

例如对 `CLOCK_MONOTONIC` 的验证，直接断内部 clock id 在某些平台上会不稳定；更好的方式是优先断“行为性质”，比如 deadline 归一化、超时窗口内完成后的返回语义。

结论：

- 黑盒优先
- 内部实现细节只在必要时断言

### 5. Mixing implementation progress with process exploration

一边修实现、一边试探性设计流程，会打断节奏。真正高效的是先把当前任务做到闭环，再抽离做流程复盘。

结论：

- 任务推进与流程总结分层处理
- 每个阶段先闭环，再总结

## Recommended Working Rules

后续继续做类似并发修复，建议固定为以下规则：

1. 每个子任务固定走 `红测 -> 最小实现 -> 单目标绿 -> 全量绿`
2. 任何子代理产出都必须本地抽查关键控制流
3. 遇到历史 C 文件，先确认编码/换行，再做编辑
4. 测试命令不并行跑，尤其是 `ctest`
5. 优先写黑盒测试，避免直接绑定平台内部实现细节
6. 单个阶段只收一个明确问题，不在同一步里夹带重构

## Next Process Improvements

如果继续优化开发流程，建议下一轮补这几项：

1. 为“关键控制流验收”写一个固定 checklist
2. 为老 C 文件增加“编码/换行探测”前置步骤
3. 把验证命令收敛成固定顺序模板
4. 把“阶段完成定义”写进后续计划文档，避免“代码改了但还没验”的灰色状态

## Summary

这轮的核心收益不是单点功能，而是把 3 条并发基础链路的行为边界收紧了：

- 日志关闭与写入的生命周期协议
- 线程等待与关闭的完成语义
- 锁/CV 原语的时间与接口语义

同时，这轮也暴露了一个更重要的事实：对这类老 C 代码库，开发效率瓶颈往往不在“想不出修法”，而在“验证链路不够收敛”和“文本修改不够克制”。后续只要把这两点流程化，整体推进速度会明显更稳定。
