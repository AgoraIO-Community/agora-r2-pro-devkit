#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Explicit, checked ADB deployment; build and packaging never invoke this tool."""

import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import shlex
import struct
import subprocess
import tarfile
import time

from build_metadata import MANIFEST_SCHEMA, inputs_digest

ROOT = Path(__file__).resolve().parents[1]
DEVICE_ROOT = "/userdata/mybot"
DATA_ROOT = "/userdata/mybot-rv1126b"
SERVICE = "/etc/init.d/S99mybot"
SERVICE_ASSETS = ("assets/service/S99mybot", "assets/service/run-service.sh",
                  "assets/service/mybot-rv1126b.conf", "assets/service/audio-prepare.sh",
                  "assets/service/poweroff.sh",
                  "assets/network/99-mybot-lease", "bin/mybot-supervisor",
                  "supervisor-manifest.json", "abi-report-supervisor.json", "bin/mybot-factory-reset",
                  "factory-reset-manifest.json", "abi-report-factory-reset.json")
EXECUTABLE_ASSETS = ("assets/service/S99mybot", "assets/service/run-service.sh",
                     "assets/service/audio-prepare.sh", "assets/network/99-mybot-lease",
                     "assets/service/poweroff.sh",
                     "assets/wifi/udhcpc.script", "assets/time/sync-busybox.sh", "bin/mybot-supervisor", "bin/mybot-factory-reset")
NAME = re.compile(r"mybot-rv1126b-(\d+\.\d+\.\d+)(?:-en-US)?-aarch64(?:-voice|-video)?\Z")
SHA256 = re.compile(r"[0-9a-f]{64}\Z")
TAG = re.compile(r"[A-Za-z0-9][A-Za-z0-9._-]{0,95}\Z")
MAX_PACKAGE_BYTES = 256 * 1024 * 1024


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def elf_aarch64(data):
    if (len(data) < 64 or data[:6] != b"\x7fELF\x02\x01" or
            struct.unpack_from("<H", data, 18)[0] != 183 or
            struct.unpack_from("<H", data, 16)[0] not in (2, 3)):
        raise ValueError("Package requires a little-endian AArch64 ELF executable/library")


