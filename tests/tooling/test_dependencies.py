#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Failure-oriented tests for offline configuration and dependency provenance checks."""

import copy
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]


def module(name, filename):
    spec = importlib.util.spec_from_file_location(name, ROOT / 'scripts' / filename)
    value = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(value)
    return value


sdk = module('sdk_dependency_test', 'configure-local-sdk.py')
dependencies = module('dependency_check_test', 'check-dependencies.py')
doctor = module('doctor_test', 'doctor.py')
updater = module('dependency_update_test', 'update-dependencies-lock.py')


def digest(data):
    return hashlib.sha256(data).hexdigest()


def elf(machine=183):
    data = bytearray(20)
    data[:6] = b'\x7fELF\x02\x01'
    struct.pack_into('<H', data, 18, machine)
    return bytes(data)


class OfflineTargetTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='mybot-dependency-test-')
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name).resolve()
        root_patch = patch.object(sdk, 'ROOT', self.root)
        root_patch.start()
        self.addCleanup(root_patch.stop)
        self.staging = self.root / 'staging'
        self.sdk_path = self.root / 'vendor/rtsa/agora_sdk'
        self.library = self.sdk_path / 'lib/aarch64/libagora-rtc-sdk.so'
        self.write(self.library, elf())
        self.write(self.sdk_path / 'include/agora_rtc_api.h', b'public api')
        self.write(self.sdk_path / '.config', b'CONFIG_MINIMAL_TIMER_INTERVAL_MS=20\nCONFIG_RTM=y\n')
        for header in ('alsa/asoundlib.h', 'openssl/ssl.h', 'xf86drm.h', 'libdrm/drm.h'):
            self.write(self.staging / 'usr/include' / header, b'header')
        self.runtime_name = 'lib/ld-linux-aarch64.so.1'
        self.runtime = self.staging / self.runtime_name
        self.write(self.runtime, elf())
        self.manifest = self.root / 'runtime-manifest.json'
        self.entries = [{'path': '/' + self.runtime_name, 'sha256': digest(elf())}]
        self.save_manifest()
        self.lock = {'snapshots': [{'name': 'rtsa', 'directory': 'vendor/rtsa',
            'sdk_subdirectory': 'agora_sdk', 'library_subpath': 'lib/aarch64/libagora-rtc-sdk.so',
            'version': 'fixture', 'library_sha256': digest(elf()),
            'headers': {'include/agora_rtc_api.h': digest(b'public api')},
            'required_config': {'CONFIG_MINIMAL_TIMER_INTERVAL_MS': '20', 'CONFIG_RTM': 'y'}}],
            'external': {'runtime': {'libraries': {self.runtime_name: digest(elf())}}}}
        self.values = {'RV1126B_STAGING': str(self.staging), 'AGORA_SDK_DIR': str(self.sdk_path),
            'AGORA_RTC_LIBRARY': str(self.library), 'RV1126B_RUNTIME_MANIFEST': str(self.manifest)}

    def write(self, path, content):
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(content)

    def save_manifest(self):
        self.manifest.write_text(json.dumps({'serial': 'private-device-id', 'libraries': self.entries}))

    def validate(self):
        return sdk.validate_target(self.values, self.lock, check_compiler=False)

    def test_summary_excludes_private_manifest_fields_and_local_paths(self):
        summary = json.dumps(self.validate())
        self.assertNotIn('private-device-id', summary)
        self.assertNotIn(str(self.root), summary)
        self.assertEqual(self.validate()['audio_ptime_ms'], 20)

    def test_rejects_unpinned_rtsa(self):
        self.library.write_bytes(elf() + b'changed')
        with self.assertRaisesRegex(ValueError, 'RTSA library SHA256'):
            self.validate()

    def test_rejects_wrong_rtsa_architecture_even_when_hash_matches(self):
        self.library.write_bytes(elf(62))
        self.lock['snapshots'][0]['library_sha256'] = digest(elf(62))
        with self.assertRaisesRegex(ValueError, 'RTSA library is not AArch64'):
            self.validate()

    def test_rejects_mismatched_public_header(self):
        (self.sdk_path / 'include/agora_rtc_api.h').write_bytes(b'wrong version')
        with self.assertRaisesRegex(ValueError, 'public header SHA256'):
            self.validate()

    def test_rejects_incompatible_ptime(self):
        (self.sdk_path / '.config').write_text('CONFIG_MINIMAL_TIMER_INTERVAL_MS=60\nCONFIG_RTM=y\n')
        with self.assertRaisesRegex(ValueError, 'CONFIG_MINIMAL_TIMER_INTERVAL_MS'):
            self.validate()

    def test_rejects_missing_required_rtm_feature(self):
        (self.sdk_path / '.config').write_text('CONFIG_MINIMAL_TIMER_INTERVAL_MS=20\n')
        with self.assertRaisesRegex(ValueError, 'CONFIG_RTM'):
            self.validate()

    def test_rejects_runtime_traversal(self):
        self.entries[0]['path'] = '/../escape.so'
        self.save_manifest()
        with self.assertRaisesRegex(ValueError, 'without traversal'):
            self.validate()

    def test_rejects_duplicate_runtime_paths(self):
        self.entries.append(copy.deepcopy(self.entries[0]))
        self.save_manifest()
        with self.assertRaisesRegex(ValueError, 'duplicate runtime'):
            self.validate()

    def test_rejects_modified_runtime(self):
        self.runtime.write_bytes(elf() + b'changed')
        with self.assertRaisesRegex(ValueError, 'staged runtime SHA256'):
            self.validate()

    def test_rejects_manifest_outside_reviewed_runtime_baseline(self):
        self.runtime.write_bytes(elf() + b'new version')
        self.entries[0]['sha256'] = sdk.sha256(self.runtime)
        self.save_manifest()
        with self.assertRaisesRegex(ValueError, 'runtime baseline'):
            self.validate()

    def test_rejects_wrong_runtime_architecture_even_when_hash_matches(self):
        self.runtime.write_bytes(elf(62))
        self.entries[0]['sha256'] = digest(elf(62))
        self.save_manifest()
        with self.assertRaisesRegex(ValueError, 'runtime is not AArch64'):
            self.validate()

    def test_rejects_runtime_only_sysroot(self):
        self.values['RV1126B_SYSROOT'] = str(self.staging)
        with self.assertRaisesRegex(ValueError, 'runtime staging is not a sysroot'):
            self.validate()

    def test_no_arguments_require_explicit_dependencies(self):
        result = subprocess.run([sys.executable, str(ROOT / 'scripts/configure-local-sdk.py')],
                                text=True, capture_output=True)
        self.assertEqual(result.returncode, 2)
        self.assertIn('provide --cross-compile or --zig', result.stderr)
        self.assertNotIn('--reference', result.stderr)
        self.assertNotIn('--rtsa-sdk,', result.stderr.split('error:')[-1])

    def configure(self, *arguments):
        lock_path = self.root / 'dependencies.lock.json'
        lock_path.write_text(json.dumps(self.lock))
        compiler_prefix = str(self.root / 'tools/aarch64-linux-gnu-')
        command = ['configure-local-sdk.py', '--cross-compile', compiler_prefix,
                   '--staging', str(self.staging), '--runtime-manifest', str(self.manifest), *arguments]
        validate = sdk.validate_target
        with patch.object(sdk, 'LOCK', lock_path), patch.object(sys, 'argv', command), \
                patch.object(sdk, 'executable', return_value=compiler_prefix + 'gcc'), \
                patch.object(sdk, 'validate_target', side_effect=lambda values, lock, **kwargs:
                             validate(values, lock, check_compiler=False)), patch('sys.stdout'):
            sdk.main()
        return sdk.load_json(self.root / 'local/sdk/config.json')

    def test_configuration_defaults_to_repository_rtsa_without_external_sdk_argument(self):
        config = self.configure()
        self.assertEqual(config['environment']['AGORA_SDK_DIR'], str(self.sdk_path))
        self.assertEqual(config['environment']['AGORA_RTC_LIBRARY'], str(self.library))
        self.assertIn(str(self.sdk_path), (self.root / 'local/sdk/environment.sh').read_text())

    def test_compatibility_sdk_argument_accepts_repository_snapshot(self):
        config = self.configure('--rtsa-sdk', str(self.sdk_path))
        self.assertEqual(config['environment']['AGORA_SDK_DIR'], str(self.sdk_path))

    def test_compatibility_sdk_argument_rejects_external_sdk(self):
        with patch('sys.stderr'), self.assertRaises(SystemExit) as raised:
            self.configure('--rtsa-sdk', str(self.root / 'external-sdk'))
        self.assertEqual(raised.exception.code, 2)
        self.assertFalse((self.root / 'local/sdk/config.json').exists())

    def test_reference_compatibility_uses_repository_rtsa(self):
        config = self.configure('--reference', str(self.root / 'reference-inputs'))
        self.assertEqual(config['environment']['AGORA_SDK_DIR'], str(self.sdk_path))

    def test_rejects_stale_external_sdk_path(self):
        self.values['AGORA_SDK_DIR'] = str(self.root / 'external-sdk')
        with self.assertRaisesRegex(ValueError, 'repository snapshot'):
            self.validate()

    def test_rejects_external_library_even_with_matching_bytes(self):
        external_library = self.root / 'external-sdk/libagora-rtc-sdk.so'
        self.write(external_library, elf())
        self.values['AGORA_RTC_LIBRARY'] = str(external_library)
        with self.assertRaisesRegex(ValueError, 'repository snapshot'):
            self.validate()

    def test_doctor_rejects_external_paths_with_current_lock(self):
        lock_path = self.root / 'dependencies.lock.json'
        lock_path.write_text(json.dumps(self.lock))
        self.values['AGORA_SDK_DIR'] = str(self.root / 'external-sdk')
        config = self.root / 'config.json'
        config.write_text(json.dumps({'schema_version': 1, 'environment': self.values,
                                     'dependency_lock_sha256': sdk.sha256(lock_path)}))
        with patch.object(doctor.sdk, 'ROOT', self.root), patch.object(doctor.sdk, 'LOCK', lock_path), \
                self.assertRaisesRegex(ValueError, 'repository snapshot'):
            doctor.target_check(config)

    def test_stale_configuration_is_rejected_before_target_probes(self):
        path = self.root / 'config.json'
        path.write_text(json.dumps({'schema_version': 1, 'dependency_lock_sha256': 'old'}))
        with self.assertRaisesRegex(ValueError, 'lock changed'):
            doctor.target_check(path)

    def test_zig_wrappers_quote_host_path_and_keep_caches_local(self):
        zig = self.root / 'host tools/zig'
        self.write(zig, b'#!/bin/sh\nexit 0\n')
        zig.chmod(0o755)
        with patch.object(sdk, 'ROOT', self.root):
            prefix = sdk.zig_wrappers(zig, self.root / 'local/sdk/bin')
        wrapper = Path(prefix + 'gcc').read_text()
        self.assertIn("'" + str(zig) + "'", wrapper)
        self.assertIn('aarch64-linux-gnu.2.41', wrapper)
        self.assertIn(str(self.root / 'local/cache/zig-global'), wrapper)
        self.assertIn('"$@"', wrapper)


class ProvenanceTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='mybot-provenance-test-')
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.put('vendor/source.c', b'original\nwrapper\n')
        self.put('vendor/LICENSE', b'license')
        self.put('component/header.h', b'public header')
        self.put('docs/fix.patch', b'patch input')
        self.put('fonts/font.c', b'generated glyphs')
        self.put('tools/generate.py', b'font generator')
        self.fonts = {'generator': 'tools/generate.py', 'fonts': {'fixture.ttf': {'sha256': digest(b'font input'), 'url': 'https://example.invalid/font'}}}
        self.put_json('fonts/sources.json', self.fonts)
        imports = {'imported_files': {'vendor/source.c': digest(b'original\n'),
            'vendor/LICENSE': digest(b'license'), 'old/header.h': digest(b'public header')}}
        self.put_json('docs/imported.json', imports)
        self.lock = {'schema_version': 1,
            'original_import': {'manifest': 'docs/imported.json', 'sha256': self.hash('docs/imported.json')},
            'snapshots': [{'directory': 'vendor', 'files': {
                'vendor/source.c': self.hash('vendor/source.c'), 'vendor/LICENSE': self.hash('vendor/LICENSE')}}],
            'import_adaptations': {
                'vendor/source.c': {'path': 'vendor/source.c', 'original_sha256': digest(b'original\n'),
                    'result_sha256': self.hash('vendor/source.c'), 'reason': 'typed callback wrapper'},
                'old/header.h': {'path': 'component/header.h', 'original_sha256': digest(b'public header'),
                    'result_sha256': self.hash('component/header.h'), 'reason': 'header relocation'}},
            'local_patches': [{'path': 'docs/fix.patch', 'sha256': self.hash('docs/fix.patch'),
                'original_path': 'vendor/source.c', 'original_sha256': digest(b'original\n'),
                'result_path': 'vendor/source.c', 'result_sha256': self.hash('vendor/source.c')}],
            'generated_fonts': {'manifest': 'fonts/sources.json', 'manifest_sha256': self.hash('fonts/sources.json'),
                'generator': 'tools/generate.py', 'generator_sha256': self.hash('tools/generate.py'),
                'inputs': self.fonts['fonts'], 'outputs': {'fonts/font.c': self.hash('fonts/font.c')}},
            'retained_licenses': {'vendor/LICENSE': self.hash('vendor/LICENSE')}}

    def put(self, name, content):
        path = self.root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(content)

    def put_json(self, name, value):
        self.put(name, json.dumps(value).encode())

    def hash(self, name):
        return sdk.sha256(self.root / name)

    def verify(self):
        return dependencies.verify(self.root, self.lock)

    def test_current_snapshot_and_relocated_history_pass(self):
        self.assertEqual(self.verify(), [])

    def test_rejects_unlocked_vendor_addition(self):
        self.put('vendor/unreviewed.c', b'new')
        self.assertIn('unlocked dependency file: vendor/unreviewed.c', self.verify())

    def test_rejects_vendor_modification(self):
        self.put('vendor/source.c', b'changed without provenance')
        self.assertIn('SHA256 mismatch: vendor/source.c', self.verify())

    def test_rejects_rewritten_history(self):
        self.put_json('docs/imported.json', {'imported_files': {}})
        self.assertIn('SHA256 mismatch: docs/imported.json', self.verify())
        self.assertTrue(any('not the historical import' in value for value in self.verify()))

    def test_rejects_missing_patch(self):
        (self.root / 'docs/fix.patch').unlink()
        self.assertIn('missing dependency input: docs/fix.patch', self.verify())

    def test_rejects_unrecorded_font_regeneration(self):
        self.put('fonts/font.c', b'new glyphs')
        self.assertIn('SHA256 mismatch: fonts/font.c', self.verify())

    def test_rejects_unrecorded_generator_change(self):
        self.put('tools/generate.py', b'changed generator')
        self.assertIn('SHA256 mismatch: tools/generate.py', self.verify())

    def test_rejects_changed_font_input(self):
        changed = copy.deepcopy(self.fonts)
        changed['fonts']['fixture.ttf']['sha256'] = digest(b'another font')
        self.put_json('fonts/sources.json', changed)
        self.assertIn('font source identities differ from the lock', self.verify())

    def test_rejects_retained_license_modification(self):
        self.put('vendor/LICENSE', b'license changed')
        self.assertIn('SHA256 mismatch: vendor/LICENSE', self.verify())

    def test_refresh_preserves_history_and_existing_reasons(self):
        before = copy.deepcopy(self.lock)
        self.put('vendor/source.c', b'original\nnew reviewed wrapper\n')
        updated, modified = updater.refresh(self.root, self.lock)
        self.assertEqual(self.lock, before)
        self.assertEqual(updated['original_import'], before['original_import'])
        self.assertEqual(updated['import_adaptations']['vendor/source.c']['original_sha256'], digest(b'original\n'))
        self.assertEqual(updated['import_adaptations']['vendor/source.c']['reason'], 'typed callback wrapper')
        self.assertTrue(modified)
        self.assertEqual(dependencies.verify(self.root, updated), [])
        again, additional = updater.refresh(self.root, updated)
        self.assertEqual(again, updated)
        self.assertEqual(additional, [])

    def test_refresh_rejects_rewritten_historical_manifest(self):
        self.put_json('docs/imported.json', {'imported_files': {}})
        with self.assertRaisesRegex(ValueError, 'historical import manifest changed'):
            updater.refresh(self.root, self.lock)

    def test_refresh_rejects_rewritten_revision_identity(self):
        self.lock['original_import']['revisions'] = {'mybot_commit': 'rewritten'}
        with self.assertRaisesRegex(ValueError, 'revisions were rewritten'):
            updater.refresh(self.root, self.lock)
        self.assertIn('historical import revisions differ from the original manifest', self.verify())

    def test_refresh_rejects_rewritten_original_hash(self):
        self.lock['import_adaptations']['vendor/source.c']['original_sha256'] = digest(b'rewritten')
        with self.assertRaisesRegex(ValueError, 'original hash was rewritten'):
            updater.refresh(self.root, self.lock)

    def test_refresh_rejects_new_adaptation_without_reason(self):
        self.put('vendor/LICENSE', b'local adaptation')
        with self.assertRaisesRegex(ValueError, 'new adaptation requires --reason'):
            updater.refresh(self.root, self.lock)
        updated, modified = updater.refresh(self.root, self.lock, reasons={'vendor/LICENSE': 'reviewed notice correction'})
        self.assertTrue(modified)
        self.assertEqual(updated['import_adaptations']['vendor/LICENSE']['original_sha256'], digest(b'license'))
        self.assertEqual(dependencies.verify(self.root, updated), [])

    def test_refresh_requires_reason_for_a_new_mapping(self):
        self.put('moved/header.h', b'public header')
        mapping = {'old/header.h': 'moved/header.h'}
        with self.assertRaisesRegex(ValueError, 'new adaptation requires --reason'):
            updater.refresh(self.root, self.lock, mappings=mapping)
        (self.root / 'component/header.h').unlink()
        updated, _ = updater.refresh(self.root, self.lock, mappings=mapping, reasons={'old/header.h': 'public include migration'})
        self.assertEqual(updated['import_adaptations']['old/header.h']['path'], 'moved/header.h')

    def add_historical(self, name, content):
        history = json.loads((self.root / 'docs/imported.json').read_text())
        history['imported_files'][name] = digest(content)
        self.put_json('docs/imported.json', history)
        self.lock['original_import']['sha256'] = self.hash('docs/imported.json')

    def register_extra_snapshot(self, **options):
        self.put('vendor-extra/include/api.h', b'independent public header')
        return updater.refresh(self.root, self.lock, add_snapshots=[{
            'name': 'extra', 'directory': 'vendor-extra', 'revision': 'fixed-revision',
            'source': 'https://example.invalid/extra'}], **options)

    def test_independent_snapshot_registration_preserves_import_history(self):
        before = (self.root / 'docs/imported.json').read_bytes()
        updated, modified = self.register_extra_snapshot()
        self.assertTrue(modified)
        self.assertEqual(updated['snapshots'][1]['revision'], 'fixed-revision')
        self.assertIn('vendor-extra/include/api.h', updated['snapshots'][1]['files'])
        self.assertEqual((self.root / 'docs/imported.json').read_bytes(), before)
        self.assertEqual(dependencies.verify(self.root, updated), [])
        again, delta = updater.refresh(self.root, updated, add_snapshots=[{
            'name': 'extra', 'directory': 'vendor-extra', 'revision': 'fixed-revision',
            'source': 'https://example.invalid/extra'}])
        self.assertEqual(again, updated)
        self.assertEqual(delta, [])

    def test_independent_snapshot_requires_fixed_identity(self):
        with self.assertRaisesRegex(ValueError, 'requires name, directory, revision and source'):
            updater.refresh(self.root, self.lock, add_snapshots=[{'name': 'extra', 'directory': 'vendor-extra'}])

    def test_snapshot_directories_cannot_overlap(self):
        self.lock['snapshots'].append({'directory': 'vendor/subdir', 'files': {}})
        with self.assertRaisesRegex(ValueError, 'directories overlap'):
            self.verify()

    def test_snapshot_file_cannot_claim_another_group(self):
        self.lock['snapshots'][0]['files']['component/header.h'] = self.hash('component/header.h')
        with self.assertRaisesRegex(ValueError, 'outside its declared directory'):
            self.verify()

    def test_vendor_root_rejects_unregistered_entries(self):
        self.put('dependencies/README.md', b'vendor index')
        self.put('dependencies/unregistered/source.c', b'unknown source')
        self.lock['vendor_root'] = 'dependencies'
        self.lock['snapshots'] = []
        self.assertIn('unregistered vendor entry: dependencies/unregistered', self.verify())
        self.put('dependencies/extra.md', b'undocumented root entry')
        self.assertIn('unregistered vendor entry: dependencies/extra.md', self.verify())

    def test_vendor_root_rejects_missing_readme(self):
        (self.root / 'dependencies').mkdir()
        self.lock['vendor_root'] = 'dependencies'
        self.lock['snapshots'] = []
        self.assertIn('missing vendor entry: dependencies/README.md', self.verify())

    def test_project_assets_are_hashed_separately_from_snapshots(self):
        self.put('assets/geometry.c', b'project geometry')
        self.lock['snapshots'].append({'name': 'view', 'source': 'project-maintained',
            'directory': 'assets', 'files': {}})
        updated, _ = updater.refresh(self.root, self.lock)
        self.assertEqual(len(updated['snapshots']), 1)
        self.assertEqual(updated['managed_assets'][0]['name'], 'view')
        self.assertEqual(dependencies.verify(self.root, updated), [])
        self.put('assets/geometry.c', b'unreviewed project geometry')
        self.assertIn('SHA256 mismatch: assets/geometry.c', dependencies.verify(self.root, updated))

    def test_prefix_mapping_expands_each_file_and_removes_old_paths(self):
        self.put('moved/header.h', b'public header')
        (self.root / 'component/header.h').unlink()
        updated, _ = updater.refresh(self.root, self.lock, prefix_mappings={'component': 'moved'},
            prefix_reasons={'component': 'public include migration'})
        entry = updated['import_adaptations']['old/header.h']
        self.assertEqual(entry['path'], 'moved/header.h')
        self.assertEqual(entry['previous_paths'], ['component/header.h'])
        self.assertEqual(dependencies.verify(self.root, updated), [])

    def test_prefix_mapping_rejects_residual_old_target(self):
        self.put('moved/header.h', b'public header')
        with self.assertRaisesRegex(ValueError, 'obsolete mapped input remains: component/header.h'):
            updater.refresh(self.root, self.lock, prefix_mappings={'component': 'moved'},
                prefix_reasons={'component': 'public include migration'})

    def test_explicit_file_reason_takes_priority_over_prefix_migration_reason(self):
        self.put('moved/header.h', b'public header with documented adaptation')
        (self.root / 'component/header.h').unlink()
        updated, _ = updater.refresh(self.root, self.lock, prefix_mappings={'component': 'moved'},
            prefix_reasons={'component': 'public include migration'},
            reasons={'old/header.h': 'relocated header and documented its public contract'})
        self.assertEqual(updated['import_adaptations']['old/header.h']['reason'],
            'relocated header and documented its public contract')
        self.assertEqual(dependencies.verify(self.root, updated), [])

    def test_prefix_mapping_rejects_overlapping_or_cyclic_rules(self):
        for mappings in ({'vendor': 'vendor/new'}, {'vendor': 'x', 'x': 'vendor'},
                         {'vendor': 'x', 'vendor/sub': 'y'}):
            with self.subTest(mappings=mappings), self.assertRaisesRegex(ValueError, 'overlap|cycle'):
                updater.refresh(self.root, self.lock, prefix_mappings=mappings,
                    prefix_reasons={name: 'reviewed move' for name in mappings})

    def test_prefix_mapping_requires_reason_and_rejects_unknown_prefix(self):
        with self.assertRaisesRegex(ValueError, 'requires --reason-prefix'):
            updater.refresh(self.root, self.lock, prefix_mappings={'component': 'moved'})
        with self.assertRaisesRegex(ValueError, 'matches no historical inputs'):
            updater.refresh(self.root, self.lock, prefix_mappings={'unknown': 'moved'},
                prefix_reasons={'unknown': 'reviewed move'})

    def relocate(self, prefixes=None, **options):
        return updater.refresh(self.root, self.lock,
            relocation_prefixes=prefixes or {'vendor': 'dependencies'},
            relocation_reasons=options.pop('relocation_reasons', {'vendor': 'separate maintained code and dependency snapshots'}),
            **options)

    def test_current_path_relocation_preserves_history_patches_and_licenses(self):
        history = (self.root / 'docs/imported.json').read_bytes()
        before = copy.deepcopy(self.lock)
        self.lock['required_inputs'] = {'vendor/source.c': self.hash('vendor/source.c')}
        (self.root / 'vendor').rename(self.root / 'dependencies')
        updated, delta = self.relocate()
        self.assertTrue(delta)
        self.assertEqual((self.root / 'docs/imported.json').read_bytes(), history)
        self.assertEqual(updated['original_import'], before['original_import'])
        self.assertEqual(set(updated['import_adaptations']), {'vendor/source.c', 'vendor/LICENSE', 'old/header.h'})
        entry = updated['import_adaptations']['vendor/source.c']
        self.assertEqual(entry['path'], 'dependencies/source.c')
        self.assertEqual(entry['original_sha256'], before['import_adaptations']['vendor/source.c']['original_sha256'])
        self.assertIn('typed callback wrapper', entry['reason'])
        self.assertEqual(updated['local_patches'][0]['original_path'], 'vendor/source.c')
        self.assertEqual(updated['local_patches'][0]['result_path'], 'dependencies/source.c')
        self.assertIn('dependencies/source.c', updated['required_inputs'])
        self.assertIn('dependencies/LICENSE', updated['retained_licenses'])
        self.assertEqual(updated['snapshots'][0]['directory'], 'dependencies')
        self.assertEqual(dependencies.verify(self.root, updated), [])
        again, delta = updater.refresh(self.root, updated)
        self.assertEqual(again, updated)
        self.assertEqual(delta, [])
        repeated, delta = updater.refresh(self.root, updated,
            relocation_prefixes={'vendor': 'dependencies'},
            relocation_reasons={'vendor': 'separate maintained code and dependency snapshots'})
        self.assertEqual(repeated, updated)
        self.assertEqual(delta, [])

    def test_relocation_updates_nonimported_assets_fonts_and_prior_destinations(self):
        self.lock['managed_assets'] = [{'name': 'view', 'directory': 'component',
            'source': 'project-maintained', 'files': {'component/header.h': self.hash('component/header.h')}}]
        (self.root / 'src').mkdir()
        (self.root / 'component').rename(self.root / 'src/core')
        (self.root / 'fonts').rename(self.root / 'src/fonts')
        updated, _ = self.relocate({'component': 'src/core', 'fonts': 'src/fonts'},
            relocation_reasons={'component': 'group maintained sources', 'fonts': 'group UI assets'})
        self.assertEqual(updated['managed_assets'][0]['directory'], 'src/core')
        self.assertIn('src/core/header.h', updated['managed_assets'][0]['files'])
        self.assertEqual(updated['generated_fonts']['manifest'], 'src/fonts/sources.json')
        self.assertIn('src/fonts/font.c', updated['generated_fonts']['outputs'])
        self.assertEqual(updated['import_adaptations']['old/header.h']['previous_paths'], ['component/header.h'])
        self.assertEqual(dependencies.verify(self.root, updated), [])

    def test_relocation_preserves_omission_history_verbatim(self):
        name = 'vendor/unused.txt'
        self.add_historical(name, b'unused')
        omitted, _ = updater.refresh(self.root, self.lock, omissions={name: 'outside production scope'})
        self.lock = omitted
        before = copy.deepcopy(omitted['import_omissions'])
        (self.root / 'vendor').rename(self.root / 'dependencies')
        updated, _ = self.relocate()
        self.assertEqual(updated['import_omissions'], before)
        self.assertEqual(dependencies.verify(self.root, updated), [])

    def test_relocation_refuses_snapshot_modification(self):
        (self.root / 'vendor').rename(self.root / 'dependencies')
        self.put('dependencies/source.c', b'changed during rename')
        with self.assertRaisesRegex(ValueError, 'relocated snapshot bytes changed'):
            self.relocate()

    def test_relocation_refuses_snapshot_additions_and_removals(self):
        (self.root / 'vendor').rename(self.root / 'dependencies')
        self.put('dependencies/extra.txt', b'not part of the reviewed snapshot')
        with self.assertRaisesRegex(ValueError, 'relocated snapshot bytes changed'):
            self.relocate()
        (self.root / 'dependencies/extra.txt').unlink()
        (self.root / 'dependencies/source.c').unlink()
        with self.assertRaisesRegex(ValueError, 'relocated snapshot bytes changed'):
            self.relocate()

    def test_relocation_rejects_residual_old_root_and_missing_destination(self):
        (self.root / 'dependencies').mkdir()
        with self.assertRaisesRegex(ValueError, 'obsolete layout root remains'):
            self.relocate()
        (self.root / 'dependencies').rmdir()
        with self.assertRaisesRegex(ValueError, 'destination is missing'):
            self.relocate({'absent': 'missing'}, relocation_reasons={'absent': 'reviewed move'})

    def test_relocation_rejects_overlap_cycles_and_missing_reason(self):
        with self.assertRaisesRegex(ValueError, 'requires --relocation-reason'):
            self.relocate(relocation_reasons={})
        for rules in ({'vendor': 'vendor/new'}, {'vendor': 'x', 'x': 'vendor'},
                      {'vendor': 'x', 'vendor/sub': 'y'}):
            with self.subTest(rules=rules), self.assertRaisesRegex(ValueError, 'overlap|cycle'):
                updater.relocate_paths(self.root, copy.deepcopy(self.lock), {}, rules,
                    {name: 'reviewed move' for name in rules})

    def test_relocation_rejects_unmatched_and_ambiguous_mappings(self):
        (self.root / 'target').mkdir()
        with self.assertRaisesRegex(ValueError, 'matches no current lock paths'):
            self.relocate({'unknown': 'target'}, relocation_reasons={'unknown': 'reviewed move'})
        (self.root / 'vendor').rename(self.root / 'dependencies')
        with self.assertRaisesRegex(ValueError, 'multiple migration rules'):
            self.relocate(mappings={'vendor/source.c': 'dependencies/source.c'})

    def test_checker_rejects_reintroduced_old_layout_roots(self):
        (self.root / 'vendor').rename(self.root / 'dependencies')
        updated, _ = self.relocate()
        self.put('vendor/untracked.txt', b'legacy directory restored')
        self.assertIn('obsolete layout root remains: vendor', dependencies.verify(self.root, updated))

    def test_checker_rejects_invalid_migration_history(self):
        for migrations, message in [
            ([{'from': 'old', 'to': 'old/nested', 'reason': 'move'}], 'prefixes overlap'),
            ([{'from': 'old', 'to': 'vendor', 'reason': ''}], 'has no explanation'),
            ([{'from': '../old', 'to': 'vendor', 'reason': 'move'}], 'without traversal'),
            ([{'from': 'old', 'to': 'vendor', 'reason': 'move'}] * 2, 'duplicate'),
            ([{'from': 'a', 'to': 'b', 'reason': 'move'}, {'from': 'b', 'to': 'a', 'reason': 'move'}], 'cycle'),
        ]:
            with self.subTest(migrations=migrations), self.assertRaisesRegex(ValueError, message):
                self.lock['path_migrations'] = migrations
                self.verify()

    def test_explicit_omission_records_original_identity_and_is_idempotent(self):
        name = 'old/.gitmodules'
        self.add_historical(name, b'obsolete submodule metadata')
        updated, _ = updater.refresh(self.root, self.lock, omissions={name: 'source snapshot is committed directly'})
        self.assertEqual(updated['import_omissions'][name], {'original_sha256': digest(b'obsolete submodule metadata'),
            'previous_path': name, 'reason': 'source snapshot is committed directly'})
        self.assertEqual(dependencies.verify(self.root, updated), [])
        again, delta = updater.refresh(self.root, updated, omissions={name: 'source snapshot is committed directly'})
        self.assertEqual(again, updated)
        self.assertEqual(delta, [])

    def test_prefix_move_then_omission_keeps_new_previous_path_without_hashing_it(self):
        name = 'old/sdk/.gitmodules'
        self.add_historical(name, b'obsolete submodule metadata')
        updated, _ = updater.refresh(self.root, self.lock, prefix_mappings={'old/sdk': 'new-sdk'},
            prefix_reasons={'old/sdk': 'flatten snapshot'}, omissions={name: 'repository metadata is not a build input'})
        self.assertEqual(updated['import_omissions'][name]['previous_path'], 'new-sdk/.gitmodules')
        self.assertNotIn(name, updated['import_adaptations'])
        self.assertEqual(dependencies.verify(self.root, updated), [])

    def test_unrecorded_removal_still_fails_refresh(self):
        self.add_historical('old/.gitmodules', b'obsolete metadata')
        with self.assertRaises(FileNotFoundError):
            updater.refresh(self.root, self.lock)

    def test_omission_rejects_unknown_or_still_present_files(self):
        with self.assertRaisesRegex(ValueError, 'not in the historical import'):
            updater.refresh(self.root, self.lock, omissions={'unknown': 'remove unknown'})
        with self.assertRaisesRegex(ValueError, 'omitted dependency input remains'):
            updater.refresh(self.root, self.lock, omissions={'vendor/source.c': 'remove live source'})

    def test_mapped_omission_rejects_either_original_or_previous_target(self):
        (self.root / 'component/header.h').unlink()
        updated, _ = updater.refresh(self.root, self.lock, omissions={'old/header.h': 'unused header'})
        self.assertEqual(updated['import_omissions']['old/header.h']['previous_path'], 'component/header.h')
        for name in ('old/header.h', 'component/header.h'):
            with self.subTest(name=name):
                self.put(name, b'public header')
                self.assertIn('omitted dependency input remains: ' + name, dependencies.verify(self.root, updated))
                (self.root / name).unlink()

    def test_omission_cannot_hide_notices_or_required_build_inputs(self):
        for name in ('old/LICENSE', 'old/NOTICE.txt', 'old/COPYRIGHT', 'old/THIRD_PARTY_NOTICES.md'):
            with self.subTest(name=name):
                self.add_historical(name, b'legal terms')
                with self.assertRaisesRegex(ValueError, 'protected dependency input'):
                    updater.refresh(self.root, self.lock, omissions={name: 'not permitted'})
                history = json.loads((self.root / 'docs/imported.json').read_text())
                del history['imported_files'][name]
                self.put_json('docs/imported.json', history)
                self.lock['original_import']['sha256'] = self.hash('docs/imported.json')
        self.lock['required_inputs'] = {'component/header.h': self.hash('component/header.h')}
        (self.root / 'component/header.h').unlink()
        with self.assertRaisesRegex(ValueError, 'protected dependency input'):
            updater.refresh(self.root, self.lock, omissions={'old/header.h': 'compiled input'})

    def test_required_input_hash_is_not_silently_refreshed(self):
        self.lock['required_inputs'] = {'component/header.h': self.hash('component/header.h')}
        self.put('component/header.h', b'unreviewed required header')
        with self.assertRaisesRegex(ValueError, 'SHA256 mismatch: component/header.h'):
            updater.refresh(self.root, self.lock)

    def test_explicit_map_and_omission_are_mutually_exclusive(self):
        with self.assertRaisesRegex(ValueError, 'both mapped and omitted'):
            updater.refresh(self.root, self.lock, mappings={'old/header.h': 'gone/header.h'},
                omissions={'old/header.h': 'unused header'})

    def test_multiple_historical_inputs_cannot_share_a_destination(self):
        self.lock['import_adaptations']['vendor/source.c']['path'] = 'component/header.h'
        with self.assertRaisesRegex(ValueError, 'share a mapped destination'):
            self.verify()

    def test_parent_symlinks_and_ambiguous_paths_are_rejected(self):
        (self.root / 'linked').symlink_to(self.root / 'component', target_is_directory=True)
        for name in ('linked/header.h', 'vendor/../component/header.h', 'vendor//source.c', 'vendor\\source.c'):
            with self.subTest(name=name), self.assertRaisesRegex(ValueError, 'symlinks|without traversal'):
                dependencies.local_path(self.root, name)

    def test_failed_cli_write_preserves_existing_lock_bytes(self):
        self.add_historical('old/.gitmodules', b'obsolete metadata')
        path = self.root / 'dependencies.lock.json'
        path.write_text(json.dumps(self.lock))
        before = path.read_bytes()
        result = subprocess.run([sys.executable, str(ROOT / 'scripts/update-dependencies-lock.py'),
            '--root', str(self.root), '--write', '--omit', 'vendor/LICENSE=not permitted'], text=True, capture_output=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('protected dependency input', result.stderr)
        self.assertEqual(path.read_bytes(), before)
        self.assertEqual(list(self.root.glob('.dependency-lock-*')), [])

    def test_checker_rejects_forged_omission_for_notice_or_unknown_input(self):
        self.lock['import_omissions'] = {'vendor/LICENSE': {
            'original_sha256': digest(b'license'), 'previous_path': 'vendor/LICENSE', 'reason': 'invalid omission'}}
        self.assertIn('protected dependency input cannot be omitted: vendor/LICENSE', self.verify())
        self.lock['import_omissions'] = {'not-imported': {
            'original_sha256': None, 'previous_path': 'not-imported', 'reason': 'invalid omission'}}
        self.assertIn('omission original hash is not the historical import: not-imported', self.verify())

    def test_omission_retains_all_prior_mapping_destinations(self):
        self.put('moved/header.h', b'public header')
        (self.root / 'component/header.h').unlink()
        moved, _ = updater.refresh(self.root, self.lock, mappings={'old/header.h': 'moved/header.h'},
            reasons={'old/header.h': 'reviewed second move'})
        (self.root / 'moved/header.h').unlink()
        omitted, _ = updater.refresh(self.root, moved, omissions={'old/header.h': 'unused header'})
        self.put('component/header.h', b'public header')
        self.assertIn('omitted dependency input remains: component/header.h', dependencies.verify(self.root, omitted))

    def test_cli_registers_snapshot_asset_and_required_input_with_idempotent_write(self):
        self.put('snapshots/README.md', b'dependency index')
        self.put('snapshots/extra/include/api.h', b'new snapshot header')
        self.put('assets/view.c', b'maintained view')
        self.lock['snapshots'] = []
        path = self.root / 'dependencies.lock.json'
        path.write_text(json.dumps(self.lock))
        command = [sys.executable, str(ROOT / 'scripts/update-dependencies-lock.py'),
            '--root', str(self.root), '--write', '--json', '--add-snapshot', 'extra=snapshots/extra',
            '--revision', 'extra=fixed-revision', '--source', 'extra=https://example.invalid/extra',
            '--managed-asset', 'view=assets', '--required-input', 'component/header.h', '--vendor-root', 'snapshots']
        first = subprocess.run(command, text=True, capture_output=True)
        self.assertEqual(first.returncode, 0, first.stderr)
        self.assertTrue(json.loads(first.stdout)['written'])
        updated = json.loads(path.read_text())
        self.assertEqual(updated['vendor_root'], 'snapshots')
        self.assertIn('component/header.h', updated['required_inputs'])
        self.assertEqual(updated['managed_assets'][0]['name'], 'view')
        self.assertEqual(dependencies.verify(self.root, updated), [])
        second = subprocess.run(command, text=True, capture_output=True)
        self.assertEqual(second.returncode, 0, second.stderr)
        self.assertFalse(json.loads(second.stdout)['written'])
        self.assertEqual(json.loads(second.stdout)['changes'], [])

    def test_refresh_rejects_unknown_adaptation_name(self):
        with self.assertRaisesRegex(ValueError, 'not in the historical import'):
            updater.refresh(self.root, self.lock, reasons={'made-up.c': 'not an import'})

    def test_refresh_cli_previews_without_writing(self):
        self.put('vendor/source.c', b'reviewed change')
        path = self.root / 'dependencies.lock.json'
        path.write_text(json.dumps(self.lock))
        before = path.read_bytes()
        result = subprocess.run([sys.executable, str(ROOT / 'scripts/update-dependencies-lock.py'),
                                 '--root', str(self.root), '--json'], text=True, capture_output=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue(json.loads(result.stdout)['review_required'])
        self.assertFalse(json.loads(result.stdout)['written'])
        self.assertEqual(path.read_bytes(), before)
        result = subprocess.run([sys.executable, str(ROOT / 'scripts/update-dependencies-lock.py'),
                                 '--root', str(self.root), '--write', '--json'], text=True, capture_output=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue(json.loads(result.stdout)['written'])
        self.assertEqual(dependencies.verify(self.root, json.loads(path.read_text())), [])

    def test_rejects_lock_path_traversal(self):
        self.lock['original_import']['manifest'] = '../outside.json'
        with self.assertRaisesRegex(ValueError, 'without traversal'):
            self.verify()

    def test_rejects_symlink_input(self):
        target = self.root / 'vendor/LICENSE'
        target.unlink()
        target.symlink_to(self.root / 'vendor/source.c')
        with self.assertRaisesRegex(ValueError, 'must not be symlinks'):
            self.verify()


if __name__ == '__main__':
    unittest.main()
