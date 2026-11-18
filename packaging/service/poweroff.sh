#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
# Called by the supervisor only after the application has released its resources.
set -eu
PATH=/usr/sbin:/usr/bin:/sbin:/bin
export PATH

backlight=/sys/class/backlight/backlight
physical=/sys/devices/platform/backlight/backlight/backlight

fail() {
    echo "MyBot poweroff: $1" >&2
    exit 1
}

integer() {
    number=$(cat "$backlight/$1") || return 1
    case "$number" in ''|*[!0-9]*|??????*) return 1;; esac
    [ "$number" -le 65535 ] || return 1
    printf '%s\n' "$number"
}

restore=0
finish() {
    trap '' HUP INT TERM
    trap - 0
    result=$1
    if [ "$restore" = 1 ]; then
        if ! { printf '%s\n' "$brightness" > "$backlight/brightness" &&
               printf '%s\n' "$power" > "$backlight/bl_power" &&
               [ "$(integer brightness)" = "$brightness" ] &&
               [ "$(integer bl_power)" = "$power" ]; }; then
            echo 'MyBot poweroff: cannot restore the previous backlight state' >&2
            result=1
        fi
    fi
    exit "$result"
}

[ "$#" -eq 0 ] || fail 'arguments are not accepted'
[ "$(id -u)" = 0 ] || fail 'root is required'
[ "$(readlink -f "$backlight")" = "$physical" ] || fail 'unexpected backlight device'
[ -d "$physical" ] && [ "$(stat -c '%u:%g:%a' "$physical")" = 0:0:755 ] ||
    fail 'unsafe backlight directory'
for property in brightness bl_power max_brightness actual_brightness; do
    path="$backlight/$property"
    [ -f "$path" ] && [ ! -L "$path" ] || fail 'unsafe backlight attribute'
    case "$property" in brightness|bl_power) permission=644;; *) permission=444;; esac
    [ "$(stat -c '%u:%g:%a' "$path")" = "0:0:$permission" ] ||
        fail 'unsafe backlight permissions'
done
[ -x /sbin/poweroff ] || fail 'normal system poweroff command is missing'
maximum=$(integer max_brightness) || fail 'invalid maximum brightness'
brightness=$(integer brightness) || fail 'invalid brightness'
actual=$(integer actual_brightness) || fail 'invalid actual brightness'
power=$(integer bl_power) || fail 'invalid backlight power state'
[ "$maximum" -gt 0 ] && [ "$brightness" -le "$maximum" ] &&
    [ "$actual" -le "$maximum" ] && [ "$power" -le 4 ] || fail 'backlight values are out of range'

# Capture every old value before the first write; a failed request remains usable.
trap 'finish "$?"' 0
trap 'exit 1' HUP INT TERM
restore=1
printf '0\n' > "$backlight/brightness"
printf '4\n' > "$backlight/bl_power"
[ "$(integer brightness)" = 0 ] && [ "$(integer actual_brightness)" = 0 ] &&
    [ "$(integer bl_power)" = 4 ] || fail 'backlight did not turn off'

# PID 1 may signal us before BusyBox returns. Keep the handoff and success
# bookkeeping uninterrupted; an accepted shutdown must not relight the panel.
trap '' HUP INT TERM
if /sbin/poweroff; then
    restore=0
    exit 0
fi
fail 'system poweroff request failed'
