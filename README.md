# ent

`ent` 是一个跨平台 C 组件库，当前主要提供以下能力：

- handle-based 初始化与多实例管理
- 日志系统
- 线程 / 锁 / 条件变量 / 线程池 / 定时器 / socket 等基础设施
- 数据库访问封装（SQLite / MySQL / PostgreSQL，按构建配置启用）
- Lua 脚本后端（可选）
- 基于 `msg/ent.msg` 的统一消息码生成

当前仓库已经接入：

- Linux (`ubuntu-24.04`) + Windows (`windows-2022`) 双平台 CI
- `ctest` 功能测试
- 性能冒烟测试
- 安装后下游 `find_package(ent CONFIG REQUIRED)` 消费验证
- Release 阶段按平台 / 架构打包 runtime / devel 产物

---

## 1. 快速构建

### Linux / macOS

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

### Windows (Visual Studio)

```powershell
cmake -S . -B build -A Win32 -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

> 当前 Windows 主路径默认按 **Win32/32 位** 构建，便于和已有的 32 位第三方库一起链接。

---

## 2. Windows 32 位 / 64 位说明

Windows 下**不能在同一个最终目标里混用 32 位和 64 位库**。

这意味着：

- 如果某个第三方 `.lib/.dll` 是 **32 位**，那么 `ent`、其余依赖、最终 EXE / DLL 也都必须是 **Win32/32 位**。
- 如果最终目标是 **x64**，那么所有参与链接的库也必须全部是 **x64**。
- **Win32 库不能直接链接到 x64 目标**，反过来也不行。

当前仓库的 Windows CI 默认使用：

```text
WINDOWS_CMAKE_PLATFORM=Win32
```

因此仓库当前默认验证的是 **32 位 Windows 构建链路**。如果你后面想切到 x64，需要保证：

- SQLite / MySQL / PostgreSQL / Lua / 你的私有第三方库
- 以及运行时 DLL

全部都有对应的 **x64 版本**。

---

## 3. 常用 CMake 开关

### 顶层开关

- `-DENT_ENABLE_PGSQL=ON|OFF`
  - 是否启用 PostgreSQL 后端探测与编译。
- `-DENT_BUILD_EXAMPLES=ON|OFF`
  - 是否构建仓库内示例程序。
- `-DBUILD_TESTING=ON|OFF`
  - 是否构建 `test/` 下的测试目标。

### comm 子目录相关开关

- `-DENT_ENABLE_SQLITE=ON|OFF`
- `-DENT_ENABLE_MYSQL=ON|OFF`
- `-DENT_ENABLE_LUA=ON|OFF`
- `-DENT_LUA_LINK_MODE=static|shared`

示例：只构建库本体，不构建 example / test，且关闭 PostgreSQL：

```bash
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DENT_BUILD_EXAMPLES=OFF \
  -DBUILD_TESTING=OFF \
  -DENT_ENABLE_PGSQL=OFF
