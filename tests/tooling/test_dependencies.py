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
        temporary = tempfile.TemporaryDirectory(prefix='mybot-provenance-test-')
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.put('vendor/README.md', b'vendor index')
        self.put('vendor/dep/source.c', b'original\nwrapper\n')
        self.put('vendor/dep/LICENSE', b'license')
        self.put('component/header.h', b'public header')
        self.put('assets/view.c', b'project view')
        self.put('docs/fix.patch', b'patch input')
        self.put('fonts/font.c', b'generated glyphs')
        self.put('tools/generate.py', b'font generator')
        self.fonts = {'generator': 'tools/generate.py', 'fonts': {
            'fixture.ttf': {'sha256': digest(b'font input'), 'url': 'https://example.invalid/font'}},
            'pillow_version': 'fixture', 'freetype_version': 'fixture'}
        self.put_json('fonts/sources.json', self.fonts)
        self.put_json('docs/imported.json', {'imported_files': {'old/source.c': digest(b'original\n')}})
        self.put_json('docs/lock-v1.json', {'schema_version': 1, 'original_import': {
            'manifest': 'docs/imported.json', 'sha256': self.hash('docs/imported.json')},
            'import_adaptations': {'old/source.c': {'path': 'obsolete/project/source.c'}}})
        self.lock = {'schema_version': 2, 'vendor_root': 'vendor',
            'history': {
                'dependency_lock_v1': {'path': 'docs/lock-v1.json', 'sha256': self.hash('docs/lock-v1.json')},
                'original_import': {'path': 'docs/imported.json', 'sha256': self.hash('docs/imported.json')}},
            'snapshots': [{'name': 'dep', 'directory': 'vendor/dep', 'revision': 'fixed',
                'source': 'https://example.invalid/dep', 'files': {
                    'vendor/dep/source.c': self.hash('vendor/dep/source.c'),
                    'vendor/dep/LICENSE': self.hash('vendor/dep/LICENSE')}}],
            'local_patches': [{'path': 'docs/fix.patch', 'sha256': self.hash('docs/fix.patch'),
                'original_path': 'old/source.c', 'original_sha256': digest(b'original\n'),
                'result_path': 'vendor/dep/source.c', 'result_sha256': self.hash('vendor/dep/source.c')}],
            'generated_fonts': {'manifest': 'fonts/sources.json', 'manifest_sha256': self.hash('fonts/sources.json'),
                'generator': 'tools/generate.py', 'generator_sha256': self.hash('tools/generate.py'),
                'pillow_version': 'fixture', 'freetype_version': 'fixture',
                'inputs': copy.deepcopy(self.fonts['fonts']), 'outputs': {'fonts/font.c': self.hash('fonts/font.c')}},
            'retained_licenses': {'vendor/dep/LICENSE': self.hash('vendor/dep/LICENSE')},
            'required_inputs': {'component/header.h': self.hash('component/header.h')}}

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

    def test_current_integrity_and_archived_mappings_are_not_live(self):
        self.assertEqual(self.verify(), [])
        self.put('obsolete/project/source.c', b'ordinary maintained code')
        self.put('src/application/new.c', b'maintained source')
        self.assertEqual(self.verify(), [])
        self.put('src/application/new.c', b'edited maintained source')
        self.put('src/ui/views/pixel_glasses/view.c', b'edited maintained UI view')
        self.assertEqual(updater.refresh(self.root, self.lock), (self.lock, []))

    def test_live_historical_mapping_keys_are_rejected(self):
        for key in ('import_adaptations', 'import_omissions', 'path_migrations', 'original_import', 'managed_assets'):
            with self.subTest(key=key):
                self.lock[key] = {}
                with self.assertRaisesRegex(ValueError, 'only in the immutable history'):
                    self.verify()
                del self.lock[key]

    def test_rejects_unlocked_missing_and_changed_vendor_files(self):
        self.put('vendor/dep/new.c', b'new')
        self.assertIn('unlocked dependency file: vendor/dep/new.c', self.verify())
        (self.root / 'vendor/dep/new.c').unlink()
        self.put('vendor/dep/source.c', b'changed')
        self.assertIn('SHA256 mismatch: vendor/dep/source.c', self.verify())
        (self.root / 'vendor/dep/source.c').unlink()
        self.assertIn('missing snapshot file: vendor/dep/source.c', self.verify())

    def test_rejects_unregistered_vendor_root_member(self):
        self.put('vendor/unregistered/thing.c', b'new dependency')
        self.assertIn('unregistered vendor entry: vendor/unregistered', self.verify())

    def test_rejects_empty_or_overlapping_dependency_groups(self):
        self.lock['snapshots'][0]['files'] = {}
        with self.assertRaisesRegex(ValueError, 'must not be empty'):
            self.verify()
        self.lock['snapshots'][0]['files'] = {'vendor/dep/source.c': self.hash('vendor/dep/source.c')}
        self.lock['snapshots'].append({'name': 'nested', 'source': 'fixture', 'revision': 'fixed',
            'directory': 'vendor/dep/nested', 'files': {'vendor/dep/nested/api.h': digest(b'header')}})
        with self.assertRaisesRegex(ValueError, 'overlap'):
            self.verify()

    def test_rejects_history_change_and_refuses_to_refresh_it(self):
        for name in ('docs/imported.json', 'docs/lock-v1.json'):
            with self.subTest(name=name):
                before = (self.root / name).read_bytes()
                self.put_json(name, {})
                self.assertIn('SHA256 mismatch: ' + name, self.verify())
                with self.assertRaisesRegex(ValueError, 'immutable dependency history changed'):
                    updater.refresh(self.root, self.lock)
                self.put(name, before)

    def test_history_references_are_cross_checked(self):
        archived = json.loads((self.root / 'docs/lock-v1.json').read_text())
        archived['original_import']['manifest'] = 'different.json'
        self.put_json('docs/lock-v1.json', archived)
        self.lock['history']['dependency_lock_v1']['sha256'] = self.hash('docs/lock-v1.json')
        self.assertIn('historical lock and original import reference differ', self.verify())

    def test_missing_patch_and_result_pin_are_checked(self):
        (self.root / 'docs/fix.patch').unlink()
        self.assertIn('missing dependency input: docs/fix.patch', self.verify())
        self.put('docs/fix.patch', b'patch input')
        self.lock['local_patches'][0]['result_sha256'] = digest(b'other result')
        self.assertIn('patch result differs from the current snapshot: vendor/dep/source.c', self.verify())

    def test_refresh_requires_explicit_vendor_and_patch_acceptance(self):
        self.put('vendor/dep/source.c', b'new reviewed wrapper')
        with self.assertRaisesRegex(ValueError, 'select --snapshot dep'):
            updater.refresh(self.root, self.lock)
        with self.assertRaisesRegex(ValueError, 'select --patch docs/fix.patch'):
            updater.refresh(self.root, self.lock, snapshots=['dep'])
        self.put('docs/fix.patch', b'reviewed replacement patch')
        before = copy.deepcopy(self.lock)
        updated, delta = updater.refresh(self.root, self.lock, snapshots=['dep'], patches=['docs/fix.patch'])
        self.assertEqual(self.lock, before)
        self.assertTrue(delta)
        self.assertEqual(updated['history'], before['history'])
        self.assertEqual(updated['local_patches'][0]['original_sha256'], digest(b'original\n'))
        self.assertEqual(dependencies.verify(self.root, updated), [])
        self.assertEqual(updater.refresh(self.root, updated), (updated, []))

    def test_snapshot_selection_cannot_silently_remove_a_notice_or_required_input(self):
        for name in ('vendor/dep/LICENSE', 'vendor/dep/NOTICE.txt', 'vendor/dep/compiled.c'):
            with self.subTest(name=name):
                if name != 'vendor/dep/LICENSE':
                    self.put(name, b'required')
                    self.lock['snapshots'][0]['files'][name] = self.hash(name)
                if name.endswith('compiled.c'):
                    self.lock['required_inputs'][name] = self.hash(name)
                (self.root / name).unlink()
                with self.assertRaisesRegex(ValueError, 'protected dependency input cannot be removed'):
                    updater.refresh(self.root, self.lock, snapshots=['dep'])
                self.put(name, b'license' if name.endswith('LICENSE') else b'required')

    def test_license_text_changes_need_explicit_separate_acceptance(self):
        self.put('vendor/dep/LICENSE', b'reviewed license correction')
        with self.assertRaisesRegex(ValueError, 'select --license vendor/dep/LICENSE'):
            updater.refresh(self.root, self.lock, snapshots=['dep'])
        updated, delta = updater.refresh(self.root, self.lock, snapshots=['dep'], licenses=['vendor/dep/LICENSE'])
        self.assertTrue(delta)
        self.assertEqual(dependencies.verify(self.root, updated), [])

    def test_required_input_changes_need_explicit_acceptance(self):
        self.put('component/header.h', b'new required input')
        with self.assertRaisesRegex(ValueError, 'select --required-input component/header.h'):
            updater.refresh(self.root, self.lock)
        updated, delta = updater.refresh(self.root, self.lock, required_inputs=['component/header.h'])
        self.assertTrue(delta)
        self.assertEqual(dependencies.verify(self.root, updated), [])

    def test_selected_snapshot_refreshes_duplicate_required_pin(self):
        name = 'vendor/dep/source.c'
        self.lock['required_inputs'][name] = self.hash(name)
        self.put(name, b'new reviewed wrapper')
        updated, _ = updater.refresh(self.root, self.lock, snapshots=['dep'], patches=['docs/fix.patch'])
        self.assertEqual(updated['required_inputs'][name], self.hash(name))

    def test_generated_fonts_refresh_without_vendor_acceptance(self):
        self.put('fonts/font.c', b'new glyphs')
        self.put('tools/generate.py', b'new generator')
        self.fonts['fonts']['fixture.ttf']['sha256'] = digest(b'updated font input')
        self.put_json('fonts/sources.json', self.fonts)
        self.assertIn('font source identities differ from the lock', self.verify())
        updated, delta = updater.refresh(self.root, self.lock)
        self.assertTrue(delta)
        self.assertEqual(updated['snapshots'], self.lock['snapshots'])
        self.assertEqual(dependencies.verify(self.root, updated), [])
        self.assertEqual(updater.refresh(self.root, updated), (updated, []))

    def test_generated_font_tool_versions_and_generator_path_are_checked(self):
        self.fonts['pillow_version'] = 'other'
        self.fonts['generator'] = 'other-generator.py'
        self.put_json('fonts/sources.json', self.fonts)
        errors = self.verify()
        self.assertIn('font tool version differs from the lock: pillow_version', errors)
        self.assertIn('font generator path differs from the lock', errors)

    def test_rejects_parent_symlinks_traversal_and_ambiguous_paths(self):
        (self.root / 'linked').symlink_to(self.root / 'component', target_is_directory=True)
        for name in ('linked/header.h', '../outside', 'vendor/../component/header.h',
                     'vendor//dep/source.c', 'vendor\\dep\\source.c', '/absolute', '.', '', 'x\ny'):
            with self.subTest(name=name), self.assertRaises(ValueError):
                dependencies.local_path(self.root, name)

    def test_rejects_snapshot_symlink_input(self):
        path = self.root / 'vendor/dep/source.c'
        path.unlink()
        path.symlink_to(self.root / 'component/header.h')
        with self.assertRaisesRegex(ValueError, 'must not be symlinks'):
            self.verify()

    def test_rejects_history_path_traversal_and_invalid_digest(self):
        self.lock['history']['original_import']['path'] = '../outside'
        with self.assertRaisesRegex(ValueError, 'without traversal'):
            self.verify()
        self.lock['history']['original_import']['path'] = 'docs/imported.json'
        self.lock['history']['original_import']['sha256'] = 'invalid'
        with self.assertRaisesRegex(ValueError, 'invalid dependency SHA256'):
            self.verify()

    def test_unknown_snapshot_or_patch_selection_is_rejected(self):
        with self.assertRaisesRegex(ValueError, 'unknown snapshot selection'):
            updater.refresh(self.root, self.lock, snapshots=['unknown'])
        with self.assertRaisesRegex(ValueError, 'unknown patch selection'):
            updater.refresh(self.root, self.lock, patches=['unknown.patch'])

    def test_cli_preview_write_idempotence_and_failed_write_atomicity(self):
        path = self.root / 'dependencies.lock.json'
        path.write_text(json.dumps(self.lock))
        before = path.read_bytes()
        self.put('fonts/font.c', b'updated generated glyphs')
        command = [sys.executable, str(ROOT / 'scripts/update-dependencies-lock.py'),
                   '--root', str(self.root), '--json']
        result = subprocess.run(command, text=True, capture_output=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue(json.loads(result.stdout)['review_required'])
        self.assertFalse(json.loads(result.stdout)['written'])
        self.assertEqual(path.read_bytes(), before)
        result = subprocess.run(command + ['--write'], text=True, capture_output=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue(json.loads(result.stdout)['written'])
        result = subprocess.run(command + ['--write'], text=True, capture_output=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertFalse(json.loads(result.stdout)['written'])
        before = path.read_bytes()
        self.put('vendor/dep/LICENSE', b'unreviewed')
        result = subprocess.run(command + ['--write'], text=True, capture_output=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(path.read_bytes(), before)
        self.assertEqual(list(self.root.glob('.dependency-lock-*')), [])


if __name__ == '__main__':
    unittest.main()
