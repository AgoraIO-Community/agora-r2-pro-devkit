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
import subprocess
import sys
import tarfile
import tempfile
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
        self.write(self.root / "CMakeLists.txt", "project(mybot_rv1126b VERSION 0.2.0 LANGUAGES C CXX)\n")
        self.write(self.root / ".gitignore", "/build*/\n/local/\n/dist/\n")
        self.write(self.root / "packaging/run.sh", 'case "${RV1126B_VIDEO:-0}" in\n0) :;;\n1) :;;\nesac\n')
        self.write(self.root / "packaging/example.env", "# fixture\n")
        self.write(self.root / "packaging/README.zh-CN.md", "fixture guide\n")
        self.write(self.root / "third_party/mybot/assets/locales/zh-CN/0.pcm", "pcm")
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

    def create_build(self, name, video):
        build = self.root / ("build-" + name)
        build.mkdir()
        config = {"schema": 1, "version": "0.2.0", "language": "zh-CN", "video": video,
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
        marker = ("MYBOT_RV1126B_BUILD_CONFIG:0.2.0;zh-CN;video=" + str(int(video)) +
                  ";ptime_ms=20;board=" + metadata.BOARD + "\0")
        inputs = metadata.build_inputs(self.root, build)
        source_marker = "MYBOT_RV1126B_SOURCE:" + inputs["source"]["sha256"] + "\0"
        inputs_marker = "MYBOT_RV1126B_INPUTS:" + metadata.json_digest(inputs) + "\0"
        self.write(build / "mybot-rv1126b", "executable " + name + "\0" + marker + source_marker + inputs_marker)
        binary = {"path": str(build / "mybot-rv1126b"), "sha256": metadata.digest(build / "mybot-rv1126b"),
                  "machine": 183, "soname": None, "interpreter": "/lib/ld-linux-aarch64.so.1",
                  "needed": ["libagora-rtc-sdk.so"], "runpaths": [metadata.RPATH]}
        rtsa = {"path": str(self.rtsa), "sha256": metadata.digest(self.rtsa),
                "machine": 183, "soname": "libagora-rtc-sdk.so", "runpaths": []}
        metadata.write_json(build / "abi-report.json", {"errors": [], "binary": binary,
                            "dependency_count": 1, "checked": [binary, rtsa]})
        self.seal(build)
        return build

    def seal(self, build):
        metadata.write_json(build / "build-manifest.json",
                            {"schema": 1, "inputs": metadata.build_inputs(self.root, build),
                             "binary_sha256": metadata.digest(build / "mybot-rv1126b"),
                             "abi_report_sha256": metadata.digest(build / "abi-report.json")})

    def refresh_source_marker(self, build):
        binary = build / "mybot-rv1126b"
        inputs = metadata.build_inputs(self.root, build)
        executable = binary.read_bytes()
        for field, value in (("SOURCE", inputs["source"]["sha256"]), ("INPUTS", metadata.json_digest(inputs))):
            current = ("MYBOT_RV1126B_" + field + ":" + value).encode() + b"\0"
            executable = re.sub(("MYBOT_RV1126B_" + field + ":[0-9a-f]{64}").encode() + b"\0",
                                current, executable)
        binary.write_bytes(executable)
        report = metadata.read_json(build / "abi-report.json")
        for entry in [report["binary"], *report["checked"]]:
            if entry["path"] == str(binary):
                entry["sha256"] = metadata.digest(binary)
        metadata.write_json(build / "abi-report.json", report)

    def test_archive_is_reproducible_and_public_metadata_has_no_host_paths(self):
        destination, archive, first_hash = packager.package(self.root, self.args)
        for report in destination.glob("*.json"):
            self.assertNotIn(str(self.base), report.read_text())
        self.args.output_dir = self.base / "second-output"
        _, second, second_hash = packager.package(self.root, self.args)
        self.assertEqual(first_hash, second_hash)
        self.assertEqual(archive.read_bytes(), second.read_bytes())
        with tarfile.open(archive) as stream:
            for item in stream.getmembers():
                self.assertEqual((item.uid, item.gid, item.uname, item.gname), (0, 0, "", ""))
                self.assertFalse(item.issym() or item.islnk())
                self.assertNotIn("..", Path(item.name).parts)
        for line in (destination / "SHA256SUMS").read_text().splitlines():
            expected, filename = line.split("  ", 1)
            self.assertEqual(expected, metadata.digest(destination / filename))

    def test_missing_video_is_rejected_in_all_profile(self):
        (self.video / "build-manifest.json").unlink()
        with self.assertRaisesRegex(ValueError, "valid JSON"):
            packager.package(self.root, self.args)
        self.assertFalse(self.args.output_dir.exists())

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
        self.write(self.root / "app/new.c", "new governance implementation")
        after = metadata.source_snapshot(self.root)
        self.assertNotEqual(before["sha256"], after["sha256"])
        with self.assertRaisesRegex(ValueError, "Stale build manifest"):
            packager.package(self.root, self.args)

    def test_build_configuration_changes_require_rebuild(self):
        config = metadata.read_json(self.voice / "build-config.json")
        config["language"] = "en-US"
        metadata.write_json(self.voice / "build-config.json", config)
        with self.assertRaisesRegex(ValueError, "Stale build manifest"):
            packager.package(self.root, self.args)

    def test_release_requires_clean_committed_source(self):
        self.write(self.root / "app/new.c", "dirty source")
        self.refresh_source_marker(self.voice)
        self.refresh_source_marker(self.video)
        self.seal(self.voice)
        self.seal(self.video)
        self.args.release = True
        with self.assertRaisesRegex(ValueError, "clean, committed"):
            packager.package(self.root, self.args)

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
        self.write(self.root / "app/new.c", "new source, unchanged version and configuration")
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
        self.write(self.root / "app/new.c", "changed while compilation was running")
        error = io.StringIO()
        with redirect_stderr(error), self.assertRaises(SystemExit) as raised:
            self.run_manifest()
        self.assertEqual(raised.exception.code, 2)
        self.assertIn("Inputs changed during build", error.getvalue())
        self.assertFalse((self.voice / "build-manifest.json").exists())


if __name__ == "__main__":
    unittest.main()
