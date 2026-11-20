# SPDX-License-Identifier: Apache-2.0
"""Exercise the init script and its launcher together on a temporary release."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class ServiceStartTests(unittest.TestCase):
    def test_checksum_once_pins_release_and_propagates_failure(self):
        with tempfile.TemporaryDirectory() as tmp:
            base = Path(tmp).resolve()
            service = base / 'etc/mybot-rv1126b'
            service.mkdir(parents=True)
            release = base / 'userdata/mybot/releases/v1'
            release.mkdir(parents=True)
            other = release.with_name('v2')
            other.mkdir()
            current = base / 'userdata/mybot/current'
            current.symlink_to(release)
            tools = base / 'tools'
            tools.mkdir()
            config = base / 'etc/mybot-rv1126b.conf'
            config.write_text('MYBOT_ENABLED=1\n')
            config.chmod(0o600)
            for name in ('SHA256SUMS', 'run.sh'):
                (release/name).touch()
                (release/name).chmod(0o755)

            def script(path, source):
                path.write_text('#!/bin/sh\n' + source)
                path.chmod(0o755)

            script(tools/'id', 'echo 0\n')
            script(tools/'stat', 'echo 0:600\n')
            script(base/'poweroff', 'exit 1\n')
            (base/'mounts').write_text('device /userdata ext4 rw 0 0\n')
            script(service/'poweroff.sh', 'exit 1\n')
            script(tools/'sha256sum', """
pwd -P >> "$FIXTURE/checks"
[ -e "$FIXTURE/accept" ] || exit 1
rm "$FIXTURE/userdata/mybot/current"
ln -s "$FIXTURE/userdata/mybot/releases/v2" "$FIXTURE/userdata/mybot/current"
""")
            script(service/'mybot-supervisor', """
case "$1" in
    status) [ -e "$FIXTURE/started" ] && exit 0; exit 3;;
    supervise) for last do :; done; echo "$last" > "$FIXTURE/started";;
esac
""")
            for name in ('S99mybot', 'run-service.sh'):
                original = (ROOT/'packaging/service'/name).read_text()
                for old in ('/etc/mybot-rv1126b', '/userdata/mybot', '/run/mybot-rv1126b'):
                    original = original.replace(old, str(base) + old)
                (base/'run').mkdir(exist_ok=True)
                original = original.replace('/proc/mounts', str(base/'mounts')).replace('/sbin/poweroff', str(base/'poweroff'))
                script(service/name, original)
            env = dict(os.environ, PATH=str(tools)+':'+os.defpath, FIXTURE=str(base))
            start = ['sh', str(service/'S99mybot'), 'start']
            rejected = subprocess.run(start, env=env, capture_output=True, text=True, timeout=5)
            self.assertNotEqual(rejected.returncode, 0)
            self.assertIn('checksum', rejected.stderr)
            self.assertFalse((base/'started').exists())
            (base/'accept').touch()
            subprocess.run(start, env=env, capture_output=True, check=True, timeout=5)
            self.assertEqual((base/'started').read_text().strip(), str(release))
            self.assertEqual((base/'checks').read_text().splitlines(), [str(release), str(release)])
