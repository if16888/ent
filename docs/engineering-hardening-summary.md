# ent 工程加固总结

本文档总结本轮对 `ent` 仓库完成的工程化改造，重点覆盖：

- 多实例 runtime 能力梳理与文档化
- runtime / devel 安装与发布形态
- timer / lock / condition variable / thread / thread-pool 的并发与生命周期问题修复
- 回归测试与 CI 纳入情况
- 后续仍值得继续收尾的优化点

---

## 1. 本轮改造目标

本轮工作的核心目标不是单点修 bug，而是把 `ent` 从“能跑”推进到“更适合作为 SDK / 可维护工程”的状态，主要围绕以下几类问题展开：

1. **多实例隔离能力不清晰**
   - 需要把 `ENT_RUNTIME` 的定位从代码层面落实到 README 层面。
2. **安装与发布形态不清晰**
   - 需要让运行时产物与开发包产物可分发、可安装、可被下游稳定消费。
3. **timer / lock / thread / tpool 存在并发与生命周期风险**
   - 需要对 signal 上下文、自删、close/add 竞态、错误路径清理等做工程化加固。
4. **缺少针对关键失败路径和竞态的回归测试**
   - 需要把关键风险点固化进测试与 CI。

---

## 2. 多实例 runtime 能力

### 2.1 `ENT_RUNTIME` 的定位

本轮明确了 `ENT_RUNTIME` 的作用：

- `ENT_Init / ENT_Run / ENT_Close` 更偏向**单实例 / 全局实例**模式
- `ENT_RuntimeInit / ENT_RuntimeRun / ENT_RuntimeClose` 更偏向**多实例 / 显式句柄**模式

`ENT_RUNTIME` 的核心价值是：

- 让一个进程内可以持有多个 runtime 实例
- 不同实例具备独立的 `name / workPath / logPath / lock / cv / runtime state`
- 避免一个实例的初始化、失败或关闭直接覆盖另一个实例的上下文

### 2.2 README 已补充的内容

README 已补充：

- 多实例代码示例
- `ENT_RUNTIME` 作用说明
- 单实例 vs 多实例结构示意图
- 使用场景与边界说明

### 2.3 多实例相关测试

`test_ent_init` 已补充多实例覆盖，包括：

- 双实例独立初始化 / 关闭
- 第二实例初始化失败不破坏第一实例
- 一个实例关闭后另一实例仍可继续运行

---

## 3. 安装与发布形态整理

### 3.1 安装组件拆分为 runtime / devel

顶层安装与发布形态已整理为两个组件：

- `runtime`
  - 共享库、DLL、运行时依赖
- `devel`
  - 头文件、静态库 / 导入库、CMake package 文件

这意味着：

- 部署机器只需要 `runtime`
- 开发机 / CI / 二次开发集成需要 `devel`

### 3.2 Release 产物

Release workflow 已支持按平台输出：

- Linux
  - `ent-<tag>-linux-x86_64-runtime.tar.gz`
  - `ent-<tag>-linux-x86_64-devel.tar.gz`
- Windows
  - `ent-<tag>-windows-win32-runtime.zip`
  - `ent-<tag>-windows-win32-devel.zip`

### 3.3 下游消费

当前仓库已具备：

- `cmake --install`
- `find_package(ent CONFIG REQUIRED)`
- `ent::ent` / `ent::ent_s`
- 安装后下游 sample 验证

因此 `ent` 已更接近标准 SDK 交付形态。

---

## 4. timer 子系统加固

timer 这条线本轮是并发与生命周期问题最集中的部分之一，已做以下处理。

### 4.1 Linux signal 路径收口

原实现中，Linux 定时器 signal 路径存在两个高风险问题：

- 在 signal handler 中执行用户回调
- 在 signal handler 中删除 timer

这些操作不是 async-signal-safe，会带来未定义行为风险。

本轮已改为：

