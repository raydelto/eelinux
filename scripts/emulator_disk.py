#!/usr/bin/env python3
"""Copy a verified ext4 (or historical ext2) image into a disposable emulator disk.

This writes regular files only. It never formats, mounts, or opens a host disk
for writing. PCSX2 USB mass storage can modify the copy during an experiment.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import stat
import struct
import sys
import tempfile
from typing import BinaryIO

SECTOR_SIZE = 512
PARTITION_START = 2048
PARTITION_OFFSET = PARTITION_START * SECTOR_SIZE
DISK_SIGNATURE = b"eeLX"
COPY_CHUNK = 1024 * 1024
DEFAULT_OUTPUT = Path(__file__).resolve().parents[1] / "build/emulator/rootfs-usb.img"


class DiskImageError(ValueError):
    """An input cannot safely be used to create an emulator disk."""


def regular_path(path: Path, *, must_exist: bool) -> Path:
    """Reject device namespaces, links, and nonregular existing destinations."""
    raw = str(path).replace("\\", "/")
    if raw.startswith(("//./", "//?/")):
        raise DiskImageError(f"device namespaces are forbidden: {path}")
    if os.name == "nt":
        reserved = {"CON", "PRN", "AUX", "NUL"}
        reserved.update(f"{prefix}{number}" for prefix in ("COM", "LPT") for number in range(1, 10))
        if any(part.split(".", 1)[0].rstrip(" :").upper() in reserved for part in path.parts):
            raise DiskImageError(f"device filenames are forbidden: {path}")
    path = path.absolute()
    # Check ancestors too: an output through a directory link is not an
    # independently chosen regular-file destination.
    for item in (path, *path.parents):
        if item.is_symlink() or getattr(item, "is_junction", lambda: False)():
            raise DiskImageError(f"symbolic links and junctions are forbidden: {item}")
    try:
        info = path.lstat()
    except FileNotFoundError:
        if must_exist:
            raise DiskImageError(f"file does not exist: {path}") from None
    else:
        if not stat.S_ISREG(info.st_mode):
            raise DiskImageError(f"regular files only; no directories or devices: {path}")
    return path.resolve()


def same_file(first: Path, second: Path) -> bool:
    if first == second:
        return True
    try:
        return os.path.samefile(first, second)
    except FileNotFoundError:
        return False


def identity(info: os.stat_result) -> tuple[int, ...]:
    values = (info.st_dev, info.st_ino, info.st_size, info.st_mtime_ns)
    # Windows Python can expose creation time via stat().st_ctime and last
    # modification time via fstat().st_ctime. Neither is the POSIX change time.
    # Identity, mtime, size and the second full SHA-256 pass still apply there.
    return values if os.name == "nt" else (*values, info.st_ctime_ns)


def filesystem_info(source: BinaryIO, size: int) -> dict[str, int | str]:
    if size < 2048 or size % SECTOR_SIZE:
        raise DiskImageError("filesystem source must contain its superblock and be aligned to 512-byte sectors")
    source.seek(1024)
    superblock = source.read(1024)
    if len(superblock) != 1024 or struct.unpack_from("<H", superblock, 0x38)[0] != 0xEF53:
        raise DiskImageError("source has no ext filesystem superblock magic at byte 1080")
    # Disk layout: e2fsprogs lib/ext2fs/ext2_fs.h and
    # https://docs.kernel.org/filesystems/ext4/super.html (shared fields).
    blocks = struct.unpack_from("<I", superblock, 0x04)[0]
    log_block = struct.unpack_from("<I", superblock, 0x18)[0]
    revision = struct.unpack_from("<I", superblock, 0x4C)[0]
    if log_block > 2 or revision > 1:
        raise DiskImageError("unsupported ext2 block size or revision for the PS2 4 KiB page size")
    block_size = 1024 << log_block
    if not blocks or blocks * block_size != size:
        raise DiskImageError("ext2 declared block count does not match the source file size")
    first_block = struct.unpack_from("<I", superblock, 0x14)[0]
    if first_block != (1 if block_size == 1024 else 0):
        raise DiskImageError("ext2 first data block is inconsistent with its block size")
    if revision == 1:
        compat, incompat, ro_compat = struct.unpack_from("<III", superblock, 0x5C)
        is_ext4 = bool(compat & 0x4 or incompat & 0x40 or ro_compat & 0x440)
        if is_ext4:
            # Match the explicit small ext4 profile produced by rootfs.py.
            # Unsupported features (64bit, bigalloc, encryption, etc.) fail closed.
            if (compat & ~0x34 or incompat & ~0x42 or ro_compat & ~0x443 or
                    not compat & 0x4 or not incompat & 0x40 or
                    struct.unpack_from("<H", superblock, 0x58)[0] != 256 or
                    not struct.unpack_from("<I", superblock, 0xE0)[0] or
                    struct.unpack_from("<I", superblock, 0xE4)[0]):
                raise DiskImageError("source requires filesystem features outside the supported ext4 profile")
            reserved_gdt=struct.unpack_from('<H',superblock,0xCE)[0]
            if (bool(compat & 0x10)!=bool(reserved_gdt) or reserved_gdt>block_size//4):
                raise DiskImageError('inconsistent or excessive ext4 online-resize metadata')
        elif incompat & ~0x2 or ro_compat & ~0x7:
            raise DiskImageError("source requires filesystem features outside the supported ext2 format")
    else:
        is_ext4 = False
    sectors = size // SECTOR_SIZE
    if sectors > 0xFFFFFFFF or PARTITION_START + sectors > 0x100000000:
        raise DiskImageError("filesystem is too large for the 32-bit MBR partition fields")
    result={"type": "ext4" if is_ext4 else "ext2", "block_size": block_size, "block_count": blocks}
    if is_ext4:
        result.update(online_resize_metadata_reserved=bool(compat&0x10),reserved_gdt_blocks=reserved_gdt)
    return result


def ext2_info(source: BinaryIO, size: int) -> dict[str, int | str]:
    """Validate historical ext2 audits without accepting ext4 under an ext2 label."""
    result = filesystem_info(source, size)
    if result["type"] != "ext2":
        raise DiskImageError("expected historical ext2 filesystem")
    return result


def chs(lba: int) -> bytes:
    """Conventional 255-head/63-sector CHS, saturated above cylinder 1023."""
    cylinder, rest = divmod(lba, 255 * 63)
    if cylinder > 1023:
        return b"\xfe\xff\xff"
    head, sector = divmod(rest, 63)
    return bytes((head, sector + 1 | (cylinder >> 2 & 0xC0), cylinder & 0xFF))


def disk_prefix(sectors: int) -> bytes:
    prefix = bytearray(PARTITION_OFFSET)
    prefix[440:444] = DISK_SIGNATURE
    struct.pack_into("<B3sB3sII", prefix, 446, 0, chs(PARTITION_START), 0x83,
                     chs(PARTITION_START + sectors - 1), PARTITION_START, sectors)
    prefix[510:512] = b"\x55\xaa"
    return bytes(prefix)


def hash_stream(source: BinaryIO) -> str:
    digest = hashlib.sha256()
    source.seek(0)
    while chunk := source.read(COPY_CHUNK):
        digest.update(chunk)
    return digest.hexdigest()


def wrap_rootfs(rootfs: Path, output: Path = DEFAULT_OUTPUT, manifest: Path | None = None) -> dict:
    """Create an MBR disk copy and its SHA-256 manifest using atomic replaces.

    Header/size checks are packaging checks, not an offline filesystem check;
    the source rootfs must already have passed e2fsck in scripts/rootfs.py.
    """
    rootfs = regular_path(Path(rootfs), must_exist=True)
    output = regular_path(Path(output), must_exist=False)
    manifest = regular_path(Path(manifest) if manifest is not None else Path(str(output) + ".json"), must_exist=False)
    if any(same_file(first, second) for first, second in ((rootfs, output), (rootfs, manifest), (output, manifest))):
        raise DiskImageError("source, disk copy and manifest must be different files, including hard links")

    temporary_paths: list[Path] = []
    # O_NOFOLLOW prevents a last-component symlink swap on POSIX. O_NONBLOCK
    # allows fstat to reject a raced FIFO without waiting for a writer.
    flags = os.O_RDONLY | getattr(os, "O_BINARY", 0) | getattr(os, "O_NOFOLLOW", 0) | getattr(os, "O_NONBLOCK", 0)
    before = rootfs.stat()
    descriptor = os.open(rootfs, flags)
    try:
        with os.fdopen(descriptor, "rb") as source:
            opened = os.fstat(source.fileno())
            if not stat.S_ISREG(opened.st_mode) or identity(opened) != identity(before):
                raise DiskImageError("source changed before it could be opened as a regular file")
            filesystem = filesystem_info(source, opened.st_size)
            sectors = opened.st_size // SECTOR_SIZE
            prefix = disk_prefix(sectors)
            output.parent.mkdir(parents=True, exist_ok=True)
            manifest.parent.mkdir(parents=True, exist_ok=True)
            # Revalidate after directory creation, before creating any files.
            regular_path(output, must_exist=False)
            regular_path(manifest, must_exist=False)
            payload_hash = hashlib.sha256()
            disk_hash = hashlib.sha256(prefix)
            with tempfile.NamedTemporaryFile(mode="wb", dir=output.parent, prefix="." + output.name + ".", suffix=".tmp", delete=False) as target:
                disk_temporary = Path(target.name)
                temporary_paths.append(disk_temporary)
                target.write(prefix)
                source.seek(0)
                remaining = opened.st_size
                while remaining:
                    chunk = source.read(min(COPY_CHUNK, remaining))
                    if not chunk:
                        raise DiskImageError("source was truncated while being copied")
                    target.write(chunk)
                    payload_hash.update(chunk)
                    disk_hash.update(chunk)
                    remaining -= len(chunk)
                target.flush()
                os.fsync(target.fileno())
            source_hash = hash_stream(source)
            if source_hash != payload_hash.hexdigest() or identity(os.fstat(source.fileno())) != identity(opened) or identity(rootfs.stat()) != identity(opened):
                raise DiskImageError("source changed during copying; disk copy was not published")

            report = {
                "schema": 1,
                "purpose": "disposable emulator disk; emulator writes may change its hash",
                "source": {"path": str(rootfs), "bytes": opened.st_size, "sha256": source_hash},
                "disk": {"path": str(output), "bytes": PARTITION_OFFSET + opened.st_size, "sha256": disk_hash.hexdigest()},
                "filesystem": filesystem,
                "partition": {"index": 1, "type": "0x83", "start_lba": PARTITION_START, "sectors": sectors, "offset_bytes": PARTITION_OFFSET},
                "disk_signature_hex": DISK_SIGNATURE.hex(),
            }
            with tempfile.NamedTemporaryFile(mode="w", encoding="utf-8", newline="\n", dir=manifest.parent, prefix="." + manifest.name + ".", suffix=".tmp", delete=False) as target:
                manifest_temporary = Path(target.name)
                temporary_paths.append(manifest_temporary)
                json.dump(report, target, indent=2, sort_keys=True)
                target.write("\n")
                target.flush()
                os.fsync(target.fileno())
            # Each artifact is replaced atomically. Consumers must validate the
            # manifest hash; two separate filenames cannot be one transaction.
            regular_path(output, must_exist=False)
            regular_path(manifest, must_exist=False)
            if any(same_file(first, second) for first, second in ((rootfs, output), (rootfs, manifest), (output, manifest))):
                raise DiskImageError("destination changed to an alias during copying")
            os.replace(disk_temporary, output)
            os.replace(manifest_temporary, manifest)
            return report
    finally:
        for temporary in temporary_paths:
            temporary.unlink(missing_ok=True)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--rootfs", type=Path, required=True, help="verified existing rootfs.ext4 regular file")
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT, help="disposable MBR image; defaults to build/emulator/rootfs-usb.img")
    parser.add_argument("--manifest", type=Path, help="SHA-256 JSON manifest; defaults to OUTPUT.json")
    args = parser.parse_args(argv)
    try:
        report = wrap_rootfs(args.rootfs, args.output, args.manifest)
    except (DiskImageError, OSError) as error:
        print(f"emulator disk: {error}", file=sys.stderr)
        return 1
    print(f"emulator disk: {report['disk']['path']} ({report['disk']['bytes']} bytes, partition 1 at LBA {PARTITION_START})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