def verify_package(path, expected, legacy=False):
    """Verify every byte before sending an archive, without extracting on the host."""
    path = Path(path)
    if not isinstance(expected, str) or not SHA256.fullmatch(expected):
        raise ValueError("An explicit lowercase SHA256 is required")
    if path.is_symlink() or not path.is_file() or path.stat().st_size > MAX_PACKAGE_BYTES:
        raise ValueError("Package must be a bounded regular archive")
    if sha256(path.read_bytes()) != expected:
        raise ValueError("Archive SHA256 mismatch")
    files, modes, names, root, total = {}, {}, set(), None, 0
    with tarfile.open(path, "r:gz") as archive:
        for member in archive:
            name = member.name.rstrip("/")
            parts = PurePosixPath(name).parts
            if (not parts or name.startswith("/") or ".." in parts or
                    any(part in ("", ".") for part in name.split("/")) or
                    not re.fullmatch(r"[A-Za-z0-9_./+-]+", name) or name in names or
                    not (member.isdir() or member.isfile()) or member.mode & 0o7022):
                raise ValueError("Unsafe or duplicate archive entry")
            names.add(name)
            if root is None:
                root = parts[0]
            if root != parts[0] or not NAME.fullmatch(root):
                raise ValueError("Archive must contain one named release directory")
            if member.isdir() and member.mode & 0o500 != 0o500:
                raise ValueError("Package directories require owner read/search permission")
            if member.isfile():
                total += member.size
                if total > MAX_PACKAGE_BYTES:
                    raise ValueError("Unpacked package is too large")
                relative = "/".join(parts[1:])
                files[relative] = archive.extractfile(member).read()
                modes[relative] = member.mode
    if not root or "SHA256SUMS" not in files:
        raise ValueError("Missing package checksum manifest")
    checksums = {}
    for line in files["SHA256SUMS"].decode("ascii").splitlines():
        match = re.fullmatch(r"([0-9a-f]{64})  ([A-Za-z0-9_./+-]+)", line)
        if not match or match[2] in checksums or match[2] == "SHA256SUMS":
            raise ValueError("Invalid package checksum entry")
        checksums[match[2]] = match[1]
    if set(checksums) != set(files) - {"SHA256SUMS"}:
        raise ValueError("Package checksums do not cover exactly all files")
    if any(sha256(files[name]) != expected_hash for name, expected_hash in checksums.items()):
        raise ValueError("Package member SHA256 mismatch")
    version = NAME.fullmatch(root)[1]
    if legacy and version != "0.1.1":
        raise ValueError("Legacy rollback import is restricted to the accepted 0.1.1 format")
    profiles, inputs_markers = [], {}
    for profile, executable in (("voice", "bin/mybot-rv1126b"),
                                ("video", "bin/mybot-rv1126b-video")):
        if executable not in files:
            continue
        profiles.append(profile)
        elf_aarch64(files[executable])
        report_name = "abi-report-" + profile + ".json"
        if report_name not in files:
            raise ValueError("Missing executable ABI report")
        report = json.loads(files[report_name])
        if (report.get("errors") != [] or report["binary"]["machine"] != 183 or
                report["binary"]["sha256"] != sha256(files[executable]) or
                "$ORIGIN/../lib" not in report["binary"].get("runpaths", [])):
            raise ValueError("ABI report does not match the AArch64 executable")
        if not legacy:
            if "build-manifest-" + profile + ".json" not in files:
                raise ValueError("Missing executable build manifest")
            manifest = json.loads(files["build-manifest-" + profile + ".json"])
            config = manifest["inputs"]["config"]
            if (manifest.get("schema") != MANIFEST_SCHEMA or config["version"] != version or
                    config["board"] != "rv1126b-test2-rk805b-v10" or
                    config["ptime_ms"] != 20 or type(config["video"]) is not bool or
                    config["video"] != (profile == "video") or
                    manifest["binary_sha256"] != sha256(files[executable]) or
                    manifest["abi_report_sha256"] != sha256(files[report_name])):
                raise ValueError("Build manifest does not match the selected package")
            if manifest["inputs"]["dependencies"].get("rtsa_sha256") != sha256(files.get("lib/libagora-rtc-sdk.so", b"")):
                raise ValueError("RTSA runtime does not match its bound build manifest")
            language = "en-US" if "-en-US-" in root else "zh-CN"
            marker = (f"MYBOT_RV1126B_BUILD_CONFIG:{version};{language};"
                      f"video={int(profile == 'video')};ptime_ms=20;"
                      "board=rv1126b-test2-rk805b-v10\0").encode()
            source_marker = ("MYBOT_RV1126B_SOURCE:" +
                             manifest["inputs"]["source"]["sha256"] + "\0").encode()
            inputs_marker = ("MYBOT_RV1126B_INPUTS:" +
                             inputs_digest(manifest["inputs"], public=True) + "\0").encode()
            if (config["language"] != language or marker not in files[executable] or
                    source_marker not in files[executable] or inputs_marker not in files[executable]):
                raise ValueError("Executable provenance markers do not match its manifest")
            inputs_markers[profile] = inputs_marker
    if not profiles or "run.sh" not in files or "lib/libagora-rtc-sdk.so" not in files:
        raise ValueError("Missing application/runtime package contents")
    executables = ["run.sh", *("bin/mybot-rv1126b" + ("-video" if profile == "video" else "")
                              for profile in profiles)]
    if not legacy:
        if any(name not in files for name in SERVICE_ASSETS):
            raise ValueError("Package lacks supervised deployment assets")
        executables.extend(EXECUTABLE_ASSETS)
    if any(name not in modes or modes[name] & 0o500 != 0o500 for name in executables):
        raise ValueError("Package startup files require owner read/execute permission")
    elf_aarch64(files["lib/libagora-rtc-sdk.so"])
    if not legacy:
        for role in ("supervisor", "factory-reset"):
            binary = files["bin/mybot-" + role]
            elf_aarch64(binary)
            manifest = json.loads(files[role + "-manifest.json"])
            report_bytes = files["abi-report-" + role + ".json"]
            report = json.loads(report_bytes)
            config = manifest["inputs"]["config"]
            source = manifest["inputs"]["source"]["sha256"]
            marker = (f"MYBOT_RV1126B_BUILD_CONFIG:{version};{config['language']};"
                      f"video={int(config['video'])};ptime_ms=20;"
                      "board=rv1126b-test2-rk805b-v10\0").encode()
            source_marker = ("MYBOT_RV1126B_SOURCE:" + source + "\0").encode()
            app_manifest = json.loads(files["build-manifest-" + profiles[0] + ".json"])
            if (manifest.get("role") != role or manifest.get("schema") != MANIFEST_SCHEMA or
                    config != app_manifest["inputs"]["config"] or
                    source != app_manifest["inputs"]["source"]["sha256"] or
                    manifest["binary_sha256"] != sha256(binary) or
                    manifest["abi_report_sha256"] != sha256(report_bytes) or
                    report.get("errors") != [] or report["binary"]["machine"] != 183 or
                    report["binary"]["sha256"] != sha256(binary) or
                    marker not in binary or source_marker not in binary or
                    inputs_digest(manifest["inputs"], public=True) !=
                    inputs_digest(app_manifest["inputs"], public=True) or
                    inputs_markers[profiles[0]] not in binary):
                raise ValueError(role.capitalize() + " ABI/provenance does not match its independent manifest")
    return {"path": path.resolve(), "name": root, "version": version,
            "sha256": expected, "profiles": profiles, "files": files}


def read_settings(path):
    """Validate private settings and normalize the deployment profile once."""
    path = Path(path)
    if path.is_symlink() or not path.is_file() or path.stat().st_mode & 0o077:
        raise ValueError("Settings must be a private regular file (chmod 600)")
    if path.stat().st_uid != os.getuid():
        raise ValueError("Settings must belong to the current user")
    settings = json.loads(path.read_text())
    if not isinstance(settings, dict) or set(settings) - {"server", "device_id", "video", "wifi_interface"}:
        raise ValueError("Settings allow only server, device_id, video and wifi_interface")
    server, device_id = settings.get("server", ""), settings.get("device_id", "")
    if (not isinstance(server, str) or not server.startswith("https://") or len(server) > 639 or
            any(character.isspace() or ord(character) < 32 for character in server) or
            not isinstance(device_id, str) or not re.fullmatch(r"[A-Za-z0-9_.:-]{1,32}", device_id)):
        raise ValueError("Settings require an HTTPS server and explicit valid device identity")
    settings.setdefault("video", True)
    if type(settings["video"]) is not bool:
        raise ValueError("video must be a JSON boolean")
    interface = settings.get("wifi_interface", "")
    if not isinstance(interface, str) or (interface and not re.fullmatch(r"[A-Za-z0-9_.-]{1,15}", interface)):
        raise ValueError("Invalid Wi-Fi interface")
    return settings


def configuration(template, settings):
    values = {"RV1126B_SERVER": settings["server"], "RV1126B_DEVICE_ID": settings["device_id"],
              "RV1126B_VIDEO": str(int(settings["video"])),
              "MYBOT_KV_STORE_DIR": DATA_ROOT,
              "MYBOT_SYSTEM_WPA_CONFIG": "/etc/wpa_supplicant.conf",
              "MYBOT_DHCPCD_LEASE_DIR": "/run/mybot-network/leases",
              "RV1126B_WIFI_INTERFACE": settings.get("wifi_interface", "")}
    return template.decode() + "\n# Deployment-owned identity; preserve across upgrades.\n" + "".join(
        f"export {key}={shlex.quote(value)}\n" for key, value in values.items())


