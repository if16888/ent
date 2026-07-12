# Log Lifecycle Architecture

`ent` 的日志模块现在同时支持 service-level 和 handle-level 生命周期语义。
这一层的目标是把“谁拥有日志服务”、“谁拥有日志句柄”、“close 时谁先退出”说清楚，而不是再引入一套新的返回码体系。

这篇文档只解释 **日志生命周期边界**，返回码速查请看 [docs/log-return-codes.md](../log-return-codes.md)，使用示例请看 [README.md](../../README.md)。

## 设计目标

- 对外明确区分日志 service 和日志 handle。
- 句柄 close 必须等待在途 writer 退出，再停止 buffer thread、flush/close 文件并释放资源。
- service close 只能在没有 live handle 时进行，不应强行回收仍在使用中的句柄。
- context-owned handle 与 default handle 的 ownership 关系必须可审计。

## 生命周期状态

日志句柄使用显式状态机：

| 状态 | 含义 |
| --- | --- |
| `ACTIVE` | 允许写日志和更新配置。 |
| `CLOSING` | 拒绝新的 writer 和配置更新，并等待在途 writer 退出。 |
| `CLOSED` | 句柄已经失效，不可复用。 |

`ENT_LogCloseHandle()` 会把句柄从 `ACTIVE` 推进到 `CLOSING`，等待资源收口完成后再进入 `CLOSED`。

## Service-level / Handle-level

The log service mutex has process lifetime and is never destroyed by
`ENT_LogClose()`. Service state is changed while that mutex is held, so an
entry racing with service close cannot lock a destroyed synchronization
object. `ENT_LogClose()` still rejects live handles and contexts; callers must
not treat service close as a forceful reclamation operation.

The service lock lifetime guarantee protects the global entry point. It does
not make a raw log handle safe after `ENT_LogCloseHandle()` or a raw context
safe after `ENT_LogCtxClose()`.

### `ENT_LogInit()` / `ENT_LogClose()`

- 这一组操作管理的是日志 service 本身。
- `ENT_LogInit()` 负责把日志服务拉起。
- `ENT_LogClose()` 负责把日志服务整体收口。
- 当仍有 live log handle 存在时，`ENT_LogClose()` 必须拒绝关闭。

### `ENT_LogInitHandle()` / `ENT_LogCloseHandle()`

- 这一组操作管理的是单个日志句柄。
- `ENT_LogInitHandle()` 创建一个归 service 管理的句柄。
- `ENT_LogCloseHandle()` 会：
  1. 让句柄进入 `CLOSING`
  2. 拒绝新的 writer / option 更新
  3. 等待在途 writer 退出
  4. 停止 buffer thread
  5. flush / close 文件
  6. 释放句柄资源

### `ENT_LogCtx*()` / context ownership

- `ENT_LogCtxInit()` 创建 context。
- `ENT_LogCtxInitHandle()` 创建由该 context 直接拥有的句柄。
- `ENT_LogCtxCloseHandle()` 和 `ENT_LogCtxClose()` 负责把 context-owned handle 一并收口。
- context API 不能越权操作 foreign handle 或 default handle。

## Flush 语义

当前公开 API 没有单独的 `ENT_LogFlush()`。
flush 语义由内部 writer 线程、flush batch / interval 配置和 close 路径共同承担：

- 普通写入会按 batch / interval 触发刷新。
- `ENT_LogCloseHandle()` 会在退出前确保需要持久化的缓冲被刷出。
- `ENT_LogClose()` 不负责替代句柄 close 的 flush 责任。

## 对调用方的建议

- 默认 handle 和 context-owned handle 不要混用。
- 不要在句柄进入 `CLOSING` 后继续写入或更新 option。
- 先 close 所有句柄，再 close service。
- 若需要把日志作为嵌入式 SDK 使用，优先遵守 ownership 边界，不要自行缓存已关闭句柄的 raw copy。

## 相关文档

- [README.md](../../README.md)
- [docs/log-return-codes.md](../log-return-codes.md)
- [docs/architecture/handle-lifecycle.md](handle-lifecycle.md)
