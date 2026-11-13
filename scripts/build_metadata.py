#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Shared, dependency-free build provenance and packaging checks."""

import hashlib
import json
from pathlib import Path
import re
import shlex
import subprocess

SCHEMA = 1
BOARD = "rv1126b-test2-rk805b-v10"
RPATH = "$ORIGIN/../lib"


def digest(path):
    with Path(path).open("rb") as stream:
        result = hashlib.sha256()
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            result.update(block)
    return result.hexdigest()


def json_digest(value):
    return hashlib.sha256(json.dumps(value, sort_keys=True, separators=(",", ":"),
                                    ensure_ascii=True).encode()).hexdigest()


def read_json(path):
    try:
        return json.loads(Path(path).read_text())
    except (OSError, ValueError) as error:
        raise ValueError("Cannot read valid JSON: " + str(path)) from error


def write_json(path, value):
    Path(path).write_text(json.dumps(value, indent=2, sort_keys=True) + "\n")


def git(root, *args):
    return subprocess.check_output(["git", "-C", str(root), *args], stderr=subprocess.DEVNULL)


def excluded(name):
    parts = Path(name).parts
    return (not parts or parts[0] in (".git", "local", "dist") or
            parts[0].startswith("build") or "__pycache__" in parts or
            parts[-1] == ".DS_Store" or parts[-1].endswith(".pyc"))


def source_snapshot(root):
    """Include tracked deletions and new governance files, never local build outputs."""
    root = Path(root).resolve()
    try:
        names = set(git(root, "ls-files", "-z", "--cached", "--others", "--exclude-standard")
                    .decode().strip("\0").split("\0"))
        revision = git(root, "rev-parse", "HEAD").decode().strip()
        dirty = bool(git(root, "status", "--porcelain", "--untracked-files=all").strip())
    except subprocess.CalledProcessError:
        names = {str(path.relative_to(root)) for path in root.rglob("*") if path.is_file()}
        revision, dirty = None, True
    entries = []
    for name in sorted(names):
        if excluded(name):
            continue
        path = root / name
        if path.is_symlink():
            # Source symlinks bind their target text; source-tree files bind contents.
            entries.append([name, "symlink", str(path.readlink())])
        elif path.is_file():
            entries.append([name, "file", digest(path), bool(path.stat().st_mode & 0o111)])
        elif not path.exists():
            entries.append([name, "deleted"])
    return {"sha256": json_digest(entries), "file_count": len(entries),
            "revision": revision, "dirty": dirty}


def tree_snapshot(path):
    path = Path(path)
    if not path.is_dir():
        raise ValueError("Missing dependency directory: " + str(path))
    entries = []
    for child in sorted(path.rglob("*")):
        if child.is_file():
            entries.append([str(child.relative_to(path)), digest(child)])
        elif child.is_symlink() and not child.exists():
            raise ValueError("Broken dependency symlink: " + str(child))
    if not entries:
        raise ValueError("Empty dependency directory: " + str(path))
    return {"sha256": json_digest(entries), "file_count": len(entries)}


def project_version(root):
    match = re.search(r"project\(mybot_rv1126b\s+VERSION\s+([0-9]+\.[0-9]+\.[0-9]+)",
                      (Path(root) / "CMakeLists.txt").read_text())
    if not match:
        raise ValueError("Cannot read authoritative CMake project version")
    return match.group(1)


def load_config(root, build):
    config = read_json(Path(build) / "build-config.json")
    if config.get("schema") != SCHEMA:
        raise ValueError("Unsupported build-config schema; reconfigure the build")
    if config.get("version") != project_version(root):
        raise ValueError("Build version differs from CMake; rebuild")
    if config.get("language") not in ("zh-CN", "en-US"):
        raise ValueError("Invalid build language")
    if type(config.get("video")) is not bool or config.get("ptime_ms") != 20:
        raise ValueError("Invalid video/ptime build configuration")
    if config.get("board") != BOARD:
        raise ValueError("Unsupported or unverified board")
    for field in ("rtsa_library", "sdk_dir", "staging_dir"):
        if not config.get("sdk", {}).get(field):
            raise ValueError("Missing sdk." + field + " in build-config")
    if config.get("toolchain", {}).get("target") != "aarch64-linux-gnu":
        raise ValueError("Unexpected compiler target")
    for field in ("file", "c_compiler", "c_compiler_id", "c_compiler_version",
                  "cxx_compiler", "cxx_compiler_id", "cxx_compiler_version"):
        if not config.get("toolchain", {}).get(field):
            raise ValueError("Missing toolchain." + field + " in build-config")
    flags = config.get("flags")
    if (not isinstance(flags, dict) or
            not all(isinstance(value, str) for value in flags.values()) or
            not all(field in flags for field in ("c", "cxx", "exe_link", "build_type"))):
        raise ValueError("Build flags must provide c/cxx/exe_link/build_type string fields")
    return config


def compiler_snapshot(path):
    compiler = Path(path)
    result = {"sha256": digest(compiler)}
    # Zig compatibility wrappers bind the actual compiler as well as the wrapper.
    with compiler.open("rb") as stream:
        is_script = stream.read(2) == b"#!"
    if is_script:
        for line in compiler.read_text().splitlines():
            if line.startswith("exec "):
                words = shlex.split(line)
                executable = Path(words[1])
                if executable.is_absolute() and executable.is_file():
                    result["wrapped_executable_sha256"] = digest(executable)
                break
    return result


