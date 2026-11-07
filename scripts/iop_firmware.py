"""Validate built PS2 IOP firmware separately from Linux userspace ELF.

SPDX-License-Identifier: GPL-2.0-only
IRX modules target the R3000 IOP, use ET_IOPRELEXEC, and intentionally omit
GNU o32 ABI attributes. These checks establish packaging integrity, not IOP
execution or correctness of relocations and hardware accesses.
"""
from __future__ import annotations

import hashlib
import json
from pathlib import Path
import re
import struct

MODULES = ("irq", "irqrelay", "ata", "dev9", "gamepad", "printk")
IOP_RAM_BYTES = 2 * 1024 * 1024


class FirmwareError(ValueError):
    pass


def validate_irx(path: Path) -> None:
    data = path.read_bytes()
    if not 52 <= len(data) <= IOP_RAM_BYTES or data[:7] != b"\x7fELF\x01\x01\x01":
        raise FirmwareError(f"{path}: expected bounded little-endian ELF32 IOP firmware")
    header = struct.unpack_from("<16sHHIIIIIHHHHHH", data)
    _, kind, machine, version, entry, phoff, shoff, flags, ehsize, phsize, phnum, shsize, shnum, shnames = header
    if (kind != 0xFF80 or machine != 8 or version != 1 or flags & ~0x7
            or ehsize != 52 or phsize != 32 or phnum != 2
            or shsize != 40 or not shnum or shnames >= shnum
            or phoff < 52 or phoff + phsize * phnum > len(data)
            or shoff < 52 or shoff + shsize * shnum > len(data)):
        raise FirmwareError(f"{path}: incompatible or malformed MIPS1 IOP IRX header")
    module_header = False
    entry_mapped = False
    for index in range(phnum):
        segment = struct.unpack_from("<8I", data, phoff + index * phsize)
        segment_kind, offset, address, _, size, memory, permissions, alignment = segment
        if offset + size > len(data) or alignment and alignment & (alignment - 1):
            raise FirmwareError(f"{path}: invalid IRX segment bounds/alignment")
        if segment_kind == 0x70000080:
            if module_header or size < 26:
                raise FirmwareError(f"{path}: invalid IOP module information")
            module_header = True
        elif segment_kind == 1:
            if size > memory or address + memory > IOP_RAM_BYTES:
                raise FirmwareError(f"{path}: invalid IOP load segment")
            entry_mapped = bool(permissions & 1 and address <= entry < address + size)
        else:
            raise FirmwareError(f"{path}: unexpected IRX program segment")
    if not module_header or not entry_mapped:
        raise FirmwareError(f"{path}: missing IOP module information or executable entry")
    for index in range(shnum):
        section = struct.unpack_from("<10I", data, shoff + index * shsize)
        # SHT_NOBITS has memory storage only and need not fit in the file.
        if section[1] != 8 and section[4] + section[5] > len(data):
            raise FirmwareError(f"{path}: truncated IRX section")


def validate_firmware(directory: Path) -> set[Path]:
    directory = Path(directory)
    manifest = directory / "manifest.json"
    if manifest.is_symlink() or not manifest.is_file():
        raise FirmwareError(f"{directory}: built IOP firmware manifest is required")
    try:
        hashes = json.loads(manifest.read_text(encoding="utf-8"))["firmware_sha256"]
    except (KeyError, ValueError, TypeError) as exc:
        raise FirmwareError(f"{manifest}: invalid firmware hash manifest") from exc
    expected = {name + ".irx" for name in MODULES}
    if not isinstance(hashes, dict) or set(hashes) != expected:
        raise FirmwareError(f"{manifest}: expected all six built IOP modules")
    unexpected = {path.name for path in directory.glob("*.irx")} - expected
    if unexpected:
        raise FirmwareError(f"{directory}: unrecorded IOP firmware: {sorted(unexpected)}")
    validated = set()
    for name in sorted(expected):
        path = directory / name
        digest = hashes[name]
        if (path.is_symlink() or not path.is_file() or not isinstance(digest, str)
                or not re.fullmatch(r"[0-9a-f]{64}", digest)):
            raise FirmwareError(f"{path}: missing regular firmware file or valid hash")
        if hashlib.sha256(path.read_bytes()).hexdigest() != digest:
            raise FirmwareError(f"{path}: firmware hash mismatch")
        validate_irx(path)
        validated.add(path)
    return validated
