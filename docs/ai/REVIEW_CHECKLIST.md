# Review Checklist

本文档用于人工或 AI review。review 的目标是发现 bug、行为回归、生命周期风险、API / ABI 破坏、跨平台差异和测试缺口。

## API / ABI

- [ ] 是否修改 public header。
- [ ] 是否新增、删除、重命名 public symbol。
- [ ] 是否改变函数签名、结构体布局、枚举值或宏值。
- [ ] 是否改变返回语义。
- [ ] 是否影响下游调用方的编译或运行假设。
- [ ] 是否需要迁移文档或兼容说明。
- [ ] 是否需要保留旧 API 或提供 deprecation 路径。

## 生命周期

- [ ] init / close 是否成对。
- [ ] close 是否可重复调用，重复调用语义是否明确。
- [ ] flush / close / free 的顺序是否安全。
- [ ] 失败路径是否释放已分配资源。
- [ ] 并发 close 是否安全。
- [ ] 是否存在句柄泄漏、文件句柄泄漏、映射句柄泄漏或内存泄漏。
- [ ] service-level close 与 handle-level close 是否区分清楚。
- [ ] closing 状态是否拒绝新操作。

## 错误码 / ent.msg

- [ ] 是否使用统一返回语义。
- [ ] 是否混用裸数字。
- [ ] 是否仍存在私有错误码扩散。
- [ ] 是否区分 OK、invalid、busy、fatal、nonfatal。
- [ ] 是否破坏旧调用方假设。
- [ ] `msg/ent.msg`、生成头文件、实现、测试和文档是否同步。
- [ ] 新增 message symbol 是否符合 `ENT_<SUBMODULE>_<NAME>`。

## 跨平台

- [ ] Linux 是否验证。
- [ ] Windows 是否验证。
- [ ] Windows x86 / x64 差异是否说明。
- [ ] 路径分隔符是否兼容。
- [ ] 文件权限和目录创建语义是否兼容。
- [ ] 文件句柄、socket、thread、mapping handle 是否释放。
- [ ] 条件编译是否清晰，是否避免平台分支漂移。
- [ ] Visual Studio toolchain 是否只在当前 shell 初始化。

## 测试

- [ ] 是否新增测试。
- [ ] 是否覆盖成功路径。
- [ ] 是否覆盖失败路径。
- [ ] 是否覆盖边界条件。
- [ ] 是否覆盖重复调用。
- [ ] 是否覆盖并发或竞争路径。
- [ ] 是否覆盖 CI 环境。
- [ ] 是否没有删除、跳过或弱化已有测试。
- [ ] 测试断言是否匹配 public API 语义，而不是实现细节。

## CI

- [ ] 是否修改 workflow。
- [ ] workflow 修改是否有明确原因。
- [ ] 是否说明环境依赖。
- [ ] 是否上传关键日志或保留可诊断输出。
- [ ] 是否存在平台特有假设。
- [ ] 是否区分编译、链接、测试和环境失败。
- [ ] 是否避免为单个平台修复而破坏另一个平台。

## 文档

- [ ] public API 变化是否同步文档。
- [ ] 行为语义变化是否记录。
- [ ] 已知限制是否记录。
- [ ] Windows / Linux 差异是否记录。
- [ ] 返回码和错误处理示例是否更新。
- [ ] backlog、decision log 或 guardrails 是否需要同步。

## Review 输出格式

```text
P0 必改：
P1 建议：
P2 优化：
验证依据：
未验证项：
结论：
```
