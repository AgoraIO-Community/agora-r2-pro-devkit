#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Offline coverage for complete, immutable RTSA release snapshots."""

import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]


def load(name, filename):
    spec = importlib.util.spec_from_file_location(name, ROOT / 'scripts' / filename)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


check = load('rtsa_snapshot_check', 'check-dependencies.py')
updater = load('rtsa_snapshot_update', 'update-dependencies-lock.py')


def digest(content):
    return hashlib.sha256(content).hexdigest()


def elf(machine=183, kind=3, bits=2, byte_order=1):
    header = bytearray(64)
    header[:6] = b'\x7fELF' + bytes((bits, byte_order))
    header[16:18] = kind.to_bytes(2, 'little')
    header[18:20] = machine.to_bytes(2, 'little')
    return bytes(header)


class RtsaSnapshotTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix='mybot-rtsa-snapshot-')
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.put('third_party/README.md', b'Vendored dependencies\n')
        self.contents = {
            'README.md': b'Official package documentation\n',
            'agora_sdk/.config': b'CONFIG_RTM=y\nCONFIG_MINIMAL_TIMER_INTERVAL_MS=20\nCONFIG_PACKAGE_INFO=x86_64\n',
            'agora_sdk/include/agora_rtc_api.h': b'public API\n',
            'agora_sdk/lib/aarch64/libagora-rtc-sdk.so': elf(),
            'agora_sdk/lib/x86_64/libagora-rtc-sdk.so': elf(62),
            'example/hello_rtsa/hello_rtsa.c': b'int main(void) { return 0; }\n',
            'example/test_data/send_audio.pcm': b'\x00\x01\x02\x03',
        }
        for name, content in self.contents.items():
            self.put('third_party/rtsa/' + name, content)
        self.snapshot = {
            'name': 'rtsa', 'version': 'fixture', 'package': 'official-fixture',
            'revision': 'official-fixture', 'source': 'https://example.invalid/rtsa.tgz',
            'archive_sha256': digest(b'official archive'),
            'scope': 'Complete official release package (unmodified)',
            'directory': 'third_party/rtsa', 'sdk_subdirectory': 'agora_sdk',
            'library_subpath': 'lib/aarch64/libagora-rtc-sdk.so', 'architecture': 'aarch64',
            'library_sha256': digest(elf()),
            'headers': {'include/agora_rtc_api.h': digest(b'public API\n')},
            'required_config': {'CONFIG_RTM': 'y', 'CONFIG_MINIMAL_TIMER_INTERVAL_MS': '20'},
            'archive_files': {name: digest(content) for name, content in self.contents.items()},
            'files': {'third_party/rtsa/' + name: digest(content) for name, content in self.contents.items()},
        }
        self.put('docs/imported.json', json.dumps({'imported_files': {}}).encode())
        fonts = {'generator': 'tools/font.py', 'fonts': {}}
        self.put('fonts/sources.json', json.dumps(fonts).encode())
        self.put('tools/font.py', b'# fixture\n')
        self.lock = {
            'schema_version': 1, 'vendor_root': 'third_party',
            'original_import': {'manifest': 'docs/imported.json', 'sha256': self.hash('docs/imported.json')},
            'snapshots': [self.snapshot], 'import_adaptations': {}, 'local_patches': [],
            'import_omissions': {}, 'managed_assets': [], 'required_inputs': {}, 'retained_licenses': {},
            'generated_fonts': {
                'manifest': 'fonts/sources.json', 'manifest_sha256': self.hash('fonts/sources.json'),
                'generator': 'tools/font.py', 'generator_sha256': self.hash('tools/font.py'),
                'inputs': {}, 'outputs': {},
            },
        }

    def put(self, name, content):
        path = self.root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(content)

    def hash(self, name):
        return digest((self.root / name).read_bytes())

    def verify(self):
        return check.verify(self.root, self.lock)

    def test_complete_package_accepts_other_architecture_and_original_metadata(self):
        self.assertEqual(self.verify(), [])

    def test_every_official_member_is_required_including_samples_and_test_data(self):
        for name, content in self.contents.items():
            with self.subTest(name=name):
                path = self.root / 'third_party/rtsa' / name
                path.unlink()
                self.assertTrue(any('missing' in error for error in self.verify()))
                path.write_bytes(content)

    def test_extra_package_file_is_rejected(self):
        self.put('third_party/rtsa/unreviewed.txt', b'extra')
        self.assertIn('unlocked dependency file: third_party/rtsa/unreviewed.txt', self.verify())

    def test_modified_sample_is_rejected(self):
        self.put('third_party/rtsa/example/test_data/send_audio.pcm', b'changed')
        self.assertIn('SHA256 mismatch: third_party/rtsa/example/test_data/send_audio.pcm', self.verify())

    def test_pruning_snapshot_manifest_does_not_prune_archive_proof(self):
        name = 'example/test_data/send_audio.pcm'
        (self.root / 'third_party/rtsa' / name).unlink()
        del self.snapshot['files']['third_party/rtsa/' + name]
        self.assertIn('complete archive inventory differs from snapshot: third_party/rtsa', self.verify())

    def test_complete_archive_proof_is_mandatory(self):
        del self.snapshot['archive_files']
        with self.assertRaisesRegex(ValueError, 'requires a complete archive inventory'):
            self.verify()

    def test_archive_proof_rejects_traversal(self):
        self.snapshot['archive_files']['../outside'] = digest(b'outside')
        with self.assertRaisesRegex(ValueError, 'without traversal'):
            self.verify()

    def test_archive_proof_rejects_invalid_digest(self):
        self.snapshot['archive_files']['README.md'] = 'not-a-sha256'
        with self.assertRaisesRegex(ValueError, 'invalid complete archive SHA256'):
            self.verify()

    def test_target_library_cannot_select_x86_even_with_matching_hash(self):
        self.snapshot['library_subpath'] = 'lib/x86_64/libagora-rtc-sdk.so'
        self.snapshot['library_sha256'] = digest(elf(62))
        self.assertTrue(any('AArch64 shared object' in error for error in self.verify()))

    def test_target_elf_class_byte_order_and_object_type_are_checked(self):
        for content in (elf(bits=1), elf(byte_order=2), elf(kind=2), b'\x7fELF\x02\x01'):
            with self.subTest(content=content):
                name = 'agora_sdk/lib/aarch64/libagora-rtc-sdk.so'
                self.put('third_party/rtsa/' + name, content)
                self.snapshot['files']['third_party/rtsa/' + name] = digest(content)
                self.snapshot['archive_files'][name] = digest(content)
                self.snapshot['library_sha256'] = digest(content)
                self.assertTrue(any('AArch64 shared object' in error for error in self.verify()))

    def test_header_metadata_must_match_the_complete_snapshot(self):
        self.snapshot['headers']['include/agora_rtc_api.h'] = digest(b'other version')
        self.assertTrue(any('RTSA target identity differs from snapshot' in error for error in self.verify()))

    def test_header_selection_cannot_escape_sdk(self):
        self.snapshot['headers']['../README.md'] = digest(b'other header')
        with self.assertRaisesRegex(ValueError, 'without traversal'):
            self.verify()

    def test_required_rtsa_configuration_is_checked_after_repinning_files(self):
        name = 'agora_sdk/.config'
        content = b'CONFIG_RTM=n\nCONFIG_MINIMAL_TIMER_INTERVAL_MS=10\n'
        self.put('third_party/rtsa/' + name, content)
        self.snapshot['files']['third_party/rtsa/' + name] = digest(content)
        self.snapshot['archive_files'][name] = digest(content)
        self.assertIn('RTSA required configuration differs: CONFIG_RTM', self.verify())
        self.assertIn('RTSA required configuration differs: CONFIG_MINIMAL_TIMER_INTERVAL_MS', self.verify())

    def test_refresh_preserves_complete_archive_and_original_import(self):
        before = (self.root / 'docs/imported.json').read_bytes()
        refreshed, changes = updater.refresh(self.root, self.lock)
        self.assertEqual(changes, [])
        self.assertEqual(refreshed['snapshots'], self.lock['snapshots'])
        self.assertEqual((self.root / 'docs/imported.json').read_bytes(), before)

    def test_refresh_refuses_to_accept_missing_extra_or_modified_archive_files(self):
        name = 'example/test_data/send_audio.pcm'
        path = self.root / 'third_party/rtsa' / name
        path.unlink()
        with self.assertRaisesRegex(ValueError, 'complete release snapshot changed'):
            updater.refresh(self.root, self.lock)
        path.write_bytes(self.contents[name])
        self.put('third_party/rtsa/extra.txt', b'extra')
        with self.assertRaisesRegex(ValueError, 'complete release snapshot changed'):
            updater.refresh(self.root, self.lock)
        (self.root / 'third_party/rtsa/extra.txt').unlink()
        path.write_bytes(b'changed')
        with self.assertRaisesRegex(ValueError, 'complete release snapshot changed'):
            updater.refresh(self.root, self.lock)

    def test_refresh_refuses_rewritten_snapshot_hashes(self):
        name = 'third_party/rtsa/README.md'
        self.put(name, b'rewritten')
        self.snapshot['files'][name] = self.hash(name)
        with self.assertRaisesRegex(ValueError, 'complete release snapshot changed'):
            updater.refresh(self.root, self.lock)


if __name__ == '__main__':
    unittest.main()
