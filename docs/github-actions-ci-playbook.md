# GitHub Actions CI Playbook

这份文档沉淀本仓库在 GitHub Actions 上补齐 Linux / Windows 编译、测试、性能冒烟时的做法和踩坑记录，目标是后续项目可以直接复用，而不是每次从头排查。

## 1. 设计原则

本仓库最终采用的是“脚本驱动 workflow”，而不是把所有逻辑直接写死在 `.github/workflows/*.yml` 里。

核心原则：

- workflow 只负责调度阶段：`configure`、`build`、`test`、`perf`
- 平台差异尽量收敛到仓库脚本里
- 本地复现命令和 CI 使用同一套脚本，避免命令漂移
- 先保证功能测试稳定，再补性能冒烟
- 性能冒烟只做“能跑完 + 输出统计”，不要一开始就做严格阈值

当前入口脚本：

- Linux: [scripts/run-ci-linux.sh](/Users/lifei/test/ent/scripts/run-ci-linux.sh)
- Windows: [scripts/run-ci-windows.ps1](/Users/lifei/test/ent/scripts/run-ci-windows.ps1)

## 2. 推荐目录结构

建议保留下面这套结构：

- `.github/workflows/ci.yml`
- `.github/workflows/release.yml`
- `scripts/run-ci-linux.sh`
- `scripts/run-ci-windows.ps1`

这样做的收益：

- 本地和 CI 运行路径一致
- Windows / Linux 的命令差异集中在脚本，而不是散落在 workflow
- 以后迁移到新项目时，先复制脚本，再替换目标名和依赖即可

## 3. 阶段划分

推荐固定成四段：

1. `configure`
2. `build`
3. `test`
4. `perf`

其中：

- `test` 负责功能正确性
- `perf` 负责性能程序冒烟，不混进 `ctest`

原因：

- 失败日志更容易定位
- `perf` 程序通常没有统一的测试框架语义
- 某个 `perf` 卡死时，不会把 `ctest` 日志搅乱

## 4. Linux 依赖策略

Linux 优先使用系统开发包，不要先引入额外包管理器复杂度。

本仓库在 Ubuntu runner 上安装：

- `ninja-build`
- `libsqlite3-dev`
- `default-libmysqlclient-dev`

建议：

- SQLite、MySQL 之类常见库，优先系统包
- CMake 检测优先系统搜索，必要时补 `pkg-config`
- 不要在 `find_path` / `find_library` 前把结果变量预先设成空字符串

这次 ent 踩过的关键坑：

- 在 [comm/CMakeLists.txt](/Users/lifei/test/ent/comm/CMakeLists.txt) 里先 `set(ENT_SQLITE_LIBRARY "")`
- 后续 `find_library(ENT_SQLITE_LIBRARY ...)` 被短路
- Linux fresh configure 直接把 SQLite backend 关掉
- 结果 `test_ent_db` 和 `test_security` 根本没生成，`ctest` 只看到 `8` 个测试

修复经验：

- 不要预设空字符串给 `find_*` 输出变量
- Linux 下可以给 `find_*` 补 `pkg-config` 的 include / lib hints
- 用 fresh build dir 验证 `CTestTestfile.cmake` 里是否真的出现目标测试

## 5. Windows 依赖策略

Windows 优先目标是“先稳定跑通”，再谈最优依赖管理。

这次 ent 最终采用：

- 不走 `vcpkg install`
- 直接使用仓库内 `3rd/sqlite` 和 `3rd/mysql`
- 配合 `ENT_ALLOW_VENDORED_DB_LIBS=ON`

原因：

- Windows CI 调试期，`vcpkg` 下载和编译太慢
- 先排构建 / 测试问题时，自带库更快

注意事项：

- 仓库内 `lib`/`dll` 的位数必须和 generator 一致
- 本仓库自带库是 32 位，所以 Windows generator 需要切到 `Win32`
- 如果测试目标直接编译实现文件而不是链接共享库，要统一加 `LINKING_LIBENT`

## 6. 测试策略

功能测试目标建议分层：

- 基础功能：初始化、线程、锁、定时器、socket、日志
- 数据库功能：SQLite-only 先打通
- 安全测试：输入校验、路径处理、SQL 注入回归

本仓库现在两端都能跑到：

- `test_ent_init`
- `test_utl_dll`
- `test_utl_thread`
- `test_ent_thread`
- `test_utl_tpool`
- `test_utl_timer`
- `test_utl_socket`
- `test_ent_db`
- `test_ent_log`
- `test_security`

经验：

