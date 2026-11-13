#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Create a verified, deterministic bundle without accessing a device."""

import argparse
import gzip
import os
from pathlib import Path
import shutil
import tarfile
import tempfile

from build_metadata import (digest, git, public_abi, public_manifest, source_snapshot,
                            verify_manifest, write_json)

ROOT = Path(__file__).resolve().parents[1]

LICENSES = [
    ("LICENSE", "project-APACHE-2.0.txt"),
    ("THIRD_PARTY_NOTICES.md", "THIRD_PARTY_NOTICES.md"),
    ("third_party/mybot/LICENSE", "mybot-LICENSE.txt"),
    ("third_party/mybot/THIRD_PARTY_NOTICES.md", "mybot-THIRD_PARTY_NOTICES.md"),
    ("third_party/aosl/LICENSE", "aosl-LICENSE.txt"),
    ("third_party/lvgl/LICENCE.txt", "lvgl-LICENSE.txt"),
    ("third_party/lvgl/COPYRIGHTS.md", "lvgl-COPYRIGHTS.md"),
    ("third_party/lvgl/scripts/built_in_font/font_license/Montserrat/OFL.txt", "Montserrat-OFL.txt"),
    ("third_party/lvgl/scripts/built_in_font/font_license/FontAwesome5/LICENSE.txt", "FontAwesome-LICENSE.txt"),
    ("components/mybot_ui/fonts/licenses/TERMINAL_FONT_LICENSE.txt", "terminal-font-LICENSE.txt"),
    ("components/mybot_ui/views/pixel_glasses/LICENSE.MIT.txt", "pixel-glasses-MIT.txt"),
    ("components/mybot_ui/fonts/sources.json", "font-sources.json"),
    ("components/mybot_ui/fonts/licenses/LVGL_VIEW_FONT_LICENSE.txt", "chinese-font-OFL.txt"),
    ("docs/dependencies/RTSA_SOURCE.txt", "RTSA_SOURCE.txt"),
    ("third_party/rtsa/PACKAGE_INFO", "RTSA-PACKAGE_INFO.txt"),
    ("third_party/rtsa/README.md", "RTSA-README.zh-CN.md"),
    ("third_party/rtsa/README.en.md", "RTSA-README.en.md"),
    ("docs/dependencies/patches/lvgl-mask-callbacks.patch", "lvgl-mask-callbacks.patch"),
    ("dependencies.lock.json", "dependencies.lock.json"),
]


def source_epoch(root):
    value = os.environ.get("SOURCE_DATE_EPOCH")
    if value is None:
        try:
            value = git(root, "show", "-s", "--format=%ct", "HEAD").decode().strip()
        except Exception:
            value = "0"
    try:
        epoch = int(value)
        if not 0 <= epoch <= 0xffffffff:
            raise ValueError
        return epoch
    except ValueError as error:
        raise ValueError("SOURCE_DATE_EPOCH must be an integer in 0..4294967295") from error


def deterministic_archive(directory, archive, epoch):
    """No host paths, owner IDs, wall-clock time or filesystem ordering in tar/gzip."""
    with Path(archive).open("wb") as stream:
        with gzip.GzipFile(filename="", mode="wb", fileobj=stream, mtime=epoch) as compressed:
            with tarfile.open(fileobj=compressed, mode="w", format=tarfile.USTAR_FORMAT) as tar:
                for path in [directory, *sorted(directory.rglob("*"))]:
                    if path.is_symlink() or not (path.is_dir() or path.is_file()):
                        raise ValueError("Unsupported package entry: " + str(path))
                    info = tarfile.TarInfo(str(path.relative_to(directory.parent)))
                    info.uid = info.gid = 0
                    info.uname = info.gname = ""
                    info.mtime = epoch
                    if path.is_dir():
                        info.type, info.mode = tarfile.DIRTYPE, 0o755
                        tar.addfile(info)
                    else:
                        info.mode = 0o755 if path.name == "run.sh" or path.parent.name == "bin" else 0o644
                        info.size = path.stat().st_size
                        with path.open("rb") as data:
                            tar.addfile(info, data)


