#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Reuse a verified local SDK without changing it or accessing the device."""
import argparse
import hashlib
import json
from pathlib import Path
import shlex

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--reference', type=Path, default=ROOT.parent / 'agora-r2-pro-devkit')
    args = parser.parse_args()
    reference = args.reference.resolve()
    zig = reference / 'local/toolchains/zig-x86_64-macos-0.15.2/zig'
    staging = reference / 'local/sdk/staging'
    rtsa = reference / 'local/dependencies/rtsa/agora_rtsa_sdk/agora_sdk'
    library = rtsa / 'lib/aarch64/libagora-rtc-sdk.so'
    for path in (zig, staging, library):
        if not path.exists():
            parser.error('Missing dependency: ' + str(path))
    expected = '0e878752644d94227c2aa5f3a9d2259c4098850297c867a2460cbbf8c66d3d98'
    if hashlib.sha256(library.read_bytes()).hexdigest() != expected:
        parser.error('Unexpected RTSA library; provide and record a matching package explicitly')
    manifest = json.loads((reference / 'local/device-runtime/manifest.json').read_text())
    for entry in manifest['libraries']:
        runtime = reference / 'local/device-runtime' / entry['path'].lstrip('/')
        staged = staging / entry['path'].lstrip('/')
        if any(hashlib.sha256(p.read_bytes()).hexdigest() != entry['sha256']
               for p in (runtime, staged)):
            parser.error('Runtime snapshot/staging mismatch: ' + entry['path'])
    binaries = ROOT / 'local/sdk/bin'
    binaries.mkdir(parents=True, exist_ok=True)
    for name, tool in [('gcc', 'cc'), ('g++', 'c++'), ('ar', 'ar'), ('ranlib', 'ranlib')]:
        command = [str(zig), tool]
        if tool in ('cc', 'c++'):
            command += ['-target', 'aarch64-linux-gnu.2.41']
        wrapper = binaries / ('aarch64-linux-gnu-' + name)
        wrapper.write_text('#!/bin/sh\n' +
            'export ZIG_GLOBAL_CACHE_DIR=' + shlex.quote(str(ROOT / 'local/cache/zig-global')) + '\n' +
            'export ZIG_LOCAL_CACHE_DIR=' + shlex.quote(str(ROOT / 'local/cache/zig-local')) + '\n' +
            'exec ' + ' '.join(shlex.quote(x) for x in command) + ' "$@"\n')
        wrapper.chmod(0o755)
    values = {'CROSS_COMPILE': str(binaries / 'aarch64-linux-gnu-'),
              'RV1126B_STAGING': str(staging), 'AGORA_SDK_DIR': str(rtsa),
              'AGORA_RTC_LIBRARY': str(library)}
    environment = ROOT / 'local/sdk/environment.sh'
    environment.write_text(''.join('export ' + key + '=' + shlex.quote(value) + '\n'
                                   for key, value in values.items()))
    print('Local SDK configured:', environment)
    print('Source SDK and device unchanged; caches stay under this repository/local.')


if __name__ == '__main__':
    main()
