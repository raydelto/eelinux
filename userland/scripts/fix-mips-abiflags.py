#!/usr/bin/env python3
"""Repair PT_MIPS_ABIFLAGS segments that patchelf left empty.
SPDX-License-Identifier: GPL-2.0-only

Buildroot's RPATH sanitizer (host-patchelf 0.13) rewrites glibc's CJK gconv
modules, whose RPATH is $ORIGIN. It moves .MIPS.abiflags into a new PT_LOAD
but leaves PT_MIPS_ABIFLAGS pointing at the old place with zero size, and
reuses the old bytes. glibc's MIPS loader reads the ABI flags through that
segment, so the modules can be rejected at dlopen time. This points the
segment back at the relocated section. Files that are intact stay untouched.
"""
import struct
import sys
from pathlib import Path

PT_LOAD, PT_MIPS_ABIFLAGS, SHT_MIPS_ABIFLAGS = 1, 0x70000003, 0x7000002A
ABIFLAGS_BYTES = 24


def repair(path: Path) -> bool:
    data = bytearray(path.read_bytes())
    if data[:4] != b"\x7fELF" or data[4] != 1 or data[5] != 1 or len(data) < 52:
        return False  # only 32-bit little-endian objects are expected here
    phoff, shoff = struct.unpack_from("<II", data, 28)
    phsize, phnum, shsize, shnum = struct.unpack_from("<HHHH", data, 42)
    phdrs = [struct.unpack_from("<8I", data, phoff + i * phsize) for i in range(phnum)]
    target = next((i for i, p in enumerate(phdrs) if p[0] == PT_MIPS_ABIFLAGS), None)
    if target is None or phdrs[target][4] >= ABIFLAGS_BYTES:
        return False
    sections = [struct.unpack_from("<10I", data, shoff + i * shsize) for i in range(shnum)] if shoff else []
    section = next((s for s in sections if s[1] == SHT_MIPS_ABIFLAGS and s[5] >= ABIFLAGS_BYTES), None)
    if section is None:
        raise RuntimeError(f"{path}: empty PT_MIPS_ABIFLAGS and no .MIPS.abiflags section")
    addr, offset, size = section[3], section[4], section[5]
    if not any(p[0] == PT_LOAD and p[1] <= offset and offset + size <= p[1] + p[4]
               and p[2] <= addr and addr - p[2] == offset - p[1] for p in phdrs):
        raise RuntimeError(f"{path}: .MIPS.abiflags is not inside a loaded segment")
    old = phdrs[target]
    struct.pack_into("<8I", data, phoff + target * phsize,
                     old[0], offset, addr, addr, size, size, old[6], old[7])
    path.write_bytes(data)
    return True


def main():
    root = Path(sys.argv[1])
    fixed = [p for p in sorted(root.rglob("*.so*")) if p.is_file() and not p.is_symlink() and repair(p)]
    for path in fixed:
        print(f"fix-mips-abiflags: repaired {path.relative_to(root)}")


if __name__ == "__main__":
    main()
