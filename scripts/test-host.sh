#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
set -eu
project_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
if [ -n "${PYTHON:-}" ]; then
    python_path=$(command -v "$PYTHON")
    set -- "-DPython3_EXECUTABLE=$python_path" "$@"
fi
. "$project_dir/scripts/build-options.sh"
rv1126b_parse_options "$@"
profile=host-$profile_locale_suffix
if [ "$profile_ubsan" = ON ]; then
    if [ "$profile_locale_suffix" = zh ]; then profile=host-ubsan; else profile=host-en-ubsan; fi
fi
build_dir="${RV1126B_HOST_BUILD_DIR:-$project_dir/build/$profile}"
cmake -S "$project_dir" -B "$build_dir" -DRV1126B_HOST_ONLY=ON -DRV1126B_BUILD_TESTS=ON \
    -DCMAKE_BUILD_TYPE=Debug -DRV1126B_LANGUAGE="$profile_language" \
    -DRV1126B_ENABLE_UBSAN="$profile_ubsan" -DMYBOT_ENABLE_VIDEO=OFF "$@"
cmake --build "$build_dir" --parallel "${RV1126B_BUILD_JOBS:-4}"
ctest --test-dir "$build_dir" --output-on-failure --no-tests=error
