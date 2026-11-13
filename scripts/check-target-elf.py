#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Inspect ELF64 files and check dependency/version closure without executing them."""

import argparse
import hashlib
import json
from pathlib import Path
import struct


def inspect(path):
    data = path.read_bytes()
    if data[:6] != b"\x7fELF\x02\x01":
        raise ValueError(str(path) + ": expected little-endian ELF64")
    header = struct.unpack_from("<16sHHIQQQIHHHHHH", data)
    machine, shoff, shsize, shnum, shstridx = header[2], header[6], header[11], header[12], header[13]
    raw_sections = [struct.unpack_from("<IIQQQQIIQQ", data, shoff + i * shsize)
                    for i in range(shnum)]

    def cstring(buffer, offset):
        return buffer[offset:buffer.index(b"\0", offset)].decode("utf-8")

    names = raw_sections[shstridx]
    names = data[names[4]:names[4] + names[5]]
    sections = {cstring(names, s[0]): s for s in raw_sections}

    def section(name):
        entry = sections.get(name)
        return data[entry[4]:entry[4] + entry[5]] if entry else b""

    strings = section(".dynstr")
    needed, soname, runpaths = [], None, []
    dynamic = section(".dynamic")
    for offset in range(0, len(dynamic), 16):
        tag, value = struct.unpack_from("<qQ", dynamic, offset)
        if tag == 1: needed.append(cstring(strings, value))
        if tag == 14: soname = cstring(strings, value)
        if tag in (15, 29): runpaths.append(cstring(strings, value))
    required = {}
    versions = section(".gnu.version_r")
    offset = 0
    while offset < len(versions):
        _, count, filename, aux, following = struct.unpack_from("<HHIII", versions, offset)
        library = cstring(strings, filename)
        required[library] = []
        current = offset + aux
        for _ in range(count):
            _, _, _, name, next_aux = struct.unpack_from("<IHHII", versions, current)
            required[library].append(cstring(strings, name))
            current += next_aux
        if not following: break
        offset += following
    provided = []
    versions = section(".gnu.version_d")
    offset = 0
    while offset < len(versions):
        _, _, _, _, _, aux, following = struct.unpack_from("<HHHHIII", versions, offset)
        name, _ = struct.unpack_from("<II", versions, offset + aux)
        provided.append(cstring(strings, name))
        if not following: break
        offset += following
    return {"path": str(path), "machine": machine, "soname": soname,
            "interpreter": section(".interp").rstrip(b"\0").decode("utf-8"),
            "needed": needed, "runpaths": runpaths, "required_versions": required,
            "provided_versions": provided, "sha256": hashlib.sha256(data).hexdigest()}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=Path)
    parser.add_argument("--library-root", type=Path, action="append", default=[])
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    binary = inspect(args.binary.resolve())
    libraries = {}
    # Order matters: earlier roots win, e.g. the new LVGL build over device libraries.
    for root in args.library_root:
        for path in sorted(root.resolve().rglob("*.so*")):
            if not path.is_file() or path.read_bytes()[:4] != b"\x7fELF":
                continue
            entry = inspect(path)
            libraries.setdefault(entry["soname"] or path.name, entry)
            libraries.setdefault(path.name, entry)
    errors, checked = [], {}
    pending = [binary]
    while pending:
        entry = pending.pop()
        if entry["path"] in checked: continue
        checked[entry["path"]] = entry
        if entry["machine"] != 183:
            errors.append(entry["path"] + ": not AArch64")
        if any(str(Path.cwd()) in value for value in entry["runpaths"]):
            errors.append(entry["path"] + ": host build path embedded in RPATH")
        for name in entry["needed"]:
            dependency = libraries.get(name)
            if not dependency:
                errors.append(entry["path"] + ": missing " + name)
                continue
            pending.append(dependency)
        for name, versions in entry["required_versions"].items():
            dependency = libraries.get(name)
            if not dependency:
                errors.append(entry["path"] + ": missing version provider " + name)
                continue
            absent = sorted(set(versions) - set(dependency["provided_versions"]))
            if absent:
                errors.append(entry["path"] + ": " + name + " lacks " + ", ".join(absent))
    if binary["interpreter"] != "/lib/ld-linux-aarch64.so.1":
        errors.append("Unexpected target interpreter: " + binary["interpreter"])
    if binary["runpaths"] != ["$ORIGIN/../lib"]:
        errors.append("Executable RPATH must be exactly $ORIGIN/../lib")
    loader = libraries.get("ld-linux-aarch64.so.1")
    if not loader or loader["machine"] != 183:
        errors.append("Missing AArch64 dynamic loader in supplied runtime")
    result = {"binary": binary, "dependency_count": len(checked) - 1,
              "checked": list(checked.values()), "errors": sorted(set(errors))}
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps({"machine": binary["machine"], "interpreter": binary["interpreter"],
                      "needed": binary["needed"], "dependency_count": result["dependency_count"],
                      "errors": result["errors"]}, indent=2))
    if errors:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