class Adb:
    def __init__(self, executable="adb", serial=None):
        self.executable, self.serial = executable, serial

    def select(self):
        result = subprocess.run([self.executable, "devices"], check=True, capture_output=True, text=True)
        devices = [line.split()[0] for line in result.stdout.splitlines()[1:]
                   if len(line.split()) == 2 and line.split()[1] == "device"]
        if self.serial:
            if self.serial not in devices:
                raise ValueError("Selected ADB device is not connected/authorized")
        elif len(devices) != 1:
            raise ValueError("Select exactly one authorized ADB device with --serial")
        else:
            self.serial = devices[0]

    def command(self, arguments, data=None, timeout=None):
        try:
            result = subprocess.run([self.executable, "-s", self.serial, *arguments], input=data,
                                    capture_output=True, text=True, timeout=timeout)
        except subprocess.TimeoutExpired as error:
            raise RuntimeError(f"ADB {arguments[0]} operation timed out; inspect the private "
                               "on-device deployment state") from error
        if result.returncode:
            # Do not echo subprocess output: service/network diagnostics may contain credentials.
            raise RuntimeError(f"ADB {arguments[0]} operation failed (exit {result.returncode}); "
                               "inspect the private on-device deployment state")
        return result.stdout

    def shell(self, script, timeout=None):
        return self.command(["shell", "sh", "-s"], "set -eu\numask 077\n" + script, timeout=timeout)


def identity_check(settings):
    return f"""
auth={shlex.quote(DATA_ROOT + '/device_auth')}
if [ -e "$auth" ] || [ -L "$auth" ]; then
    [ -f "$auth" ] && [ ! -L "$auth" ] || exit 21
    [ "$(stat -c %s "$auth")" = 1284 ] || exit 22
    [ "$(dd if="$auth" bs=1 count=4 2>/dev/null | od -An -tu4 | tr -d '[:space:]')" = 1 ] || exit 23
    saved_server=$(dd if="$auth" bs=1 skip=4 count=640 2>/dev/null | tr -d '\\000')
    saved_identity=$(dd if="$auth" bs=1 skip=644 count=128 2>/dev/null | tr -d '\\000')
    [ "$saved_server" = {shlex.quote(settings['server'])} ] &&
        [ "$saved_identity" = {shlex.quote(settings['device_id'])} ] || exit 24
    printf 'auth=match\\n'
else
    printf 'auth=absent\\n'
fi
"""


PREFLIGHT = """
[ "$(id -u)" = 0 ] || exit 10
[ "$(uname -m)" = aarch64 ] || exit 11
awk '$2=="/userdata" {found=1} END {exit !found}' /proc/mounts || exit 12
for tool in tar sha256sum stat dd od tr awk sed readlink mv cp chmod chown ln mkdir cat rm rmdir sync; do
    command -v "$tool" >/dev/null || exit 13
done
for path in /userdata/mybot /userdata/mybot/releases /userdata/mybot/backups \\
    /userdata/mybot-rv1126b /etc/mybot-rv1126b /etc/mybot-rv1126b/disabled; do
    [ ! -L "$path" ] || exit 14
    if [ -e "$path" ]; then [ -d "$path" ] && [ "$(stat -c %u "$path")" = 0 ] || exit 14; fi
done
for link in /userdata/mybot/current /userdata/mybot/previous; do
    if [ -e "$link" ] || [ -L "$link" ]; then
        [ -L "$link" ] || exit 19
        target=$(readlink "$link")
        case "$target" in releases/mybot-rv1126b-*) ;; *) exit 19;; esac
        tail=${target#releases/}
        case "$tail" in *[!A-Za-z0-9.-]*|*..*) exit 19;; esac
    fi
done
for path in /etc/botstation-device.conf /etc/dnsmasq.conf /etc/dhcpcd.conf \\
    /etc/wpa_supplicant.conf /etc/mybot-rv1126b.conf /lib/dhcpcd/dhcpcd-hooks/99-mybot-lease; do
    [ ! -L "$path" ] || exit 17
    if [ -e "$path" ]; then [ -f "$path" ] && [ "$(stat -c %u "$path")" = 0 ] || exit 18; fi
done
if [ -e /userdata/mybot-rv1126b ]; then
    [ "$(stat -c %u /userdata/mybot-rv1126b)" = 0 ] || exit 15
    [ "$(stat -c %a /userdata/mybot-rv1126b)" = 700 ] || exit 16
fi
printf 'platform=ready\\n'
"""