def build_inputs(root, build):
    config = load_config(root, build)
    sdk, toolchain = config["sdk"], config["toolchain"]
    dependencies = {"sdk": tree_snapshot(sdk["sdk_dir"]),
                    "staging": tree_snapshot(sdk["staging_dir"]),
                    "rtsa_sha256": digest(sdk["rtsa_library"]),
                    "toolchain_file_sha256": digest(toolchain["file"]),
                    "compiler": compiler_snapshot(toolchain["c_compiler"]),
                    "cxx_compiler": compiler_snapshot(toolchain["cxx_compiler"])}
    for key in ("runtime_manifest",):
        if sdk.get(key):
            dependencies[key + "_sha256"] = digest(sdk[key])
    if sdk.get("sysroot"):
        dependencies["sysroot"] = tree_snapshot(sdk["sysroot"])
    return {"schema": SCHEMA, "config": config, "source": source_snapshot(root),
            "dependencies": dependencies}


def verified_abi(build, config, source_sha256, inputs_sha256):
    build = Path(build)
    report = read_json(build / "abi-report.json")
    binary = report.get("binary", {})
    if report.get("errors") != []:
        raise ValueError("ABI report is missing passing validation")
    if binary.get("sha256") != digest(build / "mybot-rv1126b"):
        raise ValueError("Stale ABI report: executable changed")
    marker = ("MYBOT_RV1126B_BUILD_CONFIG:" + config["version"] + ";" + config["language"] +
              ";video=" + str(int(config["video"])) + ";ptime_ms=" + str(config["ptime_ms"]) +
              ";board=" + config["board"]).encode() + b"\0"
    executable = (build / "mybot-rv1126b").read_bytes()
    if marker not in executable:
        raise ValueError("Executable build identity does not match configuration; rebuild")
    source_marker = b"MYBOT_RV1126B_SOURCE:" + source_sha256.encode() + b"\0"
    if source_marker not in executable:
        raise ValueError("Executable source identity does not match prepared source; rebuild")
    inputs_marker = b"MYBOT_RV1126B_INPUTS:" + inputs_sha256.encode() + b"\0"
    if inputs_marker not in executable:
        raise ValueError("Executable inputs identity does not match prepared dependencies/configuration; rebuild")
    if (binary.get("machine") != 183 or
            binary.get("interpreter") != "/lib/ld-linux-aarch64.so.1" or
            binary.get("runpaths") != [RPATH]):
        raise ValueError("Unexpected executable ABI/interpreter/RPATH")
    rtsa_hash = digest(config["sdk"]["rtsa_library"])
    matched = False
    checked = report.get("checked", [])
    if (report.get("dependency_count") != len(checked) - 1 or
            not any(entry.get("sha256") == binary["sha256"] and
                    entry.get("path") == binary.get("path") for entry in checked)):
        raise ValueError("Incomplete ABI dependency closure")
    for entry in checked:
        if entry.get("soname", "") and entry["soname"].startswith("liblvgl.so"):
            raise ValueError("LVGL must be linked statically")
        if entry.get("machine") != 183 or entry.get("sha256") != digest(entry["path"]):
            raise ValueError("Stale ABI dependency: " + entry.get("soname", entry["path"]))
        if entry.get("soname") == "libagora-rtc-sdk.so":
            matched = entry["sha256"] == rtsa_hash
    if not matched:
        raise ValueError("RTSA library does not match verified ABI closure")
    return report


def verify_manifest(root, build, language, video):
    build = Path(build)
    manifest = read_json(build / "build-manifest.json")
    if manifest.get("schema") != SCHEMA:
        raise ValueError("Unsupported build manifest; rebuild")
    current = build_inputs(root, build)
    if manifest.get("inputs") != current:
        raise ValueError("Stale build manifest: source/configuration/dependencies changed; rebuild")
    config = current["config"]
    if config["language"] != language or config["video"] != video:
        raise ValueError("Build profile/language does not match requested package")
    report = verified_abi(build, config, current["source"]["sha256"], json_digest(current))
    if (manifest.get("binary_sha256") != digest(build / "mybot-rv1126b") or
            manifest.get("abi_report_sha256") != digest(build / "abi-report.json")):
        raise ValueError("Build binary or ABI report differs from manifest")
    return manifest, report


def public_abi(report):
    # The successful report contains no error text. Replace every host path label.
    result = json.loads(json.dumps(report))
    for entry in [result["binary"], *result["checked"]]:
        entry["path"] = entry.get("soname") or Path(entry["path"]).name
    return result


def public_manifest(manifest):
    result = json.loads(json.dumps(manifest))
    config = result["inputs"]["config"]
    config["sdk"] = {"rtsa_library": "libagora-rtc-sdk.so"}
    config["toolchain"] = {key: config["toolchain"][key]
                           for key in ("c_compiler_id", "c_compiler_version", "cxx_compiler_id",
                                       "cxx_compiler_version", "target")}
    flags = config.pop("flags")
    config["flags_sha256"] = json_digest(flags)
    config["build_type"] = flags["build_type"]
    # This hash binds the local ABI report; the package separately checksums its
    # public, path-normalized report in SHA256SUMS.
    return result
