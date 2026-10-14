#!/usr/bin/env bash
# Opt-in quality gates. Run baseline and current with the same compiler/profile.
set -euo pipefail
mode="${1:?usage: run-quality-linux.sh warnings|analyze|coverage|abi|sdk [base-ref]}"
base_ref="${2:-origin/master}"
repo="$(cd "$(dirname "$0")/.." && pwd)"
root="${QUALITY_DIR:-${repo}/build-quality}"
mkdir -p "$root"
root="$(cd "$root" && pwd)"
common=(-DCMAKE_BUILD_TYPE=Debug -DENT_BUILD_EXAMPLES=OFF
        -DENT_ENABLE_SQLITE=ON -DENT_ENABLE_MYSQL=OFF -DENT_ENABLE_PGSQL=OFF
        -DENT_ENABLE_LUA=OFF)

baseline() {
  base_sha="$(git -C "$repo" rev-parse --verify "$base_ref^{commit}")"
  base_source="$root/base-source-$base_sha"
  mkdir -p "$base_source"
  git -C "$repo" archive "$base_sha" | tar -x -C "$base_source"
}

build_library() {
  local source="$1" build="$2" flags="$3"
  cmake -S "$source" -B "$build" "${common[@]}" \
    -DBUILD_TESTING=OFF -DCMAKE_C_FLAGS="$flags ${QUALITY_C_FLAGS:-}"
  cmake --build "$build" --clean-first --target ent ent_s --parallel "${CMAKE_BUILD_PARALLEL_LEVEL:-2}"
}

case "$mode" in
  warnings|analyze)
    baseline
    flags="-Wall -Wextra"
    if [[ "$mode" == analyze ]]; then flags="$flags -fanalyzer"; fi
    build_library "$base_source" "$root/base-$mode-$base_sha" "$flags" 2>&1 | tee "$root/base-$mode.log"
    build_library "$repo" "$root/current-$mode" "$flags" 2>&1 | tee "$root/current-$mode.log"
    python3 "$repo/scripts/check-diagnostics.py" "$root/base-$mode.log" \
      "$root/current-$mode.log" --output "$root/$mode.json"
    ;;
  coverage)
    command -v gcovr
    cmake -S "$repo" -B "$root/coverage" "${common[@]}" -DENT_ENABLE_COVERAGE=ON
    cmake --build "$root/coverage" --parallel "${CMAKE_BUILD_PARALLEL_LEVEL:-2}"
    ctest --test-dir "$root/coverage" --output-on-failure
    gcovr --root "$repo" --filter "$repo/comm/" --exclude-unreachable-branches \
      --json "$root/coverage.json" --xml "$root/coverage.xml" \
      --html-details "$root/coverage.html" --print-summary "$root/coverage"
    ;;
  abi)
    command -v abidiff
    baseline
    # Same no-DB profile is necessary for comparable public type/symbol sets.
    common+=(-DENT_ENABLE_SQLITE=OFF)
    build_library "$base_source" "$root/base-abi-$base_sha" "-g -Og"
    build_library "$repo" "$root/current-abi" "-g -Og"
    # Libabigail reports pure symbol additions with exit bit 4. Allow only
    # reviewed additive *function* changes; never ignore removed/changed
    # interfaces or type/layout breaks. Retain the full ABI report as evidence.
    set +e
    abidiff --headers-dir1 "$base_source/inc" --headers-dir2 "$repo/inc" \
      "$root/base-abi-$base_sha/lib/libent.so" "$root/current-abi/lib/libent.so" \
      2>&1 | tee "$root/abi.txt"
    abi_status=${PIPESTATUS[0]}
    set -e
    if (( abi_status == 4 )); then
      if ! grep -Eq '^Functions changes summary: 0 Removed, 0 Changed, [1-9][0-9]* Added functions?' "$root/abi.txt" ||
         ! grep -Eq '^Variables changes summary: 0 Removed, 0 Changed, 0 Added variable' "$root/abi.txt" ||
         grep -Eq '^  \[[^A]\] ' "$root/abi.txt"; then
        echo "ABI review required: not a pure added-function change" >&2
        exit "$abi_status"
      fi
      echo "ABI compatible: additive public functions only (reviewed)"
    elif (( abi_status != 0 )); then
      exit "$abi_status"
    fi
    ;;
  sdk)
    build_library "$repo" "$root/current-sdk" ""
    cmake --install "$root/current-sdk" --prefix "$root/sdk"
    cp "$repo/LICENSE" "$repo/NOTICE" "$repo/THIRD_PARTY_NOTICES" "$root/sdk/"
    tar -C "$root/sdk" -czf "$root/sdk.tar.gz" .
    BUILD_DIR="$root/current-sdk" bash "$repo/scripts/validate-release-sdk.sh" \
      "$root/sdk.tar.gz" "$repo/test/downstream_consumer" 2>&1 | tee "$root/sdk.log"
    ;;
  *) echo "Unknown quality mode: $mode" >&2; exit 2 ;;
esac
