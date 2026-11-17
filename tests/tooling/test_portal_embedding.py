# SPDX-License-Identifier: Apache-2.0
"""Generated portal literals preserve bytes, token substitution and C99 limits."""

import importlib.util
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / 'scripts/embed-wifi-portal.py'
SPEC = importlib.util.spec_from_file_location('embed_wifi_portal', SCRIPT)
embedding = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(embedding)


class PortalEmbeddingTests(unittest.TestCase):
    def test_compiled_c99_roundtrip_preserves_unicode_escaping_and_token_boundaries(self):
        fixtures = [b'', b'"quoted" \\ path ??/ trigraph\n\t\r',
                    '网络 ✓ 🔒 @TOKEN@\n'.encode(),
                    b'x' * 1020 + b'@TOKEN@' + b'y' * 4097 + b'@TOKEN@']
        with tempfile.TemporaryDirectory(prefix='mybot-portal-embedding-') as name:
            directory = Path(name)
            (directory / 'roundtrip.c').write_text(
                '#include <stdio.h>\n#include <string.h>\n#include "page.h"\n'
                'int main(void) {\n'
                '  for (size_t i = 0; i < sizeof(kPortalPage) / sizeof(kPortalPage[0]); ++i)\n'
                '    if (fwrite(kPortalPage[i], 1, strlen(kPortalPage[i]), stdout) != strlen(kPortalPage[i])) return 1;\n'
                '  return 0;\n}\n')
            for data in fixtures:
                with self.subTest(data_length=len(data)):
                    parts = list(embedding.chunks(data))
                    self.assertTrue(all(len(part) <= 1024 for part in parts))
                    self.assertEqual(b''.join(parts), data)
                    self.assertEqual(sum(part.count(b'@TOKEN@') for part in parts), data.count(b'@TOKEN@'))
                    (directory / 'page.h').write_text(embedding.generate(data))
                    subprocess.run(['cc', '-std=c99', '-Werror', '-Wtrigraphs',
                                    str(directory / 'roundtrip.c'), '-o', str(directory / 'roundtrip')],
                                   check=True, capture_output=True, text=True)
                    self.assertEqual(subprocess.check_output([str(directory / 'roundtrip')]), data)

    def test_repeat_generation_keeps_header_unchanged_and_bad_input_cannot_replace_it(self):
        with tempfile.TemporaryDirectory(prefix='mybot-portal-embedding-') as name:
            directory = Path(name)
            source, output = directory / 'portal.html', directory / 'generated/page.h'
            source.write_bytes(b'<html>@TOKEN@</html>\n')
            command = [sys.executable, str(SCRIPT), '--input', str(source), '--output', str(output)]
            subprocess.run(command, check=True, capture_output=True)
            original, modified = output.read_bytes(), output.stat().st_mtime_ns
            subprocess.run(command, check=True, capture_output=True)
            self.assertEqual(output.read_bytes(), original)
            self.assertEqual(output.stat().st_mtime_ns, modified)
            for invalid in (b'bad\0input', b'bad\xffinput'):
                source.write_bytes(invalid)
                result = subprocess.run(command, capture_output=True)
                self.assertNotEqual(result.returncode, 0)
                self.assertEqual(output.read_bytes(), original)


if __name__ == '__main__':
    unittest.main()
