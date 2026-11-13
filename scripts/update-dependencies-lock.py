#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Preview or refresh reviewed current dependency hashes without rewriting import history.

This is a maintainer tool, never a build step. Review its output and the lock diff;
run dependency, asset-generation and affected build checks after writing a lock.
Complete release snapshots and external runtime identities are deliberately left unchanged.
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
    return {name: check.sha256(check.local_path(root, name))
            for name in sorted(check.inventory(root, directory))}


def expand_prefixes(root, imported, adaptations, mappings, reasons, prefix_mappings, prefix_reasons):
    """Expand reviewed directory migrations into the existing per-file lock format."""
    if set(prefix_reasons) - set(prefix_mappings):
        raise ValueError('--reason-prefix requires a corresponding --map-prefix')
    expanded, explanations = dict(mappings), dict(reasons)
    prefixes = []
    for source, destination in prefix_mappings.items():
        check.local_path(root, source)
        check.local_path(root, destination)
        if source == destination or destination.startswith(source + '/') or source.startswith(destination + '/'):
            raise ValueError('prefix migration must not overlap or map into itself: ' + source)
        if not prefix_reasons.get(source, '').strip():
            raise ValueError('prefix migration requires --reason-prefix ' + source + '=EXPLANATION')
        for previous, target in prefixes:
            if any(first == second or first.startswith(second + '/') or second.startswith(first + '/')
                   for first, second in ((source, previous), (destination, target), (source, target), (destination, previous))):
                raise ValueError('prefix migrations overlap or form a cycle')
        prefixes.append((source, destination))
        matched = False
        for name in imported:
            current = adaptations.get(name, {}).get('path', name)
            candidates = {value for value in (name, current) if value.startswith(source + '/')}
            if not candidates:
                continue
            if len(candidates) != 1:
                raise ValueError('ambiguous historical prefix migration: ' + name)
            if name in expanded:
                raise ValueError('historical input has multiple migration rules: ' + name)
            candidate = candidates.pop()
            expanded[name] = destination + candidate[len(source):]
            explanations.setdefault(name, prefix_reasons[source])
            matched = True
        if not matched:
            raise ValueError('prefix migration matches no historical inputs: ' + source)
    return expanded, explanations


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