def capability_preflight(runtime=None):
    """Read target capabilities before any backup, upload or service changes."""
    if runtime is None:
        runtime = json.loads((ROOT / "dependencies.lock.json").read_text())["external"]["runtime"]["libraries"]
    if not isinstance(runtime, dict) or not runtime:
        raise ValueError("Missing pinned target runtime baseline")
    for path, expected in runtime.items():
        if (not isinstance(path, str) or not re.fullmatch(r"(?:lib|usr/lib)/[A-Za-z0-9_.+-]+", path) or
                not isinstance(expected, str) or not SHA256.fullmatch(expected)):
            raise ValueError("Invalid pinned target runtime baseline")
    checksums = "\n".join(expected + "  /" + path for path, expected in sorted(runtime.items()))
    return """
for tool in amixer ip iw hostapd dnsmasq wpa_supplicant udhcpc; do
    command -v "$tool" >/dev/null || exit 90
done
[ -x /sbin/poweroff ] && [ -x /lib/ld-linux-aarch64.so.1 ] || exit 91
[ -r /proc/device-tree/model ] || exit 92
model=$(tr -d '\\000' < /proc/device-tree/model)
[ "$model" = 'Rockchip RV1126B TEST2 RK805B V10 Board' ] || exit 92
backlight=/sys/class/backlight/backlight
physical=/sys/devices/platform/backlight/backlight/backlight
[ "$(readlink -f "$backlight")" = "$physical" ] && [ -d "$physical" ] &&
    [ "$(stat -c '%u:%g:%a' "$physical")" = '0:0:755' ] || exit 96
for property in brightness bl_power max_brightness actual_brightness; do
    path="$backlight/$property"
    [ -f "$path" ] && [ ! -L "$path" ] || exit 96
    case "$property" in brightness|bl_power) permission=644;; *) permission=444;; esac
    [ "$(stat -c '%u:%g:%a' "$path")" = "0:0:$permission" ] || exit 96
done
backlight_integer() {
    number=$(cat "$backlight/$1") || return 1
    case "$number" in ''|*[!0-9]*|??????*) return 1;; esac
    [ "$number" -le 65535 ] || return 1
    printf '%s\\n' "$number"
}
maximum=$(backlight_integer max_brightness) || exit 96
brightness=$(backlight_integer brightness) || exit 96
actual=$(backlight_integer actual_brightness) || exit 96
power=$(backlight_integer bl_power) || exit 96
[ "$maximum" -gt 0 ] && [ "$brightness" -le "$maximum" ] &&
    [ "$actual" -le "$maximum" ] && [ "$power" -le 4 ] || exit 96
[ -d /lib/dhcpcd/dhcpcd-hooks ] && [ ! -L /lib/dhcpcd/dhcpcd-hooks ] &&
    [ "$(stat -c %u /lib/dhcpcd/dhcpcd-hooks)" = 0 ] || exit 93
for path in /run/mybot-network /run/mybot-network/leases /run/mybot-rv1126b \\
    /run/mybot-rv1126b-service; do
    [ ! -L "$path" ] || exit 94
    if [ -e "$path" ]; then
        [ -d "$path" ] && [ "$(stat -c %u "$path")" = 0 ] || exit 94
    fi
done
for setting in 'ACodec_LP Digital Gain Volume:0' 'ACodec_LP PGA Gain Volume:23'; do
    control=${setting%:*}
    expected=${setting##*:}
    information=$(amixer -c 0 cget "name=$control" 2>/dev/null) || exit 95
    printf '%s\\n' "$information" | awk -F, -v expected="$expected" '
        /type=INTEGER/ && /access=rw/ {
            for (i = 1; i <= NF; i++) {
                field = $i
                gsub(/^[[:space:]]+|[[:space:]]+$/, "", field)
                if (field ~ /^min=-?[0-9]+$/) { minimum = substr(field, 5); have_minimum = 1 }
                if (field ~ /^max=-?[0-9]+$/) { maximum = substr(field, 5); have_maximum = 1 }
            }
            if (have_minimum && have_maximum && minimum <= expected && maximum >= expected) valid = 1
        }
        END { exit !valid }
    ' || exit 95
done
sha256sum -c - >/dev/null <<'MYBOT_RUNTIME_BASELINE'
""" + checksums + """
MYBOT_RUNTIME_BASELINE
printf 'capabilities=ready\\n'
"""


def begin_transaction(tag):
    return f"""
base={DEVICE_ROOT}
tag={shlex.quote(tag)}
mkdir /run/mybot-deploy.lock || exit 30
# The lock intentionally survives this shell until the host completes all
# upload/install phases. A failed deployment leaves it for explicit inspection.
mkdir -p "$base/releases" "$base/backups"
chmod 700 "$base" "$base/backups"
backup="$base/backups/$tag"
[ ! -e "$backup" ] || exit 31
mkdir -m 700 "$backup"
set --
for path in etc/init.d/S99botstation-device etc/botstation-device.conf etc/asound.conf \\
    etc/dnsmasq.conf etc/dhcpcd.conf etc/wpa_supplicant.conf etc/mybot-rv1126b.conf \\
    etc/init.d/S99mybot etc/mybot-rv1126b lib/dhcpcd/dhcpcd-hooks/99-mybot-lease \\
    userdata/mybot-rv1126b userdata/mybot/current userdata/mybot/previous; do
    if [ -e "/$path" ] || [ -L "/$path" ]; then set -- "$@" "$path"; fi
done
[ "$#" -gt 0 ] || exit 32
tar -C / -czf "$backup/original.tar.gz" "$@"
chmod 600 "$backup/original.tar.gz"
sha256sum "$backup/original.tar.gz" | awk '{{print "backup_sha256=" $1}}'
printf 'backup=%s\\n' "$backup"
sync
"""


def install_release(adb, package, tag):
    """Upload only public archives to a checked private staging directory."""
    destination = f"{DEVICE_ROOT}/.upload-{tag}-{package['sha256'][:8]}.tar.gz"
    adb.command(["push", str(package["path"]), destination])
    return f"""
archive={shlex.quote(destination)}
[ "$(sha256sum "$archive" | awk '{{print $1}}')" = {package['sha256']} ] || exit 40
release={shlex.quote(DEVICE_ROOT + '/releases/' + package['name'])}
if [ -e "$release" ] || [ -L "$release" ]; then
    [ -d "$release" ] && [ ! -L "$release" ] || exit 41
    [ -f "$release/.archive-sha256" ] && [ ! -L "$release/.archive-sha256" ] &&
        [ "$(cat "$release/.archive-sha256")" = {package['sha256']} ] || exit 42
    (cd "$release" && sha256sum -c SHA256SUMS >/dev/null) || exit 43
else
    stage="$base/.stage-$tag-{package['sha256'][:8]}"
    mkdir -m 700 "$stage"
    tar -C "$stage" -xzf "$archive"
    chown -R 0:0 "$stage/{package['name']}"
    (cd "$stage/{package['name']}" && sha256sum -c SHA256SUMS >/dev/null) || exit 44
    printf '%s\\n' {package['sha256']} > "$stage/{package['name']}/.archive-sha256"
    mv "$stage/{package['name']}" "$release"
    rmdir "$stage"
fi
rm "$archive"
"""


