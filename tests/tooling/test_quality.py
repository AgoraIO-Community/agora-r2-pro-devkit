# SPDX-License-Identifier: Apache-2.0
"""Formatter selection must be reproducible and fail before rewriting sources."""

from contextlib import redirect_stderr, redirect_stdout
import importlib.util
import io
import json
import os
from pathlib import Path
import subprocess
import shutil
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location('check_quality', ROOT / 'scripts/check-quality.py')
quality = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(quality)


class FormatterTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix='mybot-quality-test-')
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        (self.root / 'requirements-dev.txt').write_bytes((ROOT / 'requirements-dev.txt').read_bytes())
        self.log = self.root / 'formatter-calls.jsonl'
        self.source = self.root / 'source.c'
        self.source.write_text('/* SPDX-License-Identifier: Apache-2.0 */\nint main(void){return 0;}\n')

    def formatter(self, path, version='clang-format version 11.0.1', status=0):
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(
            '#!' + sys.executable + '\n'
            'import json, sys\n'
            'from pathlib import Path\n'
            'with Path(' + repr(str(self.log)) + ').open("a") as log:\n'
            '    log.write(json.dumps(sys.argv[1:]) + "\\n")\n'
            'if sys.argv[1:] == ["--version"]:\n'
            '    print(' + repr(version) + ')\n'
            '    raise SystemExit(' + str(status) + ')\n'
            'if "-i" in sys.argv:\n'
            '    for argument in sys.argv[1:]:\n'
            '        if argument.endswith(".c"):\n'
            '            Path(argument).write_text("unexpected rewrite\\n")\n')
        path.chmod(0o755)
        return str(path)

    def calls(self):
        return [json.loads(line) for line in self.log.read_text().splitlines()]

    def test_shell_uses_actual_entrypoints_and_the_declared_interpreter(self):
        for name in ('githooks/commit-msg', 'packaging/service/S99mybot', 'packaging/network/99-mybot-lease',
                     'packaging/example.env', 'packaging/service/mybot-rv1126b.conf'):
            with self.subTest(name=name):
                self.assertEqual(quality.shell_interpreter(ROOT / name, name), 'sh')
        path = self.root / 'entry'
        for header, expected in [('#!/bin/sh', 'sh'), ('#!/usr/bin/env -S bash -e', 'bash'),
                                 ('#!/usr/bin/env python3', None), ('no shebang', None)]:
            path.write_text(header + '\n')
            self.assertEqual(quality.shell_interpreter(path, 'entry'), expected)

    def test_spacing_checks_multiline_definitions_and_leading_comments(self):
        function = 'void first(void) {\n    puts("/* } */");\n}\n'
        next_function = '/* documentation, including (); */\nstatic char *\nsecond(\n    int value) {\n}\n'
        self.assertEqual(quality.missing_function_spacing(function + next_function), [4])
        self.assertEqual(quality.missing_function_spacing(function + '\n' + next_function), [])
        self.assertEqual(quality.missing_function_spacing(function + 'static int prototype(int);\n'
                                                         + next_function), [])

    def test_pinned_major_accepts_upstream_and_distribution_version_strings(self):
        executable = self.root / 'formatter'
        for version in ('clang-format version 11.0.0',
                        'Ubuntu clang-format version 11.0.1-2 (distribution build)'):
            with self.subTest(version=version):
                self.formatter(executable, version)
                self.assertEqual(quality.checked_formatter(str(executable), self.root), str(executable))

    def test_project_virtual_environment_takes_precedence_over_system_path(self):
        local = self.formatter(self.root / 'local/dev-tools/bin/clang-format')
        system = self.root / 'system'
        self.formatter(system / 'clang-format', 'Ubuntu clang-format version 18.1.3')
        with patch.dict(os.environ, PATH=str(system)):
            self.assertEqual(quality.checked_formatter(None, self.root), local)

    def test_explicit_override_takes_precedence_over_project_virtual_environment(self):
        self.formatter(self.root / 'local/dev-tools/bin/clang-format', 'clang-format version 18.1.3')
        explicit = self.formatter(self.root / 'explicit formatter')
        self.assertEqual(quality.checked_formatter(explicit, self.root), explicit)

    def test_system_path_remains_available_when_project_tools_are_not_installed(self):
        system = self.root / 'system'
        self.formatter(system / 'clang-format')
        with patch.dict(os.environ, PATH=str(system)):
            self.assertEqual(quality.checked_formatter(None, self.root), 'clang-format')

    def test_wrong_version_explains_how_to_install_pinned_tools(self):
        executable = self.formatter(self.root / 'formatter', 'Ubuntu clang-format version 18.1.3')
        with self.assertRaisesRegex(ValueError, 'clang-format 11 is required') as failure:
            quality.checked_formatter(executable, self.root)
        self.assertIn('python3 -m venv local/dev-tools', str(failure.exception))
        self.assertIn('pip install -r requirements-dev.txt', str(failure.exception))
        self.assertEqual(self.calls(), [['--version']])

    def test_missing_or_unexecutable_formatter_has_actionable_error(self):
        executable = self.root / 'formatter'
        for present in (False, True):
            with self.subTest(present=present):
                if present:
                    executable.write_text('not executable\n')
                    executable.chmod(0o644)
                with self.assertRaisesRegex(ValueError, 'Cannot run formatter') as failure:
                    quality.checked_formatter(str(executable), self.root)
                self.assertIn('local/dev-tools/bin/python', str(failure.exception))

    def test_broken_project_tool_does_not_silently_fall_back(self):
        local = self.root / 'local/dev-tools/bin/clang-format'
        local.parent.mkdir(parents=True)
        local.symlink_to(self.root / 'missing-formatter')
        with self.assertRaisesRegex(ValueError, 'Cannot run formatter'):
            quality.checked_formatter(None, self.root)

    def test_unknown_or_failed_version_probe_is_rejected(self):
        executable = self.root / 'formatter'
        for version, status in (('unknown tool 11', 0), ('clang-format version 11.0.1', 1)):
            with self.subTest(version=version, status=status):
                self.formatter(executable, version, status)
                with self.assertRaisesRegex(ValueError, 'Cannot identify formatter version'):
                    quality.checked_formatter(str(executable), self.root)

    def test_timed_out_probe_reports_setup_instructions(self):
        with patch.object(quality.subprocess, 'run', side_effect=subprocess.TimeoutExpired('formatter', 10)):
            with self.assertRaisesRegex(ValueError, 'Cannot run formatter') as failure:
                quality.checked_formatter('formatter', self.root)
        self.assertIn('python3 -m venv local/dev-tools', str(failure.exception))

    def test_wrong_version_fix_mode_cannot_rewrite_any_sources(self):
        executable = self.formatter(self.root / 'formatter', 'clang-format version 18.1.3')
        before = self.source.read_bytes()
        arguments = ['check-quality.py', '--fix-format', '--clang-format', executable]
        output = io.StringIO()
        with patch.object(quality, 'ROOT', self.root), patch.object(sys, 'argv', arguments):
            with patch.object(quality, 'maintained_files', side_effect=AssertionError('sources were accessed')):
                with redirect_stderr(output), redirect_stdout(output):
                    with self.assertRaises(SystemExit) as failure:
                        quality.main()
        self.assertEqual(failure.exception.code, 2)
        self.assertIn('clang-format 11 is required', output.getvalue())
        self.assertEqual(self.calls(), [['--version']])
        self.assertEqual(self.source.read_bytes(), before)

    def test_documentation_version_drift_stops_quality_gate_before_formatting(self):
        executable = self.formatter(self.root / 'formatter')
        scripts = self.root / 'scripts'
        scripts.mkdir()
        for name in ('project-version.py', 'build_metadata.py'):
            shutil.copyfile(ROOT / 'scripts' / name, scripts / name)
        (self.root / 'CMakeLists.txt').write_text('project(mybot_rv1126b VERSION 0.5.1 LANGUAGES C CXX)\n')
        for name in ('README.md', 'README.zh-CN.md'):
            (self.root / name).write_text('<!-- project-version -->0.2.2<!-- /project-version -->')
        before = self.source.read_bytes()
        arguments = ['check-quality.py', '--fix-format', '--clang-format', executable]
        output = io.StringIO()
        with patch.object(quality, 'ROOT', self.root), patch.object(sys, 'argv', arguments):
            with patch.object(quality, 'maintained_files', side_effect=AssertionError('sources were accessed')):
                with redirect_stderr(output), redirect_stdout(output), self.assertRaises(SystemExit):
                    quality.main()
        self.assertIn('current version differs from CMake', output.getvalue())
        self.assertEqual(self.calls(), [['--version']])
        self.assertEqual(self.source.read_bytes(), before)


if __name__ == '__main__':
    unittest.main()
