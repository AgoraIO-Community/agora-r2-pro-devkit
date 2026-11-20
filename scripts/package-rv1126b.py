#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Create a verified, deterministic bundle without accessing a device."""

import argparse
import gzip
import json
import os
from pathlib import Path
import re
import shutil
import tarfile
import tempfile

from build_metadata import (checkout_origin, digest, git, inputs_digest, public_abi, public_manifest,
                            verified_abi, verify_manifest, write_json)

from runtime_baseline import validate_runtime
from target_elf import inspect as inspect_elf

ROOT = Path(__file__).resolve().parents[1]

EXECUTABLE_ASSETS = {
    "assets/wifi/udhcpc.script": "packaging/wifi/udhcpc.script",
    "assets/time/sync-busybox.sh": "packaging/time/sync-busybox.sh",
    "assets/network/99-mybot-lease": "packaging/network/99-mybot-lease",
    "assets/service/S99mybot": "packaging/service/S99mybot",
    "assets/service/run-service.sh": "packaging/service/run-service.sh",
    "assets/service/audio-prepare.sh": "packaging/service/audio-prepare.sh",
    "assets/service/poweroff.sh": "packaging/service/poweroff.sh",
}
DATA_ASSETS = {'assets/service/mybot-rv1126b.conf': 'packaging/service/mybot-rv1126b.conf'}

