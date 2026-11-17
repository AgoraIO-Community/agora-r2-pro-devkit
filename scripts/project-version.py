#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Read or update the CMake version and its marked current documentation references."""

import argparse
from pathlib import Path
import re
import sys

from build_metadata import project_version

ROOT = Path(__file__).resolve().parents[1]
VERSION_DOCUMENTS = ('README.md', 'README.zh-CN.md')
VERSION_PATTERN = re.compile(r'(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)')
CMAKE_VERSION = re.compile(r'(project\(mybot_rv1126b\s+VERSION\s+)([0-9]+\.[0-9]+\.[0-9]+)')
START = '<!-- project-version -->'
END = '<!-- /project-version -->'


def version_documents(root):
    result = []
    for name in VERSION_DOCUMENTS:
        path = Path(root) / name
        text = path.read_text(encoding='utf-8')
        if text.count(START) != 1 or text.count(END) != 1:
            raise ValueError(f'{name}: expected one current project-version marker pair')
        start = text.index(START) + len(START)
        end = text.index(END)
        if end < start:
            raise ValueError(f'{name}: project-version markers are out of order')
        result.append((path, text, start, end))
    return result


def check_version(root):
    expected = project_version(root)
    errors = []
    for path, text, start, end in version_documents(root):
        if text[start:end] != expected:
            errors.append(f'{path.name}: current version differs from CMake ({expected}); '
                          f'run python3 scripts/project-version.py --set {expected}')
    return errors


def set_version(root, version):
    if not VERSION_PATTERN.fullmatch(version):
        raise ValueError('Version must use MAJOR.MINOR.PATCH without leading zeroes')
    cmake = Path(root) / 'CMakeLists.txt'
    original = cmake.read_text(encoding='utf-8')
    if len(CMAKE_VERSION.findall(original)) != 1:
        raise ValueError('Expected one authoritative CMake project version')
    updated = CMAKE_VERSION.sub(lambda match: match[1] + version, original)
    # Validate every reference before changing any file. Historical records and
    # unmarked dependency versions deliberately remain untouched.
    updates = [(cmake, original, updated)]
    for path, text, start, end in version_documents(root):
        updates.append((path, text, text[:start] + version + text[end:]))
    for path, before, after in updates:
        if before != after:
            path.write_text(after, encoding='utf-8')


def main(argv=None, *, root=ROOT):
    parser = argparse.ArgumentParser(description=__doc__)
    action = parser.add_mutually_exclusive_group()
    action.add_argument('--check', action='store_true', help='Check current documentation versions')
    action.add_argument('--set', metavar='VERSION', help='Update CMake and marked current references')
    args = parser.parse_args(argv)
    try:
        if args.set is not None:
            set_version(root, args.set)
        if args.check:
            errors = check_version(root)
            if errors:
                print('\n'.join(errors), file=sys.stderr)
                return 1
            print('Project version references match CMake: ' + project_version(root))
        else:
            print(project_version(root))
    except (OSError, ValueError) as error:
        print(str(error), file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