- Windows 不要默认假设 POSIX 测试能直接复用
- `dlfcn.h`、`nanosleep`、`socketpair(AF_UNIX)`、`pthread` 函数指针签名都要逐项改成跨平台
- 先把测试改成跨平台，再纳入 CI

## 7. 性能冒烟策略

性能程序不要直接做“门槛判定”，先做 smoke：

- 程序能成功退出
- 输出统计信息
- 不挂死、不无限等待

本仓库当前性能程序：

- `perf_utl_socket`
- `perf_utl_timer`
- `perf_utl_timer_rt`（Linux only）
- `perf_utl_tpool`
- `perf_ent_log`

经验：

- `timer_rt` 更适合做观测，不适合一开始就做严格 fail 阈值
- `tpool` benchmark 不能靠粗粒度 `Sleep(10)` 轮询，否则测出来的是 benchmark 自己
- 样本太短时，CI 噪声会显著影响 `tasks/s`
- 看趋势时，要先区分“程序真实慢”还是“benchmark 设计有问题”

## 8. Windows perf 脚本经验

Windows perf 这次踩过几次典型坑，值得单独记录。

### 8.1 不要直接依赖模糊的 PowerShell 行为

给每个 perf 程序都打印明确前缀：

- `Running perf_utl_socket.exe`
- `Running perf_utl_timer.exe`
- ...

这样卡住时能立刻知道是哪一个程序。

### 8.2 必须有超时保护

每个 perf 程序都应独立超时，例如 `180` 秒。不要整步无限等。

### 8.3 不要把 stderr 自动当失败

本仓库的 `ENT_LogInit` / `ENT_LogClose` 会向 `stderr` 打成功信息。如果脚本用 `Write-Error` 转发 `stderr`，在 `$ErrorActionPreference = "Stop"` 下会把成功程序误判成失败。

正确做法：

- 捕获 `stderr`
- 正常写回控制台
- 只有超时或非零退出码才 fail

### 8.4 进程等待要看真实退出，而不是只看日志

曾经遇到“程序已经打印结果，但脚本还认为没退出”。最后改成更稳的进程等待方式，并统一回放输出。

## 9. 线程池与关闭路径经验

`utl_tpool` 这次出现过两个典型问题：

- worker 在关闭阶段错过唤醒，导致 `perf_utl_tpool` 卡死
- benchmark 本身通过轮询误伤吞吐测量

最终经验：

- 关闭时先设置退出状态，再统一唤醒等待线程
- worker 在准备等待前，先再次检查退出状态
- 给“idle worker close”单独补回归测试，避免 race 靠运气

## 10. 日志性能经验

日志这块最后采用了“双路径”策略：

- 默认路径：保持同步语义，不改变原有行为
- 显式启用 `ENT_LOG_BUFFER_E`：后台线程异步写盘

这个策略适合后续项目复用，因为它满足两个目标：

- 默认行为安全、直观、便于排障
- 高吞吐场景可以显式选择 buffered 模式

经验总结：

- 不要为了性能默认延迟普通日志落盘
- 不要为了性能改变日志轮转时间语义
- 默认语义不动，性能模式显式 opt-in，工程上最稳

## 11. 排查顺序模板

后续新项目如果遇到“CI 过不去 / Windows 特别难调”，建议按这个顺序查：

1. 先确认 configure 是否真的生成了目标 target / test
2. 再看 build/link 失败，而不是先改业务代码
3. 区分是平台实现差异，还是测试本身只适合 POSIX
4. 脚本先加运行标记和超时，再判断是不是死锁
5. perf 异常先查 benchmark 设计，再查实现
6. fresh build dir 验证，不依赖旧缓存

## 12. 新项目落地 Checklist

给新项目接 GitHub Actions 时，建议直接照这个清单做：

- 写 `scripts/run-ci-linux.sh`
- 写 `scripts/run-ci-windows.ps1`
- workflow 只调脚本，不内嵌复杂命令
- 明确 `configure/build/test/perf` 四阶段
- Linux 优先系统包
- Windows 调试期优先最稳的依赖来源
- 每个平台先补最小功能测试集合
- 再补性能程序 smoke
- 给 perf 程序加运行标记和超时
- 所有“只在 CI 出现”的问题都用 fresh configure 复现思路排查

## 13. 本仓库当前状态

截至这份文档落地时：

- Linux `test`: `10/10`
- Windows `test`: `10/10`
- Linux `perf`: 可完整执行
- Windows `perf`: 可完整执行
- `ENT_LOG_BUFFER_E` 已作为显式启用的 buffered 模式接入性能程序

如果以后继续扩展：

- 优先把新测试接进共享脚本
- 再让 workflow 调脚本，不要反过来只改 workflow
