# SPDX-License-Identifier: Apache-2.0
"""Exercise the shutdown helper against private fake sysfs and a fake poweroff."""
import json
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
HELPER = ROOT / "packaging/service/poweroff.sh"


class PoweroffHelperTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.base = Path(self.temporary.name).resolve()
        self.backlight = self.base / "physical"
        self.backlight.mkdir()
        self.backlight.chmod(0o755)
        self.alias = self.base / "backlight"
        self.alias.symlink_to(self.backlight)
        self.binaries = self.base / "bin"
        self.binaries.mkdir()
        self.options = {}
        for name, text, mode in (("brightness", "200", 0o644), ("bl_power", "0", 0o644),
                                 ("max_brightness", "255", 0o444), ("actual_brightness", "200", 0o444)):
            self.attribute(name, text, mode)
        driver = self.base / "fixture.py"
        driver.write_text("""import json,os,pathlib,signal,sys
base=pathlib.Path(__file__).parent
light=base/'physical'
options=json.loads((base/'options.json').read_text())
operation=sys.argv[1]
arguments=sys.argv[2:]
if operation=='id':
    print(1000 if options.get('nonroot') else 0)
elif operation=='readlink':
    print(pathlib.Path(arguments[-1]).resolve())
elif operation=='stat':
    path=pathlib.Path(arguments[-1])
    owner=1000 if options.get('wrong_owner') and path.name=='brightness' else 0
    print('%d:0:%o' % (owner,path.stat().st_mode & 0o777))
elif operation=='cat':
    path=pathlib.Path(arguments[-1])
    if path.name=='actual_brightness' and (light/'brightness').read_text().strip()=='0':
        if options.get('actual_read_failure'):
            sys.exit(1)
        print('200' if options.get('actual_stuck') else '0')
    else:
        print(path.read_text(),end='')
elif operation=='poweroff':
    assert not arguments
    assert (light/'brightness').read_text().strip()=='0'
    assert (light/'bl_power').read_text().strip()=='4'
    (base/'poweroff-called').write_text('yes')
    if options.get('signal'):
        os.kill(os.getppid(),signal.SIGTERM)
        sys.exit(0 if options.get('signal_success') else 1)
    sys.exit(7 if options.get('poweroff_failure') else 0)
else:
    raise RuntimeError('Unknown fixture command')
""")
        for command in ("id", "readlink", "stat", "cat", "poweroff"):
            executable = self.binaries / command
            executable.write_text("#!/bin/sh\nexec " + shlex.quote(sys.executable) + " " +
                                  shlex.quote(str(driver)) + " " + command + ' "$@"\n')
            executable.chmod(0o755)
        source = HELPER.read_text()
        source = source.replace("PATH=/usr/sbin:/usr/bin:/sbin:/bin",
                                "PATH=" + shlex.quote(str(self.binaries)))
        source = source.replace("/sys/class/backlight/backlight", str(self.alias))
        source = source.replace("/sys/devices/platform/backlight/backlight/backlight", str(self.backlight))
        source = source.replace("/sbin/poweroff", str(self.binaries / "poweroff"))
        self.helper = self.base / "poweroff.sh"
        self.helper.write_text(source)
        self.helper.chmod(0o755)

    def attribute(self, name, value, mode=None):
        path = self.backlight / name
        previous = path.stat().st_mode & 0o777 if path.exists() else 0o644
        if path.exists():
            path.chmod(0o644)
        path.write_text(value + "\n")
        path.chmod(previous if mode is None else mode)

    def execute(self, *arguments, environment=None):
        (self.base / "options.json").write_text(json.dumps(self.options))
        return subprocess.run(["/bin/sh", str(self.helper), *arguments], text=True, capture_output=True,
                              env={**os.environ, **(environment or {})}, timeout=10)

    def assert_state(self, brightness="200", power="0"):
        self.assertEqual((self.backlight / "brightness").read_text().strip(), brightness)
        self.assertEqual((self.backlight / "bl_power").read_text().strip(), power)

    def test_success_leaves_backlight_off_after_poweroff_returns_zero(self):
        result = self.execute()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue((self.base / "poweroff-called").exists())
        self.assert_state("0", "4")

    def test_failed_poweroff_restores_the_previous_visible_state(self):
        self.options["poweroff_failure"] = True
        result = self.execute()
        self.assertNotEqual(result.returncode, 0)
        self.assertTrue((self.base / "poweroff-called").exists())
        self.assert_state()

    def test_signal_restores_the_previous_visible_state(self):
        self.options["signal"] = True
        result = self.execute()
        self.assertNotEqual(result.returncode, 0)
        self.assertTrue((self.base / "poweroff-called").exists())
        self.assert_state()

    def test_successful_poweroff_does_not_restore_on_shutdown_signal(self):
        self.options.update(signal=True, signal_success=True)
        result = self.execute()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue((self.base / "poweroff-called").exists())
        self.assert_state("0", "4")

    def test_signal_after_success_before_restore_flag_clear_does_not_restore(self):
        source = self.helper.read_text()
        boundary = "    restore=0\n    exit 0"
        self.assertEqual(source.count(boundary), 1)
        self.helper.write_text(source.replace(
            boundary, '    kill -TERM "$$"\n' + boundary, 1))
        result = self.execute()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue((self.base / "poweroff-called").exists())
        self.assert_state("0", "4")

    def test_preparation_signal_rolls_back_without_requesting_poweroff(self):
        source = self.helper.read_text()
        boundary = "printf '0\\n' > \"$backlight/brightness\"\n"
        self.assertEqual(source.count(boundary), 1)
        self.helper.write_text(source.replace(
            boundary, boundary + 'kill -TERM "$$"\n', 1))
        result = self.execute()
        self.assertEqual(result.returncode, 1, result.stderr)
        self.assertFalse((self.base / "poweroff-called").exists())
        self.assert_state()

    def test_repeated_signal_cannot_interrupt_backlight_rollback(self):
        self.options["poweroff_failure"] = True
        source = self.helper.read_text()
        boundary = "printf '%s\\n' \"$brightness\" > \"$backlight/brightness\" &&\n"
        self.assertEqual(source.count(boundary), 1)
        self.helper.write_text(source.replace(
            boundary, boundary + '               kill -TERM "$$" &&\n', 1))
        result = self.execute()
        self.assertEqual(result.returncode, 1, result.stderr)
        self.assertTrue((self.base / "poweroff-called").exists())
        self.assert_state()

    def test_failed_backlight_readback_restores_without_requesting_poweroff(self):
        for option in ("actual_stuck", "actual_read_failure"):
            with self.subTest(option=option):
                self.options = {option: True}
                result = self.execute()
                self.assertNotEqual(result.returncode, 0)
                self.assertFalse((self.base / "poweroff-called").exists())
                self.assert_state()

    def test_arguments_and_nonroot_are_rejected_before_writes(self):
        result = self.execute("--help")
        self.assertNotEqual(result.returncode, 0)
        self.assert_state()
        self.options["nonroot"] = True
        result = self.execute()
        self.assertNotEqual(result.returncode, 0)
        self.assert_state()
        self.assertFalse((self.base / "poweroff-called").exists())

    def test_invalid_values_are_rejected_before_writes(self):
        for name, value in (("brightness", "-1"), ("brightness", "256"),
                            ("max_brightness", "0"), ("max_brightness", "999999999999"),
                            ("actual_brightness", "bad"), ("bl_power", "5")):
            with self.subTest(attribute=name, value=value):
                original = (self.backlight / name).read_text().strip()
                self.attribute(name, value)
                result = self.execute()
                self.assertNotEqual(result.returncode, 0)
                self.assertFalse((self.base / "poweroff-called").exists())
                self.assertEqual((self.backlight / name).read_text().strip(), value)
                self.attribute(name, original)
                self.assert_state()

    def test_unsafe_permissions_owner_and_links_are_rejected_before_writes(self):
        for name in ("brightness", "bl_power", "actual_brightness", "max_brightness"):
            with self.subTest(attribute=name):
                path = self.backlight / name
                original = path.stat().st_mode & 0o777
                path.chmod(0o666)
                result = self.execute()
                self.assertNotEqual(result.returncode, 0)
                self.assert_state()
                path.chmod(original)
        self.options["wrong_owner"] = True
        self.assertNotEqual(self.execute().returncode, 0)
        self.assert_state()
        self.options.clear()
        path = self.backlight / "brightness"
        path.rename(self.base / "linked-brightness")
        path.symlink_to(self.base / "linked-brightness")
        self.assertNotEqual(self.execute().returncode, 0)
        self.assert_state()

    def test_missing_poweroff_is_rejected_without_darkening(self):
        (self.binaries / "poweroff").unlink()
        self.assertNotEqual(self.execute().returncode, 0)
        self.assert_state()

    def test_command_and_path_environment_cannot_override_fixed_controls(self):
        result = self.execute(environment={"MYBOT_POWEROFF_COMMAND": "/bin/false",
                                           "MYBOT_BACKLIGHT_PATH": "/missing",
                                           "PATH": "/missing"})
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue((self.base / "poweroff-called").exists())
        self.assert_state("0", "4")

    def test_service_uses_the_checked_poweroff_helper(self):
        service = (ROOT / "packaging/service/run-service.sh").read_text()
        self.assertIn('--poweroff-command "$service_dir/poweroff.sh"', service)
        self.assertNotIn("--poweroff-command /sbin/poweroff", service)


if __name__ == "__main__":
    unittest.main()