def release_load_check(name, video):
    """Load only the selected package entry point's information-only command."""
    match = NAME.fullmatch(name)
    if not match:
        raise ValueError("Invalid installed release name")
    executable = "mybot-rv1126b-video" if video else "mybot-rv1126b"
    language = "en-US" if "-en-US-" in name else "zh-CN"
    return f"""
release={shlex.quote(DEVICE_ROOT + '/releases/' + name)}
[ -d "$release" ] && [ ! -L "$release" ] || exit 82
[ -d "$release/bin" ] && [ ! -L "$release/bin" ] || exit 87
for executable in "$release/run.sh" "$release/bin/{executable}"; do
    [ -f "$executable" ] && [ ! -L "$executable" ] && [ -x "$executable" ] || exit 87
done
version=$(RV1126B_VIDEO={int(video)} "$release/run.sh" --version 2>/dev/null) || exit 88
case "$version" in
    "mybot-rv1126b {match[1]}, MyBot "*", video={int(video)}, UI={language}") ;;
    *) exit 88;;
esac
"""


INSTALL_FILE = """
install_file() {
    deployment_source=$1
    deployment_target=$2
    deployment_mode=$3
    [ -f "$deployment_source" ] && [ ! -L "$deployment_source" ] || exit 45
    [ ! -L "$deployment_target" ] || exit 46
    [ ! -e "$deployment_target" ] || [ -f "$deployment_target" ] || exit 46
    deployment_parent=${deployment_target%/*}
    deployment_name=${deployment_target##*/}
    deployment_temporary="$deployment_parent/.mybot-$tag-$deployment_name"
    [ ! -e "$deployment_temporary" ] && [ ! -L "$deployment_temporary" ] || exit 47
    cp "$deployment_source" "$deployment_temporary"
    chmod "$deployment_mode" "$deployment_temporary"
    [ "$(sha256sum "$deployment_source" | awk '{print $1}')" = "$(sha256sum "$deployment_temporary" | awk '{print $1}')" ] || exit 48
    mv -f "$deployment_temporary" "$deployment_target"
}
"""


SYSTEM_POLICY = """
if [ -x /etc/init.d/S99mybot ]; then /etc/init.d/S99mybot stop >/dev/null 2>&1 || exit 50; fi
if [ -x /etc/init.d/S99botstation-device ]; then
    /etc/init.d/S99botstation-device stop >/dev/null 2>&1 || exit 51
fi
for comm in /proc/[0-9]*/comm; do
    [ -r "$comm" ] || continue
    IFS= read -r name < "$comm" || continue
    # Linux comm contains at most 15 characters; retain both full names and
    # their truncated forms. This check refuses deployment without signaling.
    case "$name" in botstation-devi|botstation-device|mybot-rv1126b|mybot-rv1126b-v|mybot-rv1126b-video) exit 52;; esac
done
mkdir -p /etc/mybot-rv1126b/disabled
chmod 755 /etc/mybot-rv1126b /etc/mybot-rv1126b/disabled
if [ -f /etc/botstation-device.conf ]; then
    awk '/^[[:space:]]*(export[[:space:]]+)?BOTSTATION_ENABLED[[:space:]]*=/ {if (!seen++) print "BOTSTATION_ENABLED=0"; next}
         {print} END {if (!seen) print "BOTSTATION_ENABLED=0"}' /etc/botstation-device.conf > "$backup/stock-disabled.conf"
else
    printf 'BOTSTATION_ENABLED=0\\n' > "$backup/stock-disabled.conf"
fi
install_file "$backup/stock-disabled.conf" /etc/botstation-device.conf 600
if [ -f /etc/wpa_supplicant.conf ]; then chmod 600 /etc/wpa_supplicant.conf; fi
if [ -e /etc/init.d/S99botstation-device ] || [ -L /etc/init.d/S99botstation-device ]; then
    [ ! -e /etc/mybot-rv1126b/disabled/S99botstation-device ] || exit 53
    mv /etc/init.d/S99botstation-device /etc/mybot-rv1126b/disabled/S99botstation-device
fi
if [ -f /etc/dnsmasq.conf ]; then
    awk '!/^[[:space:]]*(interface|except-interface|bind-interfaces|bind-dynamic)[[:space:]]*(=|$)/ {print}
         END {print "interface=usb0"; print "bind-dynamic"}' /etc/dnsmasq.conf > "$backup/dnsmasq.conf.new"
    install_file "$backup/dnsmasq.conf.new" /etc/dnsmasq.conf 644
fi
if [ -f /etc/dhcpcd.conf ]; then
    awk '!/^# MyBot owns AP interfaces$/ && !/^[[:space:]]*denyinterfaces[[:space:]]+mbap[*][[:space:]]*$/ {print}
         END {print "# MyBot owns AP interfaces"; print "denyinterfaces mbap*"}' \\
        /etc/dhcpcd.conf > "$backup/dhcpcd.conf.new"
    install_file "$backup/dhcpcd.conf.new" /etc/dhcpcd.conf 644
fi
printf 'network_policy_restart_required=true\\n'
"""


