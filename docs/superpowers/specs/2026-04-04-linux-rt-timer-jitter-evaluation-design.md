# Linux RT Timer Jitter Evaluation Design

## Goal

为 `utl_timer` 增加一套专门面向 Linux 实时核用户态的周期精度与抖动评估程序，先建立可信基线，判断当前实现距离“抖动控制在 `100 us` 以内”的目标还有多大差距。

## Scope

本次只做评估工具和结果输出，不修改 `utl_timer` 的生产实现，不把结果直接变成默认 `ctest` 的硬门限。

本次范围包括：

- 新增一个 Linux 定时器评估程序，单独构建和运行
- 针对 `1 ms` 目标周期做长期采样
- 输出周期统计和抖动分布
- 输出运行环境信息，用来解释结果

不在本次范围内的内容：

- 重构 `utl_timer` 实现
- 新增默认 CI 门限
- 内核态或驱动层实时方案
- `socket` / `tpool` 的实时性能评估

## Why A Separate Evaluation Program

当前已有的 `perf_utl_timer` 只能回答“普通用户态下大致是否稳定”，不能回答“Linux 实时核用户态下能否满足 `100 us` 以内抖动”。

主要缺口有三类：

1. 统计不够
   只有 `avg/min/max`，没有 `p99/p999`、超阈值次数和比例。

2. 环境信息缺失
   没有记录调度策略、优先级、是否绑核、内核类型，结果不能用于实时场景判断。

3. 采样时间太短
   当前只是短窗口观察，无法识别偶发大抖动。

## Approaches Considered

### A. 先做独立评估，再决定是否优化实现

做法：

- 把实时定时器评估独立成 `perf` 程序
- 先在 Linux RT 内核上跑出基线
- 再根据基线决定是否进入实现优化

优点：

- 数据更可信
- 能把“环境问题”和“实现问题”拆开
- 最适合工控和实时场景

缺点：

- 这一轮不会直接提升性能

### B. 直接改 `utl_timer`

做法：

- 在没有足够基线数据的前提下，直接修改定时器实现

优点：

- 看起来推进更快

缺点：

- 容易盲改
- 很难证明优化是否有效

## Chosen Approach

采用方案 A：先补专门的 Linux RT 定时器评估程序，再决定是否需要优化实现。

## Detailed Design

### Test Program

新增一个独立程序，建议命名为：

- `test/perf_utl_timer_rt.c`

构建方式：

- 由 `test/CMakeLists.txt` 单独生成可执行程序
- 不注册进默认 `ctest`

运行目标：

- 在 Linux 实时核用户态环境中手动运行

### Measurement Target

第一阶段固定测以下目标：

- 目标周期：`1000 us`（即 `1 ms`）

原因：

- 你的目标是抖动控制在 `100 us` 以内
- `1 ms` 周期下，`100 us` 抖动已经是较严格的 10% 周期误差控制
- 这是用户态实时定时器最有代表性的起点

后续如果需要，再扩展为：

- `500 us`
- `200 us`
- `100 us`

但这不在本次第一阶段范围内。

### Runtime Configuration

程序需要尽量显式记录并输出以下运行配置：

- `clock source`
  - `CLOCK_MONOTONIC` 或当前实现实际使用的时钟源
- 调度策略
  - `SCHED_OTHER` / `SCHED_FIFO` / `SCHED_RR`
- 线程优先级
- CPU 绑定情况
- 测试总时长或样本数

如果程序尝试设置以下内容但失败，也要输出：

- `pthread_setschedparam`
- `pthread_setaffinity_np`

这样可以区分“库不行”和“运行权限/环境没到位”。

### Sampling Model

程序按固定目标周期持续采样。

建议第一阶段默认值：

- `target_period_us = 1000`
- `samples = 100000`

原因：

- 10 万次在 `1 ms` 周期下大约是 100 秒量级
- 足够观察偶发大抖动
- 比只跑几百毫秒更接近工控长期运行特征

如果用户希望缩短时间，可以允许命令行参数覆盖，但默认值仍然以上述长时观测为准。

### Metrics To Output

程序至少输出以下指标：

- `target_period_us`
- `samples`
- `avg_period_us`
- `min_period_us`
- `max_period_us`
- `avg_jitter_us`
- `max_jitter_us`
- `p99_jitter_us`
- `p999_jitter_us`
- `over_100us_count`
- `over_100us_ratio`

其中：

- `period_us` 是相邻两次触发的真实间隔
- `jitter_us` 定义为 `abs(period_us - target_period_us)`

### Pass/Fail Policy

本次评估程序不把 `100 us` 作为默认进程退出门限。

默认行为：

- 程序只输出统计结果
- 只有初始化失败、采样失败或结果明显无效时才返回非 0

原因：

- 这一步是建立基线，不是立刻做 CI 判定
- 实时结果强依赖运行环境和调度权限

但程序要明确给出：

- 超过 `100 us` 抖动的次数
- 超过 `100 us` 抖动的比例

这样你可以直接判断是否满足工控目标。

### Relationship To Current `utl_timer`

这套评估应尽量靠近当前库的真实行为，而不是完全绕开 `utl_timer`。

优先级如下：

1. 先用当前 `UTL_TimerCreate` / `UTL_TimerDelete` / `UTL_TimerClose` 路径做观测
2. 如果当前 `utl_timer` 的回调模型无法拿到足够准确的时间戳，再在评估程序中增加最小辅助逻辑
3. 不在本次评估程序里改动 `utl_timer` 的对外语义

## Error Handling

- 运行环境不满足时，要明确打印原因
  - 例如：不是 Linux、设置实时调度失败、设置绑核失败
- 定时器初始化或删除失败时，直接返回失败
- 如果样本数不足或统计数据无意义，返回失败

## Testing Strategy

这类评估程序不适合走默认 `ctest`，验证方式分两层：

1. 本地构建验证
   - 确认可在 Linux 上编译通过
2. 实机运行验证
   - 在 Linux RT 内核用户态环境中运行
   - 收集实际统计数据

开发过程仍然遵守 TDD：

1. 先加程序骨架并让其失败
2. 先让程序能稳定输出环境信息和基础统计
3. 再补 `p99/p999` 和超阈值统计

## Risks

- 当前 `utl_timer` 底层实现可能天然不适合 `100 us` 级约束
- 用户态实时调度权限不足会显著影响结果
- 不绑核、不设实时优先级时，结果可能会远差于最终目标
- 长时间采样会放大偶发系统噪声，这既是风险，也是评估价值所在

## Success Criteria

- 新增的 Linux RT 定时器评估程序可以独立构建和运行
- 输出完整的周期和抖动统计
- 输出运行环境信息，便于解释结果
- 能明确回答当前实现下：
  - `1 ms` 周期是否可能逼近 `100 us` 内抖动
  - 超过 `100 us` 的比例有多高
  - 下一步是否值得进入 `utl_timer` 实现优化
