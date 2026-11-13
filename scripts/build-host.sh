#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
set -eu

project_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
: "${AGORA_SDK_DIR:?Set AGORA_SDK_DIR to an authorized RTSA SDK}"

case "$(uname -m)" in
    x86_64|amd64) host_arch=x86_64 ;;
    aarch64|arm64) host_arch=aarch64 ;;
    *) echo "Unsupported host architecture: $(uname -m)" >&2; exit 1 ;;
esac

AGORA_RTC_LIBRARY=${AGORA_RTC_LIBRARY:-"$AGORA_SDK_DIR/lib/$host_arch/libagora-rtc-sdk.so"}
MYBOT_AUDIO_PTIME_MS=${MYBOT_AUDIO_PTIME_MS:-60}
BOTSTATION_BUILD_DIR=${BOTSTATION_BUILD_DIR:-"$project_root/build-host"}

cmake -S "$project_root" -B "$BOTSTATION_BUILD_DIR" \
    -DCMAKE_BUILD_TYPE=Debug \
    -DAGORA_SDK_DIR="$AGORA_SDK_DIR" \
    -DAGORA_RTC_LIBRARY="$AGORA_RTC_LIBRARY" \
    -DMYBOT_AUDIO_PTIME_MS="$MYBOT_AUDIO_PTIME_MS" \
    -DMYBOT_ENABLE_VIDEO=OFF \
    -DBOTSTATION_ENABLE_LVGL_UI=OFF \
    -DBOTSTATION_BUILD_TESTS=ON

cmake --build "$BOTSTATION_BUILD_DIR" -j"${JOBS:-4}"
LD_LIBRARY_PATH="$(dirname "$AGORA_RTC_LIBRARY")${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
    ctest --test-dir "$BOTSTATION_BUILD_DIR" --output-on-failure
