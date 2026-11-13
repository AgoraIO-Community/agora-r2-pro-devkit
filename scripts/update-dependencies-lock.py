#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Preview or refresh reviewed current dependency hashes without rewriting import history.

This is a maintainer tool, never a build step. Review its output and the lock diff;
run dependency, asset-generation and affected build checks after writing a lock.
External SDK/runtime identities are deliberately left unchanged.
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


def assignments(values, description):
    result = {}
    for value in values:
        name, separator, content = value.partition('=')
        if not separator or not name or not content.strip() or name in result:
            raise ValueError(description + ' must use unique ORIGINAL_PATH=VALUE assignments')
        result[name] = content.strip()
    return result


def files_in(root, directory):
    folder = check.local_path(root, directory)
    if not folder.is_dir():
        raise ValueError('snapshot directory is missing: ' + directory)
    files = {}
    for path in sorted(folder.rglob('*')):
        name = path.relative_to(root).as_posix()
        check.local_path(root, name)
        if path.is_file():
            files[name] = check.sha256(path)
    return files


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


def refresh(root, lock, mappings=None, reasons=None, include_ui_view=False, licenses=()):
    if lock.get('schema_version') != 1:
        raise ValueError('unsupported dependency lock schema')
    mappings, reasons = mappings or {}, reasons or {}
    current = copy.deepcopy(lock)
    history = lock['original_import']
    manifest = check.local_path(root, history['manifest'])
    if check.sha256(manifest) != history['sha256']:
        raise ValueError('historical import manifest changed; restore its original contents before refreshing')
    original = json.loads(manifest.read_text(encoding='utf-8'))
    imported = original['imported_files']
    revisions = {name: value for name, value in original.items() if name not in ('imported_files', 'local_patches')}
    if history.get('revisions', {}) != revisions:
        raise ValueError('historical import revisions were rewritten')
    for name in set(mappings) | set(reasons):
        if name not in imported:
            raise ValueError('adaptation is not in the historical import: ' + name)
    adaptations = current['import_adaptations']
    for name, entry in adaptations.items():
        if entry['original_sha256'] != imported.get(name):
            raise ValueError('adaptation original hash was rewritten: ' + name)
        if not entry.get('reason', '').strip():
            raise ValueError('existing adaptation has no explanation: ' + name)
    for name, original_hash in imported.items():
        existing = adaptations.get(name)
        destination = mappings.get(name, existing['path'] if existing else name)
        result = check.sha256(check.local_path(root, destination))
        if existing or destination != name or result != original_hash:
            if (not existing or destination != existing['path']) and not reasons.get(name):
                raise ValueError('new adaptation requires --reason ' + name + '=EXPLANATION')
            reason = reasons.get(name, existing['reason'] if existing else None)
            adaptations[name] = {'path': destination, 'original_sha256': original_hash,
                                 'result_sha256': result, 'reason': reason}
    for patch in current['local_patches']:
        source = patch['original_path']
        if patch['original_sha256'] != imported.get(source):
            raise ValueError('patch original hash was rewritten: ' + source)
        patch['sha256'] = check.sha256(check.local_path(root, patch['path']))
        patch['result_path'] = adaptations[source]['path']
        patch['result_sha256'] = adaptations[source]['result_sha256']
    if include_ui_view:
        directory = 'components/mybot_ui/views/pixel_glasses'
        if not any(item['directory'] == directory for item in current['snapshots']):
            current['snapshots'].append({'name': 'pixel-glasses', 'version': 'native-640x480',
                'source': 'project-maintained',
                'scope': 'pixel-glasses view, geometry, expressions and retained licenses',
                'directory': directory, 'files': {}})
        licenses = tuple(licenses) + (
            'components/mybot_ui/views/pixel_glasses/LICENSE.MIT.txt',
            'components/mybot_ui/fonts/licenses/TERMINAL_FONT_LICENSE.txt',
            'components/mybot_ui/fonts/licenses/LVGL_VIEW_FONT_LICENSE.txt')
    for snapshot in current['snapshots']:
        snapshot['files'] = files_in(root, snapshot['directory'])
    generated = current['generated_fonts']
    generated['manifest_sha256'] = check.sha256(check.local_path(root, generated['manifest']))
    font_manifest = json.loads(check.local_path(root, generated['manifest']).read_text(encoding='utf-8'))
    generated['inputs'] = font_manifest['fonts']
    generated['generator'] = font_manifest['generator']
    generated['generator_sha256'] = check.sha256(check.local_path(root, generated['generator']))
    for version in ('pillow_version', 'freetype_version'):
        if version in font_manifest:
            generated[version] = font_manifest[version]
    generated['outputs'] = {name: check.sha256(check.local_path(root, name)) for name in generated['outputs']}
    retained = current.setdefault('retained_licenses', {})
    for name in set(retained) | set(licenses):
        retained[name] = check.sha256(check.local_path(root, name))
    current['retained_licenses'] = dict(sorted(retained.items()))
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
        if Path(temporary).exists():
            Path(temporary).unlink()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=ROOT)
    parser.add_argument('--lock', type=Path, help='defaults to <root>/dependencies.lock.json')
    parser.add_argument('--write', action='store_true', help='write changes; inspect the resulting diff before committing')
    parser.add_argument('--map', action='append', default=[], metavar='ORIGINAL_PATH=CURRENT_PATH', help='register a reviewed path migration')
    parser.add_argument('--reason', action='append', default=[], metavar='ORIGINAL_PATH=EXPLANATION', help='required for each new adaptation or changed mapping')
    parser.add_argument('--include-ui-view', action='store_true', help='register the UI files and retained licenses once')
    parser.add_argument('--include-cores3-view', dest='include_ui_view', action='store_true', help=argparse.SUPPRESS)
    parser.add_argument('--license', action='append', default=[], metavar='PATH', help='record a retained repository-relative license')
    parser.add_argument('--json', action='store_true')
    args = parser.parse_args()
    root = args.root.resolve()
    path = args.lock or root / 'dependencies.lock.json'
    try:
        lock = json.loads(path.read_text(encoding='utf-8'))
        updated, modified = refresh(root, lock, assignments(args.map, '--map'), assignments(args.reason, '--reason'),
                                    args.include_ui_view, args.license)
        if args.write and modified:
            write_lock(path, updated)
    except (OSError, ValueError, KeyError, TypeError) as error:
        parser.error(str(error))
    summary = {'written': bool(args.write and modified), 'review_required': bool(modified),
               'offline': True, 'changes': modified}
    if args.json:
        print(json.dumps(summary, indent=2))
    else:
        print('\n'.join(modified) if modified else 'Dependency lock is already current.')
        if modified:
            print('Review every reported change and the lock diff. ' +
                  ('Lock written; run dependency and affected build checks.' if args.write else 'Preview only; use --write to update.'))


if __name__ == '__main__':
    main()