- Linux 上 `UTL_TIMER_E_SIGNAL` 自动回退到 thread mode
- signal handler 不再执行用户回调，也不再删除 timer

### 4.2 线程型 timer 的 self-delete 风险处理

原实现中，线程型 timer 存在“回调里删除自己”导致的 use-after-free 风险。

本轮已改为：

- 支持线程型 timer 在回调线程中调用 `UTL_TimerDelete()`
- 删除动作改为延迟清理
- 线程退出前执行自清理，避免回调后继续踩已释放内存

### 4.3 `UTL_TimerClose()` 的遍历删除方式收口

原实现采用边遍历边删的方式，结构较脆弱。

本轮已改为：

- 每轮重新取稳定节点
- 关闭路径更加明确

### 4.4 timer 生命周期锁

当前 timer 子系统已引入独立 lifecycle lock，并在 `Init / Close` 以及 `Create / CreateUs / Delete` 的关键路径上使用状态检查，解决了：

- close 启动后继续接纳新 timer
- self-delete 清理时踩到已关闭或正在关闭的全局状态

### 4.5 timer 回归测试

已新增 / 加强的测试包括：

- slow callback 不重入
- `UTL_TimerCreateUs` 的 delete 延迟不等整周期
- Linux RT periodic timer 行为
- callback self-delete 安全
- close/create 生命周期竞争下 create 被拒绝

相关测试目标包括：

- `test_utl_timer`

---

## 5. lock / condition variable 加固

### 5.1 错误语义增强

本轮为 utility thread / lock / cv 增加并使用了更明确的消息码：

- `ENT_UTHD_WAIT_TIMEOUT`
- `ENT_UTHD_LOCK_FAILED`

### 5.2 Windows RW 条件变量标志位修正

`SleepConditionVariableSRW` 使用的共享 / 独占标志位已修正，避免读写模式语义错误。

### 5.3 timed wait 语义修正

原实现中，`UTL_CVWait()` 存在把 timeout 当作成功的风险。

本轮已改为：

- timeout 明确返回 `ENT_UTHD_WAIT_TIMEOUT`
- wait 失败与 timeout 分离

### 5.4 锁 enter / leave 错误不再静默吞掉

mutex / rw / spin 的 enter/leave 现在会更明确地映射失败状态，不再一律伪装成成功。

### 5.5 lock / cv 回归测试

新增：

- `test_utl_lock_cv`

覆盖重点包括：

- RW 锁必须显式指定读写模式
- spin lock 不能用于 CV wait
- timed wait timeout 语义
- mutex + CV 的 wake 正常链路

Windows CI 也已显式纳入该测试。

---

## 6. `ent_thread.c` 加固

### 6.1 `ENT_ThreadWaitById()` 生命周期处理

本轮对 `ENT_ThreadWaitById()` 做了关键调整：

- 先从内部线程链表摘除目标对象
- 再在链表外等待线程结束

这样可避免多个 wait / close 并发围绕同一个线程对象产生悬挂指针或重复释放风险。

### 6.2 `ENT_ThreadCreate()` 挂链失败回收

原实现中，如果线程已经创建成功，但后续挂入内部管理链表失败，会留下“线程已创建但未纳管”的洞。

本轮已改为：

- Windows：等待线程结束并关闭 handle
- POSIX：`pthread_join` 并销毁同步资源

然后才返回 `ENT_THRD_CREATE_FAILED`。

### 6.3 POSIX timeout 时钟源修正

POSIX `WaitById(..., ms)` 已改为使用 `CLOCK_MONOTONIC`，避免系统时间调整影响超时行为。

### 6.4 detach create 的返回语义修正

POSIX `ENT_ThreadDetachCreate()` 现在不会因为 `pthread_attr_destroy()` 的 warning 覆盖线程创建成功的主结果。

### 6.5 失败路径回归测试

新增：

- `test_ent_thread_failures`

覆盖重点：

- 线程已创建成功但内部挂链失败时，线程资源必须被真实回收
- Windows / POSIX 分别验证各自的回收路径

