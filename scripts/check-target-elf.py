#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Inspect ELF64 files and check dependency/version closure without executing them."""

import argparse
import json
from pathlib import Path
from target_elf import inspect


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
