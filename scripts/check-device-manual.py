#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Check manual provenance with stdlib, or explicitly refresh/reproduce its assets."""

import argparse
from datetime import date
import hashlib
from importlib import metadata
import json
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
MANIFEST = 'docs/user/assets/manual-manifest.json'
LANGUAGES = {
    'zh-CN': ('device-manual.zh-CN.md', 'device-manual', 'docs/device-operations.zh-CN.pdf'),
    'en-US': ('device-manual.en.md', 'device-manual-en', 'docs/device-operations.en.pdf'),
}
UI_FILES = ('tools/ui_preview/main.c', 'tools/ui_preview/CMakeLists.txt',
            'src/platform/src/drivers/display/pixels.c', 'src/platform/src/drivers/display/pixels.h',
            'vendor/mybot/include/mybot/platform/mybot_lcd.h', 'cmake/dependencies/public-headers.cmake',
            'cmake/dependencies/lvgl.cmake', 'cmake/dependencies/lvgl-sources.cmake')
INPUT_FILES = ('CMakeLists.txt',
               'scripts/generate-device-manual.py', 'scripts/generate-manual-font.py',
               'scripts/check-device-manual.py', 'scripts/requirements-manual.txt',
               'docs/user/device-manual.zh-CN.md', 'docs/user/device-manual.en.md')
FONT_FILES = ('MyBotManualSans.ttf', 'DejaVuSansMono-Bold.ttf')
RENDER_SOURCE = {'.c', '.h', '.cc', '.cpp', '.hpp', '.inc', '.def', '.cmake'}


def sha256(path):
    result = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            result.update(block)
    return result.hexdigest()


def checked_path(root, name):
    relative = PurePosixPath(name)
    if relative.is_absolute() or '\\' in name or '..' in relative.parts or str(relative) != name:
        raise ValueError('Invalid repository-relative manual path: ' + name)
    path = root / name
    if not path.resolve().is_relative_to(root.resolve()) or not path.is_file():
        raise ValueError('Missing or outside-repository manual file: ' + name)
    return path


def tree_files(root, directory):
    path = root / directory
    if not path.is_dir():
        raise ValueError('Missing manual input directory: ' + directory)
    return sorted(file.relative_to(root).as_posix() for file in path.rglob('*') if file.is_file())


def hashes(root, names):
    return {name: sha256(checked_path(root, name)) for name in sorted(set(names))}


def content_digest(values):
    return hashlib.sha256(json.dumps(values, sort_keys=True, separators=(',', ':')).encode()).hexdigest()


def scene_digest(outputs, directory):
    prefix = 'docs/user/assets/' + directory + '/'
    return content_digest({name[len(prefix):]: digest for name, digest in outputs.items()
                           if name.startswith(prefix) and name.endswith('.png')})


def snapshot(root):
    def sources(directory):
        return {name for name in tree_files(root, directory)
                if Path(name).suffix in RENDER_SOURCE or Path(name).name == 'CMakeLists.txt'}

    ui = set(UI_FILES) | sources('src/ui')
    inputs = set(INPUT_FILES) | ui | {'docs/user/assets/fonts/' + name for name in FONT_FILES}
    ui_digest = content_digest(hashes(root, ui | sources('vendor/lvgl')))
    version = re.search(r'project\(mybot_rv1126b\s+VERSION\s+([\d.]+)',
                        (root / 'CMakeLists.txt').read_text())
    if not version:
        raise ValueError('Cannot read application version from CMakeLists.txt')
    return hashes(root, inputs), ui_digest, version[1]


def output_names(root):
    result, languages = set(), []
    for source, directory, pdf in LANGUAGES.values():
        links = re.findall(r'!\[[^\]]*\]\(([^)]+)\)',
                           (root / 'docs/user' / source).read_text())
        expected = set()
        for link in links:
            path = PurePosixPath(link)
            if path.parent.as_posix() != 'assets/' + directory or path.suffix != '.png':
                raise ValueError('Manual images must use their language asset directory: ' + link)
            expected.add(path.name)
            result.add('docs/user/' + link)
        if not expected:
            raise ValueError('Manual source has no UI images: ' + source)
        languages.append(expected)
        result.add(pdf)
    if languages[0] != languages[1]:
        raise ValueError('Chinese and English manual scene names differ')
    return sorted(result)