Windows CI 已显式纳入该测试。

---

## 7. 线程池 `utl_tpool.c` 加固

### 7.1 close/add 生命周期收口

线程池当前已增加更明确的生命周期状态：

- `tag`
- `acceptingTasks`
- `closing`

并实现：

- `UTL_TPoolClose()` 一开始就切换到不再接单状态
- `UTL_TPoolAddTask()` 在真正入队前二次检查是否 closing
- close 开始后新任务会被拒绝

### 7.2 cleanup 锁顺序收口

原实现 close 路径中存在 `taskLock` / `recycleLock` 的顺序风险。

本轮已将 active / finish 队列清理分开处理，降低锁顺序反转风险。

### 7.3 worker 创建失败路径修补

线程池 worker 创建成功后，如果内部 thread node 挂链失败，现在会：

- 等待该 worker 退出
- 释放该 worker 的上下文
- 不把它计入有效线程数

### 7.4 生命周期回归测试

新增：

- `test_utl_tpool_lifecycle`

覆盖重点：

- 一个慢任务已开始执行
- 另一线程启动 close
- 主线程继续 add task
- 预期 add 会被拒绝

Windows CI 已显式纳入该测试。

---

## 8. 新增测试与 CI 纳入情况

### 8.1 新增测试目标

本轮新增的重要测试目标包括：

- `test_utl_lock_cv`
- `test_utl_tpool_lifecycle`
- `test_ent_thread_failures`

### 8.2 CMake 已接入

上述测试均已接入 `test/CMakeLists.txt`，并通过 `add_test(...)` 注册到 `ctest`。

### 8.3 Windows CI 显式过滤已补齐

`scripts/run-ci-windows.ps1` 已显式纳入：

- `test_utl_lock_cv`
- `test_utl_tpool_lifecycle`
- `test_ent_thread_failures`

Linux 侧直接跑 `ctest`，也会覆盖到这些新增测试。

---

## 9. 当前状态评估

从工程状态看，当前仓库已经从“代码功能可用”推进到了“更接近可交付 SDK / 可持续维护工程”的阶段，主要体现在：

- 多实例 runtime 已有代码、测试和文档三方面支撑
- runtime / devel 打包与安装路径已明确
- timer / lock / cv / thread / tpool 的关键并发风险已做第一轮加固
- 关键失败路径和生命周期竞态已开始被回归测试覆盖
- Windows / Linux 两侧 CI 对新增测试基本形成闭环

---

## 10. 仍值得继续的后续优化点

虽然本轮已经完成较多加固，但仍有一些值得后续继续收尾的点：

1. **`ent_thread` 的更多并发回归**
   - 例如 `WaitById(timeout)` 后再次等待成功的场景
   - 多线程 wait/close 更细粒度组合

2. **线程池未执行任务的取消语义**
   - 当前 close 时未执行任务会被清理，但取消语义仍可进一步明确
   - 可考虑增加 cancel callback 或文档明确约定

3. **更多句柄 tag / magic 防御**
   - 某些上下文已增加 tag，但仍可继续统一化

4. **测试覆盖继续细化**
   - 尤其是 Windows 下更强的失败路径模拟
   - 以及更明确的跨平台差异验证

5. **发布说明 / CHANGELOG 化**
   - 当前文档是工程总结
   - 如果后续版本化发布，可进一步拆成 release notes / changelog

---

## 11. 建议的后续使用方式

如果后续继续在此仓库上开发，建议优先遵循以下原则：

- 涉及 timer / thread / tpool 的修改，优先先补回归测试再改代码
- 新增跨平台同步原语或生命周期状态时，先明确：
  - create
  - run
  - wait
  - close
  - failure cleanup
  的完整路径
- 继续保持 README、测试、CI 同步推进，而不是只改代码本体

这样可以让 `ent` 更稳定地朝 SDK / 平台组件方向演进。
