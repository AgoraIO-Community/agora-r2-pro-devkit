#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Check host or target build inputs offline; never contact a device or download files."""

import argparse
import importlib.util
import json
import os
from pathlib import Path
import re
import shlex
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
_spec = importlib.util.spec_from_file_location('sdk_configuration', ROOT / 'scripts/configure-local-sdk.py')
sdk = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(sdk)


def run(command, environment=None, failure=None):
    result = subprocess.run(command, capture_output=True, text=True, timeout=30, env=environment)
    if result.returncode:
        raise ValueError(failure or ('probe failed: ' + Path(command[0]).name))
    return result.stdout.strip()


def host_check(drm_include=None):
    if sys.version_info < (3, 10):
        raise ValueError('Python 3.10 or newer is required')
    cmake = sdk.executable('cmake')
    version = run([cmake, '--version']).splitlines()[0]
    match = re.search(r'(\d+)\.(\d+)\.(\d+)', version)
    if not match or tuple(map(int, match.groups())) < (3, 25, 0):
        raise ValueError('CMake 3.25 or newer is required')
    cc = shlex.split(os.environ.get('CC', 'cc'))
    cxx = shlex.split(os.environ.get('CXX', 'c++'))
    if not cc or not cxx:
        raise ValueError('CC and CXX must name host compiler executables')
    sdk.executable(cc[0])
    sdk.executable(cxx[0])
    pkg_config = sdk.executable('pkg-config')
    environment = os.environ.copy()
    # Target build-script variables must not turn host checks into cross checks.
    environment.pop('PKG_CONFIG_SYSROOT_DIR', None)
    environment.pop('PKG_CONFIG_LIBDIR', None)
    openssl_flags = shlex.split(run([pkg_config, '--cflags', '--libs', 'openssl'], environment, 'host OpenSSL development files are unavailable via pkg-config'))
    if drm_include:
        include = Path(drm_include).expanduser().resolve()
        drm_flags = ['-I' + str(include), '-I' + str(include / 'libdrm'), '-I' + str(include.parent)]
        drm_source = 'explicit headers'
    else:
        drm_flags = shlex.split(run([pkg_config, '--cflags', 'libdrm'], environment, 'host libdrm development headers are unavailable; install libdrm-dev or provide --drm-include'))
        drm_source = 'host pkg-config'
    with tempfile.TemporaryDirectory(prefix='mybot-host-doctor-') as temporary:
        folder = Path(temporary)
        c_source = folder / 'openssl.c'
        c_source.write_text('#include <openssl/evp.h>\nint main(void) { return EVP_sha256() == 0; }\n')
        run(cc + [str(c_source), '-o', str(folder / 'openssl')] + openssl_flags, environment, 'host OpenSSL compile/link failed')
        cpp_source = folder / 'compiler.cpp'
        cpp_source.write_text('#include <string>\nint main() { return std::string("host").empty(); }\n')
        run(cxx + [str(cpp_source), '-o', str(folder / 'compiler')], environment)
        drm_source_file = folder / 'drm.c'
        drm_source_file.write_text('#include <xf86drm.h>\n#include <xf86drmMode.h>\nint main(void) { return sizeof(drmModeModeInfo) == 0; }\n')
        run(cc + ['-fsyntax-only', str(drm_source_file)] + drm_flags, environment, 'host libdrm header compilation failed')
    return {'mode': 'host', 'python': '.'.join(map(str, sys.version_info[:3])),
            'cmake': match.group(0), 'c_compiler': Path(cc[0]).name,
            'cxx_compiler': Path(cxx[0]).name, 'openssl': 'compile/link passed',
            'libdrm': drm_source + '; compile passed', 'target_dependencies_required': False}


def target_check(config_path, video=False):
    config = sdk.load_json(config_path)
    if config.get('schema_version') != 1:
        raise ValueError('unsupported local SDK configuration schema')
    if config.get('dependency_lock_sha256') != sdk.sha256(sdk.LOCK):
        raise ValueError('dependency lock changed; configure the local SDK again')
    summary = sdk.validate_target(config['environment'], sdk.load_json(sdk.LOCK), video=video)
    if summary != config.get('identity'):
        raise ValueError('configured dependency identity changed; configure the local SDK again')
    summary['mode'] = 'target'
    summary['video_headers'] = 'checked' if video else 'not requested'
    return summary


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument('--host', action='store_true', help='independent host compiler/header/link checks')
    mode.add_argument('--target', action='store_true', help='configured target inputs (default)')
    parser.add_argument('--config', type=Path, default=ROOT / 'local/sdk/config.json')
    parser.add_argument('--drm-include', type=Path, help='explicit host-compatible DRM include root or libdrm UAPI directory')
    parser.add_argument('--video', action='store_true', help='also require MPP target development headers')
    parser.add_argument('--json', action='store_true', help='print a portable JSON summary')
    args = parser.parse_args()
    if args.host and args.video:
        parser.error('--video applies only to target checks')
    try:
        summary = host_check(args.drm_include) if args.host else target_check(args.config, args.video)
    except (OSError, ValueError, KeyError, TypeError, subprocess.TimeoutExpired) as error:
        if isinstance(error, FileNotFoundError):
            message = 'required local input is missing: ' + Path(error.filename or '').name
        else:
            message = str(error)
        result = {'ok': False, 'mode': 'host' if args.host else 'target', 'error': message}
        print(json.dumps(result, indent=2) if args.json else 'Doctor failed: ' + message)
        raise SystemExit(1)
    result = {'ok': True, **summary}
    if args.json:
        print(json.dumps(result, indent=2))
    else:
        print('Doctor passed (' + summary['mode'] + ', offline)')
        for name, value in summary.items():
            if name != 'mode':
                print('  ' + name + ': ' + str(value))


if __name__ == '__main__':
    main()
