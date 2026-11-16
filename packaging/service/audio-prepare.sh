#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
# Capture controls for rv1126b-test2-rk805b-v10. This does not change playback.
set -eu

command -v amixer >/dev/null 2>&1 || {
    echo 'Audio preparation: missing amixer' >&2
    exit 1
}
prepare_control() {
    control=$1
    expected=$2
    amixer -c 0 cset "name=$control" "$expected" >/dev/null
    # Match the full numeric value, not a prefix such as 230 when expecting 23.
    amixer -c 0 cget "name=$control" | awk -v expected="$expected" '
        /: values=/ {
            sub(/^.*: values=/, "")
            count = split($0, values, ",")
            for (i = 1; i <= count; i++) {
                gsub(/[[:space:]]/, "", values[i])
                if (values[i] != expected) exit 1
            }
            seen = 1
        }
        END { if (!seen) exit 1 }
    '
}
prepare_control 'ACodec_LP Digital Gain Volume' 0
prepare_control 'ACodec_LP PGA Gain Volume' 23
