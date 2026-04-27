# Repository Guidelines

## Project Structure & Module Organization
`comm/` contains the core library implementation, including initialization, logging, threading, database access, sockets, timers, and thread-pool utilities. Public headers live in `inc/` and expose the library API with the `ent_*.h` naming pattern. `example/` contains the sample consumer program `example01.c`. Third-party runtime dependencies are stored under `3rd/`, notably MySQL and SQLite binaries used on Windows. Top-level `CMakeLists.txt` builds the C library and example target and packages installs with CPack.

## Build, Test, and Development Commands
Configure an out-of-source build:

```bash
mkdir -p build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
```

Build all targets with `cmake --build .`. Run the sample app with `./bin/example01` on Unix-like systems or the generated executable from the build directory on Windows. Install artifacts with `cmake --install .`. Packaging is enabled through CPack, so `cpack` from the build directory produces distributable archives when dependencies are present.

## Coding Style & Naming Conventions
Follow the existing C style: 4-space indentation, braces on their own line for functions and control blocks, and compact `if(...)` / `for(...)` spacing as used in `comm/ent_init.c`. Preserve the current naming scheme: public APIs use `ENT_` or `UTL_` prefixes, internal helpers use an `i` prefix (for example `iENT_CTXFree`), types are upper snake case (`MSG_ID_T`), and source/header pairs keep matching `ent_*` or `utl_*` names. No formatter configuration is committed, so keep changes stylistically consistent with adjacent files.

## Message Code Rules
When editing `msg/ent.msg` or generated message-code usage, follow these constraints:
- `module` and `submodule` names must be uppercase and must not exceed 4 characters.
- Prefer `module` names with 3 characters when possible for readability and long-term consistency.
- Keep message symbols aligned with the existing `ENT_<SUBMODULE>_<NAME>` convention.
- Current ENT submodules are: `SYS`, `INIT`, `RT`, `THRD`, `UTHD`, `SOCK`, `TMR`, `TPL`, `DLL`, `SCR`, `DBS`, `LOG`.
- `SCR` submodule is reserved for script runtime lifecycle and execution errors (load/compile/runtime/function lookup).
- `DBS` submodule is reserved for database service and backend lifecycle errors (argument validation, unsupported backend, allocation/open/query/result failures).
- Parameterized DB APIs use the `DBS` submodule as well; keep `BAD_PARAMS`, `PARAM_COUNT`, `PREPARE_FAILED`, `BIND_FAILED`, and `EXEC_FAILED` aligned with `msg/ent.msg` when extending the DB surface.
- `LOG` submodule is reserved for log service and handle lifecycle errors (argument validation, initialization state, handle state, allocation/path/format/thread/IO failures).
- Any message-code change (new/removed/renamed submodule, code, or text) must be synced in this `AGENTS.md` file in the same change set so future contributors see the latest rules and conventions.

## Testing Guidelines
There is no active test suite in this repository yet: the top-level CMake test block is present but commented out. Treat `example/example01.c` as the current smoke test and rebuild it after changes to library code. If you add tests, prefer a dedicated `test/` directory with CMake integration via `enable_testing()` and name files after the module under test, such as `test_ent_log.c`.

## Commit & Pull Request Guidelines
Recent history uses short, imperative messages like `update readme` and `modfiy for init bug`. Keep commits focused and use concise summaries that state the affected area, for example `fix ent_init null-path cleanup`. Pull requests should describe the behavioral change, list build steps used for verification, and call out platform impact when touching Windows-specific libraries or MySQL/SQLite linkage.

## Dependency & Configuration Notes
Linux builds expect system MySQL and SQLite development libraries. Windows builds rely on binaries under `3rd/` and define `_UNICODE`. Avoid hard-coding local library paths beyond what the current CMake files already require.
