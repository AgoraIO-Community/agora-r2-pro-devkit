#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Verify current dependency snapshots, archived provenance, patches and fonts offline."""

import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath

ROOT = Path(__file__).resolve().parents[1]


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def local_path(root, value):
    if (not isinstance(value, str) or '\\' in value or
            any(character in value for character in ('\0', '\r', '\n'))):
        raise ValueError('lock paths must be repository-relative without traversal')
    relative = PurePosixPath(value)
    if relative.is_absolute() or '..' in relative.parts or str(relative) in ('', '.') or str(relative) != value:
        raise ValueError('lock paths must be repository-relative without traversal')
    path = root / value
    if not path.resolve().is_relative_to(root.resolve()):
        raise ValueError('dependency path escapes the repository')
    for part in (path, *path.parents):
        if part == root:
            break
        if part.is_symlink():
            raise ValueError('dependency inputs must not be symlinks: ' + value)
    return path


def inventory(root, directory):
    folder = local_path(root, directory)
    if not folder.is_dir():
        raise ValueError('snapshot directory is missing: ' + directory)
    files = set()
    for path in folder.rglob('*'):
        name = path.relative_to(root).as_posix()
        local_path(root, name)
        if path.is_file():
            files.add(name)
        elif not path.is_dir():
            raise ValueError('dependency input must be a regular file or directory: ' + name)
    return files


def archive_inventory(root, entry):
    """Map an immutable release inventory into repository-relative snapshot paths."""
    directory = local_path(root, entry['directory'])
    if not isinstance(entry['archive_files'], dict) or not entry['archive_files']:
        raise ValueError('complete archive inventory must not be empty')
    result = {}
    for name, digest in entry['archive_files'].items():
        path = local_path(directory, name)
        if not isinstance(digest, str) or len(digest) != 64 or any(char not in '0123456789abcdef' for char in digest):
            raise ValueError('invalid complete archive SHA256: ' + name)
        result[path.relative_to(root).as_posix()] = digest
    return result


def verify_rtsa(root, entry, checked_hash, errors):
    """Validate the selected target SDK independently of the package's display metadata."""
    directory = local_path(root, entry['directory'])
    sdk = local_path(directory, entry['sdk_subdirectory'])
    library = local_path(sdk, entry['library_subpath'])
    if entry['architecture'] != 'aarch64':
        raise ValueError('RTSA target architecture must be aarch64')
    selected = {library.relative_to(root).as_posix(): entry['library_sha256']}
    selected.update({local_path(sdk, name).relative_to(root).as_posix(): digest
                     for name, digest in entry['headers'].items()})
    for name, digest in selected.items():
        if entry['files'].get(name) != digest:
            errors.append('RTSA target identity differs from snapshot: ' + name)
        checked_hash(name, digest)
    try:
        with library.open('rb') as stream:
            header = stream.read(20)
        if (header[:6] != b'\x7fELF\x02\x01' or len(header) < 20
                or int.from_bytes(header[16:18], 'little') != 3
                or int.from_bytes(header[18:20], 'little') != 183):
            errors.append('RTSA target library must be an ELF64 little-endian AArch64 shared object')
    except OSError:
        errors.append('missing RTSA target library')
    config_path = sdk / '.config'
    config_name = config_path.relative_to(root).as_posix()
    if config_name not in entry['files']:
        errors.append('RTSA configuration is outside the snapshot inventory')
    try:
        config = {}
        for line in config_path.read_text(encoding='utf-8').splitlines():
            key, separator, value = line.partition('=')
            if separator and not key.startswith('#'):
                config[key] = value
        for name, value in entry['required_config'].items():
            if config.get(name) != value:
                errors.append('RTSA required configuration differs: ' + name)
    except OSError:
        errors.append('missing RTSA configuration')


