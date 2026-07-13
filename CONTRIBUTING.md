# Contributing to ent

Thank you for your interest in contributing. This document explains how to
set up a development environment, follow the project conventions, and submit
changes that can be reviewed and verified consistently.

By intentionally submitting a contribution for inclusion in ent, you agree
that the contribution is provided under the Apache License 2.0, unless you
explicitly state otherwise before submission.

---

## Project Scope

Before designing an API or implementation, read
[`OPEN_SOURCE_SCOPE.md`](OPEN_SOURCE_SCOPE.md).

This repository publishes the reusable **ent core**. Suitable contributions
include generally useful runtime, lifecycle, logging, concurrency, basic local
IPC, database-wrapper, build, test, documentation, and packaging improvements.

The following categories are intentionally outside this public repository:

- active/standby or primary/backup high availability;
- leader election, quorum, fencing, or split-brain prevention;
- snapshot, journal, incremental, or cross-node replication;
- advanced multi-writer, RCU, transactional, or lock-free shared memory;
- distributed synchronization or industrial real-time state replication;
- libevent/libev integration layers;
- RPC, XDR, gRPC, service discovery, retry, flow-control, or streaming layers;
- private product operations, licensing, or deployment control.

Do not submit placeholder APIs, stubs, speculative roadmap documents, or TODOs
for excluded capabilities. A request may be closed as out of scope without
implying that its use case is invalid.

Every feature PR must state its scope-check result and confirm that the public
core remains independent of private repositories, binaries, services, and CI
artifacts.

---

## Development Environment

| Requirement | Minimum / supported version | Notes |
|---|---|---|
| CMake | Project minimum 3.5; 3.16+ recommended for contributors | CI uses a current CMake release |
| C compiler | MSVC 2022 / GCC 11 / Clang 14 or newer | C99 required |
| SQLite dev headers | Only when `ENT_ENABLE_SQLITE=ON` | `libsqlite3-dev` on Ubuntu |
| MySQL/MariaDB dev headers | Only when `ENT_ENABLE_MYSQL=ON` | `default-libmysqlclient-dev` on Ubuntu |
| PostgreSQL dev headers | Only when `ENT_ENABLE_PGSQL=ON` | `libpq-dev` on Ubuntu |
| Ninja | Any recent version | Optional but recommended on Linux |

All database backends default to `ON`, but SQLite, MySQL/MariaDB, and
PostgreSQL are independently optional. Install only the development packages
for the backends you enable.

### Linux quick-start

Full database configuration:

```bash
sudo apt-get install -y ninja-build libsqlite3-dev \
  default-libmysqlclient-dev libpq-dev

cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j$(nproc)
ctest --test-dir build --output-on-failure
```

Minimal configuration without database backends:

```bash
cmake -S . -B build-minimal -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DENT_ENABLE_SQLITE=OFF \
  -DENT_ENABLE_MYSQL=OFF \
  -DENT_ENABLE_PGSQL=OFF
cmake --build build-minimal -j$(nproc)
ctest --test-dir build-minimal --output-on-failure
```

### Windows quick-start

Use the vcpkg-based helper script with Visual Studio 2022 Build Tools:

```powershell
.\scripts\run-ci-windows.ps1 configure
.\scripts\run-ci-windows.ps1 build
.\scripts\run-ci-windows.ps1 test
```

The helper maps the `ENT_ENABLE_*` environment variables to both CMake backend
switches and vcpkg manifest features. For example:

```powershell
$env:ENT_ENABLE_SQLITE = "ON"
$env:ENT_ENABLE_MYSQL = "OFF"
$env:ENT_ENABLE_PGSQL = "OFF"
$env:WINDOWS_CMAKE_PLATFORM = "x64"
$env:VCPKG_TARGET_TRIPLET = "x64-windows"
.\scripts\run-ci-windows.ps1 all
```

---

## Coding Style

Rules are also maintained in [`AGENTS.md`](AGENTS.md). The essentials are:

- **Indentation:** 4 spaces, no tabs.
- **Braces:** opening brace on its own line for functions and control blocks.
- **Public API prefix:** `ENT_` or `UTL_`.
- **Internal helpers:** `i` prefix, for example `iENT_CTXFree`.
- **Types:** upper snake case, for example `MSG_ID_T`.
- **Headers:** paired as `ent_*` / `utl_*`.
- No global formatter is enforced; match the style of adjacent code.

---

## Lifecycle and Concurrency Rules

These rules are non-negotiable and are checked by the test and sanitizer
matrix:

1. Every successful `Init` has a matching `Close`.
2. Public pointer-to-handle close APIs must leave the caller's handle `NULL`
   after successful destruction.
3. Shared state must use the existing synchronization primitives. `volatile`
   is not a substitute for synchronization.
4. Tests must not use a fixed `sleep` / `Sleep` as the synchronization
   mechanism. Use join, condition variables, events, or explicit test gates.
5. A close path must clearly distinguish between "resource remains live" and
   "resource was destroyed" in both its return value and handle state.
6. Basic shared-memory changes must preserve the documented local,
   caller-synchronized contract and must not silently introduce distributed or
   advanced multi-writer semantics.

---

## Writing Tests

- New lifecycle, error, concurrency, optional-feature, or platform-specific
  code must be accompanied by a test.
- Tests live in `test/` or a focused adjacent test directory and are integrated
  through CMake/CTest.
- Cover the success path, failure path, repeated close, and resource release
  where applicable.
- Failure-injection tests may compile production sources directly when a
  narrow test seam is required. Keep these tests focused and document why.
- Do not delete, skip, or weaken existing tests to obtain a green CI run.
- Public tests must not require private repositories, private binaries, private
  services, or private product data.

---

## Commit and PR Guidelines

- One PR should form one auditable, verifiable, and revertible closure.
- Commit messages use the imperative mood, for example
  `fix ent_log close return code`.
- PR descriptions must include:
  - Summary of observable behaviour changes
  - Modified file list
  - Scope-check result against `OPEN_SOURCE_SCOPE.md`
  - Commands actually executed
  - Tested and untested platforms/configurations
  - Rollback path
- All required CI checks must pass before merge:
  - Linux normal
  - Linux ASan + UBSan
  - Linux TSan
  - Windows x86 and x64 normal
  - Windows x64 ASan
  - Optional database configuration jobs when backend selection changes
- Do not force-push after review comments unless coordinated with reviewers.

---

## Reporting Security Issues

Do not open a public issue for a suspected vulnerability. Follow
[`SECURITY.md`](SECURITY.md).

---

## Known Limitations

- **macOS** is not continuously tested in CI.
- **0.x API stability:** public APIs may change across minor versions and ABI
  compatibility is not guaranteed until 1.0.
- Optional database combinations are tested, but applications are responsible
  for installing and deploying the runtime libraries of the backends they
  choose to enable.
- The shared-memory module is limited to the documented local IPC contract and
  does not provide distributed replication or advanced multi-writer guarantees.
