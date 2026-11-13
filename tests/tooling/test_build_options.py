# SPDX-License-Identifier: Apache-2.0
"""Variant selection must be independent of flag spelling and argument order."""
import json
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / 'scripts/build-options.sh'


class BuildOptionsTests(unittest.TestCase):
    def parse(self, *args):
        result = subprocess.run(
            ['sh', '-c', '. "$1"; shift; rv1126b_parse_options "$@" || exit $?; '
             'printf "%s %s %s" "$profile_language" "$profile_video" "$profile_ubsan"',
             'parse', str(SCRIPT), *args], capture_output=True, text=True)
        return result

    def test_defaults(self):
        self.assertEqual(self.parse().stdout, 'zh-CN OFF OFF')

    def test_typed_and_alternate_booleans(self):
        self.assertEqual(self.parse('-DMYBOT_ENABLE_VIDEO:BOOL=true', '-DRV1126B_LANGUAGE:STRING=en-US').stdout,
                         'en-US ON OFF')

    def test_combination_order(self):
        a = '-DRV1126B_ENABLE_UBSAN=ON'
        b = '-DRV1126B_LANGUAGE=en-US'
        self.assertEqual(self.parse(a, b).stdout, self.parse(b, a).stdout)
        self.assertEqual(self.parse(a, b).stdout, 'en-US OFF ON')

    def test_last_value_wins(self):
        self.assertEqual(self.parse('-DMYBOT_ENABLE_VIDEO=TRUE', '-DMYBOT_ENABLE_VIDEO=OFF').stdout,
                         'zh-CN OFF OFF')

    def test_separate_definition_argument(self):
        self.assertEqual(self.parse('-D', 'RV1126B_LANGUAGE:STRING=en-US').stdout, 'en-US OFF OFF')

    def test_invalid_selectors_fail(self):
        self.assertNotEqual(self.parse('-DRV1126B_LANGUAGE=fr-FR').returncode, 0)
        self.assertNotEqual(self.parse('-DMYBOT_ENABLE_VIDEO=perhaps').returncode, 0)


class BuildScriptDependenciesTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix='mybot-build-script-test-')
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name).resolve()
        scripts = self.root / 'scripts'
        scripts.mkdir()
        for name in ('build-rv1126b.sh', 'build-options.sh'):
            (scripts / name).write_bytes((ROOT / 'scripts' / name).read_bytes())
        tools = self.root / 'tools'
        tools.mkdir()
        cmake = tools / 'cmake'
        cmake.write_text('#!' + sys.executable + '\nimport json, os, sys\n'
                         'print(json.dumps({"args": sys.argv[1:], '
                         '"sdk": os.environ.get("AGORA_SDK_DIR"), '
                         '"library": os.environ.get("AGORA_RTC_LIBRARY")}))\n')
        cmake.chmod(0o755)
        self.environment = os.environ.copy()
        for name in ('CROSS_COMPILE', 'RV1126B_STAGING', 'PYTHON'):
            self.environment.pop(name, None)
        self.environment['PATH'] = str(tools) + os.pathsep + self.environment['PATH']
        self.environment['AGORA_SDK_DIR'] = str(self.root / 'external-sdk')
        self.environment['AGORA_RTC_LIBRARY'] = str(self.root / 'external.so')

    def build(self):
        result = subprocess.run(['sh', str(self.root / 'scripts/build-rv1126b.sh')],
                                env=self.environment, capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        calls = [json.loads(line) for line in result.stdout.splitlines()]
        self.assertEqual(len(calls), 2)
        sdk = str(self.root / 'vendor/rtsa/agora_sdk')
        library = sdk + '/lib/aarch64/libagora-rtc-sdk.so'
        self.assertIn('-DAGORA_SDK_DIR=' + sdk, calls[0]['args'])
        self.assertIn('-DAGORA_RTC_LIBRARY=' + library, calls[0]['args'])
        for call in calls:
            self.assertEqual(call['sdk'], sdk)
            self.assertEqual(call['library'], library)

    def test_build_uses_repository_rtsa_despite_external_environment(self):
        self.environment.update(CROSS_COMPILE='aarch64-linux-gnu-', RV1126B_STAGING=str(self.root / 'staging'))
        self.build()

    def test_build_uses_repository_rtsa_despite_stale_local_configuration(self):
        destination = self.root / 'local/sdk'
        destination.mkdir(parents=True)
        values = {'CROSS_COMPILE': 'aarch64-linux-gnu-', 'RV1126B_STAGING': str(self.root / 'staging'),
                  'AGORA_SDK_DIR': str(self.root / 'stale-sdk'), 'AGORA_RTC_LIBRARY': str(self.root / 'stale.so')}
        (destination / 'environment.sh').write_text(''.join(
            'export ' + key + '=' + shlex.quote(value) + '\n' for key, value in values.items()))
        self.build()


if __name__ == '__main__':
    unittest.main()
