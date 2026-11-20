#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Check maintained source formatting, syntax, notices, whitespace, and local links."""
import argparse
import ast
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
GENERATED = ('src/ui/fonts/generated/',)
DATA_FILES = {'pixel_expression_masks.h', 'pixel_expression_shapes.h'}


# Configuration files sourced by the launcher and its operator examples.
SOURCED_SHELL = {'packaging/example.env', 'packaging/service/mybot-rv1126b.conf',
                 'packaging/network/99-mybot-lease'}


def shell_interpreter(path, name):
    if name in SOURCED_SHELL:
        return 'sh'
    with path.open('rb') as stream:
        first_line = stream.readline(256).rstrip()
    match = re.fullmatch(rb'#!\s*(?:/[^\s]*/)?(?:env\s+(?:-S\s+)?)?(sh|bash)(?:\s+[^\r\n]*)?', first_line)
    if match:
        return match[1].decode('ascii')
    return 'sh' if path.suffix in ('.sh', '.script') else None


def checked_formatter(requested, root=ROOT):
    """Select the project formatter and reject a different formatting baseline."""
    requirements = (root / 'requirements-dev.txt').read_text()
    pin = re.search(r'^clang-format==(\d+)(?:\.\d+)+$', requirements, re.M)
    if not pin:
        raise ValueError('requirements-dev.txt must pin the clang-format baseline')
    baseline = int(pin.group(1))
    local = root / 'local/dev-tools/bin/clang-format'
    command = requested or (str(local) if local.exists() or local.is_symlink() else 'clang-format')
    setup = ('Create the pinned project tools from the repository root:\n'
             '  python3 -m venv local/dev-tools\n'
             '  local/dev-tools/bin/python -m pip install -r requirements-dev.txt\n'
             'Then rerun scripts/check-quality.py; it selects that formatter automatically.\n'
             'Use --clang-format PATH to select another clang-format ' + str(baseline) + ' executable.')
    try:
        result = subprocess.run([command, '--version'], capture_output=True, text=True, timeout=10)
    except (OSError, subprocess.TimeoutExpired) as error:
        raise ValueError(f'Cannot run formatter {command}: {error}\n{setup}') from error
    reported = (result.stdout + '\n' + result.stderr).strip()
    version = re.search(r'\bclang-format version\s+(\d+)(?:\.\d+)*\b', reported)
    if result.returncode or not version:
        raise ValueError(f'Cannot identify formatter version for {command}: {reported}\n{setup}')
    if int(version.group(1)) != baseline:
        raise ValueError(f'clang-format {baseline} is required; {command} reports {reported}.\n{setup}')
    return command


def maintained_files():
    names = subprocess.check_output(
        ['git', 'ls-files', '--cached', '--others', '--exclude-standard', '-z'], cwd=ROOT
    ).decode().split('\0')
    return sorted({ROOT / name for name in names if name and (ROOT / name).is_file()
                   and not name.startswith(('vendor/', 'build', 'local/', 'dist/'))})


def missing_function_spacing(source):
    """Find adjacent definitions in clang-formatted C, including leading documentation."""
    tokens = re.compile(r"""/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*'""", re.S)
    # Keep offsets and line breaks while ignoring braces and parentheses inside comments or strings.
    masked = tokens.sub(lambda match: re.sub(r'[^\n]', ' ', match[0]), source)
    # A top-level closing brace is at column zero. Declarations, initializers and directives
    # terminate the lookahead before the next function body.
    boundaries = re.finditer(r'^}[ \t]*\n(?=\s*[A-Za-z_][^#;{}=]*\([^#;{}=]*\)\s*\{)', masked, re.M)
    return [source.count('\n', 0, match.end()) + 1 for match in boundaries
            if source[match.end():].partition('\n')[0].strip()]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--clang-format', metavar='PATH',
                        help='formatter executable (default: local/dev-tools/bin/clang-format, then PATH)')
    parser.add_argument('--fix-format', action='store_true')
    args = parser.parse_args()
    try:
        formatter = checked_formatter(args.clang_format, ROOT)
    except (OSError, ValueError) as error:
        parser.error(str(error))
    version = subprocess.run([sys.executable, str(ROOT / 'scripts/project-version.py'), '--check'],
                             capture_output=True, text=True)
    if version.returncode:
        parser.error(version.stderr.strip() or version.stdout.strip())
    files = maintained_files()
    errors = []
    formatted = []
    for path in files:
        name = path.relative_to(ROOT).as_posix()
        if path.suffix in ('.c', '.h', '.cc', '.cpp') and not name.startswith(GENERATED) and path.name not in DATA_FILES:
            formatted.append(path)
            if path.suffix in ('.c', '.h') and path.relative_to(ROOT).parts[0] in ('src', 'tests', 'tools'):
                for number in missing_function_spacing(path.read_text()):
                    errors.append(f'{name}:{number}: separate top-level C functions with a blank line')
            if 'SPDX-License-Identifier:' not in '\n'.join(path.read_text().splitlines()[:5]):
                errors.append(name + ': missing SPDX')
        if path.suffix == '.py':
            try:
                ast.parse(path.read_text(), filename=name)
            except SyntaxError as error:
                errors.append(str(error))
        shell = shell_interpreter(path, name)
        if shell:
            result = subprocess.run([shell, '-n', str(path)], capture_output=True, text=True)
            if result.returncode:
                errors.append(result.stderr.strip())
        if path.suffix in ('.md', '.py', '.sh', '.script', '.cmake', '.json', '.yml', '.yaml') or path.name == 'CMakeLists.txt':
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
        result = subprocess.run([formatter, '-i' if args.fix_format else '--dry-run',
                                 *([] if args.fix_format else ['--Werror']),
                                 *map(str, formatted)], capture_output=True, text=True)
        if result.returncode:
            errors.append(result.stderr.strip())
    # Formatting may change a manual input; validate the resulting source bytes.
    manual = subprocess.run([sys.executable, str(ROOT / 'scripts/check-device-manual.py'), '--check'],
                            capture_output=True, text=True)
    if manual.returncode:
        errors.append(manual.stderr.strip() or manual.stdout.strip())
    if errors:
        print('\n'.join(errors))
        raise SystemExit(1)
    print(f'Quality checks passed: {len(files)} maintained files, {len(formatted)} formatted sources.')


if __name__ == '__main__':
    main()
