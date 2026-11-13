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
        self.root = Path(self.temporary.name)
        self.staging = self.root / 'staging'
        self.sdk_path = self.root / 'sdk'
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
        self.lock = {'external': {'rtsa': {'version': 'fixture', 'library_sha256': digest(elf()),
            'headers': {'include/agora_rtc_api.h': digest(b'public api')},
            'required_config': {'CONFIG_MINIMAL_TIMER_INTERVAL_MS': '20', 'CONFIG_RTM': 'y'}},
            'runtime': {'libraries': {self.runtime_name: digest(elf())}}}}
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
        self.lock['external']['rtsa']['library_sha256'] = digest(elf(62))
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
        updated, _ = updater.refresh(self.root, self.lock, mappings=mapping, reasons={'old/header.h': 'public include migration'})
        self.assertEqual(updated['import_adaptations']['old/header.h']['path'], 'moved/header.h')

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
