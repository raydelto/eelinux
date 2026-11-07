#!/usr/bin/env python3
"""Copy a verified project USB image into a disposable emulator regular file.

No filesystem is mounted and no physical drive is opened for writing.
SPDX-License-Identifier: GPL-2.0-only
"""
from __future__ import annotations

from contextlib import ExitStack
import hashlib
import json
import os
from pathlib import Path
import re
import stat
import struct
import tempfile

from emulator_disk import (COPY_CHUNK, DISK_SIGNATURE, DiskImageError, filesystem_info,
                           hash_stream, identity, regular_path, same_file)

SECTOR = 512
PARTITION_BYTES = 64 * 1024 * 1024
PARTITION_SECTORS = PARTITION_BYTES // SECTOR
FAT_START = 2048
ROOT_START = FAT_START + PARTITION_SECTORS
DISK_BYTES = (ROOT_START + PARTITION_SECTORS) * SECTOR
PAL_VIDEO = 'PAL 576x460i@50'


def _open(stack: ExitStack, path: Path):
    before = path.stat()
    flags = os.O_RDONLY | getattr(os, 'O_BINARY', 0) | getattr(os, 'O_NOFOLLOW', 0) | getattr(os, 'O_NONBLOCK', 0)
    descriptor = os.open(path, flags)
    stream = stack.enter_context(os.fdopen(descriptor, 'rb'))
    opened = os.fstat(descriptor)
    if (not stat.S_ISREG(opened.st_mode) or opened.st_nlink != 1 or
            identity(before) != identity(opened)):
        raise DiskImageError(f'input changed or is a hard-link alias: {path}')
    return stream, opened


def _unchanged(path: Path, stream, opened):
    regular_path(path, must_exist=True)
    if (identity(os.fstat(stream.fileno())) != identity(opened) or
            identity(path.stat()) != identity(opened) or path.stat().st_nlink != 1):
        raise DiskImageError(f'input changed during validation/copying: {path}')


def _hash_region(stream, offset: int, size: int) -> str:
    stream.seek(offset)
    digest = hashlib.sha256()
    while size:
        chunk = stream.read(min(size, COPY_CHUNK))
        if not chunk:
            raise DiskImageError('disk region was truncated during validation')
        digest.update(chunk)
        size -= len(chunk)
    return digest.hexdigest()


def _record(value, name: str) -> dict:
    if not isinstance(value, dict):
        raise DiskImageError(f'manifest {name} must be an object')
    return value


def _check_record(record: dict, size: int, digest: str, name: str):
    if (type(record.get('bytes')) is not int or record['bytes'] != size or
            not isinstance(record.get('sha256'), str) or
            not re.fullmatch('[0-9a-f]{64}', record['sha256']) or record['sha256'] != digest):
        raise DiskImageError(f'{name} bytes/hash do not match the supplied regular file')


