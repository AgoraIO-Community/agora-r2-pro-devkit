#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
# Optional, foreground one-shot helper. Enable explicitly from the application.
set -eu

fail() {
    echo "Time sync: $1" >&2
    exit 2
}

[ "$#" -eq 0 ] || fail 'this helper accepts no arguments'
ntp_server=${RV1126B_NTP_SERVER:-pool.ntp.org}
case "$ntp_server" in
    ''|*[!A-Za-z0-9.-]*|-*) fail 'NTP server must be a hostname or IPv4 address' ;;
esac
[ "${#ntp_server}" -le 253 ] || fail 'NTP server name is too long'

# Permit an absolute DNS name, but never empty labels or option-like labels.
server_labels=${ntp_server%.}
case "$server_labels" in
    *[!0-9.]*) numeric_address=0 ;;
    *) numeric_address=1 ;;
esac
label_count=0
while :; do
    label=${server_labels%%.*}
    case "$label" in
        ''|-*|*-) fail 'NTP server contains an invalid label' ;;
    esac
    [ "${#label}" -le 63 ] || fail 'NTP server contains an overlong label'
    if [ "$numeric_address" -eq 1 ]; then
        [ "${#label}" -le 3 ] && [ "$label" -le 255 ] ||
            fail 'NTP server contains an invalid IPv4 octet'
    fi
    label_count=$((label_count + 1))
    case "$server_labels" in
        *.*) server_labels=${server_labels#*.} ;;
        *) break ;;
    esac
done
if [ "$numeric_address" -eq 1 ] && [ "$label_count" -ne 4 ]; then
    fail 'NTP server must contain four IPv4 octets'
fi

[ -x /bin/busybox ] || fail 'missing executable /bin/busybox'
# exec retains the owned process group; do not daemonize or alter system services.
exec /bin/busybox ntpd -n -q -p "$ntp_server"