def switch_release(name, fallback, start=False):
    return f"""
target={shlex.quote('releases/' + name)}
[ -d "$base/$target" ] && [ ! -L "$base/$target" ] || exit 60
old=$(readlink "$base/current" 2>/dev/null || true)
if [ -z "$old" ]; then old={shlex.quote('releases/' + fallback if fallback else '')}; fi
case "$old" in ''|releases/mybot-rv1126b-*) ;; *) exit 61;; esac
if [ -n "$old" ]; then
    tail=${{old#releases/}}
    case "$tail" in *[!A-Za-z0-9.-]*|*..*) exit 61;; esac
    [ -d "$base/$old" ] && [ ! -L "$base/$old" ] || exit 61
fi
if [ -n "$old" ] && [ "$old" != "$target" ]; then
    ln -s "$old" "$base/.previous-$tag"
    mv -Tf "$base/.previous-$tag" "$base/previous"
fi
ln -s "$target" "$base/.current-$tag"
mv -Tf "$base/.current-$tag" "$base/current"
sync
printf 'current=%s\\n' "$target"
{SERVICE + ' start >/dev/null 2>&1 || exit 62' if start else ':'}
"""


def rollback_release_check(name, settings):
    """Check the preserved service profile and load the target before stopping it."""
    match = NAME.fullmatch(name)
    if not match:
        raise ValueError("Invalid installed release name")
    video = int(settings["video"])
    return f"""
configuration=/etc/mybot-rv1126b.conf
[ -f "$configuration" ] && [ ! -L "$configuration" ] &&
    [ "$(stat -c '%u:%a' "$configuration")" = '0:600' ] || exit 85
actual_video=$(
    unset RV1126B_VIDEO
    . "$configuration"
    printf '%s\\n' "${{RV1126B_VIDEO:-0}}"
)
case "$actual_video" in 0|1) ;; *) exit 86;; esac
[ "$actual_video" = {video} ] || exit 86
release={shlex.quote(DEVICE_ROOT + '/releases/' + name)}
[ -d "$release" ] && [ ! -L "$release" ] || exit 82
[ -d "$release/bin" ] && [ ! -L "$release/bin" ] || exit 87
[ -f "$release/.archive-sha256" ] && [ ! -L "$release/.archive-sha256" ] || exit 83
for marker in factory-reset.pending factory-reset.setup; do
    if [ -e /userdata/mybot/"$marker" ] || [ -L /userdata/mybot/"$marker" ]; then
        [ -x "$release/bin/mybot-factory-reset" ] || exit 89
    fi
done
recorded=$(cat "$release/.archive-sha256")
[ "${{#recorded}}" = 64 ] || exit 84
case "$recorded" in *[!0-9a-f]*) exit 84;; esac
(cd "$release" && sha256sum -c SHA256SUMS >/dev/null) || exit 81
""" + release_load_check(name, bool(video))


def status_script(expected_release=None, video=None):
    if expected_release is not None and not NAME.fullmatch(expected_release):
        raise ValueError("Invalid installed release name")
    selected = "mybot-rv1126b-video" if video else "mybot-rv1126b"
    executable_check = (f'[ "$executable" = "$release/bin/{selected}" ]' if video is not None else
                        '[ "$executable" = "$release/bin/mybot-rv1126b" ] || '
                        '[ "$executable" = "$release/bin/mybot-rv1126b-video" ]')
    return f"""
[ "$(id -u)" = 0 ] || exit 10
current=
for item in current previous; do
    target=$(readlink {DEVICE_ROOT}/"$item" 2>/dev/null || true)
    case "$target" in ''|releases/mybot-rv1126b-*) ;; *) exit 70;; esac
    tail=${{target#releases/}}
    case "$tail" in *[!A-Za-z0-9.-]*|*..*) exit 70;; esac
    printf '%s=%s\\n' "$item" "$target"
    if [ "$item" = current ]; then current=$target; fi
done
service_state=not_installed
if [ -x {SERVICE} ]; then
    service_state=$({SERVICE} status 2>/dev/null) || :
    case "$service_state" in starting|running|backoff|stopping|stopped|failed|poweroff_pending|poweroff|factory_reset_pending) ;;
        *) service_state=query_failed;; esac
fi
printf 'service=%s\\n' "$service_state"
health=not_ready
verified=no
version=
pid=
state=unavailable
release={DEVICE_ROOT}/$current
expected_version=${{current#releases/mybot-rv1126b-}}
expected_version=${{expected_version%%-*}}
wanted={shlex.quote('releases/' + expected_release) if expected_release else '"$current"'}
status=/run/mybot-rv1126b/application.status
if [ "$expected_version" != 0.1.1 ] &&
   [ ! -L /run/mybot-rv1126b ] && [ -d /run/mybot-rv1126b ] &&
   [ "$(stat -c '%u:%a' /run/mybot-rv1126b)" = '0:700' ] &&
   [ -f "$status" ] && [ ! -L "$status" ] &&
   [ "$(stat -c '%u:%a:%h' "$status")" = '0:600:1' ] &&
   [ "$(stat -c %s "$status")" -le 128 ] &&
   information=$(cat "$status") && printf '%s\\n' "$information" | awk -F= '
       NF != 2 {{ exit 1 }}
       $1 == "version" && $2 ~ /^[0-9]+\\.[0-9]+\\.[0-9]+$/ {{ version++; next }}
       $1 == "pid" && $2 ~ /^[0-9]+$/ && length($2) <= 10 && $2 + 0 > 1 {{ pid++; next }}
       $1 == "state" && $2 ~ /^[a-z_]+$/ && length($2) < 32 {{ state++; next }}
       {{ exit 1 }}
       END {{ if (version != 1 || pid != 1 || state != 1) exit 1 }}
   '; then
    version=$(printf '%s\\n' "$information" | awk -F= '$1 == "version" {{print $2}}')
    pid=$(printf '%s\\n' "$information" | awk -F= '$1 == "pid" {{print $2}}')
    state=$(printf '%s\\n' "$information" | awk -F= '$1 == "state" {{print $2}}')
    executable=$(readlink /proc/"$pid"/exe 2>/dev/null || true)
    if [ -n "$current" ] && [ "$current" = "$wanted" ] && [ "$version" = "$expected_version" ] &&
       [ -d "$release" ] && [ ! -L "$release" ] && kill -0 "$pid" 2>/dev/null &&
       {{ {executable_check}; }}; then
        verified=yes
        if [ "$service_state" = running ]; then
            case "$state" in
                ready|conversation) health=ready;;
                waiting_network|waiting_clock|wifi_error|ap|pairing|offline) health=degraded;;
                starting_services) health=started;;
            esac
        fi
    fi
fi
# The accepted 0.1.1 application predates application.status. Its local
# lifetime can be confirmed, but its business state cannot be reported.
if [ "$expected_version" = 0.1.1 ] && [ "$service_state" = running ] &&
   [ "$current" = "$wanted" ] && [ -d "$release" ] && [ ! -L "$release" ]; then
    for process in /proc/[0-9]*/exe; do
        executable=$(readlink "$process" 2>/dev/null || true)
        {{ {executable_check}; }} || continue
        candidate=${{process%/exe}}
        candidate=${{candidate##*/}}
        kill -0 "$candidate" 2>/dev/null || continue
        parent=$(awk '$1 == "PPid:" && $2 ~ /^[0-9]+$/ && length($2) <= 10 && $2 + 0 > 1 {{print $2}}' \\
            /proc/"$candidate"/status 2>/dev/null) || continue
        [ -n "$parent" ] && kill -0 "$parent" 2>/dev/null || continue
        [ "$(readlink /proc/"$parent"/exe 2>/dev/null)" = /etc/mybot-rv1126b/mybot-supervisor ] || continue
        tr '\\000' '\\n' < /proc/"$parent"/cmdline 2>/dev/null | awk '
            NR == 1 {{ if ($0 != "/etc/mybot-rv1126b/mybot-supervisor") invalid=1 }}
            NR == 2 {{ if ($0 == "supervise") supervise=1; else invalid=1 }}
            $0 == "--" {{ separator=1; exit }}
            pending {{ if ($0 == "/run/mybot-rv1126b-service") runtime++; else invalid=1; pending=0; next }}
            $0 == "--runtime" {{ pending=1 }}
            END {{ exit !supervise || runtime != 1 || pending || !separator || invalid }}
        ' || continue
        version=0.1.1
        pid=$candidate
        state=unsupported
        verified=yes
        health=legacy_running
        break
    done
fi
if [ -n "$version" ] && [ -n "$pid" ]; then
    printf 'application_version=%s\\napplication_pid=%s\\n' "$version" "$pid"
fi
printf 'application_state=%s\\napplication_verified=%s\\napplication_health=%s\\n' "$state" "$verified" "$health"
"""


