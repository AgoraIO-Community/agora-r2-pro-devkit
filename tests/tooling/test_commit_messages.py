# SPDX-License-Identifier: Apache-2.0
"""The local hook and CI validate the same complete commit message."""

import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import textwrap
import unittest

ROOT = Path(__file__).resolve().parents[2]
HOOK = ROOT / 'githooks/commit-msg'


class CommitMessageTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix='mybot-commit-test-')
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)

    def check(self, message, stdin):
        if stdin:
            return subprocess.run(['sh', str(HOOK), '--stdin'], input=message,
                                  capture_output=True, text=True)
        path = self.root / 'message.txt'
        path.write_text(message)
        return subprocess.run(['sh', str(HOOK), str(path)], capture_output=True, text=True)

    def test_hook_file_and_ci_stdin_accept_the_same_complete_messages(self):
        cases = [
            ('fix(input): handle short taps\n', True),
            ('feat(ui): add settings\n\nExplain behavior.\n', True),
            ('build(metadata)!: change manifest schema\n\nBREAKING CHANGE: Rebuild existing directories.\n', True),
            ('fix(api)!: change interface\n\nReason.\n\nBREAKING CHANGE: Use the new interface.\nMore details.\n', True),
            ('fix(api)!: change interface\n', False),
            ('fix(api)!: change interface\n\nBREAKING CHANGE:\n', False),
            ('fix(api)!: change interface\n\nReason.\nBREAKING CHANGE: Use the new interface.\n', False),
            ('fix(api): change interface\n\nBREAKING CHANGE: Use the new interface.\n', False),
            ('fix(api)!: change interface\n\nBREAKING CHANGE: First.\n\nBREAKING CHANGE: Second.\n', False),
            ('fix(api): explain behavior\n\nMention BREAKING CHANGE: as ordinary text.\n', True),
            ('fix(api): explain behavior\nMissing separator.\n', False),
            ('unsupported: change behavior\n', False),
            ('fix: ' + 'x' * 68 + '\n', False),
            ('Merge branch "dev"\n', True),
            ('Revert "fix: previous change"\n', True),
        ]
        for message, accepted in cases:
            for stdin in (False, True):
                with self.subTest(message=message, stdin=stdin):
                    result = self.check(message, stdin)
                    self.assertEqual(result.returncode == 0, accepted, result.stderr)

    def test_ci_workflow_checks_complete_messages_for_push_pr_and_dispatch(self):
        # Execute the actual workflow shell block; no YAML dependency or copied
        # validator logic is needed to check the integration.
        workflow = (ROOT / '.github/workflows/ci.yml').read_text()
        step = workflow.split('      - name: Validate commit messages\n', 1)[1]
        script = textwrap.dedent(step.split('        run: |\n', 1)[1].split('\n  host:', 1)[0])
        (self.root / 'githooks').mkdir()
        shutil.copyfile(HOOK, self.root / 'githooks/commit-msg')
        (self.root / 'githooks/commit-msg').chmod(0o755)
        (self.root / 'disabled-hooks').mkdir()
        subprocess.run(['git', 'init', '-q', str(self.root)], check=True)

        def commit(message):
            path = self.root / 'message.txt'
            path.write_text(message)
            subprocess.run(['git', '-C', str(self.root), '-c', 'user.name=Fixture',
                            '-c', 'user.email=fixture@example.invalid', '-c',
                            'core.hooksPath=' + str(self.root / 'disabled-hooks'),
                            'commit', '--allow-empty', '-q', '--file', str(path)], check=True)
            return subprocess.check_output(['git', '-C', str(self.root), 'rev-parse', 'HEAD'], text=True).strip()

        def check(event, base, head):
            environment = {**os.environ, 'EVENT_NAME': event, 'BASE_SHA': base,
                           'BEFORE_SHA': base, 'HEAD_SHA': head}
            return subprocess.run(['bash', '-c', script], cwd=self.root, env=environment,
                                  capture_output=True, text=True)

        baseline = commit('chore: initialize fixture\n')
        bad = commit('fix(api)!: change interface\n')
        for event in ('push', 'pull_request', 'workflow_dispatch'):
            with self.subTest(event=event, valid=False):
                self.assertNotEqual(check(event, baseline, bad).returncode, 0)
        good = commit('fix(api)!: change interface\n\nBREAKING CHANGE: Use the new interface.\n')
        for event in ('push', 'pull_request', 'workflow_dispatch'):
            with self.subTest(event=event, valid=True):
                result = check(event, bad, good)
                self.assertEqual(result.returncode, 0, result.stderr)


if __name__ == '__main__':
    unittest.main()
