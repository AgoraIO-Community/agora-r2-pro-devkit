#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Verify committed dependency snapshots, import provenance, patches, and generated fonts offline."""

import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath

ROOT = Path(__file__).resolve().parents[1]


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def local_path(root, value):
    relative = PurePosixPath(value)
    if relative.is_absolute() or '..' in relative.parts or str(relative) in ('', '.'):
        raise ValueError('lock paths must be repository-relative without traversal')
    path = root / value
    if not path.resolve().is_relative_to(root.resolve()):
        raise ValueError('dependency path escapes the repository')
    if path.is_symlink():
        raise ValueError('dependency inputs must not be symlinks: ' + value)
    return path


def verify(root, lock):
    errors = []
    if lock.get('schema_version') != 1:
        raise ValueError('unsupported dependency lock schema')

    def checked_hash(name, expected):
        try:
            actual = sha256(local_path(root, name))
            if actual != expected:
                errors.append('SHA256 mismatch: ' + name)
        except OSError:
            errors.append('missing dependency input: ' + name)

    history = lock['original_import']
    checked_hash(history['manifest'], history['sha256'])
    original = json.loads(local_path(root, history['manifest']).read_text())
    original_files = original['imported_files']
    revisions = {name: value for name, value in original.items() if name not in ('imported_files', 'local_patches')}
    if history.get('revisions', {}) != revisions:
        errors.append('historical import revisions differ from the original manifest')
    for snapshot in lock['snapshots']:
        directory = local_path(root, snapshot['directory'])
        expected = set(snapshot['files'])
        actual = {path.relative_to(root).as_posix() for path in directory.rglob('*') if path.is_file()}
        for name in sorted(actual - expected):
            errors.append('unlocked dependency file: ' + name)
        for name in sorted(expected - actual):
            errors.append('missing snapshot file: ' + name)
        for name, digest in snapshot['files'].items():
            if not local_path(root, name).is_relative_to(directory):
                raise ValueError('snapshot file is outside its declared directory')
            checked_hash(name, digest)
    adaptations = lock['import_adaptations']
    for name, entry in adaptations.items():
        if entry['original_sha256'] != original_files.get(name):
            errors.append('adaptation original hash is not the historical import: ' + name)
        if not entry.get('reason'):
            errors.append('adaptation has no explanation: ' + name)
        checked_hash(entry['path'], entry['result_sha256'])
    for name, digest in original_files.items():
        if name not in adaptations:
            checked_hash(name, digest)
    for patch in lock['local_patches']:
        checked_hash(patch['path'], patch['sha256'])
        source = patch['original_path']
        if patch['original_sha256'] != original_files.get(source):
            errors.append('patch original hash is not the historical import: ' + source)
        checked_hash(patch['result_path'], patch['result_sha256'])
        adaptation = adaptations.get(source, {})
        if adaptation.get('result_sha256') != patch['result_sha256']:
            errors.append('patch result is not the recorded adaptation: ' + source)
    generated = lock['generated_fonts']
    checked_hash(generated['manifest'], generated['manifest_sha256'])
    checked_hash(generated['generator'], generated['generator_sha256'])
    sources = json.loads(local_path(root, generated['manifest']).read_text())
    if sources['fonts'] != generated['inputs']:
        errors.append('font source identities differ from the lock')
    for name, digest in generated['outputs'].items():
        checked_hash(name, digest)
    for name, digest in lock.get('retained_licenses', {}).items():
        checked_hash(name, digest)
    return sorted(set(errors))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=ROOT, help='checkout root (for offline verification/testing)')
    parser.add_argument('--lock', type=Path, help='lock file; defaults to <root>/dependencies.lock.json')
    parser.add_argument('--json', action='store_true')
    args = parser.parse_args()
    root = args.root.resolve()
    try:
        lock = json.loads((args.lock or root / 'dependencies.lock.json').read_text())
        errors = verify(root, lock)
    except (OSError, ValueError, KeyError, TypeError) as error:
        errors = ['invalid dependency metadata: ' + str(error)]
    summary = {'ok': not errors, 'offline': True, 'errors': errors}
    if args.json:
        print(json.dumps(summary, indent=2))
    elif errors:
        print('\n'.join(errors))
    else:
        print('Dependency provenance, snapshots, patches, and generated fonts verified (offline).')
    raise SystemExit(1 if errors else 0)


if __name__ == '__main__':
    main()
