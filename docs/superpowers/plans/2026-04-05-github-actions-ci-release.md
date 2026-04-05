# GitHub Actions CI And Release Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a minimal GitHub Actions pipeline that builds, tests, packages, and publishes artifacts for Linux and Windows without depending on vendored database binaries on Windows.

**Architecture:** Keep the first version intentionally small. CMake will prefer discovered system libraries on Linux and vcpkg-provided libraries on Windows, with vendored Windows libraries only as a fallback outside CI. GitHub Actions will use one CI workflow for push/PR validation and one release workflow for tag-based packaging and artifact publishing.

**Tech Stack:** CMake, CPack, GitHub Actions, Ubuntu apt packages, Windows vcpkg

---

### Task 1: Make database dependency discovery CI-friendly

**Files:**
- Modify: `comm/CMakeLists.txt`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Confirm the current red state**

Run:
```bash
python3 - <<'PY'
from pathlib import Path
text = Path('comm/CMakeLists.txt').read_text()
print('../3rd/sqlite' in text)
print('../3rd/mysql' in text)
PY
```

Expected: `True` and `True`, proving Windows still depends on vendored libraries.

- [ ] **Step 2: Prefer discovered libraries on every platform**

Update `comm/CMakeLists.txt` so that:
```cmake
option(ENT_ENABLE_SQLITE "Enable SQLite backend" ON)
option(ENT_ENABLE_MYSQL "Enable MySQL backend" ON)
option(ENT_ALLOW_VENDORED_DB_LIBS "Allow vendored DB libraries as a fallback" ON)
```

Windows path should first try `find_path()` / `find_library()` using the active toolchain and only fall back to `../3rd/...` when `ENT_ALLOW_VENDORED_DB_LIBS` is `ON` and discovery fails.

- [ ] **Step 3: Link using discovered library variables**

Update target linking so shared-library link lines use variables such as:
```cmake
if(ENT_ENABLE_SQLITE AND ENT_SQLITE_LIBRARY)
  target_link_libraries(${LIB_NAME_SHARE} PRIVATE ${ENT_SQLITE_LIBRARY})
endif()

if(ENT_ENABLE_MYSQL AND ENT_MYSQL_LIBRARY)
  target_link_libraries(${LIB_NAME_SHARE} PRIVATE ${ENT_MYSQL_LIBRARY})
endif()
```

Also update Windows install rules in `CMakeLists.txt` so they only install vendored DLLs when vendored fallback is actually in use.

- [ ] **Step 4: Re-run the red-check command**

Run:
```bash
python3 - <<'PY'
from pathlib import Path
text = Path('comm/CMakeLists.txt').read_text()
print('../3rd/sqlite' in text)
print('../3rd/mysql' in text)
PY
```

Expected: the file still contains fallback references, but discovered-library logic now exists and Windows is no longer hard-wired to vendored libs.

### Task 2: Add CI workflow for Linux and Windows

**Files:**
- Create: `.github/workflows/ci.yml`

- [ ] **Step 1: Confirm the current red state**

Run:
```bash
test -f .github/workflows/ci.yml
```

Expected: exit code non-zero because the workflow does not exist yet.

- [ ] **Step 2: Add the workflow**

Create `.github/workflows/ci.yml` with:
```yaml
name: CI

on:
  push:
    branches: [master]
  pull_request:
    branches: [master]

jobs:
  build-test-package:
    strategy:
      fail-fast: false
      matrix:
        include:
          - os: ubuntu-24.04
            build_type: Release
            cmake_args: -G Ninja -DENT_ENABLE_SQLITE=ON -DENT_ENABLE_MYSQL=ON
          - os: windows-2022
            build_type: Release
            cmake_args: -DCMAKE_TOOLCHAIN_FILE=$env:VCPKG_INSTALLATION_ROOT/scripts/buildsystems/vcpkg.cmake -DVCPKG_TARGET_TRIPLET=x64-windows -DENT_ENABLE_SQLITE=ON -DENT_ENABLE_MYSQL=ON -DENT_ALLOW_VENDORED_DB_LIBS=OFF
```

Linux must install `ninja-build`, `libsqlite3-dev`, and `default-libmysqlclient-dev`.
Windows must install `sqlite3` and `libmysql` through `vcpkg`.
Both jobs must configure, build, run `ctest`, run `cpack`, and upload `build/package/*`.

- [ ] **Step 3: Verify the workflow file exists and contains both platforms**

Run:
```bash
rg -n "ubuntu-24.04|windows-2022|cpack|ctest" .github/workflows/ci.yml
```

Expected: matches for both OS labels and both commands.

### Task 3: Add release workflow for tag-based packaging

**Files:**
- Create: `.github/workflows/release.yml`

- [ ] **Step 1: Confirm the current red state**

Run:
```bash
test -f .github/workflows/release.yml
```

Expected: exit code non-zero because the workflow does not exist yet.

- [ ] **Step 2: Add the workflow**

Create `.github/workflows/release.yml` with:
```yaml
name: Release

on:
  push:
    tags:
      - "v*"

permissions:
  contents: write
```

The workflow should reuse the same Linux and Windows build/package logic, download the generated package artifacts, and publish them via `softprops/action-gh-release`.

- [ ] **Step 3: Verify the workflow file exists and is tag-driven**

Run:
```bash
rg -n "tags:|softprops/action-gh-release|windows-2022|ubuntu-24.04" .github/workflows/release.yml
```

Expected: matches for release trigger, release action, and both OS labels.

### Task 4: Update user-facing documentation and verify locally

**Files:**
- Modify: `README.md`

- [ ] **Step 1: Add concise CI usage notes**

Update `README.md` to mention:
```text
- CI validates Linux and Windows builds on GitHub Actions
- Linux uses system SQLite/MySQL development packages
- Windows uses vcpkg-provided sqlite3/libmysql packages
- Tagging v* triggers packaging and release publishing
```

- [ ] **Step 2: Rebuild and test locally**

Run:
```bash
cmake --build build --target test_utl_tpool test_utl_socket test_ent_db test_security perf_utl_socket perf_utl_timer perf_utl_tpool
ctest --output-on-failure
```

Expected: build succeeds and `ctest` reports all tests passing.

- [ ] **Step 3: Smoke-check workflow YAML**

Run:
```bash
python3 - <<'PY'
from pathlib import Path
for path in ['.github/workflows/ci.yml', '.github/workflows/release.yml']:
    text = Path(path).read_text()
    assert 'ctest' in text
    assert 'cpack' in text
print('workflow-smoke-ok')
PY
```

Expected: prints `workflow-smoke-ok`.
