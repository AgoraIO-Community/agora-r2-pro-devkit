# SPDX-License-Identifier: Apache-2.0
"""Verify the board firmware runtime used by the target SDK and release builds."""

from pathlib import Path, PurePosixPath
import re
import struct

from build_metadata import digest, read_json


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


def validate_runtime(staging, manifest, baseline):
    if not manifest:
        raise ValueError('Release requires the target runtime manifest')
    if not baseline:
        raise ValueError('Missing pinned firmware runtime baseline')
    manifest_path = Path(manifest)
    entries = read_json(manifest_path).get('libraries', [])
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
        if digest(staged) != expected:
            raise ValueError('staged runtime SHA256 mismatch: ' + relative)
        if elf_machine(staged) != 183:
            raise ValueError('staged runtime is not AArch64: ' + relative)
        snapshot = manifest_path.parent / relative
        if snapshot.is_file() and digest(snapshot) != expected:
            raise ValueError('runtime snapshot SHA256 mismatch: ' + relative)
        seen[relative] = expected
    for name, expected in baseline.items():
        if seen.get(name) != expected:
            raise ValueError('runtime baseline does not match dependency lock: ' + name)
    return seen
