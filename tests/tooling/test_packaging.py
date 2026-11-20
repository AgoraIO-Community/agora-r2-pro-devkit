# SPDX-License-Identifier: Apache-2.0
"""Release guard regressions use isolated fixture repositories, never a device."""

import argparse
from contextlib import redirect_stderr, redirect_stdout
import importlib.util
import io
import json
import os
from pathlib import Path
import re
import shlex
import shutil
import struct
import subprocess
import sys
import tarfile
import tempfile
import textwrap
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))
import build_metadata as metadata

SPEC = importlib.util.spec_from_file_location("package_rv1126b", ROOT / "scripts/package-rv1126b.py")
packager = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(packager)
MANIFEST_SPEC = importlib.util.spec_from_file_location("build_manifest", ROOT / "scripts/build-manifest.py")
manifest_tool = importlib.util.module_from_spec(MANIFEST_SPEC)
MANIFEST_SPEC.loader.exec_module(manifest_tool)


class PackagingTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.base = Path(self.temporary.name)
        self.root = self.base / "source"
        self.root.mkdir()
        for source, _ in packager.LICENSES:
            self.write(self.root / source, "license fixture\n")
        self.write(self.root / 'docs/dependencies/cjson-LICENSE.txt',
                   (ROOT / 'docs/dependencies/cjson-LICENSE.txt').read_text())
        self.write(self.root / "CMakeLists.txt", "project(mybot_rv1126b VERSION 0.2.0 LANGUAGES C CXX)\n")
        self.write(self.root / ".gitignore", "/build*/\n/local/\n/dist/\n")
        self.write(self.root / "packaging/run.sh", 'case "${RV1126B_VIDEO:-0}" in\n0) :;;\n1) :;;\nesac\n')
        self.write(self.root / "packaging/example.env", (ROOT / "packaging/example.env").read_text())
        for language in ("zh-CN", "en"):
            readme = "packaging/README." + language + ".md"
            self.write(self.root / readme, (ROOT / readme).read_text())
            self.write(self.root / ("docs/device-operations." + language + ".pdf"), "fixture PDF " + language)
        for source in packager.EXECUTABLE_ASSETS.values():
            self.write(self.root / source, "#!/bin/sh\nexit 0\n")
        for source in packager.DATA_ASSETS.values():
            self.write(self.root / source, "# configuration fixture\n")
        self.write(self.root / "vendor/mybot/assets/locales/zh-CN/0.pcm", "pcm")
        self.sdk = self.base / "sdk"
        self.rtsa = self.sdk / "lib/libagora-rtc-sdk.so"
        self.write(self.rtsa, "target RTSA")
        self.write(self.sdk / "include/agora.h", "SDK header")
        self.staging = self.base / "staging"
        self.write(self.staging / "usr/include/alsa.h", "target header")
        self.toolchain = self.base / "toolchain.cmake"
        self.write(self.toolchain, "toolchain")
        self.compiler = self.base / "compiler"
        self.write(self.compiler, "compiler bytes")
        self.cxx_compiler = self.base / "cxx-compiler"
        self.write(self.cxx_compiler, "C++ compiler bytes")
        subprocess.run(["git", "init", "-q", str(self.root)], check=True)
        subprocess.run(["git", "-C", str(self.root), "add", "."], check=True)
        subprocess.run(["git", "-C", str(self.root), "-c", "user.name=Fixture",
                        "-c", "user.email=fixture@example.invalid", "commit", "-qm", "fixture"], check=True)
        self.voice = self.create_build("voice", False)
        self.video = self.create_build("video", True)
        self.args = argparse.Namespace(profile="all", language="zh-CN", voice_build=self.voice,
                                       video_build=self.video, output_dir=self.base / "output", release=False)

    def write(self, path, text):
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text)

    def create_build(self, name, video, language="zh-CN"):
        build = self.root / ("build-" + name)
        build.mkdir()
        config = {"schema": 1, "version": "0.2.0", "language": language, "video": video,
                  "ptime_ms": 20, "board": metadata.BOARD,
                  "sdk": {"rtsa_library": str(self.rtsa), "sdk_dir": str(self.sdk),
                          "staging_dir": str(self.staging)},
                  "toolchain": {"file": str(self.toolchain), "c_compiler": str(self.compiler),
                                "c_compiler_id": "Clang", "c_compiler_version": "20.0",
                                "cxx_compiler": str(self.cxx_compiler), "cxx_compiler_id": "Clang",
                                "cxx_compiler_version": "20.0", "target": "aarch64-linux-gnu"},
                  "flags": {"c": "-O3 -DNDEBUG", "cxx": "-O3 -DNDEBUG",
                            "exe_link": "-L" + str(self.staging / "usr/lib"), "build_type": "Release"}}
        metadata.write_json(build / "build-config.json", config)
        marker = ("MYBOT_RV1126B_BUILD_CONFIG:0.2.0;" + language + ";video=" + str(int(video)) +
                  ";ptime_ms=20;board=" + metadata.BOARD + "\0")
        inputs = metadata.build_inputs(self.root, build)
        source_marker = "MYBOT_RV1126B_SOURCE:" + inputs["source"]["sha256"] + "\0"
        inputs_marker = "MYBOT_RV1126B_INPUTS:" + metadata.inputs_digest(inputs) + "\0"
        self.write(build / "mybot-rv1126b", "executable " + name + "\0" + marker + source_marker + inputs_marker)
        binary = {"path": str(build / "mybot-rv1126b"), "sha256": metadata.digest(build / "mybot-rv1126b"),
                  "machine": 183, "soname": None, "interpreter": "/lib/ld-linux-aarch64.so.1",
                  "needed": ["libagora-rtc-sdk.so"], "runpaths": [metadata.RPATH]}
        rtsa = {"path": str(self.rtsa), "sha256": metadata.digest(self.rtsa),
                "machine": 183, "soname": "libagora-rtc-sdk.so", "runpaths": []}
        metadata.write_json(build / "abi-report.json", {"errors": [], "binary": binary,
                            "dependency_count": 1, "checked": [binary, rtsa]})
        for role in ('supervisor', 'factory-reset'):
            self.write(build / ('mybot-' + role), (build / 'mybot-rv1126b').read_text())
            supervisor = dict(binary, path=str(build / ('mybot-' + role)))
            metadata.write_json(build / ('abi-report-' + role + '.json'), {
                'errors': [], 'binary': supervisor, 'dependency_count': 1,
                'checked': [supervisor, rtsa]})
        self.seal(build)
        return build

    def seal(self, build):
        metadata.write_json(build / "build-manifest.json",
                            {"schema": metadata.MANIFEST_SCHEMA, "inputs": metadata.build_inputs(self.root, build),
                             "binary_sha256": metadata.digest(build / "mybot-rv1126b"),
                             "abi_report_sha256": metadata.digest(build / "abi-report.json")})

    def refresh_source_marker(self, build):
        binary = build / "mybot-rv1126b"
        inputs = metadata.build_inputs(self.root, build)
        executable = binary.read_bytes()
        for field, value in (("SOURCE", inputs["source"]["sha256"]), ("INPUTS", metadata.inputs_digest(inputs))):
            current = ("MYBOT_RV1126B_" + field + ":" + value).encode() + b"\0"
            executable = re.sub(("MYBOT_RV1126B_" + field + ":[0-9a-f]{64}").encode() + b"\0",
                                current, executable)
        binary.write_bytes(executable)
        report = metadata.read_json(build / "abi-report.json")
        for entry in [report["binary"], *report["checked"]]:
            if entry["path"] == str(binary):
                entry["sha256"] = metadata.digest(binary)
        metadata.write_json(build / "abi-report.json", report)
        for role in ('supervisor', 'factory-reset'):
            supervisor = build / ('mybot-' + role)
            supervisor.write_bytes(executable)
            helper_report = metadata.read_json(build / ('abi-report-' + role + '.json'))
            for entry in [helper_report['binary'], *helper_report['checked']]:
                if entry['path'] == str(supervisor):
                    entry['sha256'] = metadata.digest(supervisor)
            metadata.write_json(build / ('abi-report-' + role + '.json'), helper_report)

    def test_archive_is_reproducible_and_public_metadata_has_no_host_paths(self):
        destination, archive, first_hash = packager.package(self.root, self.args)
        for report in destination.glob("*.json"):
            self.assertNotIn(str(self.base), report.read_text())
        self.args.output_dir = self.base / "second-output"
        _, second, second_hash = packager.package(self.root, self.args)
        self.assertEqual(first_hash, second_hash)
        self.assertEqual(archive.read_bytes(), second.read_bytes())
        with tarfile.open(archive) as stream:
            poweroff = stream.getmember(destination.name + "/assets/service/poweroff.sh")
            self.assertEqual(poweroff.mode, 0o755)
            for item in stream.getmembers():
                self.assertEqual((item.uid, item.gid, item.uname, item.gname), (0, 0, "", ""))
                self.assertFalse(item.issym() or item.islnk())
                self.assertNotIn("..", Path(item.name).parts)
                if any(item.name.endswith('/' + name) for name in packager.EXECUTABLE_ASSETS):
                    self.assertEqual(item.mode, 0o755)
        for name in packager.EXECUTABLE_ASSETS:
            self.assertTrue((destination / name).stat().st_mode & 0o111)
        for line in (destination / "SHA256SUMS").read_text().splitlines():
            expected, filename = line.split("  ", 1)
            self.assertEqual(expected, metadata.digest(destination / filename))

    def test_portable_input_identity_survives_relocation_including_zig_wrappers(self):
        zig = self.base / "toolchain-bin/zig"
        self.write(zig, "fixture Zig executable bytes")
        for compiler, tool in ((self.compiler, "cc"), (self.cxx_compiler, "c++")):
            self.write(compiler, "#!/bin/sh\nexport ZIG_GLOBAL_CACHE_DIR=" +
                       shlex.quote(str(self.root / "local/cache/global")) +
                       "\nexport ZIG_LOCAL_CACHE_DIR=" + shlex.quote(str(self.root / "local/cache/local")) +
                       '\nif [ "$#" -eq 1 ] && [ "$1" = --version ]; then\n  exec ' +
                       shlex.quote(str(zig)) + " " + tool + " --version\nfi\nexec " +
                       shlex.quote(str(zig)) + " " + tool +
                       ' -target aarch64-linux-gnu.2.41 "$@"\n')
        self.write(self.toolchain, 'set(CMAKE_FIND_ROOT_PATH "' + str(self.staging) + '")\n')
        config = metadata.read_json(self.voice / "build-config.json")
        config["flags"]["c"] += (" -I" + str(self.staging / "usr/include") +
                                 " -isystem " + shlex.quote(str(self.sdk / "include")) +
                                 " --sysroot=" + str(self.staging) +
                                 " -ffile-prefix-map=" + str(self.root) + "=. -DRESOURCE_PATH=/target/runtime")
        config["flags"]["exe_link"] += " -Wl,-rpath-link," + str(self.staging / "lib")
        metadata.write_json(self.voice / "build-config.json", config)
        self.refresh_source_marker(self.voice)
        self.seal(self.voice)
        before = metadata.build_inputs(self.root, self.voice)
        with tempfile.TemporaryDirectory(prefix="mybot-relocated-") as temporary:
            relocated = Path(temporary) / "different-parent"
            shutil.copytree(self.base, relocated)
            for filename in ("compiler", "cxx-compiler", "toolchain.cmake", "source/build-voice/build-config.json"):
                path = relocated / filename
                path.write_text(path.read_text().replace(str(self.base), str(relocated)))
            root = relocated / "source"
            build = root / "build-voice"
            after = metadata.build_inputs(root, build)
            self.assertNotEqual(before["config"], after["config"])
            self.assertNotEqual(before["dependencies"]["compiler"]["sha256"],
                                after["dependencies"]["compiler"]["sha256"])
            self.assertEqual(before["identity"], after["identity"])
            self.assertEqual(metadata.inputs_digest(before), metadata.inputs_digest(after))
            public_before = metadata.public_manifest({"inputs": before})
            public_after = metadata.public_manifest({"inputs": after})
            self.assertEqual(public_before, public_after)
            self.assertNotIn(str(self.base), json.dumps(public_before))
            self.assertNotIn(str(relocated), json.dumps(public_after))
            with self.assertRaisesRegex(ValueError, "Stale build manifest"):
                metadata.verify_manifest(root, build, "zh-CN", False)
            self.run_manifest("--prepare")
            with patch.object(manifest_tool, "ROOT", root), \
                    patch.object(sys, "argv", ["build-manifest.py", "--build-dir", str(build), "--prepare"]), \
                    redirect_stdout(io.StringIO()):
                manifest_tool.main()
            self.assertEqual((self.voice / "build_source_identity.c").read_bytes(),
                             (build / "build_source_identity.c").read_bytes())

    def test_portable_identity_keeps_semantic_flags_and_compiler_arguments(self):
        original = metadata.read_json(self.voice / "build-config.json")
        before = metadata.inputs_digest(metadata.build_inputs(self.root, self.voice))
        for option in (" -fno-exceptions", " -DNAME=first", " -DNAME=second",
                       " -DRESOURCE_PATH=" + str(self.root), " -I/unregistered/headers"):
            with self.subTest(option=option):
                config = json.loads(json.dumps(original))
                config["flags"]["cxx"] += option
                metadata.write_json(self.voice / "build-config.json", config)
                self.assertNotEqual(before, metadata.inputs_digest(metadata.build_inputs(self.root, self.voice)))
        metadata.write_json(self.voice / "build-config.json", original)
        zig = self.base / "zig"
        self.write(zig, "compiler contents")
        wrapper = ('#!/bin/sh\nexport ZIG_GLOBAL_CACHE_DIR=/tmp/fixture-global\n'
                   'export ZIG_LOCAL_CACHE_DIR=/tmp/fixture-local\nexec ' + shlex.quote(str(zig)) +
                   ' cc -target aarch64-linux-gnu.2.41 "$@"\n')
        self.write(self.compiler, wrapper)
        wrapped_before = metadata.inputs_digest(metadata.build_inputs(self.root, self.voice))
        self.write(self.compiler, wrapper.replace("aarch64-linux-gnu.2.41", "aarch64-linux-gnu.2.40"))
        self.assertNotEqual(wrapped_before, metadata.inputs_digest(metadata.build_inputs(self.root, self.voice)))
        self.write(self.compiler, wrapper.replace('"$@"', '$@'))
        self.assertNotEqual(wrapped_before, metadata.inputs_digest(metadata.build_inputs(self.root, self.voice)))
        self.write(self.compiler, wrapper)
        self.write(zig, "changed compiler contents")
        self.assertNotEqual(wrapped_before, metadata.inputs_digest(metadata.build_inputs(self.root, self.voice)))

    def test_dependency_directory_links_are_rejected_instead_of_silently_skipped(self):
        headers = self.base / 'headers'
        self.write(headers / 'api.h', 'first contents')
        (self.staging / 'include').symlink_to(headers, target_is_directory=True)
        with self.assertRaisesRegex(ValueError, 'symlink'):
            metadata.tree_snapshot(self.staging)
        self.write(headers / 'api.h', 'changed contents')
        with self.assertRaisesRegex(ValueError, 'symlink'):
            metadata.tree_snapshot(self.staging)
        (self.staging / 'include').unlink()
        (self.staging / 'include').symlink_to('usr', target_is_directory=True)
        with self.assertRaisesRegex(ValueError, 'Directory dependency symlinks'):
            metadata.tree_snapshot(self.staging)

    def test_dependency_file_links_bind_both_link_text_and_target_bytes(self):
        include = self.staging / 'usr/include'
        self.write(include / 'first.h', 'same contents')
        self.write(include / 'second.h', 'same contents')
        link = include / 'alias.h'
        link.symlink_to('first.h')
        before = metadata.tree_snapshot(self.staging)
        link.unlink()
        link.symlink_to('second.h')
        after = metadata.tree_snapshot(self.staging)
        self.assertNotEqual(before['sha256'], after['sha256'])
        self.write(include / 'second.h', 'changed contents')
        self.assertNotEqual(after['sha256'], metadata.tree_snapshot(self.staging)['sha256'])
        self.assertEqual(before['file_count'], after['file_count'])

    def test_dependency_links_cannot_cycle_escape_or_hide_missing_inputs(self):
        outside = self.base / 'external.h'
        self.write(outside, 'external contents')
        for target in ('missing', '.', '../external.h', str(outside), 'alias.h'):
            with self.subTest(target=target):
                link = self.staging / 'alias.h'
                link.symlink_to(target)
                with self.assertRaises(ValueError):
                    metadata.tree_snapshot(self.staging)
                link.unlink()
        directory_link = self.base / 'staging-alias'
        directory_link.symlink_to(self.staging, target_is_directory=True)
        with self.assertRaisesRegex(ValueError, 'Directory dependency symlinks'):
            metadata.tree_snapshot(directory_link)

    def test_new_packages_require_portable_identity_manifest_schema(self):
        manifest = metadata.read_json(self.voice / "build-manifest.json")
        manifest["schema"] = 1
        metadata.write_json(self.voice / "build-manifest.json", manifest)
        with self.assertRaisesRegex(ValueError, "Unsupported build manifest"):
            packager.package(self.root, self.args)

    def test_missing_video_is_rejected_in_all_profile(self):
        (self.video / "build-manifest.json").unlink()
        with self.assertRaisesRegex(ValueError, "valid JSON"):
            packager.package(self.root, self.args)
        self.assertFalse(self.args.output_dir.exists())

    def test_runtime_readme_matches_package_language_and_is_checksummed(self):
        for language, readme, other in (("zh-CN", "README.zh-CN.md", "README.en.md"),
                                       ("en-US", "README.en.md", "README.zh-CN.md")):
            with self.subTest(language=language):
                self.args.language = language
                self.args.output_dir = self.base / ("readme-" + language)
                if language == "en-US":
                    self.args.voice_build = self.create_build("voice-en", False, language)
                    self.args.video_build = self.create_build("video-en", True, language)
                destination, archive, _ = packager.package(self.root, self.args)
                packaged = (destination / readme).read_text()
                revision = metadata.read_json(destination / "package-manifest.json")["source"]["revision"]
                self.assertIn(f"/blob/{revision}/docs/user/deployment.zh-CN.md", packaged)
                self.assertNotIn("/blob/dev/", packaged)
                manual = "docs/device-operations." + ("en" if language == "en-US" else language) + ".pdf"
                self.assertIn("](" + manual + ")", packaged)
                self.assertEqual((destination / manual).read_bytes(), (self.root / manual).read_bytes())
                self.assertFalse((destination / other).exists())
                checksums = dict((name, checksum) for checksum, name in
                                 (line.split("  ", 1) for line in
                                  (destination / "SHA256SUMS").read_text().splitlines()))
                self.assertEqual(checksums[readme], metadata.digest(destination / readme))
                self.assertEqual(checksums[manual], metadata.digest(destination / manual))
                with tarfile.open(archive) as stream:
                    self.assertEqual(stream.extractfile(destination.name + "/" + readme).read(), packaged.encode())

    def test_packaged_authorization_preserves_text_and_resolves_source_links(self):
        statement = ('Official statement. [Project](../../LICENSE) and [AOSL](../../vendor/aosl/LICENSE).\n'
                     '[Notices](../../THIRD_PARTY_NOTICES.md), [RTSA](RTSA_SOURCE.txt), '
                     '[Provenance](../../VENDORED_SOURCES.md).\n'
                     '[Business](https://www.shengwang.cn/contact-sales/).\n')
        self.write(self.root / 'docs/dependencies/licensing.md', statement)
        root_notice = '[Terms](vendor/aosl/LICENSE), [font inputs](src/ui/fonts/sources.json), [source](VENDORED_SOURCES.md).\n'
        original_vendor_notice = '[Original upstream link](../aosl/LICENSE).\n'
        self.write(self.root / 'THIRD_PARTY_NOTICES.md', root_notice)
        self.write(self.root / 'vendor/mybot/THIRD_PARTY_NOTICES.md', original_vendor_notice)
        for build in (self.voice, self.video):
            self.refresh_source_marker(build)
            self.seal(build)
        destination, archive, _ = packager.package(self.root, self.args)
        filename = 'licenses/DEPENDENCY_LICENSING.md'
        actual = (destination / filename).read_text()
        strip_targets = lambda text: re.sub(r'\]\([^)]+\)', ']()', text)
        self.assertEqual(strip_targets(statement), strip_targets(actual))
        self.assertIn('https://www.shengwang.cn/contact-sales/', actual)
        revision = metadata.read_json(destination / "package-manifest.json")["source"]["revision"]
        self.assertIn(f"https://github.com/junlon2006/mybot-rv1126b/blob/{revision}/VENDORED_SOURCES.md", actual)
        packaged_root_notice = (destination / 'licenses/THIRD_PARTY_NOTICES.md').read_text()
        self.assertEqual(strip_targets(root_notice), strip_targets(packaged_root_notice))
        self.assertEqual((destination / 'licenses/mybot-THIRD_PARTY_NOTICES.md').read_text(), original_vendor_notice)
        for link in re.findall(r'\]\(([^)]+)\)', actual + packaged_root_notice):
            if not link.startswith('https://'):
                self.assertTrue((destination / 'licenses' / link).is_file(), link)
        with tarfile.open(archive) as stream:
            self.assertEqual(stream.extractfile(destination.name + '/' + filename).read(), actual.encode())

    def test_cjson_mit_notice_matches_upstream_and_complete_archive(self):
        sources = ('vendor/mybot/src/support/mybot_json.c', 'vendor/mybot/src/internal/mybot_json.h')
        expected = (ROOT / 'docs/dependencies/cjson-LICENSE.txt').read_bytes()
        before = {name: metadata.digest(ROOT / name) for name in sources}
        for name in sources:
            comments = re.findall(r'/\*(.*?)\*/', (ROOT / name).read_text(), re.S)
            comment = next(block for block in comments if 'Copyright (c) 2009 Dave Gamble' in block)
            self.assertEqual(expected, (textwrap.dedent(comment).strip() + '\n').encode())
        self.write(self.root / 'THIRD_PARTY_NOTICES.md', (ROOT / 'THIRD_PARTY_NOTICES.md').read_text())
        for build in (self.voice, self.video):
            self.refresh_source_marker(build)
            self.seal(build)
        destination, archive, _ = packager.package(self.root, self.args)
        filename = 'licenses/cjson-LICENSE.txt'
        self.assertEqual((destination / filename).read_bytes(), expected)
        self.assertIn('](cjson-LICENSE.txt)', (destination / 'licenses/THIRD_PARTY_NOTICES.md').read_text())
        checksums = dict((name, checksum) for checksum, name in
                         (line.split('  ', 1) for line in
                          (destination / 'SHA256SUMS').read_text().splitlines()))
        self.assertEqual(checksums[filename], metadata.digest(destination / filename))
        with tarfile.open(archive) as stream:
            self.assertEqual(stream.extractfile(destination.name + '/' + filename).read(), expected)
        self.assertEqual(before, {name: metadata.digest(ROOT / name) for name in sources})

    def test_example_environment_matches_each_package_and_is_reproducible(self):
        for language, server in (("zh-CN", "https://botstation.shengwang.cn/api"),
                                 ("en-US", "https://botstation.agora.io/api")):
            with self.subTest(language=language):
                self.args.language = language
                if language == "en-US":
                    self.args.voice_build = self.create_build("voice-en", False, language)
                    self.args.video_build = self.create_build("video-en", True, language)
                self.args.output_dir = self.base / ("environment-" + language)
                destination, archive, first_hash = packager.package(self.root, self.args)
                environment = (destination / "example.env").read_text()
                self.assertIn("UI language is " + language, environment)
                self.assertNotIn("@UI_LANGUAGE@", environment)
                self.assertNotIn("@DEFAULT_SERVER@", environment)
                actual_server = subprocess.check_output(
                    ["sh", "-c", '. "$1"; printf "%s" "$RV1126B_SERVER"',
                     "sh", str(destination / "example.env")], text=True)
                self.assertEqual(actual_server, server)
                self.assertNotRegex(environment, r"mybot-rv1126b-\d+\.\d+\.\d+")
                helper = next(line[2:] for line in environment.splitlines()
                              if line.startswith("# export RV1126B_TIME_SYNC_HELPER="))
                resolved = subprocess.check_output(
                    ["sh", "-c", helper + '\nprintf "%s" "$RV1126B_TIME_SYNC_HELPER"'],
                    cwd=destination, text=True)
                self.assertEqual(resolved, str(destination / "assets/time/sync-busybox.sh"))
                self.assertTrue(Path(resolved).is_file())
                checksums = dict((filename, expected) for expected, filename in
                                 (line.split("  ", 1) for line in
                                  (destination / "SHA256SUMS").read_text().splitlines()))
                self.assertEqual(checksums["example.env"], metadata.digest(destination / "example.env"))
                with tarfile.open(archive) as stream:
                    self.assertEqual(stream.extractfile(destination.name + "/example.env").read(),
                                     environment.encode())
                self.args.output_dir = self.base / ("environment-repeat-" + language)
                _, repeated, repeated_hash = packager.package(self.root, self.args)
                self.assertEqual(first_hash, repeated_hash)
                self.assertEqual(archive.read_bytes(), repeated.read_bytes())

    def test_unknown_environment_language_is_rejected(self):
        with self.assertRaisesRegex(ValueError, "Unsupported example.env language"):
            packager.example_environment(self.root, "fr-FR")

    def test_missing_or_duplicate_environment_markers_are_rejected_before_publication(self):
        original = (self.root / "packaging/example.env").read_text()
        for placeholder, label in (("@UI_LANGUAGE@", "UI language"),
                                   ("@DEFAULT_SERVER@", "default server")):
            for replacement in ("", placeholder + placeholder):
                with self.subTest(placeholder=placeholder, replacement=replacement):
                    self.write(self.root / "packaging/example.env",
                               original.replace(placeholder, replacement))
                    for build in (self.voice, self.video):
                        self.refresh_source_marker(build)
                        self.seal(build)
                    with self.assertRaisesRegex(ValueError, "exactly one " + label + " placeholder"):
                        packager.package(self.root, self.args)
                    self.assertFalse(self.args.output_dir.exists())

    def test_supervisor_tampering_is_rejected(self):
        self.write(self.voice / 'mybot-supervisor', 'changed supervisor')
        with self.assertRaisesRegex(ValueError, 'Stale ABI'):
            packager.package(self.root, self.args)
        self.assertFalse(self.args.output_dir.exists())

    def test_supervisor_requires_passing_abi(self):
        report = metadata.read_json(self.voice / 'abi-report-supervisor.json')
        report['errors'] = ['missing libc']
        metadata.write_json(self.voice / 'abi-report-supervisor.json', report)
        with self.assertRaisesRegex(ValueError, 'ABI report'):
            packager.package(self.root, self.args)

    def test_explicit_voice_profile_does_not_require_video(self):
        (self.video / "build-manifest.json").unlink()
        self.args.profile = "voice"
        destination, _, _ = packager.package(self.root, self.args)
        self.assertTrue(destination.name.endswith("-voice"))
        self.assertFalse((destination / "bin/mybot-rv1126b-video").exists())

    def test_video_only_launcher_selects_video(self):
        self.args.profile = "video"
        destination, _, _ = packager.package(self.root, self.args)
        self.assertIn("${RV1126B_VIDEO:-1}", (destination / "run.sh").read_text())

    def test_wrong_language_is_rejected(self):
        self.args.language = "en-US"
        with self.assertRaisesRegex(ValueError, "profile/language"):
            packager.package(self.root, self.args)

    def test_voice_binary_in_video_slot_is_rejected(self):
        self.args.video_build = self.voice
        with self.assertRaisesRegex(ValueError, "profile/language"):
            packager.package(self.root, self.args)

    def test_changed_executable_is_rejected(self):
        self.write(self.voice / "mybot-rv1126b", "changed binary")
        with self.assertRaisesRegex(ValueError, "Stale ABI report"):
            packager.package(self.root, self.args)

    def test_changed_abi_report_is_rejected(self):
        with (self.voice / "abi-report.json").open("a") as stream:
            stream.write("\n")
        with self.assertRaisesRegex(ValueError, "differs from manifest"):
            packager.package(self.root, self.args)

    def test_changed_rtsa_sdk_staging_compiler_and_toolchain_are_rejected(self):
        for path in (self.rtsa, self.sdk / "include/agora.h",
                     self.staging / "usr/include/alsa.h", self.compiler, self.cxx_compiler, self.toolchain):
            with self.subTest(path=path):
                old = path.read_text()
                path.write_text(old + " changed")
                with self.assertRaisesRegex(ValueError, "Stale build manifest"):
                    packager.package(self.root, self.args)
                path.write_text(old)

    def test_new_untracked_source_is_included_but_build_outputs_are_excluded(self):
        before = metadata.source_snapshot(self.root)
        self.write(self.root / "build-extra/unrelated", "build output")
        self.assertEqual(before, metadata.source_snapshot(self.root))
        self.write(self.root / "src/application/new.c", "new governance implementation")
        after = metadata.source_snapshot(self.root)
        self.assertNotEqual(before["sha256"], after["sha256"])
        with self.assertRaisesRegex(ValueError, "Stale build manifest"):
            packager.package(self.root, self.args)

    def test_documentation_commit_and_runtime_resources_reuse_verified_binaries(self):
        original = metadata.read_json(self.voice / "build-manifest.json")
        compiled = original["inputs"]["source"]
        readme = "packaging/README.zh-CN.md"
        pcm = "vendor/mybot/assets/locales/zh-CN/0.pcm"
        for name in ("docs/validation/README.md", "src/ui/views/pixel_glasses/PIXEL_GLASSES_SOURCES.md",
                     readme, pcm, "LICENSE"):
            self.write(self.root / name, "Updated material for " + name)
            current = metadata.source_snapshot(self.root)
            self.assertEqual(compiled["sha256"], current["sha256"], name)
        subprocess.run(["git", "-C", str(self.root), "add", "."], check=True)
        subprocess.run(["git", "-C", str(self.root), "-c", "user.name=Fixture",
                        "-c", "user.email=fixture@example.invalid", "commit", "-qm", "materials"], check=True)
        packaged_at = metadata.checkout_origin(self.root)
        self.assertNotEqual(compiled["revision"], packaged_at["revision"])
        destination, _, _ = packager.package(self.root, self.args)
        self.assertEqual(metadata.read_json(destination / "package-manifest.json")["source"], packaged_at)
        public = metadata.read_json(destination / "build-manifest-voice.json")
        self.assertEqual(public["inputs"]["source"], compiled)
        self.assertEqual(metadata.digest(destination / "bin/mybot-rv1126b"), original["binary_sha256"])
        for source, installed in ((readme, "README.zh-CN.md"), (pcm, "assets/locales/zh-CN/0.pcm"),
                                  ("LICENSE", "licenses/project-APACHE-2.0.txt")):
            self.assertEqual((destination / installed).read_bytes(), (self.root / source).read_bytes())
        self.assertIn("  package-manifest.json\n", (destination / "SHA256SUMS").read_text())

    def test_compiled_ui_portal_and_shared_state_mapping_invalidate_binary(self):
        for name in ("src/ui/src/renderer.c", "src/platform/src/services/wifi/portal.html",
                     "src/platform/include/mybot_platform/sdk_states.def", "scripts/embed-wifi-portal.py"):
            with self.subTest(name=name):
                path = self.root / name
                self.write(path, "changed build input")
                with self.assertRaisesRegex(ValueError, "Stale build manifest"):
                    metadata.verify_manifest(self.root, self.voice, "zh-CN", False)
                path.unlink()

    def test_committing_dirty_build_does_not_upgrade_its_release_provenance(self):
        self.write(self.root / "src/application/production.c", "changed source")
        for build in (self.voice, self.video):
            self.refresh_source_marker(build)
            self.seal(build)
        subprocess.run(["git", "-C", str(self.root), "add", "src"], check=True)
        subprocess.run(["git", "-C", str(self.root), "-c", "user.name=Fixture",
                        "-c", "user.email=fixture@example.invalid", "commit", "-qm", "code"], check=True)
        self.args.release = True
        with self.assertRaisesRegex(ValueError, "binaries built from clean, committed source"):
            packager.package(self.root, self.args)

    def test_build_configuration_changes_require_rebuild(self):
        config = metadata.read_json(self.voice / "build-config.json")
        config["language"] = "en-US"
        metadata.write_json(self.voice / "build-config.json", config)
        with self.assertRaisesRegex(ValueError, "Stale build manifest"):
            packager.package(self.root, self.args)

    def test_release_requires_clean_committed_source(self):
        self.write(self.root / "src/application/new.c", "dirty source")
        self.refresh_source_marker(self.voice)
        self.refresh_source_marker(self.video)
        self.seal(self.voice)
        self.seal(self.video)
        self.args.release = True
        with self.assertRaisesRegex(ValueError, "clean, committed"):
            packager.package(self.root, self.args)

    def test_clean_release_requires_a_firmware_manifest(self):
        self.args.release = True
        with self.assertRaisesRegex(ValueError, "runtime manifest"):
            packager.package(self.root, self.args)
        self.assertFalse(self.args.output_dir.exists())

    def test_release_checks_staged_firmware_and_actual_elf_dependencies(self):
        # Minimal AArch64 ELF sections consumed by the real dependency reader.
        def elf(name="", needed=(), interpreter=""):
            strings = b"\0"
            dynamic = b""
            for tag, item in [(1, dep) for dep in needed] + ([(14, name)] if name else []):
                dynamic += struct.pack("<qQ", tag, len(strings))
                strings += item.encode() + b"\0"
            names = b"\0.shstrtab\0.interp\0.dynstr\0.dynamic\0"
            offset = 64
            table = bytes(64)
            contents = b""
            for section, data in ((".shstrtab", names), (".interp", interpreter.encode() + b"\0"),
                                  (".dynstr", strings), (".dynamic", dynamic)):
                table += struct.pack("<IIQQQQIIQQ", names.index(section.encode()), 0, 0, 0,
                                     offset, len(data), 0, 0, 0, 0)
                contents += data
                offset += len(data)
            header = struct.pack("<16sHHIQQQIHHHHHH", b"\x7fELF\x02\x01", 3, 183, 1, 0, 0,
                                 offset, 0, 64, 0, 0, 64, 5, 1)
            return header + contents + table

        loader = self.staging / "lib/ld-linux-aarch64.so.1"
        loader.parent.mkdir()
        loader.write_bytes(elf(name=loader.name))
        loader_hash = metadata.digest(loader)
        lock = {"external": {"runtime": {"libraries": {"lib/" + loader.name: loader_hash}}}}
        metadata.write_json(self.root / "dependencies.lock.json", lock)
        manifest = self.base / "runtime.json"
        metadata.write_json(manifest, {"libraries": [{"path": "/lib/" + loader.name, "sha256": loader_hash}]})
        sdk = {"staging_dir": str(self.staging), "runtime_manifest": str(manifest), "rtsa_library": str(self.rtsa)}
        binary = self.base / "payload"
        binary.write_bytes(elf(interpreter="/lib/" + loader.name))
        reports = [(binary, {"checked": [{"path": str(binary)}, {"path": str(loader)}]})]
        packager.verify_release_runtime(self.root, sdk, reports)
        binary.write_bytes(elf(needed=("libanother.so",), interpreter="/lib/" + loader.name))
        with self.assertRaisesRegex(ValueError, "Missing firmware ABI closure: libanother.so"):
            packager.verify_release_runtime(self.root, sdk, reports)
        loader.write_bytes(loader.read_bytes() + b"changed")
        with self.assertRaisesRegex(ValueError, "staged runtime SHA256 mismatch"):
            packager.verify_release_runtime(self.root, sdk, reports)

    def test_existing_bundle_is_never_overwritten(self):
        destination, _, _ = packager.package(self.root, self.args)
        marker = destination / "local-note"
        marker.write_text("preserve me")
        with self.assertRaisesRegex(ValueError, "already exists"):
            packager.package(self.root, self.args)
        self.assertEqual(marker.read_text(), "preserve me")

    def test_unexpected_rpath_is_rejected(self):
        report = metadata.read_json(self.voice / "abi-report.json")
        report["binary"]["runpaths"] = ["/usr/local/lib"]
        metadata.write_json(self.voice / "abi-report.json", report)
        self.seal(self.voice)
        with self.assertRaisesRegex(ValueError, "RPATH"):
            packager.package(self.root, self.args)

    def test_resealed_old_executable_cannot_claim_new_configuration(self):
        config = metadata.read_json(self.voice / "build-config.json")
        config["language"] = "en-US"
        metadata.write_json(self.voice / "build-config.json", config)
        self.seal(self.voice)
        self.args.profile = "voice"
        self.args.language = "en-US"
        with self.assertRaisesRegex(ValueError, "build identity"):
            packager.package(self.root, self.args)

    def test_incomplete_abi_closure_is_rejected(self):
        report = metadata.read_json(self.voice / "abi-report.json")
        report["checked"] = report["checked"][1:]
        metadata.write_json(self.voice / "abi-report.json", report)
        self.seal(self.voice)
        with self.assertRaisesRegex(ValueError, "Incomplete ABI"):
            packager.package(self.root, self.args)

    def test_unsafe_archive_symlink_is_rejected(self):
        tree = self.base / "unsafe"
        tree.mkdir()
        (tree / "outside").symlink_to(self.rtsa)
        with self.assertRaisesRegex(ValueError, "Unsupported package entry"):
            packager.deterministic_archive(tree, self.base / "unsafe.tar.gz", 0)

    def test_invalid_epoch_is_rejected(self):
        with patch.dict(os.environ, {"SOURCE_DATE_EPOCH": "-1"}):
            with self.assertRaisesRegex(ValueError, "SOURCE_DATE_EPOCH"):
                packager.package(self.root, self.args)

    def run_manifest(self, *arguments):
        with patch.object(manifest_tool, "ROOT", self.root), \
                patch.object(sys, "argv", ["build-manifest.py", "--build-dir", str(self.voice), *arguments]), \
                redirect_stdout(io.StringIO()):
            manifest_tool.main()

    def test_preparation_invalidates_previous_manifest_and_finalization_succeeds(self):
        self.run_manifest("--prepare")
        self.assertFalse((self.voice / "build-manifest.json").exists())
        self.assertTrue((self.voice / "build-inputs.json").exists())
        self.run_manifest()
        metadata.verify_manifest(self.root, self.voice, "zh-CN", False)

    def test_unchanged_source_identity_is_not_rewritten(self):
        self.run_manifest("--prepare")
        source = self.voice / "build_source_identity.c"
        expected = metadata.source_snapshot(self.root)["sha256"]
        self.assertIn('"MYBOT_RV1126B_SOURCE:' + expected + '\\0"', source.read_text())
        os.utime(source, (1000000000, 1000000000))
        previous_time = source.stat().st_mtime_ns
        self.run_manifest("--prepare")
        self.assertEqual(previous_time, source.stat().st_mtime_ns)

    def test_preparing_changed_source_cannot_reseal_old_binary_with_same_config(self):
        self.write(self.root / "src/application/new.c", "new source, unchanged version and configuration")
        self.run_manifest("--prepare")
        error = io.StringIO()
        with redirect_stderr(error), self.assertRaises(SystemExit) as raised:
            self.run_manifest()
        self.assertEqual(raised.exception.code, 2)
        self.assertIn("Executable source identity", error.getvalue())
        self.assertFalse((self.voice / "build-manifest.json").exists())

    def test_preparing_changed_sdk_cannot_reseal_old_binary_with_same_source(self):
        before = metadata.source_snapshot(self.root)
        self.write(self.sdk / "include/agora.h", "changed SDK header, unchanged source")
        self.assertEqual(before, metadata.source_snapshot(self.root))
        self.run_manifest("--prepare")
        error = io.StringIO()
        with redirect_stderr(error), self.assertRaises(SystemExit) as raised:
            self.run_manifest()
        self.assertEqual(raised.exception.code, 2)
        self.assertIn("Executable inputs identity", error.getvalue())
        self.assertFalse((self.voice / "build-manifest.json").exists())

    def test_preparing_changed_cxx_wrapper_cannot_reseal_old_binary(self):
        self.write(self.cxx_compiler, "changed C++ compiler wrapper")
        self.run_manifest("--prepare")
        error = io.StringIO()
        with redirect_stderr(error), self.assertRaises(SystemExit) as raised:
            self.run_manifest()
        self.assertEqual(raised.exception.code, 2)
        self.assertIn("Executable inputs identity", error.getvalue())
        self.assertFalse((self.voice / "build-manifest.json").exists())

    def test_changed_effective_flags_require_rebuild(self):
        config = metadata.read_json(self.voice / "build-config.json")
        config["flags"]["cxx"] += " -fno-exceptions"
        metadata.write_json(self.voice / "build-config.json", config)
        with self.assertRaisesRegex(ValueError, "Stale build manifest"):
            packager.package(self.root, self.args)
        self.run_manifest("--prepare")
        error = io.StringIO()
        with redirect_stderr(error), self.assertRaises(SystemExit) as raised:
            self.run_manifest()
        self.assertEqual(raised.exception.code, 2)
        self.assertIn("Executable inputs identity", error.getvalue())

    def test_cxx_metadata_and_string_flags_are_required(self):
        original = metadata.read_json(self.voice / "build-config.json")
        for key in ("cxx_compiler", "cxx_compiler_id", "cxx_compiler_version"):
            config = json.loads(json.dumps(original))
            del config["toolchain"][key]
            metadata.write_json(self.voice / "build-config.json", config)
            with self.subTest(field=key), self.assertRaisesRegex(ValueError, "Missing toolchain"):
                metadata.load_config(self.root, self.voice)
        for flags in (None, [], {"c": 1}, {"c": "-O2"}):
            config = json.loads(json.dumps(original))
            config["flags"] = flags
            metadata.write_json(self.voice / "build-config.json", config)
            with self.subTest(flags=flags), self.assertRaisesRegex(ValueError, "Build flags"):
                metadata.load_config(self.root, self.voice)

    def test_cli_default_builds_follow_selected_language(self):
        for language, suffix in (("zh-CN", "zh"), ("en-US", "en")):
            with self.subTest(language=language), patch.object(packager, "ROOT", self.root), \
                    patch.object(sys, "argv", ["package-rv1126b.py", "--language", language]), \
                    patch.object(packager, "package", return_value=("bundle", "archive", "sha")) as create, \
                    redirect_stdout(io.StringIO()):
                packager.main()
                args = create.call_args.args[1]
                self.assertEqual(args.voice_build, self.root / "build" / ("rv1126b-voice-" + suffix))
                self.assertEqual(args.video_build, self.root / "build" / ("rv1126b-video-" + suffix))

    def test_source_change_during_build_prevents_finalization(self):
        self.run_manifest("--prepare")
        self.write(self.root / "src/application/new.c", "changed while compilation was running")
        error = io.StringIO()
        with redirect_stderr(error), self.assertRaises(SystemExit) as raised:
            self.run_manifest()
        self.assertEqual(raised.exception.code, 2)
        self.assertIn("Inputs changed during build", error.getvalue())
        self.assertFalse((self.voice / "build-manifest.json").exists())


if __name__ == "__main__":
    unittest.main()