cmake --build build
```

---

## 4. 本地安装

安装到自定义目录：

```bash
cmake -S . -B build-install -DCMAKE_BUILD_TYPE=Release
cmake --build build-install
cmake --install build-install --prefix "$PWD/.local-install"
```

Windows：

```powershell
cmake -S . -B build-install -A Win32 -DCMAKE_BUILD_TYPE=Release
cmake --build build-install --config Release
cmake --install build-install --config Release --prefix "$PWD/.local-install"
```

安装后会导出 CMake package，可供下游通过 `find_package(ent CONFIG REQUIRED)` 使用。

如果你只想安装运行时，或只想安装开发包，也可以按组件安装：

```bash
cmake --install build-install --prefix "$PWD/.local-runtime" --component runtime
cmake --install build-install --prefix "$PWD/.local-devel" --component devel
```

---

## 5. 下游项目使用方式

安装完成后，下游工程可这样引用：

```cmake
find_package(ent CONFIG REQUIRED)
add_executable(app main.c)
```

### 链接共享库

```cmake
target_link_libraries(app PRIVATE ent::ent)
```

### 链接静态库

```cmake
target_link_libraries(app PRIVATE ent::ent_s)
```

当前仓库已补充安装后下游 sample：

- `test/downstream_consumer/CMakeLists.txt`
- `test/downstream_consumer/main.c`

CI 会先 `cmake --install`，再使用这个 sample 验证安装后的 `find_package` 与链接链路。

---

## 6. 示例：最小下游 `CMakeLists.txt`

```cmake
cmake_minimum_required(VERSION 3.15)
project(ent_downstream_consumer C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

find_package(ent CONFIG REQUIRED)

add_executable(ent_downstream_consumer main.c)
target_link_libraries(ent_downstream_consumer PRIVATE ent::ent_s)
```

如果 `ent` 安装在非系统目录，配置下游工程时传入：

```bash
cmake -S . -B build -DCMAKE_PREFIX_PATH=/path/to/ent/install
```

---

## 7. CI 脚本

Linux：

- `./scripts/run-ci-linux.sh configure`
- `./scripts/run-ci-linux.sh build`
- `./scripts/run-ci-linux.sh test`
- `./scripts/run-ci-linux.sh install-consumer`
- `./scripts/run-ci-linux.sh perf`

Windows：

- `.\scripts\run-ci-windows.ps1 configure`
- `.\scripts\run-ci-windows.ps1 build`
- `.\scripts\run-ci-windows.ps1 test`
- `.\scripts\run-ci-windows.ps1 install-consumer`
- `.\scripts\run-ci-windows.ps1 perf`

`install-consumer` 阶段会执行：

1. 安装当前构建产物到 staging 目录
2. 使用 `test/downstream_consumer` 作为独立下游工程重新配置
3. 通过 `find_package(ent CONFIG REQUIRED)` 链接安装产物
4. 构建并运行 sample

---

## 8. Release 产物

Release workflow 现在会按平台 / 架构打包两类文件：

### Linux

- `ent-<tag>-linux-x86_64-runtime.tar.gz`
- `ent-<tag>-linux-x86_64-devel.tar.gz`

### Windows

- `ent-<tag>-windows-win32-runtime.zip`
- `ent-<tag>-windows-win32-devel.zip`

其中：

- `runtime`：运行时文件，例如共享库、DLL、运行时依赖 DLL
- `devel`：头文件、静态库 / 导入库、CMake package 文件

这样使用方可以按需获取：

- 只部署运行环境时，拿 `runtime`
- 需要二次开发 / 编译接入时，拿 `devel`

---

## 9. 多实例 handle 使用示例

`ent` 现在通过 `ENT_HANDLE` 句柄管理多个隔离实例。典型流程如下：

```c
#include "ent_init.h"
#include "ent_log.h"
#include "ent_msg.h"

int main(void)
{
    ENT_HANDLE handleA = NULL;
    ENT_HANDLE handleB = NULL;
    MSG_ID_T sts = ENT_SYS_NORMAL;

    sts = ENT_Init(&handleA, "node_a", "./work_a", LOG_LEV_WARN_E, ENT_MODE_NORMAL_E);
    if(sts != ENT_SYS_NORMAL)
    {
        return 1;
    }

    sts = ENT_Init(&handleB, "node_b", "./work_b", LOG_LEV_WARN_E, ENT_MODE_NORMAL_E);
    if(sts != ENT_SYS_NORMAL)
    {
        ENT_Close(&handleA);
        return 1;
    }

    /* 可选：对单个 handle 设置 RT 属性 */
    sts = ENT_SetRtAttributes(handleA, -1, ENT_RT_POLICY_OTHER_E, 0);
    if(sts != ENT_SYS_NORMAL && sts != ENT_RT_NOTRT)
    {
        ENT_Close(&handleB);
        ENT_Close(&handleA);
        return 1;
    }

    /* 实际项目中通常把 ENT_Run() 放到各自 worker thread 里：
       workerA: ENT_Run(handleA);
       workerB: ENT_Run(handleB);
       main thread 负责 Stop -> join workers -> Close 收口；如果还会并发发起新入口，先由 owner lock / lifecycle lock 互斥。 */

    ENT_Stop(handleB);
    ENT_Stop(handleA);

    ENT_Close(&handleB);
    ENT_Close(&handleA);
    return 0;
}
```

### 9.1 `ENT_HANDLE` 的作用

`ENT_HANDLE` 是对外统一的实例句柄。它的作用是：

- 让每个实例持有独立的上下文
- 保持初始化、运行、关闭语义一致
- 避免一个实例的关闭直接覆盖另一个实例的状态

更完整的生命周期说明见 [docs/architecture/handle-lifecycle.md](docs/architecture/handle-lifecycle.md)。

### 9.2 什么时候应该用 `ENT_HANDLE`

更适合用 handle 接口的场景包括：

- 一个进程里同时管理多个逻辑节点 / 站点 / 对象
- 把 `ent` 当作 SDK 嵌入到更大的宿主程序中
- 测试 / 仿真 / 多租户场景下，希望不同实例各自持有独立状态

### 9.2.1 `ENT_HANDLE` 返回语义速查表

这张表只列最常见的返回语义，完整的返回码定义请参考 [docs/log-return-codes.md](docs/log-return-codes.md)。

| API | Typical success / expected return | Common reject cases |
| --- | --- | --- |
| `ENT_Init(&handle, ...)` | `ENT_SYS_NORMAL` | `ENT_INIT_INVALID_ARGUMENT` for NULL output pointer or invalid args; `ENT_SYS_ALREADY_INITIALIZED` when `*handle != NULL` |
| `ENT_Close(&handle)` | `ENT_SYS_NORMAL` | `ENT_INIT_INVALID_ARGUMENT` when the handle pointer itself is NULL; `ENT_SYS_CLOSE_UNINITIALIZED` when `*handle == NULL`; `ENT_SYS_BAD_HANDLE` for an invalid handle object / magic mismatch |
| `ENT_Run(handle)` | `ENT_SYS_NORMAL` when the instance is stopped normally by `ENT_Stop()` | `ENT_SYS_RUN_UNINITIALIZED` when `handle == NULL`; `ENT_SYS_BAD_HANDLE` for an invalid handle object / magic mismatch; `ENT_SYS_STOPPED` when stop was already requested |
| `ENT_Stop(handle)` | `ENT_SYS_NORMAL` | `ENT_SYS_INVALID_ARGUMENT` when `handle == NULL`; `ENT_SYS_BAD_HANDLE` for an invalid handle object / magic mismatch; `ENT_SYS_STOPPED` when the instance is already stopped |
| `ENT_SetRtAttributes(handle, ...)` | `ENT_SYS_NORMAL`; `ENT_RT_NOTRT` is the expected return when realtime mode is not available | `ENT_RT_NOT_INITIALIZED` when the runtime side is not initialized; `ENT_SYS_BAD_HANDLE` for an invalid handle object / magic mismatch |

成功 `ENT_Close(&handle)` 后，调用方变量会被置为 `NULL`，之前保存的 raw 复制值不再有可调用契约。
一旦 `ENT_Close()` 开始，调用方就不应再从其他线程并发发起新的 `ENT_Run()`、`ENT_Stop()` 或 `ENT_SetRtAttributes()`；
这类互斥应由上层 owner lock / lifecycle lock 自行保证。
这就是当前冻结的 API contract，不会通过 registry 自动升级成“close 开始后任意并发也安全”。

### 9.3 它的边界

`ENT_HANDLE` 隔离的是**实例状态**，不是把库里所有东西都做成完全物理隔离。
当前实现更准确的理解是：

- `ENT_HANDLE` 负责隔离每个实例自己的 `ENT_CTX`
- 某些基础设施仍然可能由库内部统一管理，例如部分日志服务或公共子系统

因此，多实例设计既要保证：

- A、B 两个 handle 的上下文互不覆盖

也要保证：

- 共享基础设施不会因为某一个实例失败或关闭被误杀
- 不要把同一个 `ENT_HANDLE` 句柄重复 close
- 如果需要并发运行多个实例，建议把 `ENT_Run()` 放到各自 worker thread 中调度
- `ENT_Run()` 被 `ENT_Stop()` 正常唤醒后返回 `ENT_SYS_NORMAL`
- 如果需要让 `ENT_Run()` 提前退出，先调用 `ENT_Stop()`，join worker thread，再调用 `ENT_Close()`
- 如果还会并发发起新的入口，必须先把 owner lock / lifecycle lock 置于互斥态，再开始 `ENT_Close()`

当前仓库测试已覆盖：

- 多实例独立初始化
- 一个实例关闭后另一个实例仍可继续运行
- 第二个实例初始化失败不会破坏第一个实例
## 10. 当前测试覆盖概览

已接入 `ctest` 的测试目标包括：

- `test_ent_init`
- `test_ent_msg`
- `test_ent_script`
- `test_utl_dll`
- `test_utl_thread`
- `test_ent_thread`
- `test_utl_tpool`
- `test_utl_timer`
- `test_utl_socket`
- `test_ent_db`
- `test_ent_log`
- `test_security`

其中 `test_security` 重点覆盖：

- 输入校验
- 过长字符串防御
- NULL 参数拒绝
- SQL 注入场景
- 参数化数据库接口的安全行为

另外 `test_ent_init` 已补充多实例覆盖，包括：

- 双实例独立初始化 / 运行 / 关闭
- 第二实例在 lock 初始化阶段失败
- 第二实例在 log option 设置阶段失败
- 已成功实例在另一实例失败后仍可继续运行和关闭

---

## 11. PostgreSQL / MySQL / SQLite 说明

- SQLite / MySQL / PostgreSQL 都是**按构建结果启用**，不是运行时热插拔。
- PostgreSQL 现在有显式顶层开关 `ENT_ENABLE_PGSQL`。
- Windows CI 当前默认关闭 PostgreSQL 探测：
  - Windows CI also requires SQLite / MySQL / PostgreSQL to be available.
- 静态库 `ent_s` 已显式传播系统库和数据库库依赖，便于下游静态链接。

### 11.1 DB 并发与生命周期语义

如果把 `ent` 的 DB 模块当作**高并发 / 公共库接口**来用，建议按下面的语义理解，而不是把它当成“随时可重配、随时可硬关”的轻量封装。

#### 句柄生命周期状态机

每个 `DB_HANDLE` 现在都有显式生命周期状态机：

- `ENT_DB_HANDLE_CREATED_E`
- `ENT_DB_HANDLE_ACTIVE_E`
- `ENT_DB_HANDLE_CLOSING_E`
- `ENT_DB_HANDLE_CLOSED_E`

对外可以这样理解：

- `ACTIVE`：允许 `open/read/write/readParams/writeParams`
- `CLOSING`：拒绝新的 DB 操作进入，并等待在途操作退出
- `CLOSED`：句柄已经失效，不可再复用

主路径包括：

- `ENT_DbOpen`
- `ENT_DbRead`
- `ENT_DbWrite`
- `ENT_DbReadParams`
- `ENT_DbWriteParams`
- `ENT_DbCloseHandle`
- `iENT_DbReInit`

其中：

- 读写 / open 会登记活跃操作
- `ENT_DbCloseHandle()` 会先禁止新请求进入，再等待在途请求退出，然后才真正关闭后端连接并释放句柄
- `iENT_DbReInit()` 在句柄忙时不会在线热切换配置，而是直接拒绝

#### `ENT_DbCloseHandle()` 的语义

`ENT_DbCloseHandle()` 不再是“立即 free 句柄”的语义，而是：

1. 把 handle 状态切到 `CLOSING`
2. 拒绝新的 DB 操作进入
3. 等待当前已进入的活跃操作退出
4. 再关闭连接并释放资源

这意味着：

- 如果一个线程正在 `ENT_DbRead()` / `ENT_DbWrite()`
- 另一个线程调用 `ENT_DbCloseHandle()`

那么 close 会等待在途请求结束，而不是直接抢先释放句柄。

当 handle 已经处于 `CLOSING` 时：

- 新的 `ENT_DbOpen`
- 新的 `ENT_DbRead`
- 新的 `ENT_DbReadParams`
- 第二次 `ENT_DbCloseHandle`

都会返回：

- `ENT_DBS_IN_USE`

当 `ENT_DbCloseHandle()` 已经成功完成后，句柄就变成**无效句柄**，不能再继续复用。

#### `ENT_DbClose()` 的语义

`ENT_DbClose()` 关闭的是**DB 服务层**，不是单个 handle。

如果当前仍然存在 live handle，`ENT_DbClose()` 会返回：

- `ENT_DBS_IN_USE`

也就是说，推荐顺序是：

1. 先关闭所有 `DB_HANDLE`
2. 再调用 `ENT_DbClose()` 关闭 DB 服务

而不是反过来。

#### `iENT_DbReInit()` 的语义

`iENT_DbReInit()` 现在更适合理解为：

- 初始化期 / 停机期接口
- 非忙状态下的重配置接口

如果当前 handle 正在：

- 执行活跃 read/write
- 或已经进入 `CLOSING`

则 `iENT_DbReInit()` 会返回：

- `ENT_DBS_IN_USE`

因此它**不是**一个可随意在线热切换连接配置的接口。

#### 推荐的 API 使用方式

从安全和长期维护角度，推荐默认优先使用：

- `ENT_DbReadParams`
- `ENT_DbWriteParams`

而不是直接把外部输入拼到：

- `ENT_DbRead`
- `ENT_DbWrite`

中。

也就是说：

- raw SQL API 更适合固定 SQL、内部受控场景
- 带外部输入的查询 / 写入，优先走参数化接口

#### 推荐使用原则

如果你把 `ent` DB 模块作为公共组件使用，建议遵循这些原则：

- 不要在业务线程仍在使用 handle 时强行 close
- 不要把 `iENT_DbReInit()` 当成运行期热切换接口
- 先关所有 handle，再关 DB service
- 默认优先使用参数化接口
- 多线程共享同一 handle 时，明确由上层约束谁负责生命周期收尾
- `ENT_DbCloseHandle()` 成功返回后，句柄立即视为失效句柄，不能继续复用

当前测试已覆盖的生命周期风险点包括：

- `ENT_DbClose()` 在仍有 live handle 时必须返回 `ENT_DBS_IN_USE`
- `ENT_DbCloseHandle()` 在 active read 未退出时必须等待
- 句柄进入 `CLOSING` 后，新的 `open/read/readParams/close` 必须返回 `ENT_DBS_IN_USE`
- `iENT_DbReInit()` 在 active read 未退出时必须返回 `ENT_DBS_IN_USE`

### 11.2 Log 并发与生命周期语义

日志模块现在也采用了显式句柄状态机，语义与 DB 模块保持一致的 service-level / handle-level 生命周期风格。

#### 日志句柄状态机

每个日志句柄都有明确状态：

- `ENT_LOG_HANDLE_CREATED_E`
- `ENT_LOG_HANDLE_ACTIVE_E`
- `ENT_LOG_HANDLE_CLOSING_E`
- `ENT_LOG_HANDLE_CLOSED_E`

其中：

- `ACTIVE`：允许写日志和更新配置
- `CLOSING`：拒绝新的 writer 进入、拒绝新的配置更新，并等待在途 writer 退出
- `CLOSED`：句柄失效，不可复用

#### `ENT_LogCloseHandle()` 的语义

`ENT_LogCloseHandle()` 现在是显式 close 流程：

1. `ACTIVE -> CLOSING`
2. 拒绝新 writer 进入
3. 等待 active writer 归零
4. 停止 buffer thread、flush/close 文件
5. 释放句柄资源并进入 `CLOSED`

如果第二次 close 命中 `CLOSING`，会返回 in-use/busy 风格错误（当前日志模块仍使用 `-3`）。

#### `ENT_LogClose()` 的 service-level 语义

`ENT_LogClose()` 关闭的是日志服务本身，不是单个句柄。

当仍有 live log handle 时会拒绝关闭并返回 busy/in-use 风格错误（当前为 `-3`）。推荐顺序是：

1. 先关闭所有日志句柄（包括默认句柄和私有句柄）
2. 再调用 `ENT_LogClose()` 关闭日志服务

`ENT_LogClose()` 只负责 service 级别资源收口，不会替代 `ENT_LogCloseHandle()` 去强行回收仍在使用中的句柄。

#### `ENT_LogCtx*()` 的 ownership 语义

显式 context API 只能操作同一个 context 创建的句柄：

- `ENT_LogCtxInitHandle(ctx, &handle, ...)` 创建由 `ctx` 拥有的句柄
- `ENT_LogCtxSetOption()`、`ENT_LogCtxCloseHandle()` 和 `ENT_LogCtxRaw/Fatal/Error/Warn/Print/Debug()` 只接受同一个 `ctx` 拥有的句柄
- 空 context 不能代理默认句柄，也不能操作其他 context 的句柄
- 将 foreign handle、非 context 显式句柄或默认句柄 `NULL` 传给 context API，会返回 `ENT_LOG_BAD_HANDLE`
- 默认句柄和 `ENT_LogInitHandle()` 创建的非 context 显式句柄，应使用非 context 的 `ENT_Log*()` API
- `ENT_LogCtxClose(ctx)` 会在释放 context 前自动关闭仍然 live 的 context-owned handle

#### closing 状态下的拒绝行为

日志句柄进入 `CLOSING` 后：

- 新写入（`raw/print/fatal/error/warn/debug`）会被拒绝
- 新配置（`ENT_LogSetOption`）会被拒绝

这可以避免 close 期间出现“边写边销毁”或“先释放旧配置后新配置校验失败”导致的状态破坏。

#### 当前日志返回值语义

日志模块的返回值已经切入 `msg/ent.msg` 生成链路，`ENT_LOG_*` 码来自新增的 `LOG` 子模块。公共 API 的成功路径继续使用 `ENT_SYS_NORMAL`，过滤 / no-op 场景使用 `ENT_LOG_NON_FATAL`，其余错误按具体原因返回更细的日志消息码。

---

## 12. Lua 说明

Lua 后端默认关闭：

```bash
cmake -S . -B build -DENT_ENABLE_LUA=ON -DENT_LUA_LINK_MODE=static
```

静态模式默认约定源码位于：

```text
3rd/lua/src
```

若源码树不完整，构建系统会给出 warning 并自动关闭 Lua 后端。

---

## 13. 兼容性说明

老 README 中保留过较早期的 VS 工程使用痕迹；当前推荐方式已经统一到 **CMake + CI 脚本 + 安装后下游消费验证 + runtime/devel 打包**。

如果你要把 `ent` 当作对外发布的 SDK 来用，建议优先采用本 README 中的：

- CMake 选项控制
- `cmake --install`
- `find_package(ent CONFIG REQUIRED)`
- `ent::ent` / `ent::ent_s` 目标链接
- runtime / devel 分离分发
- 与目标位宽一致的第三方库集合（Win32 全链路 32 位 / x64 全链路 64 位）
## 14. Windows / Linux 依赖与构建

数据库后端统一优先使用系统安装包或 vcpkg 管理的依赖，不再从仓库内的 `3rd` 目录回退取二进制库。

### Windows

按架构分别建立构建目录，不要在同一个 build tree 里混用 x86 和 x64。

```powershell
cmake -S . -B build-win-x64 -G "Visual Studio 17 2022" -A x64 `
  -DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake `
  -DVCPKG_TARGET_TRIPLET=x64-windows
cmake --build build-win-x64 --config Release

cmake -S . -B build-win-x86 -G "Visual Studio 17 2022" -A Win32 `
  -DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake `
  -DVCPKG_TARGET_TRIPLET=x86-windows
cmake --build build-win-x86 --config Release
```

本地 vcpkg 验证记录（2026-05-02）：

- `x86-windows`：`build-win-x86-vcpkg` 使用 `-A Win32`、`-DVCPKG_TARGET_TRIPLET=x86-windows`、`-DENT_ENABLE_SQLITE=ON`、`-DENT_ENABLE_MYSQL=ON`、`-DENT_ENABLE_PGSQL=ON` 配置通过，`cmake --build build-win-x86-vcpkg --config Release` 构建通过。
- `x64-windows`：`build-win-x64-vcpkg` 使用 `-A x64`、`-DVCPKG_TARGET_TRIPLET=x64-windows`、`-DENT_ENABLE_SQLITE=ON`、`-DENT_ENABLE_MYSQL=ON`、`-DENT_ENABLE_PGSQL=ON` 配置通过，`cmake --build build-win-x64-vcpkg --config Release` 构建通过。
- vcpkg manifest 依赖为 `sqlite3`、`libmariadb`、`libpq`；两个架构均由 vcpkg 安装对应 triplet 的库，不再混用仓库内 `3rd/mysql` 或 `3rd/sqlite` 二进制。
- CMake 导出规则中，数据库后端的第三方 include 目录只作为库自身的 `PRIVATE` 编译输入；安装后的 `ent` / `ent_s` target 不应暴露 `vcpkg_installed/<triplet>/include` 这类 build-tree 路径。

### Linux

优先安装发行版开发包，再通过 `pkg-config` 发现库。

```bash
sudo apt install build-essential cmake pkg-config `
  libsqlite3-dev libmariadb-dev libpq-dev
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

### WSL

如果要把代码同步到 `/home/lf/workspace` 后再在 WSL 里编译，直接用仓库里的脚本：

```powershell
scripts\run-ci-wsl.ps1
```

脚本会把当前仓库同步到 `/home/lf/workspace/ent`，然后在 WSL 内执行 `cmake` 和 `ctest`。

### 说明

- SQLite 对应 `sqlite3`
- MySQL / MariaDB 对应 `libmariadb` 或 `mysqlclient`
- PostgreSQL 对应 `libpq`
- Lua 仍然使用仓库内的 `3rd/lua` 源码目录