def read_manifest(root):
    result = json.loads((root / MANIFEST).read_text())
    fields = {'schema_version', 'document_date', 'application_version', 'ui_source_sha256',
              'scene_sha256', 'inputs', 'outputs'}
    if set(result) != fields or type(result['schema_version']) is not int or result['schema_version'] != 2:
        raise ValueError('Unsupported or incomplete manual manifest schema')
    if (not isinstance(result['scene_sha256'], dict) or set(result['scene_sha256']) != set(LANGUAGES)
            or any(not isinstance(digest, str) or not re.fullmatch(r'[0-9a-f]{64}', digest)
                   for digest in result['scene_sha256'].values())):
        raise ValueError('Invalid bilingual scene digests')
    if date.fromisoformat(result['document_date']).isoformat() != result['document_date']:
        raise ValueError('Invalid document_date')
    for field in ('inputs', 'outputs'):
        if not isinstance(result[field], dict) or not result[field]:
            raise ValueError('Manual manifest needs a nonempty ' + field + ' map')
        for name, digest in result[field].items():
            checked_path(root, name)
            if not isinstance(digest, str) or not re.fullmatch('[0-9a-f]{64}', digest):
                raise ValueError('Invalid manual digest: ' + name)
    return result


def differences(expected, actual, label):
    return [label + ': ' + name for name in sorted(set(expected) | set(actual))
            if expected.get(name) != actual.get(name)]


def check(root):
    manifest = read_manifest(root)
    inputs, ui_digest, version = snapshot(root)
    errors = differences(manifest['inputs'], inputs, 'manual input changed')
    expected_outputs = output_names(root)
    if sorted(manifest['outputs']) != expected_outputs:
        errors.append('manual output set differs from bilingual sources')
    actual = hashes(root, expected_outputs)
    errors += differences(manifest['outputs'], actual, 'manual output changed')
    for language, (_, directory, _) in LANGUAGES.items():
        if manifest['scene_sha256'][language] != scene_digest(actual, directory):
            errors.append('manual scene content changed: ' + language)
    if manifest['ui_source_sha256'] != ui_digest:
        errors.append('manual UI source identity changed')
    if manifest['application_version'] != version:
        errors.append('manual application version changed')
    if errors:
        raise ValueError('\n'.join(errors) + '\nRefresh with scripts/check-device-manual.py --refresh.')
    return manifest


def require_pdf_tools(root):
    requirements = (root / 'scripts/requirements-manual.txt').read_text()
    for package in ('Pillow', 'reportlab', 'PyMuPDF'):
        required = re.search(r'^' + package + r'==([^\s]+)$', requirements, re.M)
        if not required:
            raise ValueError('Manual requirements must pin ' + package)
        try:
            installed = metadata.version(package)
        except metadata.PackageNotFoundError as error:
            raise ValueError('Install scripts/requirements-manual.txt in the manual environment') from error
        if installed != required[1]:
            raise ValueError(f'Manual generation needs {package}=={required[1]}; found {installed}')


def build_preview(root, directory, language):
    directory = directory.resolve()
    values = {}
    for line in (directory / 'CMakeCache.txt').read_text().splitlines():
        match = re.fullmatch(r'([^:#]+):[^=]+=(.*)', line)
        if match:
            values[match[1]] = match[2]
    if Path(values.get('CMAKE_HOME_DIRECTORY', '')).resolve() != root.resolve():
        raise ValueError('Preview build belongs to a different source tree: ' + str(directory))
    if values.get('RV1126B_HOST_ONLY', '').upper() not in ('ON', '1', 'TRUE', 'YES') or values.get('RV1126B_LANGUAGE') != language:
        raise ValueError('Preview requires a host build with matching UI language: ' + str(directory))
    subprocess.run(['cmake', '--build', str(directory), '--target', 'mybot-ui-preview'], check=True)
    return checked_path(directory, 'mybot-ui-preview')


def verify_pdf(path, source):
    import fitz
    chapters = re.findall(r'^# (.+)$', source.read_text(), re.M)
    with fitz.open(path) as document:
        if document.page_count != len(chapters) + 2:
            raise ValueError('Manual PDF page count does not match its source')
        toc = document.get_toc()
        if [entry[1] for entry in toc] != chapters or len(document[1].get_links()) != len(chapters):
            raise ValueError('Manual PDF outline or contents links are incomplete')
        text = ''.join(page.get_text() for page in document)
        if 'cmake' not in text or 'adb' not in text or '\ufffd' in text:
            raise ValueError('Manual PDF commands are not selectable or contain replacement glyphs')
        for page in document:
            fonts = {re.sub(r'^[A-Z]{6}\+', '', item[3]): item[0] for item in page.get_fonts()}
            used = {span['font'] for block in page.get_text('dict')['blocks'] if 'lines' in block
                    for line in block['lines'] for span in line['spans'] if span['text'].strip()}
            for name in used:
                # ReportLab may declare an unused core Helvetica resource.
                # Only fonts that actually paint text must have embedded data.
                if name not in fonts or not document.extract_font(fonts[name])[3]:
                    raise ValueError('Manual PDF uses an unembedded font: ' + name)


