#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
set -eu
project_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
if [ -n "${PYTHON:-}" ]; then
    python_path=$(command -v "$PYTHON")
    set -- "-DPython3_EXECUTABLE=$python_path" "$@"
fi
if [ -z "${CROSS_COMPILE:-}" ] && [ -f "$project_dir/local/sdk/environment.sh" ]; then
    . "$project_dir/local/sdk/environment.sh"
fi
: "${CROSS_COMPILE:?Configure an explicit SDK with scripts/configure-local-sdk.py}"
: "${RV1126B_STAGING:?Set target dependency staging}"
AGORA_SDK_DIR="$project_dir/vendor/rtsa/agora_sdk"
AGORA_RTC_LIBRARY="$AGORA_SDK_DIR/lib/aarch64/libagora-rtc-sdk.so"
export AGORA_SDK_DIR AGORA_RTC_LIBRARY
. "$project_dir/scripts/build-options.sh"
rv1126b_parse_options "$@"
variant=voice
if [ "$profile_video" = ON ]; then variant=video; fi
language=$profile_locale_suffix
build_dir="${RV1126B_BUILD_DIR:-$project_dir/build/rv1126b-$variant-$language}"
export PKG_CONFIG_SYSROOT_DIR="$RV1126B_STAGING"
export PKG_CONFIG_LIBDIR="$RV1126B_STAGING/usr/lib/pkgconfig:$RV1126B_STAGING/usr/share/pkgconfig"
export PKG_CONFIG_PATH=
cmake -S "$project_dir" -B "$build_dir" \
    -DCMAKE_TOOLCHAIN_FILE="$project_dir/cmake/toolchains/aarch64-linux-gnu.cmake" \
    -DRV1126B_CROSS_COMPILE="$CROSS_COMPILE" -DRV1126B_STAGING="$RV1126B_STAGING" \
    -DAGORA_SDK_DIR="$AGORA_SDK_DIR" -DAGORA_RTC_LIBRARY="$AGORA_RTC_LIBRARY" \
    -DCMAKE_BUILD_TYPE=Release -DRV1126B_LANGUAGE="$profile_language" \
    -DMYBOT_ENABLE_VIDEO="$profile_video" -DRV1126B_ENABLE_UBSAN=OFF "$@"
# CMake's default target brackets compilation with input and artifact manifests.
cmake --build "$build_dir" --parallel "${RV1126B_BUILD_JOBS:-4}"
