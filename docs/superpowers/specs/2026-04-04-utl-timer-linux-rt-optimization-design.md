# UTL Timer Linux RT Optimization Design

## Goal

针对 Linux 实时核用户态场景，为 `utl_timer` 增加一条更适合 `1 ms` 周期、目标抖动控制在 `100 us` 以内的 RT 专用实现路径，并保留现有普通路径的兼容性。

## Scope

本次设计只聚焦 Linux RT 优化，不扩展到 Windows，也不改造 `socket` 或 `tpool`。

本次范围包括：

- 为 Linux 增加 RT 专用 timer 路径
- 时钟源从 `CLOCK_REALTIME` 转向 `CLOCK_MONOTONIC`
- 用绝对时间唤醒替代当前的 timer 回调调度方式
- 提升内部时间粒度到 `ns`
- 减少用户回调执行对下一拍抖动的放大

不在本次范围内的内容：

- Windows timer 重构
- 完整的多级实时调度器框架
- 内核态或驱动层实现
- 直接把新的 RT 语义强推给所有现有调用方

## Current Problems

### 1. 时钟源不适合实时评估

当前 Linux 路径使用 `CLOCK_REALTIME`。

问题：

- 会受系统时间调整影响
- 对周期精度和 jitter 评估不稳定

### 2. 接口粒度过粗

当前 `UTL_TimerCreate(..., int ms, ...)` 只有毫秒粒度。

问题：

- 对 `1 ms` 周期来说，外部配置和内部表示都太粗
- 无法为后续更细粒度需求留出空间

### 3. `SIGEV_THREAD` 回调模型不适合低抖动目标

当前 Linux 实现依赖 POSIX timer + `SIGEV_THREAD`。

问题：

- 每次触发都引入额外线程调度链路
- 用户回调执行时间和线程唤醒时机直接进入 jitter
- 很难稳定逼近 `100 us` 以内

## Approaches Considered

### A. 新增 Linux RT 专用路径，保留旧路径

做法：

- 保留当前普通 `UTL_TimerCreate` 路径兼容老调用方
- 新增 RT 专用 API 和内部实现
- Linux RT 场景走新路径

优点：

- 兼容性最好
- 风险可控
- 可以并行比较旧路径和 RT 路径

缺点：

- 同时维护两套 Linux timer 行为

### B. 直接替换现有 Linux 路径

做法：

- Linux 下所有 timer 都改成新实现

优点：

- 长期结构更干净

缺点：

- 对现有调用方影响更大
- 风险更高

### C. 一步到位重写成完整调度器

做法：

- 自建 timer 调度线程、最小堆或有序链表、执行队列

优点：

- 长期上限最高

缺点：

- 明显超出当前最小必要范围

## Chosen Approach

采用方案 A：新增 Linux RT 专用路径，保留旧路径作为兼容实现。

## Detailed Design

### Public API Strategy

保留现有 API：

- `UTL_TimerCreate(UTL_TIMER_T* pTimer, unsigned int type, int ms, UTL_TIMER_EV_F evCb, void* data)`

新增 RT 专用 API，建议二选一：

方案 1：

- `UTL_TimerCreateUs(UTL_TIMER_T* pTimer, unsigned int type, int period_us, UTL_TIMER_EV_F evCb, void* data)`

方案 2：

- `UTL_TimerCreateEx(UTL_TIMER_T* pTimer, const UTL_TIMER_CFG* cfg, UTL_TIMER_EV_F evCb, void* data)`

我建议先选方案 1，因为这次目标非常明确，就是给 Linux RT 场景增加 `us` 粒度周期配置。

### Internal Time Representation

RT 路径内部统一使用 `ns`：

- 周期
- 下一次 deadline
- 实际触发时间戳

原因：

- `1 ms` 周期下，`100 us` 抖动目标要求内部时间计算足够细
- 用 `ms` 内部表示很容易在舍入和累计偏移上丢精度

### Clock Source

RT 路径统一使用：

- `CLOCK_MONOTONIC`

不再使用：

- `CLOCK_REALTIME`

原因：

- 周期调度不应被系统校时扰动
- 更符合实时控制场景的时间语义

### Scheduling Model

RT 路径不再依赖 `SIGEV_THREAD`。

改为：

- 一个专用高优先级 timer worker 线程
- 使用 `clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, ...)`
- 按绝对时间点周期唤醒

工作方式：

1. 创建 timer 时初始化上下文
2. worker 线程记录起始基准时间
3. 计算下一次绝对 deadline
4. 用 `TIMER_ABSTIME` 睡眠到 deadline
5. 到点后记录实际触发时间
6. 投递或执行回调
7. deadline += period
8. 继续下一轮

这样可以避免：

- 相对 sleep 造成的累计漂移
- 每拍根据“当前时间 + period”重新计算导致的相位滑动

### Callback Execution Model

调度线程不建议直接长时间执行用户回调。

推荐模型：

- 调度线程负责准点触发和记录时间
- 回调执行和调度逻辑解耦

可选两种方式：

1. 简单方式：单独执行线程
   - 调度线程只发信号
   - 执行线程串行运行用户回调

2. 更轻量方式：调度线程直接调用回调，但明确要求回调必须短小

对于 `1 ms / 100 us` 目标，我建议选 1。

原因：

- 回调耗时一旦不可控，下一拍 jitter 会明显放大
- 工控场景更需要调度线程职责单一

### Timer Context Changes

`TIMER_CTX_T` 在 Linux RT 路径下至少需要补充：

- 周期 `period_ns`
- 下一次 deadline `next_deadline_ns`
- 实际触发统计字段
- worker 线程句柄
- 回调执行同步字段
- 退出标志

### Linux RT Environment Hints

RT 路径应允许外部配合以下运行策略：

- `SCHED_FIFO` 或 `SCHED_RR`
- 高优先级线程
- CPU 绑核

这些不一定都在库内部强制完成，但至少：

- API 或文档要明确支持这种运行方式
- `perf_utl_timer_rt` 要能验证这些条件是否已生效

### Verification Strategy

继续使用 `perf_utl_timer_rt` 作为主评估工具。

优化后的验证目标：

- `target_period_us = 1000`
- 重点观察：
  - `max_jitter_us`
  - `p99_jitter_us`
  - `p999_jitter_us`
  - `over_100us_ratio`

判定原则：

- 先看 `p99`
- 再看 `p999`
- 最后看超阈值比例

工控场景下，不应只看平均值。

## Error Handling

- RT 路径初始化失败时，要有明确返回码和日志
- worker 线程创建失败时，创建流程必须完整回滚
- timer 删除时，要确保 worker 线程可控退出
- 回调线程或执行路径异常时，不能导致整个 timer 子系统失控

## Risks

- 新旧路径并存会增加维护复杂度
- 若回调执行线程设计不当，可能引入新的队列堆积问题
- `1 ms / 100 us` 目标依赖 RT 内核、调度策略和 CPU 隔离条件，不能只靠库本身保证

## Success Criteria

- Linux RT 下新增专用 timer 路径
- 时钟源切到 `CLOCK_MONOTONIC`
- 不再依赖 `SIGEV_THREAD` 作为 RT 主路径
- 内部时间粒度提升到 `ns`
- 在 `perf_utl_timer_rt` 中能明确评估：
  - `1 ms` 周期下是否接近 `100 us` 抖动目标
  - 超过 `100 us` 的比例是否可接受
