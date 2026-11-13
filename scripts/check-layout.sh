#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
set -eu

project_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$project_root"

required_paths='
CMakeLists.txt
VERSION
src/botstation-device/main.c
src/botstation-device/version.h.in
src/platform/rv1126b/rv1126b_platform.c
tests/unit/test_config.c
tests/platform/rv1126b/test_platform.c
boards/rv1126b-r2pro/config/botstation-device.conf.in
integrations/buildroot-external/external.desc
third_party/manifests/SDK_SOURCE.txt
third_party/manifests/RTSA_SOURCE.txt
docs/build.md
'

for required_path in $required_paths; do
    [ -e "$required_path" ] || {
        echo "missing required path: $required_path" >&2
        exit 1
    }
done

for stale_path in \
    app/botstation-device \
    third_party/mybot \
    third_party/agora-rtsa/agora_rtsa_sdk/agora_sdk/include; do
    [ ! -e "$stale_path" ] || {
        echo "stale path is still present: $stale_path" >&2
        exit 1
    }
done

grep -R -n -E 'app/botstation-device/(CMakeLists|README|main\.c|platform|tests|third_party)|third_party/mybot' \
    --include='*.md' --include='*.c' --include='*.h' --include='*.sh' \
    --include='*.mk' --include='CMakeLists.txt' \
    README.md CHANGELOG.md .github docs boards integrations src tests \
    CMakeLists.txt .gitignore && {
    echo "stale path reference found" >&2
    exit 1
}

echo "repository layout checks passed"
