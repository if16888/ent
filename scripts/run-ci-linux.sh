#!/usr/bin/env bash
set -euo pipefail

stage="${1:-all}"
build_dir="${BUILD_DIR:-build-ci}"
install_dir="${INSTALL_DIR:-${build_dir}/install}"
downstream_build_dir="${DOWNSTREAM_BUILD_DIR:-${build_dir}/downstream-consumer}"
downstream_source_dir="${DOWNSTREAM_SOURCE_DIR:-test/downstream_consumer}"
ci_log_dir="${CI_LOG_DIR:-}"
ent_enable_sqlite="${ENT_ENABLE_SQLITE:-ON}"
ent_enable_mysql="${ENT_ENABLE_MYSQL:-ON}"
ent_enable_pgsql="${ENT_ENABLE_PGSQL:-ON}"

log_file() {
  local name="$1"

  if [[ -z "${ci_log_dir}" ]]; then
    return 1
  fi

  mkdir -p "${ci_log_dir}"
  printf '%s/%s.log' "${ci_log_dir}" "${name}"
}

run_with_log() {
  local name="$1"
  shift
  local log_path=""

  if log_path="$(log_file "${name}")"; then
    "$@" 2>&1 | tee "${log_path}"
  else
    "$@"
  fi
}

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
    -DENT_ENABLE_SQLITE="${ent_enable_sqlite}"
    -DENT_ENABLE_MYSQL="${ent_enable_mysql}"
    -DENT_ENABLE_PGSQL="${ent_enable_pgsql}"
  )

  if command -v ninja >/dev/null 2>&1; then
    cmake_args+=(-G Ninja)
  else
    cmake_args+=(-G "Unix Makefiles")
  fi

  printf 'Database backends: SQLite=%s MySQL=%s PostgreSQL=%s\n' \
    "${ent_enable_sqlite}" "${ent_enable_mysql}" "${ent_enable_pgsql}"
  run_with_log configure cmake "${cmake_args[@]}"
}

run_build() {
  run_with_log build cmake --build "${build_dir}"
}

run_test() {
  if log_path="$(log_file test)"; then
    (
      cd "${build_dir}"
      ctest --output-on-failure
    ) 2>&1 | tee "${log_path}"
  else
    (
      cd "${build_dir}"
      ctest --output-on-failure
    )
  fi
}

run_install_consumer() {
  local -a consumer_args
  local install_log=""

  if install_log="$(log_file install-consumer)"; then
    {
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
    } 2>&1 | tee "${install_log}"
  else
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
  fi
}

run_perf() {
  local perf_log=""

  if perf_log="$(log_file perf)"; then
    {
      run_perf_binary "perf_utl_socket" "./${build_dir}/bin/perf_utl_socket"
      run_perf_binary "perf_utl_timer" "./${build_dir}/bin/perf_utl_timer"
      run_perf_binary "perf_utl_timer_rt" "./${build_dir}/bin/perf_utl_timer_rt"
      run_perf_binary "perf_utl_tpool" "./${build_dir}/bin/perf_utl_tpool"
      run_perf_binary "perf_ent_log" "./${build_dir}/bin/perf_ent_log"
    } 2>&1 | tee "${perf_log}"
  else
    run_perf_binary "perf_utl_socket" "./${build_dir}/bin/perf_utl_socket"
    run_perf_binary "perf_utl_timer" "./${build_dir}/bin/perf_utl_timer"
    run_perf_binary "perf_utl_timer_rt" "./${build_dir}/bin/perf_utl_timer_rt"
    run_perf_binary "perf_utl_tpool" "./${build_dir}/bin/perf_utl_tpool"
    run_perf_binary "perf_ent_log" "./${build_dir}/bin/perf_ent_log"
  fi
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
