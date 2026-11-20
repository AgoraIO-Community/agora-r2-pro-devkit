#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
set -eu

service_dir=/etc/mybot-rv1126b
runtime=/run/mybot-rv1126b-service
release_root=/userdata/mybot/releases

fail() {
    echo "MyBot service: $1" >&2
    exit 1
}

verify_release() {
    [ -n "$1" ] && [ -d "$1" ] || fail 'release is missing'
    resolved=$(readlink -f "$1") || fail 'cannot resolve release'
    case "$resolved" in
        "$release_root"/*) ;;
        *) fail 'release must belong to /userdata/mybot/releases' ;;
    esac
    [ -x "$resolved/run.sh" ] && [ -f "$resolved/SHA256SUMS" ] ||
        fail 'release is incomplete'
    release=$resolved
}

case "${1:-}" in
    exec)
        [ "$#" -eq 2 ] || fail 'invalid child invocation'
        verify_release "$2"
        # Resume any confirmed transaction before audio, networking or the SDK starts.
        "$service_dir/mybot-factory-reset"
        if [ -e /userdata/mybot/factory-reset.setup ] && [ ! -x "$release/bin/mybot-factory-reset" ]; then
            fail 'finish Wi-Fi setup with a reset-capable release before rollback'
        fi
        # Every supervised launch restores the verified capture baseline.
        "$service_dir/audio-prepare.sh"
        exec "$release/run.sh"
        ;;
    supervise|prepare)
        [ "$#" -eq 1 ] || fail 'invalid supervisor invocation'
        ;;
    *) fail 'expected supervise or exec' ;;
esac

[ "$(id -u)" -eq 0 ] || fail 'this board service requires root'
config=/etc/mybot-rv1126b.conf
[ -f "$config" ] && [ ! -L "$config" ] && [ "$(stat -c '%u:%a' "$config")" = '0:600' ] ||
    fail 'configuration must be a root-owned regular 0600 file'
. "$config"
[ "${MYBOT_ENABLED:-0}" = 1 ] || fail 'service is disabled'

# Do not create application or log directories before the persistent mount.
attempt=0
while ! awk '$2 == "/userdata" { ready=1 } END { exit !ready }' /proc/mounts; do
    attempt=$((attempt + 1))
    [ "$attempt" -lt 15 ] || fail '/userdata is not mounted'
    sleep 1
done
verify_release /userdata/mybot/current
(cd "$release" && sha256sum -c SHA256SUMS >/dev/null) || fail 'release checksum verification failed'

case "${RV1126B_VIDEO:-0}" in
    0|1) ;;
    *) fail 'RV1126B_VIDEO must be 0 or 1' ;;
esac
# Bind one process lifetime to physical release assets, even if current changes.
export MYBOT_ASSETS_DIR="$release/assets"
export MYBOT_KV_STORE_DIR=/userdata/mybot-rv1126b
export MYBOT_STATUS_PATH=/run/mybot-rv1126b/application.status
export MYBOT_SUPERVISOR_RUNTIME="$runtime"
# Factory reset and the board DHCP lease policy target this station.
export RV1126B_WIFI_INTERFACE="${RV1126B_WIFI_INTERFACE:-wlan0}"
[ ! -L /run/mybot-rv1126b ] || fail 'application status directory must not be a link'
mkdir -p /run/mybot-rv1126b
chmod 0700 /run/mybot-rv1126b
mkdir -p /userdata/mybot/logs
chmod 0700 /userdata/mybot/logs
[ "$1" != prepare ] || exit 0
[ -x /sbin/poweroff ] || fail 'normal system poweroff command is missing'
[ -x "$service_dir/poweroff.sh" ] || fail 'backlight poweroff helper is missing'
exec "$service_dir/mybot-supervisor" supervise --runtime "$runtime" \
    --log-dir /userdata/mybot/logs --factory-reset --poweroff-command "$service_dir/poweroff.sh" \
    -- "$service_dir/run-service.sh" exec "$release" </dev/null
