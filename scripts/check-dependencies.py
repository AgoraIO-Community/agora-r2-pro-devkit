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
    if not isinstance(value, str) or '\\' in value:
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


def protected_omission(name, previous, lock):
    """Notices and explicitly pinned build inputs cannot be pruned."""
    required = lock.get('required_inputs', {})
    for value in (name, previous):
        leaf = PurePosixPath(value).name.lower()
        if (any(word in leaf for word in ('license', 'licence', 'copyright', 'notice'))
                or leaf == 'copying' or leaf.startswith('copying.')
                or value in required):
            return True
    return False


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
    return files


def verify_groups(root, lock, checked_hash, errors):
    """Require disjoint snapshot ownership, also for project-maintained assets."""
    groups = [('snapshot', entry) for entry in lock['snapshots']]
    groups += [('managed asset', entry) for entry in lock.get('managed_assets', [])]
    directories, owners, names = [], {}, set()
    for kind, entry in groups:
        directory = local_path(root, entry['directory'])
        if entry.get('name'):
            if entry['name'] in names:
                raise ValueError('duplicate dependency group name: ' + entry['name'])
            names.add(entry['name'])
        for other in directories:
            if directory.is_relative_to(other) or other.is_relative_to(directory):
                raise ValueError('dependency group directories overlap: ' + entry['directory'])
        directories.append(directory)
        expected = set(entry['files'])
        actual = inventory(root, entry['directory'])
        for name in sorted(actual - expected):
            errors.append('unlocked dependency file: ' + name)
        for name in sorted(expected - actual):
            errors.append('missing ' + kind + ' file: ' + name)
        for name, digest in entry['files'].items():
            if not local_path(root, name).is_relative_to(directory):
                raise ValueError('snapshot file is outside its declared directory')
            if name in owners:
                raise ValueError('dependency file has multiple owners: ' + name)
            owners[name] = entry['directory']
            checked_hash(name, digest)
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
    verify_groups(root, lock, checked_hash, errors)
    adaptations = lock['import_adaptations']
    omissions = lock.get('import_omissions', {})
    if set(adaptations) & set(omissions):
        raise ValueError('historical input cannot be both mapped and omitted')
    destinations = {}
    for name, entry in adaptations.items():
        if name not in original_files or entry['original_sha256'] != original_files[name]:
            errors.append('adaptation original hash is not the historical import: ' + name)
        if not entry.get('reason'):
            errors.append('adaptation has no explanation: ' + name)
        destination = entry['path']
        if destination in destinations:
            raise ValueError('historical inputs share a mapped destination: ' + destination)
        destinations[destination] = name
        for previous in {name, *entry.get('previous_paths', [])} - {destination}:
            if local_path(root, previous).exists():
                errors.append('obsolete mapped input remains: ' + previous)
        checked_hash(entry['path'], entry['result_sha256'])
    for name, entry in omissions.items():
        if name not in original_files or entry['original_sha256'] != original_files[name]:
            errors.append('omission original hash is not the historical import: ' + name)
        if not entry.get('reason', '').strip():
            errors.append('omission has no explanation: ' + name)
        previous = entry['previous_path']
        if protected_omission(name, previous, lock):
            errors.append('protected dependency input cannot be omitted: ' + name)
        for old in {name, previous, *entry.get('previous_paths', [])}:
            if local_path(root, old).exists():
                errors.append('omitted dependency input remains: ' + old)
    for name, digest in original_files.items():
        if name not in adaptations and name not in omissions:
            if name in destinations:
                raise ValueError('historical inputs share a mapped destination: ' + name)
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
    for name, digest in lock.get('required_inputs', {}).items():
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