LICENSES = [
    ("LICENSE", "project-APACHE-2.0.txt"),
    ("THIRD_PARTY_NOTICES.md", "THIRD_PARTY_NOTICES.md"),
    ("docs/dependencies/licensing.md", "DEPENDENCY_LICENSING.md"),
    ("docs/dependencies/cjson-LICENSE.txt", "cjson-LICENSE.txt"),
    ("vendor/mybot/LICENSE", "mybot-LICENSE.txt"),
    ("vendor/mybot/THIRD_PARTY_NOTICES.md", "mybot-THIRD_PARTY_NOTICES.md"),
    ("vendor/aosl/LICENSE", "aosl-LICENSE.txt"),
    ("vendor/lvgl/LICENCE.txt", "lvgl-LICENSE.txt"),
    ("vendor/lvgl/COPYRIGHTS.md", "lvgl-COPYRIGHTS.md"),
    ("vendor/lvgl/scripts/built_in_font/font_license/Montserrat/OFL.txt", "Montserrat-OFL.txt"),
    ("vendor/lvgl/scripts/built_in_font/font_license/FontAwesome5/LICENSE.txt", "FontAwesome-LICENSE.txt"),
    ("src/ui/fonts/licenses/TERMINAL_FONT_LICENSE.txt", "terminal-font-LICENSE.txt"),
    ("src/ui/views/pixel_glasses/LICENSE.MIT.txt", "pixel-glasses-MIT.txt"),
    ("src/ui/fonts/sources.json", "font-sources.json"),
    ("src/ui/fonts/licenses/LVGL_VIEW_FONT_LICENSE.txt", "chinese-font-OFL.txt"),
    ("docs/dependencies/RTSA_SOURCE.txt", "RTSA_SOURCE.txt"),
    ("vendor/rtsa/PACKAGE_INFO", "RTSA-PACKAGE_INFO.txt"),
    ("vendor/rtsa/README.md", "RTSA-README.zh-CN.md"),
    ("vendor/rtsa/README.en.md", "RTSA-README.en.md"),
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
                        executable = (path.name == "run.sh" or path.parent.name == "bin" or
                                      path.relative_to(directory).as_posix() in EXECUTABLE_ASSETS)
                        info.mode = 0o755 if executable else 0o644
                        info.size = path.stat().st_size
                        with path.open("rb") as data:
                            tar.addfile(info, data)


def example_environment(root, language):
    servers = {"zh-CN": "https://botstation.shengwang.cn/api",
               "en-US": "https://botstation.agora.io/api"}
    if language not in servers:
        raise ValueError("Unsupported example.env language: " + str(language))
    template = (Path(root) / "packaging/example.env").read_text()
    for placeholder, value, label in (("@UI_LANGUAGE@", language, "UI language"),
                                      ("@DEFAULT_SERVER@", servers[language], "default server")):
        if template.count(placeholder) != 1:
            raise ValueError("Expected exactly one " + label + " placeholder in example.env template")
        template = template.replace(placeholder, value)
    return template


def package_markdown(root, name, packaged, revision):
    """Resolve project links against the checkout used to assemble the archive."""
    root = Path(root).resolve()
    source = root / name

    def link(match):
        target = match[1]
        if target.startswith(("https://", "http://", "#", "mailto:")):
            return match[0]
        path, separator, fragment = target.partition("#")
        relative = str((source.parent / path).resolve().relative_to(root))
        destination = packaged.get(relative)
        if destination is None:
            destination = f"https://github.com/AgoraIO-Community/agora-r2-pro-devkit/blob/{revision or 'main'}/{relative}"
        return "](" + destination + separator + fragment + ")"

    return re.sub(r"\]\(([^)]+)\)", link, source.read_text())


def verify_release_runtime(root, sdk, reports):
    staging = Path(sdk["staging_dir"])
    if not sdk.get("runtime_manifest"):
        raise ValueError("Release requires the target runtime manifest")
    baseline = json.loads((root / "dependencies.lock.json").read_text())["external"]["runtime"]["libraries"]
    validate_runtime(staging, sdk.get("runtime_manifest"), baseline)
    pinned = {((staging / name).resolve(), digest): Path(name).name for name, digest in baseline.items()}
    rtsa = Path(sdk["rtsa_library"]).resolve()
    inspected = {}
    for binary_path, report in reports:
        supplied, needed = set(), set()
        if not any(Path(entry["path"]).resolve() == binary_path.resolve() for entry in report["checked"]):
            raise ValueError("Release ABI report must include its executable")
        for entry in report["checked"]:
            path = Path(entry["path"]).resolve()
            if path not in inspected:
                inspected[path] = inspect_elf(path)
            elf = inspected[path]
            if path == binary_path.resolve():
                if elf["interpreter"] != "/lib/ld-linux-aarch64.so.1":
                    raise ValueError("Invalid release interpreter")
                needed.add(Path(elf["interpreter"]).name)
            else:
                name = elf["soname"] or path.name
                if path == rtsa and name == "libagora-rtc-sdk.so":
                    supplied.add(name)
                elif pinned.get((path, elf["sha256"])) == name:
                    supplied.add(name)
                else:
                    raise ValueError("ABI dependency outside pinned firmware runtime: " + name)
            needed.update(elf["needed"])
        if needed - supplied:
            raise ValueError("Missing firmware ABI closure: " + ", ".join(sorted(needed - supplied)))


def package(root, args):
    root = Path(root)
    profiles = ("voice", "video") if args.profile == "all" else (args.profile,)
    validated = []
    origin = checkout_origin(root)
    if args.release and (origin["dirty"] or not origin["revision"]):
        raise ValueError("Release packaging requires a clean, committed Git source tree")
    for profile in profiles:
        build = Path(getattr(args, profile + "_build")).resolve()
        manifest, report = verify_manifest(root, build, args.language, profile == "video")
        inputs = manifest['inputs']
        if args.release and (inputs["source"]["dirty"] or not inputs["source"]["revision"]):
            raise ValueError("Release requires binaries built from clean, committed source")
        reports = [(build / "mybot-rv1126b", report)]
        for role in ("supervisor", "factory-reset"):
            helper = verified_abi(build, inputs['config'], inputs['source']['sha256'], inputs_digest(inputs),
                                  binary_name='mybot-' + role, report_name='abi-report-' + role + '.json',
                                  require_rtsa=False)
            reports.append((build / ("mybot-" + role), helper))
        if args.release:
            verify_release_runtime(root, inputs["config"]["sdk"], reports)
        validated.append((profile, build, manifest, report))
    baseline = validated[0][2]["inputs"]
    for _, _, manifest, _ in validated[1:]:
        inputs = manifest["inputs"]
        if (inputs["source"] != baseline["source"] or
                inputs["dependencies"] != baseline["dependencies"] or
                inputs["config"]["version"] != baseline["config"]["version"]):
            raise ValueError("Profiles were built from different source/dependencies/version")
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
    environment = example_environment(root, baseline["config"]["language"])
    epoch = source_epoch(root)
    output.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=".package-", dir=output) as temporary:
        staging = Path(temporary) / name
        (staging / "bin").mkdir(parents=True)
        (staging / "lib").mkdir()
        write_json(staging / "package-manifest.json", {"schema": 1, "source": origin})
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
        shutil.copytree(root / "vendor/mybot/assets/locales", staging / "assets/locales")
        for relative, source in EXECUTABLE_ASSETS.items():
            target = staging / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(root / source, target)
            target.chmod(0o755)
        for relative, source in DATA_ASSETS.items():
            (staging / relative).parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(root / source, staging / relative)
        helper_build = validated[0][1]
        for role in ("supervisor", "factory-reset"):
            binary = "mybot-" + role
            report_name = "abi-report-" + role + ".json"
            shutil.copyfile(helper_build / binary, staging / "bin" / binary)
            (staging / "bin" / binary).chmod(0o755)
            write_json(staging / report_name, public_abi(json.loads((helper_build / report_name).read_text())))
            helper_manifest = public_manifest(validated[0][2])
            helper_manifest.update(role=role, binary_sha256=digest(staging / "bin" / binary),
                                   abi_report_sha256=digest(staging / report_name))
            write_json(staging / (role + "-manifest.json"), helper_manifest)
        launcher = (root / "packaging/run.sh").read_text()
        if args.profile == "video":
            if "${RV1126B_VIDEO:-0}" not in launcher:
                raise ValueError("Cannot select video profile in launcher template")
            launcher = launcher.replace("${RV1126B_VIDEO:-0}", "${RV1126B_VIDEO:-1}")
        (staging / "run.sh").write_text(launcher)
        (staging / "run.sh").chmod(0o755)
        (staging / "example.env").write_text(environment)
        readme = "README.en.md" if args.language == "en-US" else "README.zh-CN.md"
        manual = "docs/device-operations." + ("en" if args.language == "en-US" else "zh-CN") + ".pdf"
        (staging / "docs").mkdir()
        shutil.copyfile(root / manual, staging / manual)
        (staging / readme).write_text(package_markdown(root, "packaging/" + readme,
                                                     {manual: manual}, origin["revision"]))
        (staging / "licenses").mkdir()
        for source, filename in LICENSES:
            if source in ("THIRD_PARTY_NOTICES.md", "docs/dependencies/licensing.md"):
                (staging / "licenses" / filename).write_text(package_markdown(root, source, dict(LICENSES), origin["revision"]))
            else:
                shutil.copyfile(root / source, staging / "licenses" / filename)
        checksums = [digest(path) + "  " + str(path.relative_to(staging))
                     for path in sorted(staging.rglob("*")) if path.is_file()]
        (staging / "SHA256SUMS").write_text("\n".join(checksums) + "\n")
        temporary_archive = Path(temporary) / archive.name
        deterministic_archive(staging, temporary_archive, epoch)
        archive_digest = digest(temporary_archive)
        temporary_checksum = Path(temporary) / checksum.name
        temporary_checksum.write_text(archive_digest + "  " + archive.name + "\n")
        if checkout_origin(root) != origin:
            raise ValueError("Packaging checkout changed during assembly; retry with stable inputs")
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
    parser.add_argument("--release", action="store_true", help="Require committed source and the pinned firmware runtime for every ABI dependency")
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
