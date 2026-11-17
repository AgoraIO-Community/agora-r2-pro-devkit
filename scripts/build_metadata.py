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
MANIFEST_SCHEMA = 2
IDENTITY_SCHEMA = 1
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


def known_paths(root, build, config):
    """Assign stable roles only to explicitly configured build locations."""
    paths = {str(Path(root).resolve()): "<SOURCE>", str(Path(root)): "<SOURCE>",
             str(Path(build).resolve()): "<BUILD>", str(Path(build)): "<BUILD>"}
    for group, fields in (("sdk", ("sdk_dir", "staging_dir", "sysroot", "runtime_manifest", "rtsa_library")),
                          ("toolchain", ("file", "c_compiler", "cxx_compiler"))):
        for field in fields:
            if config[group].get(field):
                paths[str(Path(config[group][field]).resolve())] = "<" + group.upper() + ":" + field + ">"
                paths[str(config[group][field])] = "<" + group.upper() + ":" + field + ">"
    return sorted(((path, role) for path, role in paths.items() if path.startswith("/") and path != "/"),
                  key=lambda entry: len(entry[0]), reverse=True)


def portable_path(value, paths):
    for path, role in paths:
        if value == path or value.startswith(path + "/"):
            return role + value[len(path):]
    return value


def portable_flags(flags, paths):
    """Normalize path arguments, preserving option order and unknown semantics."""
    result, expecting_path = [], False
    separate = ("-I", "-L", "-B", "-isystem", "-iquote", "-idirafter", "-include", "-imacros",
                "-isysroot", "--sysroot")
    attached = ("--sysroot=", "-isystem", "-iquote", "-idirafter", "-include", "-imacros", "-isysroot",
                "-I", "-L", "-B")
    for token in shlex.split(flags):
        if expecting_path:
            result.append(portable_path(token, paths))
            expecting_path = False
        elif token in separate:
            result.append(token)
            expecting_path = True
        elif token.startswith(("-ffile-prefix-map=", "-fdebug-prefix-map=", "-fmacro-prefix-map=")):
            option, mapping = token.split("=", 1)
            before, separator, after = mapping.partition("=")
            result.append(option + "=" + portable_path(before, paths) + separator + after)
        elif token.startswith("-Wl,"):
            parts, path_next = token.split(","), False
            for index in range(1, len(parts)):
                if path_next:
                    parts[index] = portable_path(parts[index], paths)
                    path_next = False
                elif parts[index] in ("-rpath-link", "--rpath-link", "-L", "--library-path"):
                    path_next = True
                elif parts[index].startswith("-L"):
                    parts[index] = "-L" + portable_path(parts[index][2:], paths)
            result.append(",".join(parts))
        else:
            for prefix in attached:
                if token.startswith(prefix) and len(token) > len(prefix):
                    token = prefix + portable_path(token[len(prefix):], paths)
                    break
            result.append(token)
    return result


def portable_text(text, paths):
    for path, role in paths:
        text = re.sub(re.escape(path) + r"(?=$|[/\s\"';)])", lambda _match: role, text)
    return text


