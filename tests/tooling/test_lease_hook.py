# SPDX-License-Identifier: Apache-2.0
"""Exercise dhcpcd event records with a fake association, without networking."""

import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class LeaseHookTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.bin = self.root / 'bin'
        self.bin.mkdir()
        executable = self.bin / 'wpa_cli'
        executable.write_text('#!/bin/sh\nprintf "%s\\n" "$FAKE_ASSOCIATION"\n')
        executable.chmod(0o755)
        self.nonce = self.root / 'nonce'
        self.nonce.write_text('generation-one\n')
        script = (ROOT / 'packaging/network/99-mybot-lease').read_text()
        self.script = self.root / 'hook'
        self.script.write_text(script.replace('/proc/sys/kernel/random/uuid', str(self.nonce)))
        self.leases = self.root / 'leases'

    def event(self, reason='BOUND', interface='wlan0', address='192.0.2.10'):
        environment = dict(os.environ, PATH=str(self.bin) + os.pathsep + os.environ['PATH'],
                           MYBOT_DHCPCD_LEASE_DIR=str(self.leases), reason=reason,
                           interface=interface, new_ip_address=address,
                           FAKE_ASSOCIATION='bssid=02:11:22:33:44:55\nwpa_state=COMPLETED')
        return subprocess.run(['sh', str(self.script)], env=environment,
                              capture_output=True, text=True, timeout=3)

    def test_bound_renew_and_expire(self):
        result = self.event()
        self.assertEqual(result.returncode, 0, result.stderr)
        record = self.leases / 'wlan0.lease'
        self.assertIn('generation=generation-one\n', record.read_text())
        self.assertIn('ipv4=192.0.2.10\n', record.read_text())
        self.assertEqual(record.stat().st_mode & 0o777, 0o600)
        self.assertEqual(self.leases.stat().st_mode & 0o777, 0o700)
        self.nonce.write_text('generation-two\n')
        self.assertEqual(self.event('RENEW').returncode, 0)
        self.assertIn('generation=generation-two\n', record.read_text())
        self.assertEqual(self.event('EXPIRE').returncode, 0)
        self.assertFalse(record.exists())

    def test_ap_and_invalid_events_publish_nothing(self):
        for reason, interface, address in [('BOUND', 'mbap100', '192.0.2.10'),
                                           ('BOUND', '../../bad', '192.0.2.10'),
                                           ('BOUND', 'wlan0', '999.2.3.4'),
                                           ('BOUND6', 'wlan0', '192.0.2.10')]:
            result = self.event(reason, interface, address)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertFalse((self.leases / 'wlan0.lease').exists())

    def test_hook_does_not_exit_the_dhcpcd_parent(self):
        environment = dict(os.environ, reason='IGNORE', interface='')
        result = subprocess.run(['sh', '-c', '. "$1"; printf alive', 'hook-test', str(self.script)],
                                env=environment, capture_output=True, text=True, timeout=3)
        self.assertEqual(result.returncode, 0)
        self.assertEqual(result.stdout, 'alive')


if __name__ == '__main__':
    unittest.main()
