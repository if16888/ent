# Handle Lifecycle Architecture

`ent` 现在的对外实例模型以 `ENT_HANDLE` 为中心。它替代了早期的 `ENT_Runtime*` 公共入口，统一承载 Init / Run / Stop / Close 的生命周期闭环。

这篇文档只解释 **句柄生命周期**，不重复 README 的使用示例，也不重复 `docs/log-return-codes.md` 的返回码表。

## 设计目标

- 对外只有一套 handle-based 生命周期入口。
- 同一个进程里可以同时管理多个实例。
- 每个实例都必须有清晰的初始化、运行、停止和关闭边界。
- 关闭必须是可审计、可等待、可回收的，而不是“直接 free 掉再赌调用方没有在跑”。
- 如果调用方允许多线程入口，那么 close 与新入口之间必须由上层 owner lock / lifecycle lock 互斥；ent 不提供全局 handle registry 来替调用方做这层互斥。

## 生命周期状态

`ENT_HANDLE` 在实现上经历三个显式生命周期状态：

| 状态 | 含义 |
| --- | --- |
| `ACTIVE` | 句柄可正常运行、停止、设置属性和关闭。 |
| `CLOSING` | `ENT_Close()` 已开始收口，新的运行或配置入口必须被拒绝。 |
| `CLOSED` | 资源已经释放或标记为无效，句柄不能再复用。 |

`stopRequested` 是 `ENT_CTX` 上独立的停止请求标志，不是 `handleState` 枚举状态。

这几个状态和标志的存在目的，不是增加复杂度，而是把“谁还在用这个句柄”说清楚。

## 入口语义

### `ENT_Init(ENT_HANDLE* handle, ...)`

- `handle` 必须指向调用方持有的句柄变量。
- 调用成功后，`*handle` 变成一个可用的实例句柄。
- 同一个句柄变量如果已经非空，再次初始化必须拒绝，不允许静默覆盖。

### `ENT_Run(ENT_HANDLE handle)`

- `ENT_Run()` 是实例的主运行入口。
- 它适合放在 worker thread 中执行，而不是和主线程串行阻塞调用。
- 当 `ENT_Stop()` 已经请求停止时，`ENT_Run()` 应该尽快退出。
- 正常被 `ENT_Stop()` 唤醒后，返回值以 `ENT_SYS_NORMAL` 作为主语义。

### `ENT_Stop(ENT_HANDLE handle)`

- `ENT_Stop()` 只负责发出停止请求，并唤醒可能阻塞的运行路径。
- 它不负责释放资源。
- 如果句柄已经无效或已经关闭，必须返回能明确表达状态的错误码，而不是把所有情况都混成一个结果。

### `ENT_Close(ENT_HANDLE* handle)`

- `ENT_Close()` 负责终止句柄生命周期。
- 它必须先让实例进入收口态，再等待正在运行的路径退出，最后才释放资源。
- 成功后，调用方持有的句柄变量必须被置为 `NULL`。
- 成功 close 后，旧 raw 复制值立即失效，不允许继续把关闭前保存的指针当成可调用 handle 使用。
- `ENT_Close()` 只承诺等待已经进入运行路径的调用退出；它不承诺在 close 开始后，对其他线程刚刚发起的新 `ENT_Run()` / `ENT_Stop()` / `ENT_SetRtAttributes()` 入口提供代码级全局防御。

### `ENT_SetRtAttributes(ENT_HANDLE handle, ...)`

- `ENT_SetRtAttributes()` 只作用于有效句柄。
- 它属于实例级配置入口，不应该绕过句柄生命周期检查。

## 并发与收口原则

句柄生命周期的核心原则是：

1. 新的运行入口不能在关闭开始后继续进入。
2. 关闭不能在运行路径还在使用句柄时直接释放内存。
3. 停止只负责触发退出，不负责直接销毁。
4. 句柄失效后，当前仍可访问的无效对象必须被拒绝；成功 close 后保留下来的旧 raw 复制值不再有任何可调用契约。
5. 如果上层需要多线程入口保证，必须用 owner lock / lifecycle lock 在 close 开始前先把新入口和 close 互斥掉。

这也是为什么实现里要保留 magic tag、running/stopRequested 和 active call 之类的状态。
这些状态可以收口已经进入调用路径的实例，但它们不是全局 registry，也不负责把“close 开始后新来的调用”全部变成一个统一的强同步保证。

## 对调用方的建议

- 多实例时，不要把所有实例的 `ENT_Run()` 串成一个顺序调用链。
- 更常见的模式是：每个实例一个 worker thread，主线程负责 `ENT_Stop()` / `ENT_Close()` 收口。
- 不要把同一个 `ENT_HANDLE` 重复 close。
- 不要在句柄已经停止或关闭后继续复用旧指针；成功 `ENT_Close()` 后只应继续使用被置为 `NULL` 的那个句柄变量。
- 如果外层会跨线程发起新的入口调用，必须先让 owner lock / lifecycle lock 保护 close、run、stop 和属性更新之间的互斥。

## 相关文档

- [README.md](../../README.md)
- [docs/log-return-codes.md](../log-return-codes.md)
- [docs/threading-lifecycle.md](../threading-lifecycle.md)

## 相关测试

- `test/test_ent_init.c`
- `test/test_ent_msg.c`
- `test/downstream_consumer/main.c`