def _fat32(stream, kernel_size: int, kernel_hash: str) -> dict:
    """Validate BPB geometry, FAT copies and actual EELINUX.ELF file readback."""
    base = FAT_START * SECTOR
    stream.seek(base)
    boot = stream.read(SECTOR)
    if len(boot) != SECTOR or boot[510:512] != b'\x55\xaa':
        raise DiskImageError('FAT32 boot-sector signature is missing')
    sector_bytes = struct.unpack_from('<H', boot, 11)[0]
    cluster_sectors = boot[13]
    reserved = struct.unpack_from('<H', boot, 14)[0]
    fats = boot[16]
    root_entries = struct.unpack_from('<H', boot, 17)[0]
    total16 = struct.unpack_from('<H', boot, 19)[0]
    fat16 = struct.unpack_from('<H', boot, 22)[0]
    hidden, total, fat_sectors = struct.unpack_from('<III', boot, 28)
    root_cluster = struct.unpack_from('<I', boot, 44)[0]
    version = struct.unpack_from('<H', boot, 42)[0]
    extflags = struct.unpack_from('<H', boot, 40)[0]
    if (sector_bytes != SECTOR or cluster_sectors != 1 or not reserved or fats != 2 or
            root_entries or total16 or fat16 or not fat_sectors or hidden != FAT_START or
            total != PARTITION_SECTORS or boot[82:90] != b'FAT32   ' or version or extflags):
        raise DiskImageError('unexpected project FAT32 BPB geometry')
    data_sectors = total - reserved - fats * fat_sectors
    clusters = data_sectors // cluster_sectors
    if (not 65525 <= clusters < 0x0FFFFFF5 or
            fat_sectors * SECTOR // 4 < clusters + 2 or
            not 2 <= root_cluster <= clusters + 1):
        raise DiskImageError('invalid FAT32 cluster region or FAT size')
    fat_offset = base + reserved * SECTOR
    fat_bytes = fat_sectors * SECTOR
    if _hash_region(stream, fat_offset, fat_bytes) != _hash_region(stream, fat_offset + fat_bytes, fat_bytes):
        raise DiskImageError('FAT32 mirrored FAT tables differ')
    data_offset = base + (reserved + fats * fat_sectors) * SECTOR

    def next_cluster(cluster):
        if not 2 <= cluster <= clusters + 1:
            raise DiskImageError('FAT32 chain points outside its data region')
        stream.seek(fat_offset + cluster * 4)
        data = stream.read(4)
        if len(data) != 4:
            raise DiskImageError('truncated FAT32 chain')
        return struct.unpack('<I', data)[0] & 0x0FFFFFFF

    def read_cluster(cluster):
        if not 2 <= cluster <= clusters + 1:
            raise DiskImageError('invalid FAT32 data cluster')
        stream.seek(data_offset + (cluster - 2) * SECTOR)
        data = stream.read(SECTOR)
        if len(data) != SECTOR:
            raise DiskImageError('truncated FAT32 data cluster')
        return data

    seen = set()
    cluster = root_cluster
    found = None
    finished = False
    while not finished:
        if cluster in seen or len(seen) >= 128:
            raise DiskImageError('cyclic or oversized FAT32 root directory')
        seen.add(cluster)
        data = read_cluster(cluster)
        for index in range(0, SECTOR, 32):
            entry = data[index:index + 32]
            if entry[0] == 0:
                finished = True
                break
            if entry[0] == 0xE5 or entry[11] == 0x0F or entry[11] & 0x08:
                continue
            if entry[:11] == b'EELINUX ELF':
                if found is not None or entry[11] & 0x10:
                    raise DiskImageError('duplicate or nonregular EELINUX.ELF in FAT32 root')
                file_cluster = struct.unpack_from('<H', entry, 20)[0] << 16 | struct.unpack_from('<H', entry, 26)[0]
                found = file_cluster, struct.unpack_from('<I', entry, 28)[0]
        if not finished:
            cluster = next_cluster(cluster)
            finished = cluster >= 0x0FFFFFF8
    if found is None or found[1] != kernel_size:
        raise DiskImageError('actual FAT32 EELINUX.ELF is missing or has the wrong size')
    cluster, remaining = found
    seen.clear()
    digest = hashlib.sha256()
    while remaining:
        if cluster in seen:
            raise DiskImageError('cyclic FAT32 EELINUX.ELF cluster chain')
        seen.add(cluster)
        data = read_cluster(cluster)
        used = min(remaining, SECTOR)
        digest.update(data[:used])
        remaining -= used
        cluster = next_cluster(cluster)
    if cluster < 0x0FFFFFF8 or digest.hexdigest() != kernel_hash:
        raise DiskImageError('actual FAT32 EELINUX.ELF payload differs from the supplied kernel')
    return {'type': 'FAT32', 'bytes': PARTITION_BYTES, 'sector_bytes': SECTOR,
            'cluster_sectors': cluster_sectors, 'data_clusters': clusters,
            'fat_sectors': fat_sectors, 'root_cluster': root_cluster}