def compiler_snapshot(path):
    compiler = Path(path)
    result = {"sha256": digest(compiler)}
    # Zig compatibility wrappers bind the actual compiler as well as the wrapper.
    with compiler.open("rb") as stream:
        is_script = stream.read(2) == b"#!"
    if is_script:
        lines = compiler.read_text().splitlines()
        wrapped_executable = None
        for line in lines:
            if line.lstrip().startswith("exec "):
                words = shlex.split(line.lstrip())
                executable = Path(words[1])
                if executable.is_absolute() and executable.is_file():
                    result["wrapped_executable_sha256"] = digest(executable)
                    wrapped_executable = str(executable)
                break
        # Generated Zig wrappers contain host-specific executable and cache
        # paths. Canonicalize these fields while retaining every other line and
        # argument, and independently bind the actual compiler's bytes.
        cache_keys = ("ZIG_GLOBAL_CACHE_DIR", "ZIG_LOCAL_CACHE_DIR")
        if (all(any(line.startswith("export " + key + "=") for line in lines) for key in cache_keys)
                and "wrapped_executable_sha256" in result):
            normalized = []
            for line in lines:
                words = shlex.split(line.lstrip())
                if (words and words[0] == "exec" and len(words) > 2 and
                        words[1] == wrapped_executable and words[2] in ("cc", "c++")):
                    prefix = line[:len(line) - len(line.lstrip())] + "exec "
                    executable = shlex.quote(wrapped_executable)
                    if line.startswith(prefix + executable + " "):
                        line = prefix + "<WRAPPED_COMPILER>" + line[len(prefix + executable):]
                    normalized.append(line)
                elif any(line.startswith("export " + key + "=") for key in cache_keys):
                    assignment = shlex.split(line)[1:]
                    if len(assignment) == 1:
                        key, value = assignment[0].split("=", 1)
                        if Path(value).is_absolute() and line == "export " + key + "=" + shlex.quote(value):
                            line = "export " + key + "=<CACHE>"
                    normalized.append(line)
                else:
                    normalized.append(line)
            result["portable_sha256"] = json_digest(normalized)
    result.setdefault("portable_sha256", result["sha256"])
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
    source = source_snapshot(root)
    paths = known_paths(root, build, config)
    portable_config = json.loads(json.dumps(config))
    for group, fields in (("sdk", ("sdk_dir", "staging_dir", "sysroot", "runtime_manifest", "rtsa_library")),
                          ("toolchain", ("file", "c_compiler", "cxx_compiler"))):
        for field in fields:
            if portable_config[group].get(field):
                portable_config[group][field] = portable_path(portable_config[group][field], paths)
    # Hash complete normalized argument lists: unrecognized flags remain bound,
    # but their literal values need not expose local paths in public manifests.
    portable_config["flags"] = {key: value if key == "build_type" else
                                {"sha256": json_digest(portable_flags(value, paths))}
                                for key, value in config["flags"].items()}
    portable_dependencies = json.loads(json.dumps(dependencies))
    for key in ("compiler", "cxx_compiler"):
        portable_dependencies[key]["sha256"] = portable_dependencies[key].pop("portable_sha256")
    portable_dependencies["toolchain_file_sha256"] = json_digest(
        portable_text(Path(toolchain["file"]).read_text(), paths))
    identity = {"schema": IDENTITY_SCHEMA, "config": portable_config,
                "source": {key: source[key] for key in ("sha256", "file_count")},
                "dependencies": portable_dependencies}
    return {"schema": MANIFEST_SCHEMA, "config": config, "source": source,
            "dependencies": dependencies, "identity": identity}


def inputs_digest(inputs, *, public=False):
    """The portable identity is shared by emitted markers and package checks."""
    identity = inputs.get("identity", {})
    if inputs.get("schema") != MANIFEST_SCHEMA or identity.get("schema") != IDENTITY_SCHEMA:
        raise ValueError("Portable inputs identity is missing or obsolete; reconfigure and rebuild")
    for field in ("version", "language", "video", "ptime_ms", "board"):
        if identity["config"][field] != inputs["config"][field]:
            raise ValueError("Portable inputs identity does not match configuration")
    if any(identity["source"][key] != inputs["source"][key] for key in ("sha256", "file_count")):
        raise ValueError("Portable inputs identity does not match source")
    if public and (inputs["config"]["flags_sha256"] != json_digest(identity["config"]["flags"]) or
                   inputs["dependencies"] != identity["dependencies"] or
                   any(value != identity["config"]["toolchain"][key]
                       for key, value in inputs["config"].get("toolchain", {}).items())):
        raise ValueError("Portable inputs identity does not match public metadata")
    return json_digest(identity)


def verified_abi(build, config, source_sha256, inputs_sha256, *,
                 binary_name="mybot-rv1126b", report_name="abi-report.json", require_rtsa=True):
    build = Path(build)
    report = read_json(build / report_name)
    binary = report.get("binary", {})
    if report.get("errors") != []:
        raise ValueError("ABI report is missing passing validation")
    if binary.get("sha256") != digest(build / binary_name):
        raise ValueError("Stale ABI report: executable changed")
    marker = ("MYBOT_RV1126B_BUILD_CONFIG:" + config["version"] + ";" + config["language"] +
              ";video=" + str(int(config["video"])) + ";ptime_ms=" + str(config["ptime_ms"]) +
              ";board=" + config["board"]).encode() + b"\0"
    executable = (build / binary_name).read_bytes()
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
    if require_rtsa and not matched:
        raise ValueError("RTSA library does not match verified ABI closure")
    return report


def verify_manifest(root, build, language, video):
    build = Path(build)
    manifest = read_json(build / "build-manifest.json")
    if manifest.get("schema") != MANIFEST_SCHEMA:
        raise ValueError("Unsupported build manifest; rebuild")
    current = build_inputs(root, build)
    if manifest.get("inputs") != current:
        raise ValueError("Stale build manifest: source/configuration/dependencies changed; rebuild")
    config = current["config"]
    if config["language"] != language or config["video"] != video:
        raise ValueError("Build profile/language does not match requested package")
    report = verified_abi(build, config, current["source"]["sha256"], inputs_digest(current))
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
    config["flags_sha256"] = json_digest(result["inputs"]["identity"]["config"]["flags"])
    config["build_type"] = flags["build_type"]
    result["inputs"]["dependencies"] = result["inputs"]["identity"]["dependencies"]
    # This hash binds the local ABI report; the package separately checksums its
    # public, path-normalized report in SHA256SUMS.
    return result