def package(root, args):
    root = Path(root)
    profiles = ("voice", "video") if args.profile == "all" else (args.profile,)
    validated = []
    for profile in profiles:
        build = Path(getattr(args, profile + "_build")).resolve()
        manifest, report = verify_manifest(root, build, args.language, profile == "video")
        validated.append((profile, build, manifest, report))
    baseline = validated[0][2]["inputs"]
    for _, _, manifest, _ in validated[1:]:
        inputs = manifest["inputs"]
        if (inputs["source"] != baseline["source"] or
                inputs["dependencies"] != baseline["dependencies"] or
                inputs["config"]["version"] != baseline["config"]["version"]):
            raise ValueError("Profiles were built from different source/dependencies/version")
    if args.release:
        source = source_snapshot(root)
        if source["dirty"] or not source["revision"]:
            raise ValueError("Release packaging requires a clean, committed Git source tree")
    version = baseline["config"]["version"]
    name = "mybot-rv1126b-" + version
    if args.language != "zh-CN":
        name += "-" + args.language
    name += "-aarch64"
    if args.profile != "all":
        name += "-" + args.profile
    output = Path(args.output_dir).resolve()
    destination = output / name
    archive = output / (name + ".tar.gz")
    checksum = output / (name + ".tar.gz.sha256")
    if any(path.exists() for path in (destination, archive, checksum)):
        raise ValueError("Bundle destination already exists; use a fresh --output-dir: " + str(destination))
    epoch = source_epoch(root)
    output.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=".package-", dir=output) as temporary:
        staging = Path(temporary) / name
        (staging / "bin").mkdir(parents=True)
        (staging / "lib").mkdir()
        for profile, build, manifest, report in validated:
            filename = "mybot-rv1126b" + ("-video" if profile == "video" else "")
            shutil.copyfile(build / "mybot-rv1126b", staging / "bin" / filename)
            (staging / "bin" / filename).chmod(0o755)
            public_report = public_abi(report)
            report_file = staging / ("abi-report-" + profile + ".json")
            write_json(report_file, public_report)
            public_build = public_manifest(manifest)
            public_build["abi_report_sha256"] = digest(report_file)
            write_json(staging / ("build-manifest-" + profile + ".json"), public_build)
        shutil.copyfile(baseline["config"]["sdk"]["rtsa_library"], staging / "lib/libagora-rtc-sdk.so")
        shutil.copytree(root / "third_party/mybot/assets/locales", staging / "assets/locales")
        launcher = (root / "packaging/run.sh").read_text()
        if args.profile == "video":
            if "${RV1126B_VIDEO:-0}" not in launcher:
                raise ValueError("Cannot select video profile in launcher template")
            launcher = launcher.replace("${RV1126B_VIDEO:-0}", "${RV1126B_VIDEO:-1}")
        (staging / "run.sh").write_text(launcher)
        (staging / "run.sh").chmod(0o755)
        shutil.copyfile(root / "packaging/example.env", staging / "example.env")
        shutil.copyfile(root / "packaging/README.zh-CN.md", staging / "README.zh-CN.md")
        (staging / "licenses").mkdir()
        for source, filename in LICENSES:
            shutil.copyfile(root / source, staging / "licenses" / filename)
        checksums = [digest(path) + "  " + str(path.relative_to(staging))
                     for path in sorted(staging.rglob("*")) if path.is_file()]
        (staging / "SHA256SUMS").write_text("\n".join(checksums) + "\n")
        temporary_archive = Path(temporary) / archive.name
        deterministic_archive(staging, temporary_archive, epoch)
        archive_digest = digest(temporary_archive)
        temporary_checksum = Path(temporary) / checksum.name
        temporary_checksum.write_text(archive_digest + "  " + archive.name + "\n")
        # Publish only after every verification, copy and archive operation succeeded.
        staging.rename(destination)
        temporary_archive.rename(archive)
        temporary_checksum.rename(checksum)
    return destination, archive, archive_digest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--profile", choices=("all", "voice", "video"), default="all")
    parser.add_argument("--language", choices=("zh-CN", "en-US"), default="zh-CN")
    parser.add_argument("--voice-build", type=Path,
                        help="Default: build/rv1126b-voice-zh (or -en for en-US)")
    parser.add_argument("--video-build", type=Path,
                        help="Default: build/rv1126b-video-zh (or -en for en-US)")
    parser.add_argument("--output-dir", type=Path, default=ROOT / "dist")
    parser.add_argument("--release", action="store_true", help="Require clean, committed source")
    args = parser.parse_args()
    language_suffix = "zh" if args.language == "zh-CN" else "en"
    for profile in ("voice", "video"):
        if getattr(args, profile + "_build") is None:
            setattr(args, profile + "_build", ROOT / "build" / ("rv1126b-" + profile + "-" + language_suffix))
    try:
        destination, archive, checksum = package(ROOT, args)
    except (OSError, ValueError, KeyError) as error:
        parser.error(str(error))
    print("Bundle:", destination)
    print("Archive:", archive)
    print("SHA256:", checksum)
    print("No device was accessed.")


if __name__ == "__main__":
    main()
