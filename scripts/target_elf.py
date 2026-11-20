# SPDX-License-Identifier: Apache-2.0
"""Read target ELF dynamic dependencies without executing target code."""

import hashlib
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
