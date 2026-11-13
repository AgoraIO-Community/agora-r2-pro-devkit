#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
set -eu
package_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
export MYBOT_ASSETS_DIR="${MYBOT_ASSETS_DIR:-$package_dir/assets}"
export MYBOT_KV_STORE_DIR="${MYBOT_KV_STORE_DIR:-/userdata/mybot-rv1126b}"
case "${RV1126B_VIDEO:-0}" in
    0) executable="$package_dir/bin/mybot-rv1126b" ;;
    1) executable="$package_dir/bin/mybot-rv1126b-video" ;;
    *) echo 'RV1126B_VIDEO must be 0 or 1' >&2; exit 2 ;;
esac
if [ ! -x "$executable" ]; then
    echo "Missing test executable: $executable" >&2
    exit 1
fi
exec "$executable" "$@"
