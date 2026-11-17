# SPDX-License-Identifier: Apache-2.0
"""Manual provenance catches relevant drift without PDF packages or a device."""

import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location('check_device_manual', ROOT / 'scripts/check-device-manual.py')
manual = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(manual)


class ManualAssetsTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix='mybot-manual-test-')
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        for name in manual.INPUT_FILES + manual.UI_FILES:
            self.put(name, b'fixture input\n')
        self.put('CMakeLists.txt', b'project(mybot_rv1126b VERSION 1.2.3 LANGUAGES C CXX)\n')
        self.put('src/ui/src/view.c', b'fixture renderer\n')
        self.put('vendor/lvgl/src/label.c', b'fixture LVGL\n')
        for name in manual.FONT_FILES + ('LICENSE.txt',):
            self.put('docs/user/assets/fonts/' + name, b'fixture font input\n')
        for source, directory, pdf in manual.LANGUAGES.values():
            self.put('docs/user/' + source, ('# Manual\n![UI](assets/' + directory + '/ready.png)\n').encode())
            self.put('docs/user/assets/' + directory + '/ready.png', b'fixture image bytes\n')
            self.put(pdf, b'fixture PDF bytes\n')
        inputs, ui_digest, version = manual.snapshot(self.root)
        self.manifest = {'schema_version': 1, 'document_date': '2026-11-17',
                         'application_version': version, 'ui_source_sha256': ui_digest,
                         'inputs': inputs, 'outputs': manual.hashes(self.root, manual.output_names(self.root))}
        self.write_manifest()

    def put(self, name, data):
        path = self.root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)

    def write_manifest(self):
        self.put(manual.MANIFEST, (json.dumps(self.manifest) + '\n').encode())

    def test_check_uses_stdlib_and_ignores_ordinary_application_edits(self):
        self.assertEqual(manual.check(self.root), self.manifest)
        self.put('src/application/main.c', b'unrelated application change\n')
        self.assertEqual(manual.check(self.root), self.manifest)

    def test_renderer_and_new_ui_inputs_are_detected(self):
        self.put('src/ui/src/new-view.c', b'new screen\n')
        with self.assertRaisesRegex(ValueError, 'manual input changed: src/ui/src/new-view.c'):
            manual.check(self.root)

    def test_actual_lvgl_content_joins_ui_identity(self):
        self.put('vendor/lvgl/src/label.c', b'changed rendering dependency\n')
        with self.assertRaisesRegex(ValueError, 'manual UI source identity changed'):
            manual.check(self.root)

    def test_manual_output_corruption_is_detected(self):
        self.put('docs/device-operations.en.pdf', b'truncated')
        with self.assertRaisesRegex(ValueError, 'manual output changed: docs/device-operations.en.pdf'):
            manual.check(self.root)

    def test_bilingual_scene_names_must_match(self):
        self.put('docs/user/device-manual.en.md', b'# Manual\n![UI](assets/device-manual-en/new.png)\n')
        with self.assertRaisesRegex(ValueError, 'scene names differ'):
            manual.check(self.root)

    def test_manifest_paths_cannot_escape_the_repository(self):
        self.manifest['inputs']['../outside'] = 'a' * 64
        self.write_manifest()
        with self.assertRaisesRegex(ValueError, 'Invalid repository-relative'):
            manual.check(self.root)

    def test_schema_and_document_date_are_strict(self):
        self.manifest['schema_version'] = True
        self.write_manifest()
        with self.assertRaisesRegex(ValueError, 'schema'):
            manual.read_manifest(self.root)
        self.manifest['schema_version'] = 1
        self.manifest['document_date'] = 'not-a-date'
        self.write_manifest()
        with self.assertRaises(ValueError):
            manual.read_manifest(self.root)

    def test_preview_is_built_only_after_source_and_language_match(self):
        directory = self.root / 'build/preview'
        self.put('build/preview/CMakeCache.txt',
                 ('CMAKE_HOME_DIRECTORY:INTERNAL=' + str(self.root) + '\n'
                  'RV1126B_HOST_ONLY:BOOL=ON\nRV1126B_LANGUAGE:STRING=zh-CN\n').encode())
        self.put('build/preview/mybot-ui-preview', b'fixture executable')
        with patch.object(manual.subprocess, 'run') as run:
            self.assertEqual(manual.build_preview(self.root, directory, 'zh-CN'), directory.resolve() / 'mybot-ui-preview')
            run.assert_called_once_with(['cmake', '--build', str(directory.resolve()), '--target', 'mybot-ui-preview'], check=True)
        with patch.object(manual.subprocess, 'run') as run:
            with self.assertRaisesRegex(ValueError, 'matching UI language'):
                manual.build_preview(self.root, directory, 'en-US')
            run.assert_not_called()

    def test_failed_render_does_not_publish_partial_assets(self):
        before = (self.root / manual.MANIFEST).read_bytes()
        outputs = manual.hashes(self.root, manual.output_names(self.root))
        with patch.object(manual, 'require_pdf_tools'):
            with patch.object(manual, 'render_assets', side_effect=ValueError('render failed')):
                with self.assertRaisesRegex(ValueError, 'render failed'):
                    manual.refresh(self.root, {})
        self.assertEqual((self.root / manual.MANIFEST).read_bytes(), before)
        self.assertEqual(manual.hashes(self.root, manual.output_names(self.root)), outputs)

    def test_full_generation_requires_all_three_pinned_pdf_tool_versions(self):
        versions = {'Pillow': '11.3.0', 'reportlab': '4.4.3', 'PyMuPDF': '1.26.4'}
        self.put('scripts/requirements-manual.txt',
                 ''.join(name + '==' + version + '\n' for name, version in versions.items()).encode())
        for changed in versions:
            installed = dict(versions, **{changed: '0.0.0'})
            with self.subTest(package=changed):
                with patch.object(manual.metadata, 'version', side_effect=installed.__getitem__):
                    with self.assertRaisesRegex(ValueError, 'Manual generation needs ' + changed):
                        manual.require_pdf_tools(self.root)

    def test_explicit_date_can_recover_a_corrupted_manifest(self):
        self.put(manual.MANIFEST, b'corrupt JSON')
        with patch.object(manual, 'require_pdf_tools'):
            with patch.object(manual, 'render_assets', side_effect=ValueError('render entered')):
                with self.assertRaisesRegex(ValueError, 'render entered'):
                    manual.refresh(self.root, {}, '2026-11-17')


if __name__ == '__main__':
    unittest.main()
