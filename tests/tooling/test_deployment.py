# SPDX-License-Identifier: Apache-2.0
"""Deployment safeguards use public archive fixtures and mocked ADB only."""

from contextlib import redirect_stdout
import importlib.util
import io
import json
from pathlib import Path
import re
import shlex
import struct
import subprocess
import sys
import tarfile
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location("deployment", ROOT / "scripts/deploy-rv1126b.py")
deployment = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(deployment)


def target_elf(payload=b""):
    value = bytearray(64)
    value[:6] = b"\x7fELF\x02\x01"
    struct.pack_into("<HH", value, 16, 3, 183)
    return bytes(value) + payload


class DeploymentTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.base = Path(self.temporary.name)
        self.name = "mybot-rv1126b-0.4.0-aarch64"
        marker = ("MYBOT_RV1126B_BUILD_CONFIG:0.4.0;zh-CN;video=0;ptime_ms=20;"
                  "board=rv1126b-test2-rk805b-v10\0MYBOT_RV1126B_SOURCE:" + "a" * 64 +
                  "\0MYBOT_RV1126B_INPUTS:" + "b" * 64 + "\0")
        binary = target_elf(marker.encode())
        report = {"binary": {"machine": 183, "sha256": deployment.sha256(binary),
                             "runpaths": ["$ORIGIN/../lib"]}, "errors": []}
        report_bytes = json.dumps(report).encode()
        manifest = {"schema": 1, "binary_sha256": deployment.sha256(binary),
                    "abi_report_sha256": deployment.sha256(report_bytes),
                    "inputs": {"config": {"version": "0.4.0", "language": "zh-CN", "video": False,
                                           "board": "rv1126b-test2-rk805b-v10", "ptime_ms": 20},
                               "source": {"sha256": "a" * 64}}}
        self.files = {"bin/mybot-rv1126b": binary, "bin/mybot-supervisor": binary,
                      "lib/libagora-rtc-sdk.so": target_elf(), "run.sh": b"#!/bin/sh\n",
                      "abi-report-voice.json": report_bytes,
                      "build-manifest-voice.json": json.dumps(manifest).encode()}
        self.files["abi-report-supervisor.json"] = report_bytes
        self.files["supervisor-manifest.json"] = json.dumps({**manifest, "role": "supervisor"}).encode()
        for name in deployment.SERVICE_ASSETS:
            self.files.setdefault(name, b"#!/bin/sh\n")
        self.settings = {"server": "https://example.invalid/api", "device_id": "FIXTURE-ONLY",
                         "video": False, "wifi_interface": "wlan0"}

    def archive(self, files=None, extra=None, name=None):
        files = dict(self.files if files is None else files)
        checksums = "".join(deployment.sha256(data) + "  " + path + "\n"
                            for path, data in sorted(files.items()))
        files["SHA256SUMS"] = checksums.encode()
        archive_path = self.base / "package.tar.gz"
        with tarfile.open(archive_path, "w:gz") as archive:
            for path, data in files.items():
                info = tarfile.TarInfo((name or self.name) + "/" + path)
                info.size, info.mode = len(data), 0o644
                archive.addfile(info, io.BytesIO(data))
            if extra is not None:
                archive.addfile(extra)
        return archive_path, deployment.sha256(archive_path.read_bytes())

    def test_valid_archive_binds_checksums_elf_and_manifest(self):
        path, checksum = self.archive()
        value = deployment.verify_package(path, checksum)
        self.assertEqual(value["profiles"], ["voice"])
        self.assertEqual(value["name"], self.name)

    def test_archive_digest_is_required_and_verified(self):
        path, _ = self.archive()
        for checksum in (None, "short", "0" * 64):
            with self.subTest(checksum=checksum), self.assertRaises(ValueError):
                deployment.verify_package(path, checksum)

    def test_traversal_and_symlink_members_are_rejected(self):
        for name, kind in (("../escape", tarfile.REGTYPE),
                           (self.name + "/link", tarfile.SYMTYPE)):
            extra = tarfile.TarInfo(name)
            extra.type, extra.linkname = kind, "/etc/passwd"
            path, checksum = self.archive(extra=extra)
            with self.subTest(name=name), self.assertRaisesRegex(ValueError, "Unsafe"):
                deployment.verify_package(path, checksum)

    def test_duplicate_members_are_rejected(self):
        extra = tarfile.TarInfo(self.name + "/run.sh")
        path, checksum = self.archive(extra=extra)
        with self.assertRaisesRegex(ValueError, "duplicate"):
            deployment.verify_package(path, checksum)

    def test_member_tampering_is_rejected(self):
        path, checksum = self.archive()
        with tarfile.open(path) as archive:
            entries = [(item, archive.extractfile(item).read()) for item in archive]
        with tarfile.open(path, "w:gz") as archive:
            for item, data in entries:
                if item.name.endswith("run.sh"):
                    data = b"tampered\n"
                    item.size = len(data)
                archive.addfile(item, io.BytesIO(data))
        checksum = deployment.sha256(path.read_bytes())
        with self.assertRaisesRegex(ValueError, "member SHA256"):
            deployment.verify_package(path, checksum)

    def test_wrong_elf_architecture_is_rejected(self):
        broken = dict(self.files)
        value = bytearray(broken["bin/mybot-rv1126b"])
        struct.pack_into("<H", value, 18, 62)
        broken["bin/mybot-rv1126b"] = bytes(value)
        path, checksum = self.archive(broken)
        with self.assertRaisesRegex(ValueError, "AArch64"):
            deployment.verify_package(path, checksum)

    def test_stale_manifest_or_abi_report_is_rejected(self):
        for filename in ("build-manifest-voice.json", "abi-report-voice.json"):
            files = dict(self.files)
            value = json.loads(files[filename])
            if filename.startswith("build"):
                value["binary_sha256"] = "0" * 64
            else:
                value["errors"] = ["missing dependency"]
            files[filename] = json.dumps(value).encode()
            path, checksum = self.archive(files)
            with self.subTest(filename=filename), self.assertRaises(ValueError):
                deployment.verify_package(path, checksum)

    def test_missing_deployment_assets_prevent_installation(self):
        files = dict(self.files)
        del files["assets/service/S99mybot"]
        path, checksum = self.archive(files)
        with self.assertRaisesRegex(ValueError, "deployment assets"):
            deployment.verify_package(path, checksum)

    def test_supervisor_has_independent_role_hash_and_provenance_guards(self):
        for field, value in (("role", "voice"), ("binary_sha256", "0" * 64),
                             ("abi_report_sha256", "0" * 64)):
            files = dict(self.files)
            manifest = json.loads(files["supervisor-manifest.json"])
            manifest[field] = value
            files["supervisor-manifest.json"] = json.dumps(manifest).encode()
            path, checksum = self.archive(files)
            with self.subTest(field=field), self.assertRaisesRegex(ValueError, "Supervisor"):
                deployment.verify_package(path, checksum)

    def test_video_only_supervisor_uses_selected_profile_markers(self):
        files = dict(self.files)
        binary = files.pop("bin/mybot-rv1126b").replace(b"video=0", b"video=1")
        report = json.loads(files.pop("abi-report-voice.json"))
        report["binary"]["sha256"] = deployment.sha256(binary)
        report_bytes = json.dumps(report).encode()
        manifest = json.loads(files.pop("build-manifest-voice.json"))
        manifest["inputs"]["config"]["video"] = True
        manifest["binary_sha256"] = deployment.sha256(binary)
        manifest["abi_report_sha256"] = deployment.sha256(report_bytes)
        files.update({"bin/mybot-rv1126b-video": binary, "bin/mybot-supervisor": binary,
                      "abi-report-video.json": report_bytes, "abi-report-supervisor.json": report_bytes,
                      "build-manifest-video.json": json.dumps(manifest).encode(),
                      "supervisor-manifest.json": json.dumps({**manifest, "role": "supervisor"}).encode()})
        path, checksum = self.archive(files, name=self.name + "-video")
        self.assertEqual(deployment.verify_package(path, checksum)["profiles"], ["video"])

    def test_only_accepted_legacy_format_bypasses_build_manifest(self):
        files = dict(self.files)
        del files["build-manifest-voice.json"]
        path, checksum = self.archive(files, name="mybot-rv1126b-0.1.1-aarch64")
        self.assertEqual(deployment.verify_package(path, checksum, legacy=True)["version"], "0.1.1")
        with self.assertRaises(ValueError):
            deployment.verify_package(path, checksum)
        path, checksum = self.archive(files)
        with self.assertRaisesRegex(ValueError, "0.1.1"):
            deployment.verify_package(path, checksum, legacy=True)

    def test_settings_reject_credentials_unknown_fields_and_injection(self):
        for update in ({"token": "fixture-secret"}, {"device_id": "bad\nidentity"},
                       {"server": "http://example.invalid"}, {"wifi_interface": "wlan0;id"},
                       {"video": 1}):
            path = self.base / "settings.json"
            path.write_text(json.dumps({**self.settings, **update}))
            path.chmod(0o600)
            with self.subTest(update=update), self.assertRaises(ValueError):
                deployment.read_settings(path)

    def test_configuration_preserves_private_data_and_single_wifi_source(self):
        value = deployment.configuration(b"# fixture\n", self.settings)
        self.assertIn("MYBOT_KV_STORE_DIR=/userdata/mybot-rv1126b", value)
        self.assertIn("MYBOT_SYSTEM_WPA_CONFIG=/etc/wpa_supplicant.conf", value)
        self.assertNotIn("device_auth", value)
        self.assertNotIn("wifi.credentials", value)

    def test_usb_dns_policy_preserves_address_range_and_handles_late_address(self):
        program = re.search(r"awk '([^']+)' /etc/dnsmasq.conf", deployment.SYSTEM_POLICY)[1]
        original = ("interface=old0\nbind-interfaces\nbind-dynamic\n"
                    "listen-address=10.201.126.1\ndhcp-range=10.201.126.2,10.201.126.200,12h\n")
        result = subprocess.run(["awk", program], input=original, text=True, capture_output=True, check=True)
        self.assertIn("listen-address=10.201.126.1\n", result.stdout)
        self.assertIn("dhcp-range=10.201.126.2,10.201.126.200,12h\n", result.stdout)
        self.assertNotIn("bind-interfaces", result.stdout)
        self.assertEqual(result.stdout.count("bind-dynamic\n"), 1)
        self.assertEqual(result.stdout.count("interface=usb0\n"), 1)
        again = subprocess.run(["awk", program], input=result.stdout, text=True, capture_output=True, check=True)
        self.assertEqual(result.stdout, again.stdout)

    def test_identity_comparison_never_reads_token_region(self):
        auth = self.base / "device_auth"
        token = b"FIXTURE-TOKEN-MUST-STAY-ON-DEVICE"
        auth.write_bytes(struct.pack("<I", 1) + self.settings["server"].encode().ljust(640, b"\0") +
                         self.settings["device_id"].encode().ljust(128, b"\0") + token.ljust(512, b"\0"))
        with patch.object(deployment, "DATA_ROOT", str(self.base)):
            script = deployment.identity_check(self.settings)
        # Provide portable metadata-only stat for macOS and Linux host runs.
        prefix = "set -eu\nstat() { printf '1284\\n'; }\n"
        result = subprocess.run(["sh", "-s"], input=prefix + script, text=True, capture_output=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "auth=match\n")
        self.assertNotIn(token.decode(), script + result.stdout + result.stderr)
        self.assertNotIn("skip=772", script)
        wrong = {**self.settings, "device_id": "DIFFERENT"}
        with patch.object(deployment, "DATA_ROOT", str(self.base)):
            script = deployment.identity_check(wrong)
        result = subprocess.run(["sh", "-s"], input=prefix + script, text=True, capture_output=True)
        self.assertEqual(result.returncode, 24)
        self.assertNotIn(token.decode(), result.stdout + result.stderr)

    def test_mutating_actions_default_to_no_adb_and_redacted_plan(self):
        path, checksum = self.archive()
        settings = self.base / "settings.json"
        settings.write_text(json.dumps(self.settings))
        settings.chmod(0o600)
        output = io.StringIO()
        with patch.object(deployment, "Adb") as adb, redirect_stdout(output):
            self.assertEqual(deployment.main(["deploy", "--package", str(path), "--sha256", checksum,
                                              "--settings", str(settings)]), 0)
        adb.assert_not_called()
        self.assertTrue(json.loads(output.getvalue())["dry_run"])
        self.assertNotIn(self.settings["device_id"], output.getvalue())
        self.assertNotIn(self.settings["server"], output.getvalue())

    def test_adb_selection_requires_an_unambiguous_authorized_device(self):
        result = subprocess.CompletedProcess([], 0, "List of devices attached\na\tdevice\nb\tdevice\n", "")
        with patch.object(deployment.subprocess, "run", return_value=result):
            with self.assertRaisesRegex(ValueError, "exactly one"):
                deployment.Adb().select()
            adb = deployment.Adb(serial="b")
            adb.select()
            self.assertEqual(adb.serial, "b")
            with self.assertRaisesRegex(ValueError, "not connected"):
                deployment.Adb(serial="unavailable").select()

    def test_adb_failures_do_not_echo_remote_credentials(self):
        result = subprocess.CompletedProcess([], 24, "fixture-secret-token", "fixture-wifi-password")
        with patch.object(deployment.subprocess, "run", return_value=result):
            with self.assertRaisesRegex(RuntimeError, "exit 24") as error:
                deployment.Adb(serial="fixture").shell("exit 24")
        self.assertNotIn("fixture-secret", str(error.exception))
        self.assertNotIn("fixture-wifi", str(error.exception))

    def test_execute_deploy_backs_up_before_upload_and_stops_before_switch(self):
        path, checksum = self.archive()
        settings = self.base / "settings.json"
        settings.write_text(json.dumps(self.settings))
        settings.chmod(0o600)
        output = io.StringIO()
        with patch.object(deployment, "Adb") as adb_class, patch.object(deployment, "receipt") as receipt:
            adb = adb_class.return_value
            adb.shell.return_value = "backup_sha256=" + "b" * 64 + "\n"
            with redirect_stdout(output):
                deployment.main(["deploy", "--package", str(path), "--sha256", checksum,
                                 "--settings", str(settings), "--tag", "fixture", "--execute"])
            calls = adb.method_calls
            backup_index = next(i for i, call in enumerate(calls)
                                if call[0] == "shell" and "tar -C / -czf" in call[1][0])
            upload_index = next(i for i, call in enumerate(calls) if call[0] == "command")
            self.assertLess(backup_index, upload_index)
            install = next(call[1][0] for call in calls
                           if call[0] == "shell" and "stock-disabled.conf" in call[1][0])
            self.assertLess(install.index("S99mybot stop"), install.index(".current-$tag"))
            self.assertNotIn("reboot", install)
            self.assertNotIn(self.settings["device_id"], json.dumps(receipt.call_args.args[2]))
            syntax = subprocess.run(["sh", "-n"], input=install, text=True, capture_output=True)
            self.assertEqual(syntax.returncode, 0, syntax.stderr)
            self.assertEqual(calls[-1][1][0], "rmdir /run/mybot-deploy.lock\n")
        self.assertNotIn(self.settings["device_id"], output.getvalue())

    def test_private_settings_permissions_are_enforced(self):
        path = self.base / "settings.json"
        path.write_text(json.dumps(self.settings))
        path.chmod(0o644)
        with self.assertRaisesRegex(ValueError, "private regular"):
            deployment.read_settings(path)

    def test_atomic_installer_verifies_bytes_and_rejects_link_targets(self):
        source, target = self.base / "source file", self.base / "target file"
        source.write_text("fixture content\n")
        target.write_text("old content\n")
        digest_command = shlex.quote(sys.executable) + " -c " + shlex.quote(
            "import hashlib,sys; print(hashlib.sha256(open(sys.argv[1],'rb').read()).hexdigest())")
        prefix = 'set -eu\ntag=fixture\nsha256sum() { ' + digest_command + ' "$1"; }\n'
        call = "install_file " + shlex.quote(str(source)) + " " + shlex.quote(str(target)) + " 600\n"
        result = subprocess.run(["sh", "-s"], input=prefix + deployment.INSTALL_FILE + call,
                                text=True, capture_output=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(target.read_bytes(), source.read_bytes())
        self.assertEqual(target.stat().st_mode & 0o777, 0o600)
        target.unlink()
        target.symlink_to(source)
        result = subprocess.run(["sh", "-s"], input=prefix + deployment.INSTALL_FILE + call,
                                text=True, capture_output=True)
        self.assertEqual(result.returncode, 46)
        self.assertTrue(target.is_symlink())

    def test_backup_is_device_only_and_restricted(self):
        script = deployment.begin_transaction("fixture")
        self.assertIn('mkdir -m 700 "$backup"', script)
        self.assertIn('chmod 600 "$backup/original.tar.gz"', script)
        self.assertNotIn("pull", script)
        self.assertNotIn("reboot", script)

    def test_receipt_contains_no_identity_credentials(self):
        path = deployment.receipt("fixture", "deploy", {"device_backup_only": True}, self.base / "output")
        self.assertEqual(path.stat().st_mode & 0o777, 0o600)
        self.assertEqual(path.parent.stat().st_mode & 0o777, 0o700)
        self.assertNotIn(self.settings["device_id"], path.read_text())

    def test_stock_service_is_disabled_using_its_actual_switch(self):
        stock = self.base / 'stock.conf'
        stock.write_text('BOTSTATION_ENABLED=1\nOTHER_SETTING=keep\n')
        policy = deployment.SYSTEM_POLICY
        start = policy.index('if [ -f /etc/botstation-device.conf ]')
        end = policy.index('install_file "$backup/stock-disabled.conf"', start)
        script = 'backup=' + shlex.quote(str(self.base)) + '\n' + policy[start:end]
        script = script.replace('/etc/botstation-device.conf', shlex.quote(str(stock)))
        result = subprocess.run(['sh'], input=script, text=True, capture_output=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        saved = (self.base / 'stock-disabled.conf').read_text()
        self.assertIn('BOTSTATION_ENABLED=0\n', saved)
        self.assertNotIn('BOTSTATION_ENABLED=1', saved)
        self.assertIn('OTHER_SETTING=keep\n', saved)

    def test_generated_shell_is_syntactically_valid(self):
        scripts = [deployment.PREFLIGHT, deployment.identity_check(self.settings),
                   deployment.begin_transaction("fixture"), deployment.INSTALL_FILE, deployment.SYSTEM_POLICY,
                   deployment.switch_release(self.name, "mybot-rv1126b-0.1.1-aarch64"),
                   deployment.status_script()]
        for script in scripts:
            result = subprocess.run(["sh", "-n"], input=script, text=True, capture_output=True)
            self.assertEqual(result.returncode, 0, result.stderr)


if __name__ == "__main__":
    unittest.main()