def copy_hardware_disk(image: Path, rootfs: Path, kernel: Path, output: Path) -> dict:
    """Validate then exclusively publish an unchanged, disposable USB-image copy."""
    image, rootfs, kernel = [regular_path(Path(path), must_exist=True) for path in (image, rootfs, kernel)]
    evidence = regular_path(Path(str(image) + '.json'), must_exist=True)
    output = regular_path(Path(output), must_exist=False)
    manifest = regular_path(Path(str(output) + '.json'), must_exist=False)
    paths = [image, rootfs, kernel, evidence, output, manifest]
    if any(same_file(first, second) for index, first in enumerate(paths) for second in paths[index + 1:]):
        raise DiskImageError('inputs, disposable disk and manifest must not alias, including hard links')
    if output.exists() or manifest.exists():
        raise DiskImageError('choose a new disposable image and manifest; existing files are preserved')
    temporary_paths = []
    published = []
    try:
        with ExitStack() as stack:
            opened = {path: _open(stack, path) for path in paths[:4]}
            disk, disk_stat = opened[image]
            root, root_stat = opened[rootfs]
            elf, elf_stat = opened[kernel]
            evidence_stream, evidence_stat = opened[evidence]
            if (not PARTITION_BYTES <= root_stat.st_size <= 4096 * 1024 * 1024 or
                    root_stat.st_size % (1024 * 1024) or
                    not 0 <= disk_stat.st_size-(ROOT_START * SECTOR+root_stat.st_size) < 1024*1024 or
                    disk_stat.st_size%SECTOR or
                    not 0 < elf_stat.st_size <= 4 * 1024 * 1024 or
                    not 0 < evidence_stat.st_size <= 4 * 1024 * 1024):
                raise DiskImageError('unexpected project disk, rootfs, kernel or manifest size')
            report = _record(json.load(evidence_stream), 'root')
            padding=disk_stat.st_size-(ROOT_START*SECTOR+root_stat.st_size)
            if type(report.get('disk',{}).get('trailing_padding_bytes',0)) is not int or \
               report.get('disk',{}).get('trailing_padding_bytes',0)!=padding:
                raise DiskImageError('trailing padding differs from the manifest')
            if padding:
                disk.seek(ROOT_START*SECTOR+root_stat.st_size)
                if disk.read(padding)!=bytes(padding):raise DiskImageError('trailing padding must contain zeros')
            if type(report.get('schema')) is not int or report['schema'] != 1:
                raise DiskImageError('unsupported hardware image manifest schema')
            hashes = {path: hash_stream(stream) for path, (stream, _) in opened.items()}
            _check_record(_record(report.get('disk'), 'disk'), disk_stat.st_size, hashes[image], 'hardware disk')
            _check_record(_record(report.get('source_rootfs'), 'source_rootfs'), root_stat.st_size, hashes[rootfs], 'rootfs')
            kernel_record = _record(report.get('kernel'), 'kernel')
            _check_record(kernel_record, elf_stat.st_size, hashes[kernel], 'kernel')
            command_line = kernel_record.get('command_line')
            if not isinstance(command_line, str):
                raise DiskImageError('kernel command line must declare a checked video mode and root on /dev/sda2')
            args = command_line.split()
            from video import BOOT_MODES
            checked_video = {('PAL ' if name == 'pal' else name.upper() + ' ') + spec:
                             'video=ps2fb:' + spec for name, spec in BOOT_MODES.items() if spec}
            expected_video = checked_video.get(kernel_record.get('video'))
            if (kernel_record.get('root_device') != '/dev/sda2' or expected_video is None or
                    kernel_record.get('fat_path') != 'EELINUX.ELF' or
                    [arg for arg in args if arg.startswith('root=')] != ['root=/dev/sda2'] or
                    [arg for arg in args if arg.startswith('video=')] != [expected_video] or
                    [arg for arg in args if arg.startswith('rootfstype=')] not in
                    [['rootfstype=ext4'], ['rootfstype=ext2']]):
                raise DiskImageError('hardware image must declare matching checked video and ext4 or historical ext2 root partition 2')
            disk.seek(0)
            mbr = disk.read(SECTOR)
            if mbr[510:512] != b'\x55\xaa' or mbr[440:444] != DISK_SIGNATURE or any(mbr[478:510]):
                raise DiskImageError('unexpected hardware MBR signature or extra partitions')
            partitions = report.get('partitions')
            if not isinstance(partitions, list) or len(partitions) != 2:
                raise DiskImageError('hardware manifest must contain exactly two partitions')
            verified_partitions = []
            for index, kind, start in ((1, 0x0C, FAT_START), (2, 0x83, ROOT_START)):
                status, _, actual_kind, _, actual_start, sectors = struct.unpack_from('<B3sB3sII', mbr, 446 + (index - 1) * 16)
                expected_sectors = PARTITION_SECTORS if index == 1 else root_stat.st_size // SECTOR
                if (status != 0 or actual_kind != kind or actual_start != start or sectors != expected_sectors or
                        (actual_start + sectors) * SECTOR > disk_stat.st_size):
                    raise DiskImageError('actual MBR has an invalid, overlapping or unexpected partition layout')
                expected = {'index': index, 'type': f'0x{kind:02x}', 'start_lba': start,
                            'sectors': sectors, 'offset_bytes': start * SECTOR}
                declared = _record(partitions[index - 1], f'partition {index}')
                if any(type(declared.get(key)) is not type(value) or declared[key] != value for key, value in expected.items()):
                    raise DiskImageError('manifest partition geometry differs from the actual MBR')
                verified_partitions.append(expected)
            filesystem = filesystem_info(root, root_stat.st_size)
            if 'rootfstype=' + filesystem['type'] not in args:
                raise DiskImageError('kernel filesystem setting differs from the actual root image')
            if _hash_region(disk, ROOT_START * SECTOR, root_stat.st_size) != hashes[rootfs]:
                raise DiskImageError('actual ext2 partition payload differs from the supplied rootfs')
            geometry = _fat32(disk, elf_stat.st_size, hashes[kernel])
            verified_partitions[0]['filesystem'] = geometry
            verified_partitions[1]['filesystem'] = filesystem
            output.parent.mkdir(parents=True, exist_ok=True)
            regular_path(output, must_exist=False)
            regular_path(manifest, must_exist=False)
            with tempfile.NamedTemporaryFile(mode='wb', dir=output.parent, prefix='.' + output.name + '.', suffix='.tmp', delete=False) as target:
                disk_temporary = Path(target.name)
                temporary_paths.append(disk_temporary)
                disk.seek(0)
                copied = hashlib.sha256()
                remaining = disk_stat.st_size
                while remaining:
                    chunk = disk.read(min(remaining, COPY_CHUNK))
                    if not chunk:
                        raise DiskImageError('hardware image was truncated while copying')
                    target.write(chunk)
                    copied.update(chunk)
                    remaining -= len(chunk)
                target.flush()
                os.fsync(target.fileno())
            if copied.hexdigest() != hashes[image]:
                raise DiskImageError('hardware image changed while copying')
            for path, (stream, info) in opened.items():
                _unchanged(path, stream, info)
                if hash_stream(stream) != hashes[path]:
                    raise DiskImageError(f'input bytes changed during copying: {path}')
                _unchanged(path, stream, info)
            result = {'schema': 1, 'purpose': 'disposable emulator copy of the hardware USB image',
                      'source': {'path': str(rootfs), 'bytes': root_stat.st_size, 'sha256': hashes[rootfs]},
                      'disk': {'path': str(output), 'bytes': disk_stat.st_size, 'sha256': hashes[image]},
                      'hardware_source': {'path': str(image), 'bytes': disk_stat.st_size, 'sha256': hashes[image],
                                          'manifest_path': str(evidence), 'manifest_sha256': hashes[evidence]},
                      'kernel': {'path': str(kernel), 'bytes': elf_stat.st_size, 'sha256': hashes[kernel]},
                      'filesystem': filesystem, 'partition': verified_partitions[1],
                      'partitions': verified_partitions, 'required_guest_root': '/dev/sda2',
                      'video': kernel_record['video'], 'disk_signature_hex': DISK_SIGNATURE.hex()}
            with tempfile.NamedTemporaryFile(mode='w', encoding='utf-8', newline='\n', dir=output.parent,
                                             prefix='.' + manifest.name + '.', suffix='.tmp', delete=False) as target:
                manifest_temporary = Path(target.name)
                temporary_paths.append(manifest_temporary)
                json.dump(result, target, indent=2, sort_keys=True)
                target.write('\n')
                target.flush()
                os.fsync(target.fileno())
            for destination, temporary in ((output, disk_temporary), (manifest, manifest_temporary)):
                regular_path(destination, must_exist=False)
                os.link(temporary, destination)  # Atomic and refuses a concurrently created output.
                published.append((destination, temporary))
            return result
    except BaseException:
        for destination, temporary in reversed(published):
            if same_file(destination, temporary):
                destination.unlink()
        raise
    finally:
        for temporary in temporary_paths:
            temporary.unlink(missing_ok=True)
