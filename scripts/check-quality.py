#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Check maintained source formatting, syntax, notices, whitespace, and local links."""
import argparse
import ast
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]
GENERATED = ('src/ui/fonts/generated/',)
DATA_FILES = {'pixel_expression_masks.h', 'pixel_expression_shapes.h'}


def maintained_files():
    names = subprocess.check_output(
        ['git', 'ls-files', '--cached', '--others', '--exclude-standard', '-z'], cwd=ROOT
    ).decode().split('\0')
    return sorted({ROOT / name for name in names if name and (ROOT / name).is_file()
                   and not name.startswith(('vendor/', 'build', 'local/', 'dist/'))})


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--clang-format', default='clang-format')
    parser.add_argument('--fix-format', action='store_true')
    args = parser.parse_args()
    files = maintained_files()
    errors = []
    formatted = []
    for path in files:
        name = path.relative_to(ROOT).as_posix()
        if path.suffix in ('.c', '.h', '.cc', '.cpp') and not name.startswith(GENERATED) and path.name not in DATA_FILES:
            formatted.append(path)
            if 'SPDX-License-Identifier:' not in '\n'.join(path.read_text().splitlines()[:5]):
                errors.append(name + ': missing SPDX')
        if path.suffix == '.py':
            try:
                ast.parse(path.read_text(), filename=name)
            except SyntaxError as error:
                errors.append(str(error))
        if path.suffix == '.sh':
            result = subprocess.run(['sh', '-n', str(path)], capture_output=True, text=True)
            if result.returncode:
                errors.append(result.stderr.strip())
        if path.suffix in ('.md', '.py', '.sh', '.cmake', '.json', '.yml', '.yaml') or path.name == 'CMakeLists.txt':
            for no, line in enumerate(path.read_text().splitlines(), 1):
                if line.rstrip() != line:
                    errors.append(f'{name}:{no}: trailing whitespace')
        if path.suffix == '.md':
            # Code examples may contain illustrative links; validate prose only.
            prose = re.sub(r'```[^\n]*\n.*?```', '', path.read_text(), flags=re.S)
            for target in re.findall(r'\]\(([^)]+)\)', prose):
                target = target.strip('<>').split('#', 1)[0]
                if not target or re.match(r'[a-zA-Z][a-zA-Z0-9+.-]*:', target):
                    continue
                if not (path.parent / target).exists():
                    errors.append(f'{name}: missing local link {target}')
    if formatted:
        result = subprocess.run([args.clang_format, '-i' if args.fix_format else '--dry-run',
                                 *([] if args.fix_format else ['--Werror']),
                                 *map(str, formatted)], capture_output=True, text=True)
        if result.returncode:
            errors.append(result.stderr.strip())
    if errors:
        print('\n'.join(errors))
        raise SystemExit(1)
    print(f'Quality checks passed: {len(files)} maintained files, {len(formatted)} formatted sources.')


if __name__ == '__main__':
    main()
