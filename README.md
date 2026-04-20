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
- Release 阶段按平台 / 架构打包 SDK 产物

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

Release workflow 现在会按平台 / 架构打包 SDK：

- Linux: `ent-<tag>-linux-x86_64-sdk.tar.gz`
- Windows: `ent-<tag>-windows-win32-sdk.zip`

打包内容来自 `cmake --install` 的安装树，因此会包含：

- `include/`
- `lib/`
- `bin/`（如有运行时文件）
- `CMake package files`

这样下游可以直接解压后使用 `CMAKE_PREFIX_PATH` 指向安装目录。

---

## 9. 当前测试覆盖概览

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

---

## 10. PostgreSQL / MySQL / SQLite 说明

- SQLite / MySQL / PostgreSQL 都是**按构建结果启用**，不是运行时热插拔。
- PostgreSQL 现在有显式顶层开关 `ENT_ENABLE_PGSQL`。
- Windows CI 当前默认关闭 PostgreSQL 探测：
  - `WINDOWS_DISABLE_PGSQL=ON`
- 静态库 `ent_s` 已显式传播系统库和数据库库依赖，便于下游静态链接。

---

## 11. Lua 说明

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

## 12. 兼容性说明

老 README 中保留过较早期的 VS 工程使用痕迹；当前推荐方式已经统一到 **CMake + CI 脚本 + 安装后下游消费验证 + SDK 打包**。

如果你要把 `ent` 当作对外发布的 SDK 来用，建议优先采用本 README 中的：

- CMake 选项控制
- `cmake --install`
- `find_package(ent CONFIG REQUIRED)`
- `ent::ent` / `ent::ent_s` 目标链接
- 与目标位宽一致的第三方库集合（Win32 全链路 32 位 / x64 全链路 64 位）
