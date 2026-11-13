#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Compare deterministic workflow/expression frames with the accepted UI baseline."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--language', choices=('zh-CN', 'en-US'), required=True)
    args = parser.parse_args()
    expected = json.loads((ROOT / 'tests/support/ui-preview-sha256.json').read_text())['languages'][args.language]
    with tempfile.TemporaryDirectory(prefix='mybot-ui-baseline-') as temporary:
        subprocess.run([str(args.binary.resolve()), temporary], check=True, stdout=subprocess.DEVNULL)
        actual = {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in Path(temporary).glob('*.ppm')}
    changed = sorted(name for name in expected.keys() | actual.keys() if expected.get(name) != actual.get(name))
    if changed:
        raise SystemExit('UI baseline mismatch: ' + ', '.join(changed))
    print(f'{args.language}: {len(actual)} frames match the accepted 0.1.1 baseline byte for byte.')


if __name__ == '__main__':
    main()
