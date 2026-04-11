1. 配置项目
   假设机器上已经安装了cmake 3及visual studio 2010等开发工具，并安装了NSIS打包工具
   mkdir -p build
   cd build
   cmake ..
2. 构建项目
   cmake --build .
   如需禁用数据库后端，可在配置时追加 `-DENT_ENABLE_MYSQL=OFF` 或 `-DENT_ENABLE_SQLITE=OFF`
   如需启用 Lua 脚本后端，可在配置时追加 `-DENT_ENABLE_LUA=ON`
   Lua 链接方式默认静态，可通过 `-DENT_LUA_LINK_MODE=shared` 切换为动态库
3. 切换构建类型
   cmake -D CMAKE_BUILD_TYPE=Release ..
4. 使用ide
   进入build目录，使用visual studio 2010打开ent.sln

5. 测试补充记录
   当前已补充并接入 `ctest` 的测试目标：
   `test_ent_init`、`test_utl_dll`、`test_utl_thread`、`test_ent_thread`、`test_utl_tpool`、`test_utl_timer`、`test_utl_socket`、`test_ent_db`、`test_ent_log`、`test_security`
   覆盖范围包括：
   `ent_init` 的初始化/关闭、输入校验、失败回收
   `utl_dll` 的空链表、头插尾插、头删尾删
   `utl_thread` 的锁/CV 参数校验和最小加锁解锁路径
   `ent_thread` 的参数校验、创建/等待/关闭生命周期
   `utl_tpool` 的参数校验和 worker 启动失败路径
   `utl_timer` 的初始化、关闭、单次和周期定时器
   `utl_socket` 的参数校验和 localhost 最小回环收发
   `ent_db` 的 SQLite-only 初始化、打开、读写回环、非法 SQL、回调失败路径
   `ent_log` 的日志系统初始化判断、多级别选项及动态输出校验
   `security` 安全审计：ent_init sprintf 缓冲区越界、ent_log 空路径下溢/负数 maxNum/非法日志等级、ent_db SQL 注入（DROP TABLE/UNION SELECT）、NULL sql 拒绝

6. SQLite-only 测试配置
   当前数据库测试按 SQLite-only 方式验证，可使用以下命令重新配置：
   `cmake -S . -B build -DENT_ENABLE_SQLITE=ON -DENT_ENABLE_MYSQL=OFF`

7. Lua 第三方源码约定（静态链接优先）
   默认约定 Lua 源码位于：
   `3rd/lua/src`
   其中应包含 `lua.h` 和 Lua 5.4 的核心 `.c` 文件（不含 `lua.c` / `luac.c` 可执行入口也可）。
   启用 Lua 静态构建示例：
   `cmake -S . -B build -DENT_ENABLE_LUA=ON -DENT_LUA_LINK_MODE=static`
   若源码不存在，构建系统会给出警告并自动关闭 Lua 后端。

8. 验证命令
   在 `build` 目录执行：
   `ctest --output-on-failure`
   如需单独验证数据库测试：
   `ctest --output-on-failure -R test_ent_db`
   示例程序构建验证：
   `cmake --build build --target example01`

9. GitHub Actions
   当前仓库已提供 Linux (`ubuntu-24.04`) 和 Windows (`windows-2022`) 的 CI。
   两个平台都通过共享脚本执行：
   Linux: `./scripts/run-ci-linux.sh`
   Windows: `.\scripts\run-ci-windows.ps1`
   阶段统一为：
   `configure`、`build`、`test`、`perf`
   当前功能测试已恢复到双平台 `10/10`，性能冒烟也可完整执行。

10. CI 经验文档
   这次 GitHub Actions 的平台差异、踩坑记录、脚本化经验和后续项目可复用的 checklist 已整理到：
   `docs/github-actions-ci-playbook.md`

11. 本地下游消费安装
   如需让其他 CMake 项目通过 `find_package(ent CONFIG REQUIRED)` 使用本库，可先本地安装：
   `cmake -S . -B build-install -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release`
   `cmake --build build-install --parallel 4`
   `cmake --install build-install --prefix /Users/lifei/test/ent/.local-install`
   下游项目配置时传入：
   `-DCMAKE_PREFIX_PATH=/Users/lifei/test/ent/.local-install`
