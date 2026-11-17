#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Preview or refresh current dependency identities after an explicit review.

Generated fonts refresh normally. Vendor changes, patch changes,
licenses and required inputs need explicit selection. Complete release inventories
and immutable historical records are never refreshed by this tool.
"""

import argparse
import copy
import importlib.util
import json
import os
from pathlib import Path
import tempfile

ROOT = Path(__file__).resolve().parents[1]
_spec = importlib.util.spec_from_file_location('dependency_lock_check', ROOT / 'scripts/check-dependencies.py')
check = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(check)


def files_in(root, directory):
    return {name: check.sha256(check.local_path(root, name))
            for name in sorted(check.inventory(root, directory))}


def changes(before, after, prefix=''):
    if isinstance(before, dict) and isinstance(after, dict):
        result = []
        for name in sorted(set(before) | set(after)):
            label = prefix + '.' + name if prefix else name
            if name not in before:
                result.append('added ' + label)
            elif name not in after:
                result.append('removed ' + label)
            else:
                result.extend(changes(before[name], after[name], label))
        return result
    if isinstance(before, list) and isinstance(after, list):
        result = []
        for index in range(max(len(before), len(after))):
            label = prefix + '[' + str(index) + ']'
            if index >= len(before):
                result.append('added ' + label)
            elif index >= len(after):
                result.append('removed ' + label)
            else:
                result.extend(changes(before[index], after[index], label))
        return result
    return ['updated ' + prefix] if before != after else []


def protected(name, lock):
    leaf = Path(name).name.lower()
    return (name in lock.get('required_inputs', {}) or name in lock.get('retained_licenses', {}) or
            any(word in leaf for word in ('license', 'licence', 'copyright', 'notice')) or
            leaf == 'copying' or leaf.startswith('copying.'))


def refresh(root, lock, *, snapshots=(), patches=(), licenses=(), required_inputs=()):
    if lock.get('schema_version') != 2:
        raise ValueError('unsupported dependency lock schema; expected 2')
    current = copy.deepcopy(lock)
    selected = set(snapshots)
    known = {entry['name'] for entry in current['snapshots']}
    if selected - known:
        raise ValueError('unknown snapshot selection: ' + ', '.join(sorted(selected - known)))
    for record in current['history'].values():
        if check.sha256(check.local_path(root, record['path'])) != record['sha256']:
            raise ValueError('immutable dependency history changed: ' + record['path'])
    for entry in current['snapshots']:
        actual = files_in(root, entry['directory'])
        previous = entry['files']
        if 'archive_files' in entry:
            original = check.archive_inventory(root, entry)
            if previous != original or actual != original:
                raise ValueError('complete release snapshot changed; restore the pinned archive contents '
                                 'or explicitly review an official release replacement: ' + entry['directory'])
        elif actual != previous:
            removed = set(previous) - set(actual)
            forbidden = sorted(name for name in removed if protected(name, current))
            if forbidden:
                raise ValueError('protected dependency input cannot be removed: ' + ', '.join(forbidden))
            if entry['name'] not in selected:
                raise ValueError('snapshot changed; review and select --snapshot ' + entry['name'])
        entry['files'] = actual
    selected_patches = set(patches)
    known_patches = {entry['path'] for entry in current['local_patches']}
    if selected_patches - known_patches:
        raise ValueError('unknown patch selection: ' + ', '.join(sorted(selected_patches - known_patches)))
    for entry in current['local_patches']:
        patch_hash = check.sha256(check.local_path(root, entry['path']))
        result_hash = check.sha256(check.local_path(root, entry['result_path']))
        if (patch_hash != entry['sha256'] or result_hash != entry['result_sha256']):
            if entry['path'] not in selected_patches:
                raise ValueError('patch changed; review and select --patch ' + entry['path'])
            entry['sha256'], entry['result_sha256'] = patch_hash, result_hash
    for field, accepted in (('retained_licenses', licenses), ('required_inputs', required_inputs)):
        accepted = set(accepted)
        entries = current.setdefault(field, {})
        for name in sorted(set(entries) | accepted):
            actual = check.sha256(check.local_path(root, name))
            if name in accepted:
                entries[name] = actual
            elif entries[name] != actual:
                # A selected snapshot also explicitly accepts its current build
                # inputs. Retained license text still needs separate acceptance.
                reviewed = (field == 'required_inputs' and any(
                    entry['name'] in selected and name in entry['files']
                    for entry in current['snapshots']))
                if not reviewed:
                    option = '--license ' if field == 'retained_licenses' else '--required-input '
                    raise ValueError('protected input changed; review and select ' + option + name)
                entries[name] = actual
    generated = current['generated_fonts']
    manifest = check.local_path(root, generated['manifest'])
    font_sources = json.loads(manifest.read_text())
    generated['manifest_sha256'] = check.sha256(manifest)
    generated['inputs'] = font_sources['fonts']
    generated['generator'] = font_sources['generator']
    generated['generator_sha256'] = check.sha256(check.local_path(root, generated['generator']))
    for field in ('pillow_version', 'freetype_version'):
        if field in font_sources:
            generated[field] = font_sources[field]
        else:
            generated.pop(field, None)
    generated['outputs'] = {name: check.sha256(check.local_path(root, name))
                            for name in generated['outputs']}
    errors = check.verify(root, current)
    if errors:
        raise ValueError('refreshed lock is inconsistent: ' + '; '.join(errors))
    return current, changes(lock, current)


def write_lock(path, lock):
    """Replace atomically, preserving the existing lock file permissions."""
    descriptor, temporary = tempfile.mkstemp(prefix='.dependency-lock-', dir=path.parent)
    try:
        with os.fdopen(descriptor, 'w', encoding='utf-8') as stream:
            stream.write(json.dumps(lock, ensure_ascii=False, indent=2) + '\n')
        os.chmod(temporary, path.stat().st_mode & 0o777)
        os.replace(temporary, path)
    finally:
        Path(temporary).unlink(missing_ok=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=ROOT)
    parser.add_argument('--lock', type=Path, help='defaults to <root>/dependencies.lock.json')
    parser.add_argument('--write', action='store_true', help='write reviewed changes atomically')
    parser.add_argument('--snapshot', action='append', default=[], metavar='NAME',
                        help='explicitly accept reviewed current vendor bytes; cannot refresh a complete release')
    parser.add_argument('--patch', action='append', default=[], metavar='PATH',
                        help='accept a reviewed patch/result change without rewriting its upstream identity')
    parser.add_argument('--license', action='append', default=[], metavar='PATH')
    parser.add_argument('--required-input', action='append', default=[], metavar='PATH')
    parser.add_argument('--json', action='store_true')
    args = parser.parse_args()
    root = args.root.resolve()
    path = args.lock or root / 'dependencies.lock.json'
    try:
        lock = json.loads(path.read_text())
        updated, delta = refresh(root, lock, snapshots=args.snapshot, patches=args.patch,
                                 licenses=args.license, required_inputs=args.required_input)
        if args.write and delta:
            write_lock(path, updated)
    except (OSError, ValueError, KeyError, TypeError) as error:
        parser.error(str(error))
    result = {'written': bool(args.write and delta), 'review_required': bool(delta),
              'offline': True, 'changes': delta}
    if args.json:
        print(json.dumps(result, indent=2))
    elif delta:
        print('\n'.join(delta))
        print('Review every change and the lock diff. ' +
              ('Lock written; run dependency and affected checks.' if args.write else 'Preview only; use --write.'))
    else:
        print('Dependency lock is already current.')


if __name__ == '__main__':
    main()
