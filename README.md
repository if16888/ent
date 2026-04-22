# ent

`ent` 是一个跨平台 C 组件库，当前主要提供以下能力：

- 运行时初始化与多实例 runtime 管理
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
- `-DENT_ALLOW_VENDORED_DB_LIBS=ON|OFF`
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

## 9. 多实例 runtime 使用示例

`ent` 现在支持通过 `ENT_RUNTIME` 句柄管理多个隔离的 runtime 实例。典型流程如下：

```c
#include "ent_init.h"
#include "ent_log.h"
#include "ent_msg.h"

int main(void)
{
    ENT_RUNTIME runtimeA = NULL;
    ENT_RUNTIME runtimeB = NULL;
    MSG_ID_T sts = 0;

    sts = ENT_RuntimeInit(&runtimeA, "node_a", "./work_a", LOG_LEV_WARN_E, ENT_MODE_NORMAL_E);
    if(sts != ENT_SYS_NORMAL)
    {
        return 1;
    }

    sts = ENT_RuntimeInit(&runtimeB, "node_b", "./work_b", LOG_LEV_WARN_E, ENT_MODE_NORMAL_E);
    if(sts != ENT_SYS_NORMAL)
    {
        ENT_RuntimeClose(runtimeA);
        return 1;
    }

    /* 可选：对单个 runtime 设置 RT 属性 */
    sts = ENT_RuntimeSetRtAttributes(runtimeA, -1, ENT_RT_POLICY_OTHER_E, 0);
    if(sts != ENT_SYS_NORMAL && sts != ENT_RT_NOTRT)
    {
        ENT_RuntimeClose(runtimeB);
        ENT_RuntimeClose(runtimeA);
        return 1;
    }

    /* 实际项目中通常在各自线程中运行 */
    ENT_RuntimeRun(runtimeA);
    ENT_RuntimeRun(runtimeB);

    ENT_RuntimeClose(runtimeB);
    ENT_RuntimeClose(runtimeA);
    return 0;
}
```

### 9.1 `ENT_RUNTIME` 的作用

`ENT_RUNTIME` 的核心作用是：

**把原来全局唯一的运行时上下文，变成“每个实例各自一份上下文”的显式句柄。**

也就是说：

- `ENT_Init / ENT_Run / ENT_Close` 更偏向**单实例 / 全局实例**模式
- `ENT_RuntimeInit / ENT_RuntimeRun / ENT_RuntimeClose` 则是**多实例 / 显式实例句柄**模式

可以把它理解成下面这个差异：

```text
单实例模式
-----------
process
  └── gEntCtx
       ├── entName
       ├── workPath
       ├── logPath
       ├── entLock
       ├── entCV
       └── init / rt state

多实例模式
-----------
process
  ├── runtimeA -> ENT_CTX_A
  │      ├── entName = node_a
  │      ├── workPath = ./work_a
  │      ├── logPath
  │      ├── entLock
  │      ├── entCV
  │      └── init / rt state
  │
  └── runtimeB -> ENT_CTX_B
         ├── entName = node_b
         ├── workPath = ./work_b
         ├── logPath
         ├── entLock
         ├── entCV
         └── init / rt state
```

这意味着在同一个进程里，你可以：

- 同时持有多个 runtime
- 给每个 runtime 不同的 `name / workPath / logPath`
- 分别初始化、运行、关闭它们
- 避免一个实例的失败或关闭直接覆盖另一个实例的上下文

### 9.2 什么时候应该用 `ENT_RUNTIME`

更适合用多实例接口的场景包括：

- 一个进程里同时管理多个逻辑节点 / 站点 / 对象
- 把 `ent` 当作 SDK 嵌入到更大的宿主程序中
- 测试 / 仿真 / 多租户场景下，希望不同实例各自持有独立状态

### 9.3 它的边界

`ENT_RUNTIME` 隔离的是**实例状态**，不是把库里所有东西都做成完全物理隔离。

当前实现更准确的理解是：

- `ENT_RUNTIME` 负责隔离每个实例自己的 `ENT_CTX`
- 某些基础设施仍然可能由库内部统一管理，例如部分日志服务或公共子系统

因此，多实例设计既要保证：

- A、B 两个 runtime 的上下文互不覆盖

也要保证：

- 共享基础设施不会因为某一个实例失败或关闭被误关

这也是为什么仓库里现在专门补了多实例失败路径和关闭顺序测试。

建议遵循这几个原则：

- 每个 `ENT_RUNTIME` 对应独立的 `name/workPath`
- 失败路径下，只关闭已经成功初始化的实例
- 不要把同一个 `ENT_RUNTIME` 句柄重复 close
- 如果需要并发运行多个实例，建议在各自线程中调度 `ENT_RuntimeRun()`

当前仓库测试已覆盖：

- 多实例独立初始化
- 一个实例关闭后另一个仍可继续运行
- 第二个实例初始化失败不会破坏第一个实例

---

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
  - `WINDOWS_DISABLE_PGSQL=ON`
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

#### closing 状态下的拒绝行为

日志句柄进入 `CLOSING` 后：

- 新写入（`raw/print/fatal/error/warn/debug`）会被拒绝
- 新配置（`ENT_LogSetOption`）会被拒绝

这可以避免 close 期间出现“边写边销毁”或“先释放旧配置后新配置校验失败”导致的状态破坏。

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
