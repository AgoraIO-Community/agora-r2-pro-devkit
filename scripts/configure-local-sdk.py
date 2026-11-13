#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Configure offline target tools and runtime inputs with the repository's pinned RTSA SDK."""

import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import shlex
import shutil
import struct
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
LOCK = ROOT / 'dependencies.lock.json'


def sha256(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def load_json(path):
    return json.loads(Path(path).read_text(encoding='utf-8'))


def elf_machine(path):
    with Path(path).open('rb') as stream:
        header = stream.read(20)
    if len(header) != 20 or header[:6] != b'\x7fELF\x02\x01':
        raise ValueError('expected little-endian ELF64')
    return struct.unpack_from('<H', header, 18)[0]


def runtime_relative(path):
    value = PurePosixPath(path)
    if not value.is_absolute() or '..' in value.parts or str(value) == '/':
        raise ValueError('runtime path must be an absolute target path without traversal')
    return str(value).lstrip('/')


def executable(command):
    found = shutil.which(str(command))
    if not found:
        raise ValueError('missing executable: ' + Path(command).name)
    return str(Path(found).absolute())


def checked_command(command):
    try:
        result = subprocess.run(command, text=True, capture_output=True, timeout=30)
    except (OSError, subprocess.TimeoutExpired) as error:
        raise ValueError('could not run ' + Path(command[0]).name) from error
    if result.returncode:
        raise ValueError('command failed: ' + Path(command[0]).name)
    return result.stdout.strip()


def vendored_rtsa(lock):
    matches = [entry for entry in lock['snapshots'] if entry.get('name') == 'rtsa']
    if len(matches) != 1:
        raise ValueError('dependency lock must contain one RTSA snapshot')
    rtsa = matches[0]
    sdk = (ROOT / rtsa['directory'] / rtsa['sdk_subdirectory']).resolve()
    library = sdk / rtsa['library_subpath']
    return rtsa, sdk, library.resolve()


def validate_target(values, lock, check_compiler=True, video=False):
    """Validate inputs and return portable identities, excluding private manifest data."""
    external = lock['external']
    rtsa, expected_sdk, expected_library = vendored_rtsa(lock)
    staging = Path(values['RV1126B_STAGING'])
    sdk = Path(values['AGORA_SDK_DIR'])
    library = Path(values['AGORA_RTC_LIBRARY'])
    if sdk.resolve() != expected_sdk or library.resolve() != expected_library:
        raise ValueError('RTSA must use the repository snapshot; configure the local SDK again')
    if not staging.is_dir():
        raise ValueError('target staging is missing')
    if sha256(library) != rtsa['library_sha256']:
        raise ValueError('RTSA library SHA256 does not match the dependency lock')
    if elf_machine(library) != 183:
        raise ValueError('RTSA library is not AArch64')
    for relative, expected in rtsa['headers'].items():
        if sha256(sdk / relative) != expected:
            raise ValueError('RTSA public header SHA256 mismatch: ' + relative)
    settings = dict(re.findall(r'^(CONFIG_\w+)=(.*)$', (sdk / '.config').read_text(), re.M))
    for name, expected in rtsa['required_config'].items():
        if settings.get(name) != str(expected):
            raise ValueError('RTSA configuration mismatch: ' + name)
    for header in ('alsa/asoundlib.h', 'openssl/ssl.h', 'xf86drm.h', 'libdrm/drm.h'):
        if not (staging / 'usr/include' / header).is_file():
            raise ValueError('missing target development header: ' + header)
    if video and not (staging / 'usr/include/rockchip/rk_mpi.h').is_file():
        raise ValueError('missing target video development header: rockchip/rk_mpi.h')
    manifest_path = Path(values['RV1126B_RUNTIME_MANIFEST'])
    entries = load_json(manifest_path).get('libraries', [])
    if not entries:
        raise ValueError('runtime manifest has no libraries')
    seen = {}
    for entry in entries:
        relative = runtime_relative(entry['path'])
        if relative in seen:
            raise ValueError('duplicate runtime manifest path: ' + relative)
        expected = entry['sha256']
        if not re.fullmatch(r'[0-9a-f]{64}', expected):
            raise ValueError('invalid runtime SHA256: ' + relative)
        staged = staging / relative
        if sha256(staged) != expected:
            raise ValueError('staged runtime SHA256 mismatch: ' + relative)
        if elf_machine(staged) != 183:
            raise ValueError('staged runtime is not AArch64: ' + relative)
        snapshot = manifest_path.parent / relative
        if snapshot.is_file() and sha256(snapshot) != expected:
            raise ValueError('runtime snapshot SHA256 mismatch: ' + relative)
        seen[relative] = expected
    for name, expected in external['runtime']['libraries'].items():
        if seen.get(name) != expected:
            raise ValueError('runtime baseline does not match dependency lock: ' + name)
    sysroot = values.get('RV1126B_SYSROOT')
    if sysroot:
        sysroot = Path(sysroot)
        if not all((sysroot / 'usr/include' / name).is_file() for name in ('stdio.h', 'stdint.h')):
            raise ValueError('sysroot lacks libc development headers; runtime staging is not a sysroot')
        if not any(sysroot.glob('**/crt1.o')) and not any(sysroot.glob('**/Scrt1.o')):
            raise ValueError('sysroot lacks libc startup objects')
    compiler = None
    compiler_version = None
    if check_compiler:
        prefix = values['CROSS_COMPILE']
        for suffix in ('gcc', 'g++', 'ar', 'ranlib'):
            executable(prefix + suffix)
        with tempfile.TemporaryDirectory(prefix='mybot-target-doctor-') as temporary:
            folder = Path(temporary)
            for suffix, extension in (('gcc', '.c'), ('g++', '.cc')):
                source = folder / ('probe' + extension)
                source.write_text('#include <stdint.h>\n#include <stdio.h>\nint mybot_probe(void) { return sizeof(uint64_t); }\n')
                output = folder / (suffix + '.o')
                command = [prefix + suffix, '-c', str(source), '-o', str(output)]
                if sysroot:
                    command += ['--sysroot=' + str(sysroot)]
                checked_command(command)
                if elf_machine(output) != 183:
                    raise ValueError('target compiler does not produce AArch64')
        compiler = 'aarch64-linux-gnu'
        version = checked_command([prefix + 'gcc', '--version']).splitlines()
        if not version:
            raise ValueError('target compiler returned no version identity')
        compiler_version = version[0]
    identity = hashlib.sha256(json.dumps(seen, sort_keys=True, separators=(',', ':')).encode()).hexdigest()
    return {'architecture': 'aarch64', 'compiler_target': compiler, 'compiler_version': compiler_version,
            'rtsa_version': rtsa['version'], 'rtsa_sha256': rtsa['library_sha256'],
            'audio_ptime_ms': int(rtsa['required_config']['CONFIG_MINIMAL_TIMER_INTERVAL_MS']),
            'runtime_library_count': len(seen), 'runtime_identity': identity,
            'sysroot': 'explicit' if sysroot else 'toolchain-provided'}


def zig_wrappers(zig, directory):
    zig = executable(zig)
    checked_command([zig, 'version'])
    directory.mkdir(parents=True, exist_ok=True)
    for suffix, tool in (('gcc', 'cc'), ('g++', 'c++'), ('ar', 'ar'), ('ranlib', 'ranlib')):
        command = [zig, tool]
        if tool in ('cc', 'c++'):
            command += ['-target', 'aarch64-linux-gnu.2.41']
        wrapper = directory / ('aarch64-linux-gnu-' + suffix)
        content = '#!/bin/sh\n'
        for key, name in (('ZIG_GLOBAL_CACHE_DIR', 'zig-global'), ('ZIG_LOCAL_CACHE_DIR', 'zig-local')):
            content += 'export ' + key + '=' + shlex.quote(str(ROOT / 'local/cache' / name)) + '\n'
        if tool in ('cc', 'c++'):
            # Zig 0.15 mishandles glibc-version triples in Clang introspection flags.
            content += 'if [ "$#" -eq 1 ] && [ "$1" = "--version" ]; then\n'
            content += '  exec ' + shlex.quote(zig) + ' ' + tool + ' --version\nfi\n'
            content += 'if [ "$#" -eq 1 ] && [ "$1" = "-dumpmachine" ]; then\n'
            content += '  printf \"%s\\n\" aarch64-linux-gnu\n  exit 0\nfi\n'
        content += 'exec ' + ' '.join(shlex.quote(part) for part in command) + ' "$@"\n'
        wrapper.write_text(content, encoding='utf-8')
        wrapper.chmod(0o755)
    return str(directory / 'aarch64-linux-gnu-')


def argument_parser():
    parser = argparse.ArgumentParser(description=__doc__)
    compiler = parser.add_mutually_exclusive_group()
    compiler.add_argument('--cross-compile', help='AArch64 GCC-compatible executable prefix')
    compiler.add_argument('--zig', type=Path, help='host Zig executable; create local AArch64 wrappers')
    parser.add_argument('--staging', type=Path, help='target development headers and verified runtime libraries')
    parser.add_argument('--rtsa-sdk', type=Path,
                        help='compatibility option; must point to vendor/rtsa/agora_sdk')
    parser.add_argument('--runtime-manifest', type=Path, help='existing offline target library manifest')
    parser.add_argument('--sysroot', type=Path, help='optional complete libc development sysroot')
    parser.add_argument('--reference', type=Path, help=argparse.SUPPRESS)
    return parser


def main():
    parser = argument_parser()
    args = parser.parse_args()
    if args.reference:
        reference = args.reference.expanduser().resolve()
        args.staging = args.staging or reference / 'local/sdk/staging'
        args.runtime_manifest = args.runtime_manifest or reference / 'local/device-runtime/manifest.json'
        if not args.cross_compile and not args.zig:
            matches = sorted((reference / 'local/toolchains').glob('zig-*/zig'))
            if len(matches) != 1:
                parser.error('provide --zig or --cross-compile when compiler selection is ambiguous')
            args.zig = matches[0]
    if not all((args.cross_compile or args.zig, args.staging, args.runtime_manifest)):
        parser.error('provide --cross-compile or --zig, --staging, and --runtime-manifest')
    try:
        lock = load_json(LOCK)
        _, sdk, library = vendored_rtsa(lock)
        if args.rtsa_sdk and args.rtsa_sdk.expanduser().resolve() != sdk:
            raise ValueError('--rtsa-sdk must point to the repository snapshot vendor/rtsa/agora_sdk')
        values = {'RV1126B_STAGING': str(args.staging.expanduser().resolve()),
                  'AGORA_SDK_DIR': str(sdk), 'AGORA_RTC_LIBRARY': str(library),
                  'RV1126B_RUNTIME_MANIFEST': str(args.runtime_manifest.expanduser().resolve())}
        if args.sysroot:
            values['RV1126B_SYSROOT'] = str(args.sysroot.expanduser().resolve())
        validate_target(values, lock, check_compiler=False)
        if args.zig:
            values['CROSS_COMPILE'] = zig_wrappers(args.zig.expanduser(), ROOT / 'local/sdk/bin')
        else:
            prefix = os.path.expanduser(args.cross_compile)
            values['CROSS_COMPILE'] = str(Path(executable(prefix + 'gcc')).parent / Path(prefix).name)
        summary = validate_target(values, lock)
        destination = ROOT / 'local/sdk'
        destination.mkdir(parents=True, exist_ok=True)
        (destination / 'config.json').write_text(json.dumps({'schema_version': 1, 'environment': values,
            'dependency_lock_sha256': sha256(LOCK), 'identity': summary}, indent=2) + '\n', encoding='utf-8')
        (destination / 'environment.sh').write_text(''.join('export ' + key + '=' + shlex.quote(value) + '\n'
            for key, value in sorted(values.items())), encoding='utf-8')
    except (OSError, ValueError, KeyError, TypeError) as error:
        parser.error(str(error))
    print('Configured local/sdk/environment.sh (offline; dependency inputs unchanged)')
    print(json.dumps(summary, indent=2))


if __name__ == '__main__':
    main()
