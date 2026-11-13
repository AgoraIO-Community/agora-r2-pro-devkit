#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
set -eu
project_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
if [ -z "${CROSS_COMPILE:-}" ] && [ -f "$project_dir/local/sdk/environment.sh" ]; then
    . "$project_dir/local/sdk/environment.sh"
fi
: "${CROSS_COMPILE:?Run python3 scripts/configure-local-sdk.py, or provide a toolchain}"
: "${RV1126B_STAGING:?Set target dependency staging}"
: "${AGORA_SDK_DIR:?Set target RTSA SDK}"
: "${AGORA_RTC_LIBRARY:?Set target AArch64 RTSA library}"
build_dir="${RV1126B_BUILD_DIR:-$project_dir/build-rv1126b}"
export PKG_CONFIG_SYSROOT_DIR="$RV1126B_STAGING"
export PKG_CONFIG_LIBDIR="$RV1126B_STAGING/usr/lib/pkgconfig:$RV1126B_STAGING/usr/share/pkgconfig"
export PKG_CONFIG_PATH=
cmake -S "$project_dir" -B "$build_dir" \
    -DCMAKE_TOOLCHAIN_FILE="$project_dir/cmake/toolchains/aarch64-linux-gnu.cmake" \
    -DRV1126B_CROSS_COMPILE="$CROSS_COMPILE" -DRV1126B_STAGING="$RV1126B_STAGING" \
    -DAGORA_SDK_DIR="$AGORA_SDK_DIR" -DAGORA_RTC_LIBRARY="$AGORA_RTC_LIBRARY" \
    -DCMAKE_BUILD_TYPE=Release "$@"
cmake --build "$build_dir" --parallel "${RV1126B_BUILD_JOBS:-4}"
python3 "$project_dir/scripts/check-target-elf.py" "$build_dir/mybot-rv1126b" \
    --library-root "$RV1126B_STAGING" --library-root "$(dirname -- "$AGORA_RTC_LIBRARY")" \
    --output "$build_dir/abi-report.json"
