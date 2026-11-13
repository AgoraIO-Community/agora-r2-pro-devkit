#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Create a local test bundle. Never connects to, installs on or starts a device."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import tarfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--voice-build', type=Path, default=ROOT / 'build-rv1126b')
    parser.add_argument('--video-build', type=Path, default=ROOT / 'build-rv1126b-video')
    parser.add_argument('--rtsa-library', type=Path)
    args = parser.parse_args()
    version = re.search(r'project\(mybot_rv1126b VERSION ([0-9.]+)',
                        (ROOT / 'CMakeLists.txt').read_text()).group(1)
    name = 'mybot-rv1126b-' + version + '-aarch64'
    destination = ROOT / 'dist' / name
    if destination.exists():
        parser.error('Bundle already exists; move it aside before rebuilding: ' + str(destination))
    voice = args.voice_build.resolve()
    cache = (voice / 'CMakeCache.txt').read_text()
    rtsa = args.rtsa_library or Path(re.search(r'(?m)^AGORA_RTC_LIBRARY:FILEPATH=(.+)$', cache).group(1))
    builds = [('mybot-rv1126b', voice, 'voice')]
    video = args.video_build.resolve()
    if (video / 'mybot-rv1126b').is_file():
        builds.append(('mybot-rv1126b-video', video, 'video'))
    for _, build, _ in builds:
        report = json.loads((build / 'abi-report.json').read_text())
        if report['errors'] or report['binary']['sha256'] != hashlib.sha256((build / 'mybot-rv1126b').read_bytes()).hexdigest():
            parser.error('Missing or stale passing ABI report for ' + str(build))
        matching = next((x for x in report['checked'] if x['soname'] == 'libagora-rtc-sdk.so'), None)
        if not matching or matching['sha256'] != hashlib.sha256(rtsa.read_bytes()).hexdigest():
            parser.error('RTSA library does not match verified build')
        if any(x['soname'] and x['soname'].startswith('liblvgl.so') for x in report['checked']):
            parser.error('Bundle must use the application-local static LVGL')
    (destination / 'bin').mkdir(parents=True)
    (destination / 'lib').mkdir()
    for filename, build, profile in builds:
        shutil.copy2(str(build / 'mybot-rv1126b'), str(destination / 'bin' / filename))
        (destination / 'bin' / filename).chmod(0o755)
        shutil.copy2(str(build / 'abi-report.json'), str(destination / ('abi-report-' + profile + '.json')))
    shutil.copy2(str(rtsa), str(destination / 'lib/libagora-rtc-sdk.so'))
    shutil.copytree(str(ROOT / 'third_party/mybot/assets/locales'), str(destination / 'assets/locales'))
    shutil.copy2(str(ROOT / 'packaging/run.sh'), str(destination / 'run.sh'))
    (destination / 'run.sh').chmod(0o755)
    shutil.copy2(str(ROOT / 'packaging/example.env'), str(destination / 'example.env'))
    shutil.copy2(str(ROOT / 'docs/first-validation.zh-CN.md'), str(destination / 'README.zh-CN.md'))
    notices = destination / 'licenses'
    notices.mkdir()
    for source, filename in [
        ('LICENSE', 'project-APACHE-2.0.txt'),
        ('THIRD_PARTY_NOTICES.md', 'THIRD_PARTY_NOTICES.md'),
        ('third_party/mybot/LICENSE', 'mybot-LICENSE.txt'),
        ('third_party/mybot/THIRD_PARTY_NOTICES.md', 'mybot-THIRD_PARTY_NOTICES.md'),
        ('third_party/mybot/third_party/aosl/LICENSE', 'aosl-LICENSE.txt'),
        ('third_party/lvgl/LICENCE.txt', 'lvgl-LICENSE.txt'),
        ('third_party/lvgl/scripts/built_in_font/font_license/Montserrat/OFL.txt', 'Montserrat-OFL.txt'),
        ('third_party/lvgl/scripts/built_in_font/font_license/FontAwesome5/LICENSE.txt', 'FontAwesome-LICENSE.txt'),
        ('ui/view/TERMINAL_FONT_LICENSE.txt', 'terminal-font-LICENSE.txt'),
        ('ui/view/LICENSE.MIT.txt', 'cores3-view-MIT.txt'),
        ('ui/fonts/sources.json', 'font-sources.json'),
        ('ui/view/LVGL_VIEW_FONT_LICENSE.txt', 'chinese-font-OFL.txt'),
        ('docs/RTSA_SOURCE.txt', 'RTSA_SOURCE.txt'),
        ('docs/patches/lvgl-mask-callbacks.patch', 'lvgl-mask-callbacks.patch'),
    ]:
        shutil.copy2(str(ROOT / source), str(notices / filename))
    checksums = []
    for path in sorted(destination.rglob('*')):
        if path.is_file():
            checksums.append(hashlib.sha256(path.read_bytes()).hexdigest() + '  ' + str(path.relative_to(destination)))
    (destination / 'SHA256SUMS').write_text('\n'.join(checksums) + '\n')
    archive = destination.with_name(destination.name + '.tar.gz')
    with tarfile.open(str(archive), 'w:gz') as tar:
        tar.add(str(destination), arcname=name)
    digest = hashlib.sha256(archive.read_bytes()).hexdigest()
    archive.with_name(archive.name + '.sha256').write_text(digest + '  ' + archive.name + '\n')
    print('Bundle:', destination)
    print('Archive:', archive)
    print('SHA256:', digest)
    print('No device was accessed.')


if __name__ == '__main__':
    main()
