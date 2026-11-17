# SPDX-License-Identifier: Apache-2.0
import contextlib
import importlib.util
import io
from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
SPEC = importlib.util.spec_from_file_location('project_version_tool', ROOT / 'scripts/project-version.py')
version_tool = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(version_tool)


class ProjectVersionTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        (self.root / 'CMakeLists.txt').write_text('project(mybot_rv1126b VERSION 0.5.1 LANGUAGES C CXX)\n')
        for name in version_tool.VERSION_DOCUMENTS:
            (self.root / name).write_text('Current: **<!-- project-version -->0.5.1<!-- /project-version -->**\n')
        (self.root / 'history.md').write_text('Accepted package: mybot-rv1126b-0.1.1-aarch64\n')

    def run_cli(self, *args):
        output, errors = io.StringIO(), io.StringIO()
        with contextlib.redirect_stdout(output), contextlib.redirect_stderr(errors):
            result = version_tool.main(args, root=self.root)
        return result, output.getvalue(), errors.getvalue()

    def snapshot(self):
        return {path.name: path.read_bytes() for path in self.root.iterdir()}

    def test_read_uses_cmake_and_check_detects_documentation_drift(self):
        self.assertEqual(self.run_cli(), (0, '0.5.1\n', ''))
        readme = self.root / 'README.md'
        readme.write_text(readme.read_text().replace('0.5.1', '0.2.2'))
        before = self.snapshot()
        result, output, error = self.run_cli('--check')
        self.assertEqual(result, 1)
        self.assertEqual(output, '')
        self.assertIn('README.md', error)
        self.assertIn('--set 0.5.1', error)
        self.assertEqual(before, self.snapshot())

    def test_single_update_syncs_current_documents_and_preserves_history(self):
        history = (self.root / 'history.md').read_bytes()
        self.assertEqual(self.run_cli('--set', '2.3.4'), (0, '2.3.4\n', ''))
        self.assertEqual(version_tool.check_version(self.root), [])
        self.assertEqual((self.root / 'history.md').read_bytes(), history)
        before = self.snapshot()
        self.assertEqual(self.run_cli('--set', '2.3.4')[0], 0)
        self.assertEqual(before, self.snapshot())

    def test_missing_reversed_or_duplicate_marker_rejects_update_without_writes(self):
        bad_documents = ('Current: 0.5.1',
                         version_tool.END + '0.5.1' + version_tool.START,
                         (version_tool.START + '0.5.1' + version_tool.END) * 2)
        for contents in bad_documents:
            with self.subTest(contents=contents):
                (self.root / 'README.zh-CN.md').write_text(contents)
                before = self.snapshot()
                self.assertEqual(self.run_cli('--set', '2.3.4')[0], 1)
                self.assertEqual(before, self.snapshot())

    def test_invalid_version_is_rejected_without_writes(self):
        for version in ('', '01.2.3', '1.2', '1.2.3-rc1', '1.2.3\n', '1.2.3; echo wrong'):
            with self.subTest(version=version):
                before = self.snapshot()
                self.assertEqual(self.run_cli('--set', version)[0], 1)
                self.assertEqual(before, self.snapshot())

    def test_check_reports_missing_marker_without_creating_files(self):
        (self.root / 'README.md').unlink()
        before = self.snapshot()
        self.assertEqual(self.run_cli('--check')[0], 1)
        self.assertEqual(before, self.snapshot())


if __name__ == '__main__':
    unittest.main()
