# Thread Lock Deadlock Reduction Design

## Goal

消除 `ent_thread` 和 `utl_tpool` 中已经确认的高风险死锁路径，同时保持现有 API 和主要行为不变。

## Scope

本次修改只覆盖以下点：

- `ENT_ThreadWaitById` 不再持有 `dllLock` 做阻塞等待
- `ENT_ThreadClose` 不再持有 `dllLock` 做 `pthread_join`
- `utl_tpool` 中 `TASK_E_TYPE_PAUSE` 分支不再在持锁状态下自旋

不在本次范围内的内容：

- 重构线程模型或改动公开 API
- 引入新的同步原语
- 重写线程池生命周期管理

## Current Problems

### 1. `ENT_ThreadWaitById`

当前 Windows 实现会先拿 `dllLock`，再执行 `WaitForSingleObject`。这会把线程表的所有操作串行化到一个潜在的无限等待上，并可能与线程退出路径形成互相等待。

### 2. `ENT_ThreadClose`

当前 POSIX 实现会在持有 `dllLock` 的情况下循环 `pthread_cancel` 和 `pthread_join`。如果被等待线程的退出路径还需要访问相同线程上下文，就可能形成死锁。

### 3. `utl_tpool` pause 分支

worker 在持有 `taskLock` 时，如果 `taskType == TASK_E_TYPE_PAUSE`，会直接 `continue`，导致锁一直不释放。虽然当前没有公开接口触发该状态，但这是一个明显的潜在死锁点。

## Approaches Considered

### A. 最小修复，缩小持锁范围

做法：

- 只在锁内完成句柄校验、链表摘取和状态读取
- 所有阻塞等待都移到锁外
- pause 分支改为释放锁后重新等待

优点：

- 改动最小
- 风险最低
- 能直接覆盖当前已知死锁点

缺点：

- 保留现有线程节点生命周期模型

### B. 增加线程节点状态机

做法：

- 为 `THREAD_DB` 增加 `joining/closed` 等状态
- 所有等待和回收操作通过状态位协调

优点：

- 并发语义更明确

缺点：

- 改动较大
- 测试复杂度明显上升

### C. 重构线程管理层

做法：

- 把线程回收和线程登记拆成两个独立层次

优点：

- 长期可维护性最好

缺点：

- 超出本次需求

## Chosen Approach

采用方案 A：最小修复，缩小持锁范围。

## Detailed Design

### `ENT_ThreadWaitById`

调整为两阶段：

1. 在 `dllLock` 内完成：
   - 校验 `handle` 和 `tid`
   - 读取目标线程节点
   - 提取等待所需的线程标识
2. 释放 `dllLock` 后：
   - 执行 `WaitForSingleObject` 或 `pthread_join` / `pthread_tryjoin_np`
3. 等待成功后重新短暂加锁：
   - 把线程节点从链表移除
   - 清理节点并释放内存

关键约束：

- 不能在持锁状态下做阻塞等待
- 超时返回时保留 `tid`
- 成功回收时保持当前 API 语义：清空 `tid`

### `ENT_ThreadClose`

调整为循环摘取：

1. 加锁
2. 从链表摘出一个线程节点
3. 立即释放锁
4. 在锁外做 `cancel/join/close/free`
5. 重复直到链表为空

关键约束：

- `dllLock` 只保护链表结构本身
- 线程终止等待必须在锁外完成

### `utl_tpool`

对 worker 主循环的 `PAUSE` 分支做最小修正：

- 禁止在持有 `taskLock` 时直接 `continue`
- 遇到 `PAUSE` 时释放锁，然后回到等待路径

关键约束：

- 不能改变现有 `QUIT` 退出流程
- 不能让 `UTL_TPoolAddTask` 和 `UTL_TPoolClose` 被 worker 长时间阻塞

## Error Handling

- 参数非法时保持现有返回码
- join/wait 的错误记录逻辑保留
- 对于超时路径，保持现有 `return 1` 语义

## Testing Strategy

按 TDD 增加回归测试：

- `test_ent_thread.c`
  - 增加针对 `ENT_ThreadWaitById` 的锁范围回归
  - 增加针对 `ENT_ThreadClose` 的关闭流程回归
- `test_utl_tpool.c`
  - 增加针对 worker 特殊状态分支不会长期占有 `taskLock` 的回归测试

验证层次：

- 单测聚焦目标模块
- 最后跑全量 `ctest --output-on-failure`

## Risks

- 当前代码依赖历史链表/句柄生命周期，修改时需要避免 double free
- `ENT_ThreadWaitById` 如果先在锁外等待，必须确保回收阶段重新获取节点时逻辑一致
- `utl_tpool` 的 pause 分支虽然目前不可达，但修复时不能影响 quit 流程

## Success Criteria

- 不再存在已确认的“持锁 join/wait”路径
- `utl_tpool` 不再存在“持锁 continue”路径
- 新增回归测试通过
- 全量 `ctest` 通过
