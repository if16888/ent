#!/usr/bin/env bash
set -euo pipefail

archive="$1"
consumer_source="$2"
root="$(mktemp -d)"
sdk="${root}/sdk"
mkdir -p "${sdk}"
trap 'rm -rf "${root}"' EXIT

tar -xzf "${archive}" -C "${sdk}"

mapfile -t cmake_files < <(find "${sdk}" -type f -name '*.cmake' -print)
for pattern in \
  'vcpkg_installed' \
  'D:/a/' \
  'C:\a\' \
  '/home/runner/work/' \
  "${GITHUB_WORKSPACE:-__unset_workspace__}" \
  "$(realpath "${BUILD_DIR:-build}")"
do
  if grep -F -n -- "${pattern}" "${cmake_files[@]}"; then
    echo "Exported CMake metadata contains forbidden path: ${pattern}" >&2
    exit 1
  fi
done

for linkage in shared static; do
  build="${root}/consumer-${linkage}"
  cmake \
    -S "${consumer_source}" \
    -B "${build}" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_PREFIX_PATH="${sdk}" \
    -DENT_CONSUMER_LINKAGE="${linkage}"
  cmake --build "${build}"
  LD_LIBRARY_PATH="${sdk}/lib${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}" \
    "${build}/ent_downstream_consumer"
done
