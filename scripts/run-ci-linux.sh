#!/usr/bin/env bash
set -euo pipefail

stage="${1:-all}"
build_dir="${BUILD_DIR:-build-ci}"
install_dir="${INSTALL_DIR:-${build_dir}/install}"
downstream_build_dir="${DOWNSTREAM_BUILD_DIR:-${build_dir}/downstream-consumer}"
downstream_source_dir="${DOWNSTREAM_SOURCE_DIR:-test/downstream_consumer}"

run_perf_binary() {
  local name="$1"
  local path="$2"

  echo "Running ${name}"
  if command -v timeout >/dev/null 2>&1; then
    timeout 180 "${path}"
  else
    "${path}"
  fi
}

run_configure() {
  local -a cmake_args

  cmake_args=(
    -S .
    -B "${build_dir}"
    -DCMAKE_BUILD_TYPE=Release
    -DENT_ENABLE_SQLITE=ON
    -DENT_ENABLE_MYSQL=ON
    -DENT_ENABLE_PGSQL=ON
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

run_install_consumer() {
  local -a consumer_args

  cmake --install "${build_dir}" --prefix "${install_dir}"

  consumer_args=(
    -S "${downstream_source_dir}"
    -B "${downstream_build_dir}"
    -DCMAKE_PREFIX_PATH="$(pwd)/${install_dir}"
    -DCMAKE_BUILD_TYPE=Release
  )

  if command -v ninja >/dev/null 2>&1; then
    consumer_args+=(-G Ninja)
  else
    consumer_args+=(-G "Unix Makefiles")
  fi

  cmake "${consumer_args[@]}"
  cmake --build "${downstream_build_dir}"
  "./${downstream_build_dir}/ent_downstream_consumer"
}

run_perf() {
  run_perf_binary "perf_utl_socket" "./${build_dir}/bin/perf_utl_socket"
  run_perf_binary "perf_utl_timer" "./${build_dir}/bin/perf_utl_timer"
  run_perf_binary "perf_utl_timer_rt" "./${build_dir}/bin/perf_utl_timer_rt"
  run_perf_binary "perf_utl_tpool" "./${build_dir}/bin/perf_utl_tpool"
  run_perf_binary "perf_ent_log" "./${build_dir}/bin/perf_ent_log"
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
  install-consumer)
    run_install_consumer
    ;;
  perf)
    run_perf
    ;;
  all)
    run_configure
    run_build
    run_test
    run_install_consumer
    run_perf
    ;;
  *)
    echo "Unknown stage: ${stage}" >&2
    exit 1
    ;;
esac