def refresh(root, lock, mappings=None, reasons=None, include_ui_view=False, licenses=(), *,
            prefix_mappings=None, prefix_reasons=None, omissions=None, add_snapshots=(),
            managed_assets=(), required_inputs=(), vendor_root=None):
    if lock.get('schema_version') != 1:
        raise ValueError('unsupported dependency lock schema')
    mappings, reasons = mappings or {}, reasons or {}
    omissions = omissions or {}
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
    if set(mappings) & set(omissions):
        raise ValueError('historical input cannot be both mapped and omitted')
    mappings, reasons = expand_prefixes(root, imported, current['import_adaptations'], mappings, reasons,
                                        prefix_mappings or {}, prefix_reasons or {})
    for name in set(mappings) | set(reasons) | set(omissions):
        if name not in imported:
            raise ValueError('adaptation is not in the historical import: ' + name)
    adaptations = current['import_adaptations']
    for name, entry in adaptations.items():
        if name not in imported or entry['original_sha256'] != imported[name]:
            raise ValueError('adaptation original hash was rewritten: ' + name)
        if not entry.get('reason', '').strip():
            raise ValueError('existing adaptation has no explanation: ' + name)
    omitted = current.setdefault('import_omissions', {})
    required = current.setdefault('required_inputs', {})
    for name in required_inputs:
        required[name] = check.sha256(check.local_path(root, name))
    for name, entry in omitted.items():
        if name not in imported or entry['original_sha256'] != imported[name]:
            raise ValueError('omission original hash was rewritten: ' + name)
        if not entry.get('reason', '').strip():
            raise ValueError('existing omission has no explanation: ' + name)
    if set(adaptations) & set(omitted):
        raise ValueError('historical input cannot be both mapped and omitted')
    if (set(mappings) & set(omitted)) - set(omissions):
        raise ValueError('omitted historical input cannot also be mapped')
    for name, reason in omissions.items():
        previous = mappings.get(name, adaptations.get(name, {}).get('path', name))
        if name in omitted:
            previous = omitted[name]['previous_path']
        if not reason.strip():
            raise ValueError('omission requires an explanation: ' + name)
        if check.protected_omission(name, previous, current):
            raise ValueError('protected dependency input cannot be omitted: ' + name)
        for old in {name, previous, *adaptations.get(name, {}).get('previous_paths', [])}:
            if check.local_path(root, old).exists():
                raise ValueError('omitted dependency input remains: ' + old)
        previous_paths = set(adaptations.get(name, {}).get('previous_paths', []))
        previous_paths.update(omitted.get(name, {}).get('previous_paths', []))
        if name in adaptations and adaptations[name]['path'] not in (name, previous):
            previous_paths.add(adaptations[name]['path'])
        omitted[name] = {'original_sha256': imported[name], 'previous_path': previous, 'reason': reason}
        if previous_paths:
            omitted[name]['previous_paths'] = sorted(previous_paths - {name, previous})
        adaptations.pop(name, None)
    for name, original_hash in imported.items():
        if name in omitted:
            continue
        existing = adaptations.get(name)
        destination = mappings.get(name, existing['path'] if existing else name)
        result = check.sha256(check.local_path(root, destination))
        if existing or destination != name or result != original_hash:
            if (not existing or destination != existing['path']) and not reasons.get(name):
                raise ValueError('new adaptation requires --reason ' + name + '=EXPLANATION')
            reason = reasons.get(name, existing['reason'] if existing else None)
            entry = {'path': destination, 'original_sha256': original_hash,
                     'result_sha256': result, 'reason': reason}
            previous_paths = set(existing.get('previous_paths', [])) if existing else set()
            if existing and existing['path'] != destination and existing['path'] != name:
                previous_paths.add(existing['path'])
            if previous_paths:
                entry['previous_paths'] = sorted(previous_paths)
            adaptations[name] = entry
    for patch in current['local_patches']:
        source = patch['original_path']
        if source in omitted:
            raise ValueError('patched dependency input cannot be omitted: ' + source)
        if patch['original_sha256'] != imported.get(source):
            raise ValueError('patch original hash was rewritten: ' + source)
        patch['sha256'] = check.sha256(check.local_path(root, patch['path']))
        patch['result_path'] = adaptations[source]['path']
        patch['result_sha256'] = adaptations[source]['result_sha256']
    # A project-maintained view is an application asset, not an upstream snapshot.
    assets = current.setdefault('managed_assets', [])
    for entry in list(current['snapshots']):
        if entry.get('source') == 'project-maintained':
            assets.append(entry)
            current['snapshots'].remove(entry)
    if include_ui_view:
        directory = 'components/mybot_ui/views/pixel_glasses'
        if not any(item['directory'] == directory for item in assets):
            assets.append({'name': 'pixel-glasses', 'version': 'native-640x480',
                'source': 'project-maintained',
                'scope': 'pixel-glasses view, geometry, expressions and retained licenses',
                'directory': directory, 'files': {}})
        licenses = tuple(licenses) + (
            'components/mybot_ui/views/pixel_glasses/LICENSE.MIT.txt',
            'components/mybot_ui/fonts/licenses/TERMINAL_FONT_LICENSE.txt',
            'components/mybot_ui/fonts/licenses/LVGL_VIEW_FONT_LICENSE.txt')
    for entry in add_snapshots:
        if not all(entry.get(field) for field in ('name', 'directory', 'revision', 'source')):
            raise ValueError('new snapshot requires name, directory, revision and source')
        matching = [item for item in current['snapshots'] if item.get('name') == entry['name'] or item['directory'] == entry['directory']]
        if matching:
            if len(matching) != 1 or any(matching[0].get(field) != entry[field] for field in entry if field != 'files'):
                raise ValueError('snapshot is already registered with a different identity: ' + entry['name'])
            continue
        current['snapshots'].append(copy.deepcopy(entry))
    for entry in managed_assets:
        matching = [item for item in assets if item.get('name') == entry.get('name') or item['directory'] == entry['directory']]
        if matching:
            if len(matching) != 1 or any(matching[0].get(field) != entry[field] for field in entry if field != 'files'):
                raise ValueError('managed asset is already registered with a different identity: ' + entry['name'])
            continue
        assets.append(copy.deepcopy(entry))
    if vendor_root is not None:
        current['vendor_root'] = vendor_root
    for snapshot in current['snapshots'] + assets:
        actual = files_in(root, snapshot['directory'])
        if 'archive_files' in snapshot:
            original_files = check.archive_inventory(root, snapshot)
            if snapshot['files'] != original_files or actual != original_files:
                raise ValueError('complete release snapshot changed; restore the pinned archive contents or explicitly review a release replacement: '
                                 + snapshot['directory'])
        snapshot['files'] = actual
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
    parser.add_argument('--map-prefix', action='append', default=[], metavar='OLD_DIRECTORY=NEW_DIRECTORY', help='expand a reviewed directory move into per-file mappings')
    parser.add_argument('--reason-prefix', action='append', default=[], metavar='OLD_DIRECTORY=EXPLANATION')
    parser.add_argument('--omit', action='append', default=[], metavar='ORIGINAL_PATH=EXPLANATION', help='record an explicitly removed historical file; notices and required inputs are protected')
    parser.add_argument('--add-snapshot', action='append', default=[], metavar='NAME=DIRECTORY')
    parser.add_argument('--revision', action='append', default=[], metavar='NAME=REVISION', help='required with --add-snapshot')
    parser.add_argument('--source', action='append', default=[], metavar='NAME=URL', help='required with --add-snapshot')
    parser.add_argument('--managed-asset', action='append', default=[], metavar='NAME=DIRECTORY', help='register a project-maintained asset group')
    parser.add_argument('--required-input', action='append', default=[], metavar='PATH', help='pin and protect a current build input against omission')
    parser.add_argument('--vendor-root', metavar='DIRECTORY', help='require direct snapshot directories and a single README.md entry')
    parser.add_argument('--include-ui-view', action='store_true', help='register the UI files and retained licenses once')
    parser.add_argument('--include-cores3-view', dest='include_ui_view', action='store_true', help=argparse.SUPPRESS)
    parser.add_argument('--license', action='append', default=[], metavar='PATH', help='record a retained repository-relative license')
    parser.add_argument('--json', action='store_true')
    args = parser.parse_args()
    root = args.root.resolve()
    path = args.lock or root / 'dependencies.lock.json'
    try:
        lock = json.loads(path.read_text(encoding='utf-8'))
        snapshot_directories = assignments(args.add_snapshot, '--add-snapshot')
        revisions = assignments(args.revision, '--revision')
        sources = assignments(args.source, '--source')
        if set(revisions) != set(snapshot_directories) or set(sources) != set(snapshot_directories):
            raise ValueError('--add-snapshot requires matching --revision and --source names')
        snapshots = [{'name': name, 'directory': directory, 'revision': revisions[name], 'source': sources[name],
                      'scope': 'reviewed vendored source snapshot', 'files': {}}
                     for name, directory in snapshot_directories.items()]
        assets = [{'name': name, 'directory': directory, 'source': 'project-maintained', 'files': {}}
                  for name, directory in assignments(args.managed_asset, '--managed-asset').items()]
        updated, modified = refresh(root, lock, assignments(args.map, '--map'), assignments(args.reason, '--reason'),
                                    args.include_ui_view, args.license,
                                    prefix_mappings=assignments(args.map_prefix, '--map-prefix'),
                                    prefix_reasons=assignments(args.reason_prefix, '--reason-prefix'),
                                    omissions=assignments(args.omit, '--omit'), add_snapshots=snapshots,
                                    managed_assets=assets, required_inputs=args.required_input, vendor_root=args.vendor_root)
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