def verify_groups(root, lock, checked_hash, errors):
    """Require complete, disjoint ownership of current vendor snapshots."""
    directories, owners, names = [], {}, set()
    for entry in lock['snapshots']:
        directory = local_path(root, entry['directory'])
        if not all(isinstance(entry.get(field), str) and entry[field].strip()
                   for field in ('name', 'source', 'revision')):
            raise ValueError('snapshot requires name, source and revision')
        if not isinstance(entry.get('files'), dict) or not entry['files']:
            raise ValueError('dependency inventory must not be empty: ' + entry['directory'])
        if entry.get('name'):
            if entry['name'] in names:
                raise ValueError('duplicate dependency group name: ' + entry['name'])
            names.add(entry['name'])
        for other in directories:
            if directory.is_relative_to(other) or other.is_relative_to(directory):
                raise ValueError('dependency group directories overlap: ' + entry['directory'])
        directories.append(directory)
        if 'archive_files' in entry and entry['files'] != archive_inventory(root, entry):
            errors.append('complete archive inventory differs from snapshot: ' + entry['directory'])
        expected = set(entry['files'])
        actual = inventory(root, entry['directory'])
        for name in sorted(actual - expected):
            errors.append('unlocked dependency file: ' + name)
        for name in sorted(expected - actual):
            errors.append('missing snapshot file: ' + name)
        for name, digest in entry['files'].items():
            if not local_path(root, name).is_relative_to(directory):
                raise ValueError('snapshot file is outside its declared directory')
            if name in owners:
                raise ValueError('dependency file has multiple owners: ' + name)
            owners[name] = entry['directory']
            checked_hash(name, digest)
        if entry['name'] == 'rtsa':
            if 'archive_files' not in entry:
                raise ValueError('RTSA requires a complete archive inventory')
            verify_rtsa(root, entry, checked_hash, errors)
    if 'vendor_root' in lock:
        folder = local_path(root, lock['vendor_root'])
        snapshot_directories = {local_path(root, entry['directory']) for entry in lock['snapshots']}
        for directory in snapshot_directories:
            if directory.parent != folder:
                raise ValueError('vendor snapshot must be a direct child of vendor_root')
        readme = folder / 'README.md'
        if not readme.is_file():
            errors.append('missing vendor entry: ' + readme.relative_to(root).as_posix())
        for path in folder.iterdir():
            local_path(root, path.relative_to(root).as_posix())
            if path not in snapshot_directories and path != readme:
                errors.append('unregistered vendor entry: ' + path.relative_to(root).as_posix())


def valid_sha256(value):
    return (isinstance(value, str) and len(value) == 64 and
            all(character in '0123456789abcdef' for character in value))


def verify(root, lock):
    """Check current content, with historical records verified only as archives."""
    errors = []
    if lock.get('schema_version') != 2:
        raise ValueError('unsupported dependency lock schema; expected 2')
    obsolete = {'original_import', 'import_adaptations', 'import_omissions', 'path_migrations',
                'managed_assets'}
    if obsolete & lock.keys():
        raise ValueError('schema 2 stores historical mappings only in the immutable history archive')

    def checked_hash(name, expected):
        if not valid_sha256(expected):
            raise ValueError('invalid dependency SHA256: ' + name)
        try:
            actual = sha256(local_path(root, name))
            if actual != expected:
                errors.append('SHA256 mismatch: ' + name)
        except OSError:
            errors.append('missing dependency input: ' + name)

    history = lock['history']
    if not {'dependency_lock_v1', 'original_import'} <= history.keys():
        raise ValueError('both historical lock and original import references are required')
    for record in history.values():
        checked_hash(record['path'], record['sha256'])
    archived = json.loads(local_path(root, history['dependency_lock_v1']['path']).read_text())
    imported = history['original_import']
    if (archived.get('schema_version') != 1 or
            archived.get('original_import', {}).get('manifest') != imported['path'] or
            archived.get('original_import', {}).get('sha256') != imported['sha256']):
        errors.append('historical lock and original import reference differ')
    verify_groups(root, lock, checked_hash, errors)
    snapshot_files = {name: value for entry in lock['snapshots']
                      for name, value in entry['files'].items()}
    for patch in lock['local_patches']:
        checked_hash(patch['path'], patch['sha256'])
        local_path(root, patch['original_path'])
        if not valid_sha256(patch['original_sha256']):
            raise ValueError('invalid patch original SHA256: ' + patch['path'])
        result = patch['result_path']
        checked_hash(result, patch['result_sha256'])
        if snapshot_files.get(result) != patch['result_sha256']:
            errors.append('patch result differs from the current snapshot: ' + result)
    generated = lock['generated_fonts']
    checked_hash(generated['manifest'], generated['manifest_sha256'])
    checked_hash(generated['generator'], generated['generator_sha256'])
    sources = json.loads(local_path(root, generated['manifest']).read_text())
    if sources['fonts'] != generated['inputs']:
        errors.append('font source identities differ from the lock')
    if sources['generator'] != generated['generator']:
        errors.append('font generator path differs from the lock')
    for field in ('pillow_version', 'freetype_version'):
        if sources.get(field) != generated.get(field):
            errors.append('font tool version differs from the lock: ' + field)
    for name, digest in generated['outputs'].items():
        checked_hash(name, digest)
    for field in ('retained_licenses', 'required_inputs'):
        for name, digest in lock.get(field, {}).items():
            checked_hash(name, digest)
    for name, digest in lock.get('external', {}).get('runtime', {}).get('libraries', {}).items():
        local_path(root, name)
        if not valid_sha256(digest):
            raise ValueError('invalid external runtime SHA256: ' + name)
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
        print('Current dependency snapshots, patches, fonts and immutable history verified (offline; schema 2).')
    raise SystemExit(1 if errors else 0)


if __name__ == '__main__':
    main()
