#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Read an explicit runtime-library allowlist over ADB; write only on the host."""

import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import shlex
import subprocess
import tempfile


LIBRARIES = (
    "/lib/ld-linux-aarch64.so.1",
    "/lib/libc.so.6", "/lib/libm.so.6", "/lib/libmvec.so.1", "/lib/libdl.so.2",
    "/lib/libpthread.so.0", "/lib/librt.so.1", "/lib/libresolv.so.2",
    "/usr/lib/libasound.so.2", "/usr/lib/libssl.so.3", "/usr/lib/libcrypto.so.3",
    "/usr/lib/libdrm.so.2", "/usr/lib/librockchip_mpp.so.1",
    "/usr/lib/libfreetype.so.6", "/usr/lib/libpng16.so.16", "/usr/lib/libz.so.1",
    "/usr/lib/libstdc++.so.6", "/usr/lib/libgcc_s.so.1", "/usr/lib/libatomic.so.1",
    "/usr/lib/libbz2.so.1.0", "/usr/lib/libbrotlidec.so.1",
    "/usr/lib/libbrotlicommon.so.1",
)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--serial", required=True, help="ADB device serial")
    parser.add_argument("--output", type=Path, default=Path("local/device-runtime"))
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    adb = ["adb", "-s", args.serial]

    def shell(command):
        return subprocess.run(adb + ["shell", command], check=True,
                              stdout=subprocess.PIPE, stderr=subprocess.PIPE).stdout

    manifest = {"serial": args.serial, "libraries": [], "missing": []}
    # These are public system properties, never application config or logs.
    for label, command in (("uname", "uname -a"),
                           ("os_release", "cat /etc/os-release"),
                           ("kernel_build", "cat /proc/version")):
        manifest[label] = shell(command).decode("utf-8", errors="replace").strip()

    for remote in LIBRARIES:
        try:
            resolved = shell("readlink -f " + shlex.quote(remote)).decode().strip()
            path = PurePosixPath(resolved)
            if (not path.is_absolute() or ".." in path.parts or
                    not resolved.startswith(("/lib/", "/usr/lib/"))):
                raise ValueError("library symlink leaves the allowed library directories")
            shell("test -f " + shlex.quote(resolved))
        except subprocess.CalledProcessError:
            manifest["missing"].append(remote)
            continue
        destination = output / remote.lstrip("/")
        destination.parent.mkdir(parents=True, exist_ok=True)
        # Atomic local replacement avoids following an existing destination symlink.
        with tempfile.NamedTemporaryFile(dir=destination.parent, delete=False) as stream:
            temporary = Path(stream.name)
            try:
                subprocess.run(adb + ["exec-out", "cat " + shlex.quote(resolved)],
                               stdout=stream, check=True)
            except BaseException:
                temporary.unlink()
                raise
        data = temporary.read_bytes()
        if not data.startswith(b"\x7fELF"):
            temporary.unlink()
            raise ValueError("expected an ELF runtime library: " + remote)
        temporary.replace(destination)
        manifest["libraries"].append({"path": remote, "resolved": resolved,
                                      "size": len(data),
                                      "sha256": hashlib.sha256(data).hexdigest()})
        print("read " + remote, flush=True)

    (output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print("Runtime snapshot: " + str(output))
    print("This is a runtime snapshot, not a development sysroot.")


if __name__ == "__main__":
    main()