def render_assets(root, staging, builds, document_date):
    from PIL import Image
    for language, (source, image_directory, pdf) in LANGUAGES.items():
        preview = build_preview(root, builds[language], language)
        ppm = staging / 'ppm' / language
        ppm.mkdir(parents=True)
        result = subprocess.run([str(preview), '--manual', str(ppm)],
                                check=True, capture_output=True, text=True)
        if 'language=' + language not in result.stdout:
            raise ValueError('Preview reported a different UI language')
        images = staging / 'docs/user/assets' / image_directory
        images.mkdir(parents=True)
        for path in sorted(ppm.glob('*.ppm')):
            with Image.open(path) as image:
                if image.size != (640, 480):
                    raise ValueError('Manual scene has the wrong size: ' + path.name)
                image.convert('RGB').save(images / (path.stem + '.png'), optimize=False, compress_level=9)
        rendered = hashes(staging, [path.relative_to(staging).as_posix() for path in images.glob('*.png')])
        baseline = 'ui-' + scene_digest(rendered, image_directory)[:12]
        source_copy = staging / 'docs/user' / source
        shutil.copyfile(root / 'docs/user' / source, source_copy)
        subprocess.run([sys.executable, str(root / 'scripts/generate-device-manual.py'),
                        '--language', language, '--source', str(source_copy), '--output', str(staging / pdf),
                        '--date', document_date, '--baseline', baseline], check=True)
        verify_pdf(staging / pdf, source_copy)
    expected = output_names(root)
    actual = sorted(path.relative_to(staging).as_posix() for path in (staging / 'docs').rglob('*')
                    if path.suffix in ('.png', '.pdf'))
    if expected != actual:
        raise ValueError('Rendered manual scene set differs from the bilingual sources')
    return hashes(staging, expected)


def compare_rendered(root, staging, names):
    from PIL import Image
    for name in names:
        if name.endswith('.png'):
            with Image.open(root / name) as committed, Image.open(staging / name) as reproduced:
                if committed.size != reproduced.size or committed.convert('RGB').tobytes() != reproduced.convert('RGB').tobytes():
                    raise ValueError('Manual PNG pixels do not reproduce: ' + name)
        elif (root / name).read_bytes() != (staging / name).read_bytes():
            raise ValueError('Manual PDF bytes do not reproduce: ' + name)


def refresh(root, builds, requested_date=None, reproduce=False):
    require_pdf_tools(root)
    if reproduce:
        previous = check(root)
    elif requested_date:
        previous = {}
    else:
        previous = json.loads((root / MANIFEST).read_text()) if (root / MANIFEST).exists() else {}
    document_date = requested_date or previous.get('document_date')
    if not document_date or date.fromisoformat(document_date).isoformat() != document_date:
        raise ValueError('The first refresh requires --date YYYY-MM-DD')
    inputs, ui_digest, version = snapshot(root)
    (root / 'build').mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='manual-assets-', dir=root / 'build') as temporary:
        staging = Path(temporary)
        outputs = render_assets(root, staging, builds, document_date)
        if (inputs, ui_digest, version) != snapshot(root):
            raise ValueError('Manual inputs changed during rendering; retry with stable sources')
        if reproduce:
            compare_rendered(root, staging, outputs)
            return
        # Publish only after both languages completed. The manifest is last, so
        # interruption is detected by the next check instead of blessing a mix.
        for name in outputs:
            target = root / name
            target.parent.mkdir(parents=True, exist_ok=True)
            os.replace(staging / name, target)
        scenes = {language: scene_digest(outputs, values[1]) for language, values in LANGUAGES.items()}
        manifest = {'schema_version': 2, 'document_date': document_date, 'application_version': version,
                    'ui_source_sha256': ui_digest, 'scene_sha256': scenes, 'inputs': inputs, 'outputs': outputs}
        target = root / MANIFEST
        temporary_manifest = staging / 'manual-manifest.json'
        temporary_manifest.write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + '\n')
        os.replace(temporary_manifest, target)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    action = parser.add_mutually_exclusive_group()
    action.add_argument('--check', action='store_true', help='check input and output hashes using stdlib only (default)')
    action.add_argument('--refresh', action='store_true', help='build previews and replace bilingual PNG/PDF and manifest')
    action.add_argument('--reproduce', action='store_true', help='build in isolation and compare PDF bytes and PNG pixels')
    parser.add_argument('--build-zh', type=Path, default=ROOT / 'build/host-zh')
    parser.add_argument('--build-en', type=Path, default=ROOT / 'build/host-en')
    parser.add_argument('--date', help='fixed document date; defaults to the existing manifest date')
    args = parser.parse_args()
    try:
        if args.refresh or args.reproduce:
            refresh(ROOT, {'zh-CN': args.build_zh, 'en-US': args.build_en}, args.date, args.reproduce)
        else:
            check(ROOT)
    except (OSError, ValueError, KeyError, TypeError, subprocess.CalledProcessError) as error:
        parser.error(str(error))
    print('Manual assets ' + ('reproduced' if args.reproduce else 'refreshed' if args.refresh else 'verified') + '.')


if __name__ == '__main__':
    main()
