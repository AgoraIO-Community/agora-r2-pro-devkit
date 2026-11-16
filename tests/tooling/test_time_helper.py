# SPDX-License-Identifier: Apache-2.0
"""Validate the optional wrapper with a fake BusyBox, never a real time setter."""

import os
from pathlib import Path
import shlex
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class TimeHelperTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.directory = Path(temporary.name)
        self.busybox = self.directory / 'fake busybox'
        self.busybox.write_text('#!/bin/sh\nprintf "%s\\n" "$@"\n')
        self.busybox.chmod(0o755)
        # Replace only the fixed executable in an isolated copy. Every test
        # exercises production validation and argument passing without NTP I/O.
        script = (ROOT / 'packaging/time/sync-busybox.sh').read_text()
        executable = shlex.quote(str(self.busybox))
        self.assertIn('[ -x /bin/busybox ]', script)
        self.assertIn('exec /bin/busybox ntpd', script)
        script = script.replace('[ -x /bin/busybox ]', '[ -x ' + executable + ' ]')
        script = script.replace('exec /bin/busybox ntpd', 'exec ' + executable + ' ntpd')
        self.wrapper = self.directory / 'sync-time.sh'
        self.wrapper.write_text(script)

    def run_helper(self, server=None, args=()):
        environment = dict(os.environ)
        environment.pop('RV1126B_NTP_SERVER', None)
        if server is not None:
            environment['RV1126B_NTP_SERVER'] = server
        return subprocess.run(['sh', str(self.wrapper), *args], env=environment,
                              capture_output=True, text=True, timeout=3)

    def test_hosts_and_default_are_passed_as_one_argument(self):
        for host in (None, 'pool.ntp.org', 'ntp.example.', '192.168.1.1', 'time-server.local'):
            with self.subTest(host=host):
                result = self.run_helper(host)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(result.stdout.splitlines(),
                                 ['ntpd', '-n', '-q', '-p', host or 'pool.ntp.org'])

    def test_invalid_hosts_never_execute_busybox(self):
        for host in ('-p', 'host name', 'host\nname', 'a;touch /tmp/unexpected',
                     '$(id)', '`id`', 'https://pool.ntp.org', 'host:123', '::1',
                     '.host', 'a..b', '-host', 'host-', 'a.' + 'b' * 64,
                     '999.0.0.1', '127.0.0', '1.2.3.4.5', 'a' * 254):
            with self.subTest(host=host):
                result = self.run_helper(host)
                self.assertNotEqual(result.returncode, 0)
                self.assertEqual(result.stdout, '')

    def test_extra_arguments_are_rejected(self):
        result = self.run_helper(args=('--daemonize',))
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(result.stdout, '')


if __name__ == '__main__':
    unittest.main()
