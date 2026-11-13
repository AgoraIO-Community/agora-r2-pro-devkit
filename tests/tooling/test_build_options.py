# SPDX-License-Identifier: Apache-2.0
"""Variant selection must be independent of flag spelling and argument order."""
from pathlib import Path
import subprocess
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


if __name__ == '__main__':
    unittest.main()