def wait_for_application(adb, release, video, timeout=30):
    """Confirm a local application lifetime; cloud readiness is independent."""
    deadline = time.monotonic() + timeout
    while True:
        remaining = max(0.1, deadline - time.monotonic())
        output = adb.shell(status_script(release, video), timeout=min(5, remaining))
        status = dict(line.split("=", 1) for line in output.splitlines() if "=" in line)
        legacy = (NAME.fullmatch(release)[1] == "0.1.1" and
                  status.get("application_health") == "legacy_running" and
                  status.get("application_state") == "unsupported")
        if (status.get("service") == "running" and status.get("application_verified") == "yes" and
                (status.get("application_health") in ("ready", "degraded", "started") or legacy)):
            return output
        if time.monotonic() >= deadline:
            raise RuntimeError("Selected application did not reach a verified local startup state; "
                               "inspect the private on-device service state")
        time.sleep(0.2)


def receipt(tag, action, values, output_root=ROOT / "local/deployment"):
    if not TAG.fullmatch(tag):
        raise ValueError("Invalid deployment tag")
    directory = Path(output_root) / tag
    directory.mkdir(parents=True, exist_ok=False, mode=0o700)
    directory.chmod(0o700)
    path = directory / "receipt.json"
    path.write_text(json.dumps({"schema": 1, "action": action, **values}, indent=2) + "\n")
    path.chmod(0o600)
    return path


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=("preflight", "deploy", "rollback", "status"))
    parser.add_argument("--serial")
    parser.add_argument("--adb", default="adb")
    parser.add_argument("--settings", type=Path, help="Private JSON: server/device_id/video/wifi_interface")
    parser.add_argument("--package", type=Path)
    parser.add_argument("--sha256")
    parser.add_argument("--rollback-package", type=Path, help="Import the accepted 0.1.1 rollback archive")
    parser.add_argument("--rollback-sha256")
    parser.add_argument("--tag", help="Unique public deployment tag")
    parser.add_argument("--release", help="Explicit installed release name for rollback; default: previous")
    parser.add_argument("--execute", action="store_true", help="Allow device mutation; otherwise deploy/rollback only print a plan")
    parser.add_argument("--dry-run", action="store_true", help="Explicitly select the default mutation-free plan")
    parser.add_argument("--start", action="store_true", help="Start S99mybot after switching; does not reboot")
    args = parser.parse_args(argv)
    if args.execute and args.dry_run:
        parser.error("--execute and --dry-run are mutually exclusive")
    try:
        package = verify_package(args.package, args.sha256) if args.package else None
        rollback = verify_package(args.rollback_package, args.rollback_sha256, legacy=True) if args.rollback_package else None
        settings = read_settings(args.settings) if args.settings else None
        if args.action in ("deploy", "rollback") and settings is None:
            raise ValueError("A private --settings file is required before changing installed versions")
        if args.action == "deploy" and not package:
            raise ValueError("deploy requires --package and --sha256")
        if package and settings:
            selected = "video" if settings["video"] else "voice"
            if selected not in package["profiles"]:
                raise ValueError(f"Package lacks the selected {selected} profile")
        if args.release and not NAME.fullmatch(args.release):
            raise ValueError("Invalid installed release name")
        tag = args.tag or datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
        if not TAG.fullmatch(tag):
            raise ValueError("Invalid deployment tag")
        if args.action in ("deploy", "rollback") and not args.execute:
            print(json.dumps({"action": args.action, "dry_run": True, "tag": tag,
                              "release": package["name"] if package else args.release or "previous",
                              "profile": "video" if settings["video"] else "voice",
                              "backup": DEVICE_ROOT + "/backups/" + tag,
                              "data": "preserve existing private store; never import stock auth",
                              "identity": "compare server/id on device; token is never read",
                              "network": "usb0 dnsmasq bind-dynamic + dhcpcd deny mbap*; operator reload required",
                              "autostart": args.start, "reboot": False}, indent=2))
            return 0
        adb = Adb(args.adb, args.serial)
        adb.select()
        if args.action == "status":
            print(adb.shell(status_script()).strip())
            return 0
        output = adb.shell(PREFLIGHT + capability_preflight() + (identity_check(settings) if settings else ""))
        if args.action == "preflight":
            print(output.strip())
            return 0
        rollback_target = args.release
        if args.action == "rollback":
            if rollback_target is None:
                value = adb.shell(f"readlink {DEVICE_ROOT}/previous\n").strip()
                if not value.startswith("releases/") or not NAME.fullmatch(value[9:]):
                    raise ValueError("No checked previous release is available")
                rollback_target = value[9:]
            adb.shell(rollback_release_check(rollback_target, settings))
        # Backup completes before uploads, system changes, or link replacement.
        output = adb.shell(begin_transaction(tag))
        if args.action == "deploy":
            staging_script = f"base={DEVICE_ROOT}\ntag={shlex.quote(tag)}\n"
            if rollback:
                staging_script += install_release(adb, rollback, tag)
            staging_script += install_release(adb, package, tag)
            staging_script += release_load_check(package["name"], settings["video"])
            adb.shell(staging_script)
            config = configuration(package["files"]["assets/service/mybot-rv1126b.conf"], settings)
            deployment = f"base={DEVICE_ROOT}\ntag={shlex.quote(tag)}\nbackup=\"$base/backups/$tag\"\n"
            deployment += identity_check(settings) + INSTALL_FILE + SYSTEM_POLICY
            release = DEVICE_ROOT + "/releases/" + package["name"]
            deployment += f"""
install_file {shlex.quote(release + '/assets/service/S99mybot')} {SERVICE} 755
install_file {shlex.quote(release + '/assets/service/run-service.sh')} /etc/mybot-rv1126b/run-service.sh 755
install_file {shlex.quote(release + '/assets/service/audio-prepare.sh')} /etc/mybot-rv1126b/audio-prepare.sh 755
install_file {shlex.quote(release + '/assets/service/poweroff.sh')} /etc/mybot-rv1126b/poweroff.sh 755
install_file {shlex.quote(release + '/bin/mybot-factory-reset')} /etc/mybot-rv1126b/mybot-factory-reset 755
install_file {shlex.quote(release + '/bin/mybot-supervisor')} /etc/mybot-rv1126b/mybot-supervisor 755
[ -d /lib/dhcpcd/dhcpcd-hooks ] && [ ! -L /lib/dhcpcd/dhcpcd-hooks ] || exit 54
install_file {shlex.quote(release + '/assets/network/99-mybot-lease')} /lib/dhcpcd/dhcpcd-hooks/99-mybot-lease 644
for directory in /run/mybot-network /run/mybot-network/leases; do
    [ ! -L "$directory" ] || exit 55
    mkdir -p "$directory"
    chmod 700 "$directory"
done
cat > "$backup/service.conf.new" <<'MYBOT_PRIVATE_CONFIGURATION'
{config}MYBOT_PRIVATE_CONFIGURATION
chmod 600 "$backup/service.conf.new"
install_file "$backup/service.conf.new" /etc/mybot-rv1126b.conf 600
"""
            deployment += switch_release(package["name"], rollback["name"] if rollback else None, args.start)
            output += adb.shell(deployment)
        else:
            rollback_script = f"base={DEVICE_ROOT}\ntag={shlex.quote(tag)}\n"
            rollback_script += identity_check(settings)
            rollback_script += rollback_release_check(rollback_target, settings)
            rollback_script += f"{SERVICE} stop >/dev/null 2>&1 || exit 80\n"
            rollback_script += switch_release(rollback_target, None, args.start)
            output += adb.shell(rollback_script)
        if args.start:
            selected = package["name"] if args.action == "deploy" else rollback_target
            print(wait_for_application(adb, selected, settings["video"]).strip())
        adb.shell("rmdir /run/mybot-deploy.lock\n")
        values = {"tag": tag, "backup": DEVICE_ROOT + "/backups/" + tag,
                  "package_sha256": package["sha256"] if package else None,
                  "device_backup_only": True, "data_preserved": True}
        for line in output.splitlines():
            if line.startswith("backup_sha256=") and SHA256.fullmatch(line.split("=", 1)[1]):
                values["backup_sha256"] = line.split("=", 1)[1]
        print("Receipt:", receipt(tag, args.action, values))
        print("Deployment completed; network policy reload remains an operator step. No reboot requested.")
        return 0
    except (OSError, ValueError, KeyError, tarfile.TarError, UnicodeError, struct.error,
            RuntimeError, subprocess.SubprocessError) as error:
        parser.error(str(error))


if __name__ == "__main__":
    main()
