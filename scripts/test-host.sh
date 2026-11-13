#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
set -eu
project_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
if [ -f "$project_dir/local/sdk/environment.sh" ]; then
    . "$project_dir/local/sdk/environment.sh"
fi
build_dir="${RV1126B_HOST_BUILD_DIR:-$project_dir/build-host}"
cmake -S "$project_dir" -B "$build_dir" -DRV1126B_HOST_ONLY=ON -DRV1126B_BUILD_TESTS=ON \
    -DCMAKE_BUILD_TYPE=Debug "$@"
cmake --build "$build_dir" --parallel "${RV1126B_BUILD_JOBS:-4}"
ctest --test-dir "$build_dir" --output-on-failure
