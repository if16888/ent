#!/usr/bin/env bash
set -euo pipefail

stage="${1:-all}"
build_dir="${BUILD_DIR:-build-ci}"

run_configure() {
  local -a cmake_args

  cmake_args=(
    -S .
    -B "${build_dir}"
    -DCMAKE_BUILD_TYPE=Release
    -DENT_ENABLE_SQLITE=ON
    -DENT_ENABLE_MYSQL=ON
  )

  if command -v ninja >/dev/null 2>&1; then
    cmake_args+=(-G Ninja)
  else
    cmake_args+=(-G "Unix Makefiles")
  fi

  cmake "${cmake_args[@]}"
}

run_build() {
  cmake --build "${build_dir}"
}

run_test() {
  (
    cd "${build_dir}"
    ctest --output-on-failure
  )
}

run_perf() {
  "./${build_dir}/bin/perf_utl_socket"
  "./${build_dir}/bin/perf_utl_timer"
  "./${build_dir}/bin/perf_utl_timer_rt"
  "./${build_dir}/bin/perf_utl_tpool"
  "./${build_dir}/bin/perf_ent_log"
}

case "${stage}" in
  configure)
    run_configure
    ;;
  build)
    run_build
    ;;
  test)
    run_test
    ;;
  perf)
    run_perf
    ;;
  all)
    run_configure
    run_build
    run_test
    run_perf
    ;;
  *)
    echo "Unknown stage: ${stage}" >&2
    exit 1
    ;;
esac
