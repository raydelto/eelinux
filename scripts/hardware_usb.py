#!/usr/bin/env python3
"""Package a checked PS2 audit kernel and GNU root in a regular USB disk image.

This script never opens a physical drive for writing or mounts a filesystem.
FAT32 partition 1 is browsable by wLaunchELF; ext4 partition 2 is /dev/sda2.
SPDX-License-Identifier: GPL-2.0-only
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import stat
import struct
import subprocess
import sys
import tempfile

from emulator import validate_kernel
from emulator_disk import (COPY_CHUNK, DISK_SIGNATURE, DiskImageError, chs,
                           filesystem_info, hash_stream, identity, regular_path, same_file)

SECTOR = 512
PARTITION_BYTES = 64 * 1024 * 1024
PARTITION_SECTORS = PARTITION_BYTES // SECTOR
FAT_START = 2048
ROOT_START = FAT_START + PARTITION_SECTORS
DISK_BYTES = (ROOT_START + PARTITION_SECTORS) * SECTOR
KERNEL_NAME = 'EELINUX.ELF'
DEFAULT_OUTPUT = Path(__file__).resolve().parents[1] / 'build/images/hardware-576p/eelinux-usb-576p.img'
README = b'''eeLinux 7.2.8 PAL experimental hardware test\r
Open EELINUX.ELF using your existing Tuna/wLaunchELF setup.\r
Linux root is partition 2 (/dev/sda2). Video is forced PAL 50 Hz.\r
Login policy comes from the supplied GNU root filesystem.\r
Boot and login on the physical PS2 remain experimental.\r
See docs/hardware-test.md in the source project.\r
'''


def tool(name: str) -> str:
    executable = shutil.which(name)
    if executable:
        return executable
    for directory in ('/usr/sbin', '/sbin', '/usr/bin'):
        candidate = Path(directory) / name
        if candidate.is_file() and os.access(candidate, os.X_OK):
            return str(candidate)
    raise DiskImageError(f'{name} is required; install dosfstools and mtools in Linux/WSL')


def _open_regular(path: Path):
    before = path.stat()
    flags = os.O_RDONLY | getattr(os, 'O_BINARY', 0) | getattr(os, 'O_NOFOLLOW', 0) | getattr(os, 'O_NONBLOCK', 0)
    descriptor = os.open(path, flags)
    opened = os.fstat(descriptor)
    if not stat.S_ISREG(opened.st_mode) or identity(before) != identity(opened):
        os.close(descriptor)
        raise DiskImageError(f'input changed before opening: {path}')
    return os.fdopen(descriptor, 'rb'), opened


def _snapshot(path: Path, limit: int) -> tuple[bytes, tuple[int, ...]]:
    stream, opened = _open_regular(path)
    with stream:
        if opened.st_size > limit:
            raise DiskImageError(f'input exceeds its size limit: {path}')
        data = stream.read(limit + 1)
        if len(data) != opened.st_size or identity(os.fstat(stream.fileno())) != identity(opened):
            raise DiskImageError(f'input changed during reading: {path}')
        if identity(path.stat()) != identity(opened):
            raise DiskImageError(f'input path changed during reading: {path}')
    return data, identity(opened)


def _sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def validate_inputs(kernel: Path, config: Path, report_path: Path,
                    video_mode: str = '576p', require_cache_handoff: bool = False) -> dict:
    """Require reviewed ELF bytes, a matching root2 config and the selected mode."""
    from video import BOOT_MODES
    modes = {name: spec for name, spec in BOOT_MODES.items() if spec}
    if video_mode not in modes:
        raise DiskImageError('a checked, explicit hardware video profile is required')
    video = modes[video_mode]
    kernel_data, kernel_identity = _snapshot(kernel, 4 * 1024 * 1024)
    config_data, config_identity = _snapshot(config, 512 * 1024)
    report_data, report_identity = _snapshot(report_path, 4 * 1024 * 1024)
    validate_kernel(kernel)
    report = json.loads(report_data)
    if report.get('image_sha256') != _sha(kernel_data) or report.get('image_bytes') != len(kernel_data):
        raise DiskImageError('kernel bytes do not match the supplied build report')
    if report.get('config_sha256') != _sha(config_data):
        raise DiskImageError('kernel config does not match the supplied build report')
    if report.get('root_device') != '/dev/sda2':
        raise DiskImageError('kernel build report must identify /dev/sda2 as root')
    lines = re.findall(r'^CONFIG_CMDLINE=(.+)$', config_data.decode('utf-8'), re.M)
    if len(lines) != 1:
        raise DiskImageError('kernel config must contain exactly one command line')
    command_line = json.loads(lines[0])
    if not isinstance(command_line, str):
        raise DiskImageError('kernel command line must be a string')
    arguments = command_line.split()
    if ([arg for arg in arguments if arg.startswith('root=')] != ['root=/dev/sda2'] or
            [arg for arg in arguments if arg.startswith('video=')] != ['video=ps2fb:' + video] or
            [arg for arg in arguments if arg.startswith('rootfstype=')] not in
            [['rootfstype=ext4'], ['rootfstype=ext2']]):
        raise DiskImageError(f'kernel must use /dev/sda2 ext4 or historical ext2 root and forced {video_mode.upper()} {video}')
    layout = report.get('layout', {})
    unpacked = report.get('initramfs_unpacked_bytes')
    if (layout.get('decompressor_overlap_check') != 'passed' or
            layout.get('ram_bytes') != 32 * 1024 * 1024 or
            not isinstance(unpacked, int) or isinstance(unpacked, bool) or
            not 0 < unpacked <= 4 * 1024 * 1024):
        raise DiskImageError('kernel needs a passed 32 MiB layout and bounded real initramfs report')
    if require_cache_handoff:
        checks = report.get('compiled_cache_checks', {})
        if (checks.get('writeback_before_instruction_invalidate_before_kernel_jump') is not True or
                checks.get('sync_immediately_before_after_cache') is not True or
                checks.get('no_cache_instruction_at_page_tail') is not True or
                checks.get('function_alignment_bytes') != 64 or
                checks.get('both_cache_ways') is not True):
            raise DiskImageError('EECACHE.ELF requires the checked compiled cache-handoff report')
    return {'kernel_bytes': kernel_data, 'config_bytes': config_data, 'report_bytes': report_data,
            'identities': {kernel: kernel_identity, config: config_identity, report_path: report_identity},
            'command_line': command_line, 'build_report': report}


def fat32_info(path: Path) -> dict:
    """Check FAT32 BPB geometry independently of the formatter's exit status."""
    with path.open('rb') as stream:
        boot = stream.read(512)
    if len(boot) != 512 or boot[510:512] != b'\x55\xaa':
        raise DiskImageError('FAT partition has no valid boot-sector signature')
    sector_bytes = struct.unpack_from('<H', boot, 11)[0]
    cluster_sectors = boot[13]
    reserved = struct.unpack_from('<H', boot, 14)[0]
    fats = boot[16]
    root_entries = struct.unpack_from('<H', boot, 17)[0]
    total16 = struct.unpack_from('<H', boot, 19)[0]
    fat16 = struct.unpack_from('<H', boot, 22)[0]
    hidden, total, fat_sectors = struct.unpack_from('<III', boot, 28)
    root_cluster = struct.unpack_from('<I', boot, 44)[0]
    if (sector_bytes != SECTOR or cluster_sectors != 1 or not reserved or fats != 2 or
            root_entries or total16 or fat16 or not fat_sectors or hidden != FAT_START or
            total * sector_bytes != path.stat().st_size or boot[82:90] != b'FAT32   '):
        raise DiskImageError('unexpected FAT32 BPB geometry')
    data_sectors = total - reserved - fats * fat_sectors
    clusters = data_sectors // cluster_sectors
    if (not 65525 <= clusters < 0x0FFFFFF5 or
            fat_sectors * sector_bytes // 4 < clusters + 2 or
            not 2 <= root_cluster <= clusters + 1):
        raise DiskImageError('FAT data region is not a valid FAT32 cluster count')
    return {'type': 'FAT32', 'bytes': path.stat().st_size, 'sector_bytes': sector_bytes,
            'cluster_sectors': cluster_sectors, 'data_clusters': clusters,
            'fat_sectors': fat_sectors, 'root_cluster': root_cluster}


def disk_prefix(root_sectors: int = PARTITION_SECTORS) -> bytes:
    prefix = bytearray(FAT_START * SECTOR)
    prefix[440:444] = DISK_SIGNATURE
    for index, kind, start in ((0, 0x0C, FAT_START), (1, 0x83, ROOT_START)):
        sectors = PARTITION_SECTORS if index == 0 else root_sectors
        struct.pack_into('<B3sB3sII', prefix, 446 + 16 * index, 0, chs(start), kind,
                         chs(start + sectors - 1), start, sectors)
    prefix[510:512] = b'\x55\xaa'
    return bytes(prefix)


def build_image(rootfs: Path, kernel: Path, config: Path, kernel_report: Path,
                output: Path = DEFAULT_OUTPUT, manifest: Path | None = None, *,
                ntsc: tuple[Path, Path, Path] | None = None,
                cache: tuple[Path, Path, Path] | None = None, video_mode: str = '576p',
                root_size_mib: int = 64, readme_bytes: bytes | None = None,
                disk_size_bytes: int | None = None) -> dict:
    from video import BOOT_MODES
    if isinstance(root_size_mib, bool) or not isinstance(root_size_mib, int) or not 64 <= root_size_mib <= 4096:
        raise DiskImageError('root size must be 64..4096 MiB')
    if readme_bytes is not None and (not isinstance(readme_bytes, bytes) or not 1 <= len(readme_bytes) <= 65536):
        raise DiskImageError('custom README must contain 1..65536 bytes')
    root_bytes = root_size_mib * 1024 * 1024
    root_sectors = root_bytes // SECTOR
    disk_bytes = ROOT_START * SECTOR + root_bytes
    padding=0
    if disk_size_bytes is not None:
        if isinstance(disk_size_bytes,bool) or not isinstance(disk_size_bytes,int) or \
           disk_size_bytes<disk_bytes or disk_size_bytes%SECTOR or disk_size_bytes-disk_bytes>=1024*1024:
            raise DiskImageError('explicit disk size must be sector aligned with less than 1 MiB trailing padding')
        padding=disk_size_bytes-disk_bytes;disk_bytes=disk_size_bytes
    triplets = [(KERNEL_NAME, video_mode, False, (kernel, config, kernel_report))]
    for value, name, mode, cache_required in ((ntsc, 'EENTSC.ELF', 'ntsc', False),
                                             (cache, 'EECACHE.ELF', 'pal', True)):
        if value is not None:
            if len(value) != 3:
                raise DiskImageError(f'{name} requires an ELF/config/report triplet')
            triplets.append((name, mode, cache_required, value))
    rootfs = regular_path(Path(rootfs), must_exist=True)
    prepared = [(name, mode, cache_required,
                 tuple(regular_path(Path(p), must_exist=True) for p in paths))
                for name, mode, cache_required, paths in triplets]
    inputs = [rootfs, *(p for _, _, _, paths in prepared for p in paths)]
    kernel, config, kernel_report = prepared[0][3]
    output = regular_path(Path(output), must_exist=False)
    manifest = regular_path(Path(manifest) if manifest is not None else Path(str(output) + '.json'), must_exist=False)
    paths = [*inputs, output, manifest]
    if any(same_file(first, second) for i, first in enumerate(paths) for second in paths[i + 1:]):
        raise DiskImageError('inputs, image and manifest must be different regular files')
    if output.exists() or manifest.exists():
        raise DiskImageError('choose new image and manifest paths; existing artifacts are preserved')
    payloads = [(name, mode, paths, validate_inputs(*paths, mode, cache_required))
                for name, mode, cache_required, paths in prepared]
    validated = payloads[0][3]
    source, opened = _open_regular(rootfs)
    temporary_paths: list[Path] = []
    try:
        with source:
            if opened.st_size != root_bytes:
                raise DiskImageError(f'this hardware layout requires an existing {root_size_mib} MiB root image')
            filesystem = filesystem_info(source, opened.st_size)
            for _, _, _, payload in payloads:
                if 'rootfstype=' + filesystem['type'] not in payload['command_line'].split():
                    raise DiskImageError('kernel filesystem setting differs from the actual root image')
            executables = {name: tool(name) for name in ('mkfs.fat', 'fsck.fat', 'mcopy', 'mtype', 'mdir')}
            output.parent.mkdir(parents=True, exist_ok=True)
            manifest.parent.mkdir(parents=True, exist_ok=True)
            regular_path(output, must_exist=False)
            regular_path(manifest, must_exist=False)
            environment = dict(os.environ, MTOOLSRC=os.devnull)
            def run(arguments):
                return subprocess.run(arguments, check=True, capture_output=True, env=environment, timeout=60)

            with tempfile.TemporaryDirectory(prefix='eelinux-hardware-usb-') as scratch_name:
                scratch = Path(scratch_name)
                fat = scratch / 'boot.fat'
                with fat.open('wb') as target:
                    target.truncate(PARTITION_BYTES)
                for name, _, _, payload in payloads:
                    (scratch / name).write_bytes(payload['kernel_bytes'])
                readme = README if readme_bytes is None else readme_bytes
                if readme_bytes is None and video_mode != 'pal':
                    readme = readme.replace(b'PAL experimental hardware test',
                        (video_mode + ' experimental hardware test').encode())
                    readme = readme.replace(b'Video is forced PAL 50 Hz',
                        ('Video profile is ' + video_mode).encode())
                readme += f"Filesystem: {filesystem['type']}.\r\n".encode()
                for name, mode, _, _ in payloads[1:]:
                    readme += f'{name}: separate {mode.upper()} diagnostic; Linux root remains /dev/sda2.\r\n'.encode()
                (scratch / 'README.TXT').write_bytes(readme)
                formatted = run([executables['mkfs.fat'], '-F', '32', '-s', '1', '-h', str(FAT_START),
                                 '-n', 'EELINUX', '--invariant', str(fat)])
                geometry = fat32_info(fat)
                for name in (*(entry[0] for entry in payloads), 'README.TXT'):
                    run([executables['mcopy'], '-i', str(fat), str(scratch / name), '::/' + name])
                checked = run([executables['fsck.fat'], '-n', str(fat)])
                directory = run([executables['mdir'], '-i', str(fat), '::/'])
                if run([executables['mtype'], '-i', str(fat), '::/README.TXT']).stdout != readme:
                    raise DiskImageError('FAT README readback differs from the prepared file')
                for name, _, _, payload in payloads:
                    readback = scratch / ('readback-' + name)
                    run([executables['mcopy'], '-i', str(fat), '::/' + name, str(readback)])
                    if readback.read_bytes() != payload['kernel_bytes']:
                        raise DiskImageError(f'FAT {name} readback differs from the reviewed ELF')
                prefix = disk_prefix(root_sectors)
                disk_hash = hashlib.sha256(prefix)
                root_hash = hashlib.sha256()
                with tempfile.NamedTemporaryFile(mode='wb', dir=output.parent,
                        prefix='.' + output.name + '.', suffix='.tmp', delete=False) as target:
                    disk_temporary = Path(target.name)
                    temporary_paths.append(disk_temporary)
                    target.write(prefix)
                    with fat.open('rb') as fat_source:
                        while chunk := fat_source.read(COPY_CHUNK):
                            target.write(chunk)
                            disk_hash.update(chunk)
                    source.seek(0)
                    remaining = opened.st_size
                    while remaining:
                        chunk = source.read(min(COPY_CHUNK, remaining))
                        if not chunk:
                            raise DiskImageError('root filesystem changed while copying')
                        target.write(chunk)
                        disk_hash.update(chunk)
                        root_hash.update(chunk)
                        remaining -= len(chunk)
                    if padding:
                        tail=bytes(padding);target.write(tail);disk_hash.update(tail)
                    target.flush()
                    os.fsync(target.fileno())
                if (disk_temporary.stat().st_size != disk_bytes or
                        hash_stream(source) != root_hash.hexdigest() or
                        identity(os.fstat(source.fileno())) != identity(opened) or
                        identity(rootfs.stat()) != identity(opened)):
                    raise DiskImageError('root source changed; hardware image was not published')
                for _, _, triplet, payload in payloads:
                    for path, expected in payload['identities'].items():
                        if identity(path.stat()) != expected:
                            raise DiskImageError(f'kernel build input changed: {path}')
                    for path, content in zip(triplet, (payload['kernel_bytes'], payload['config_bytes'], payload['report_bytes'])):
                        if _snapshot(path, len(content))[0] != content:
                            raise DiskImageError(f'kernel build input changed: {path}')
                def kernel_record(name, mode, triplet, payload):
                    elf, conf, evidence = triplet
                    build = payload['build_report']
                    return {'path': str(elf), 'fat_path': name, 'bytes': len(payload['kernel_bytes']),
                            'sha256': _sha(payload['kernel_bytes']), 'config_path': str(conf),
                            'config_sha256': _sha(payload['config_bytes']), 'build_report_path': str(evidence),
                            'build_report_sha256': _sha(payload['report_bytes']),
                            'api_patch_sha256': build.get('api_patch_sha256'),
                            'root_device': '/dev/sda2',
                            'video': ('PAL 576x460i@50' if mode == 'pal' else
                                      'NTSC 576x384i@60' if mode == 'ntsc' else
                                      mode.upper() + ' ' + BOOT_MODES[mode]),
                            'command_line': payload['command_line'],
                            'cache_handoff_probe': build.get('compiled_cache_checks', {}).get(
                                'writeback_before_instruction_invalidate_before_kernel_jump') is True}
                build = validated['build_report']
                report = {
                    'schema': 1, 'purpose': f'single USB {video_mode} hardware audit; regular image file only',
                    'disk': {'path': str(output), 'bytes': disk_bytes, 'sha256': disk_hash.hexdigest(),
                             'trailing_padding_bytes':padding},
                    'source_rootfs': {'path': str(rootfs), 'bytes': opened.st_size, 'sha256': root_hash.hexdigest()},
                    'kernel': kernel_record(*payloads[0]),
                    'diagnostics': [kernel_record(*entry) for entry in payloads[1:]],
                    'partitions': [
                        {'index': 1, 'type': '0x0c', 'start_lba': FAT_START, 'sectors': PARTITION_SECTORS,
                         'offset_bytes': FAT_START * SECTOR, 'filesystem': geometry},
                        {'index': 2, 'type': '0x83', 'start_lba': ROOT_START, 'sectors': root_sectors,
                         'offset_bytes': ROOT_START * SECTOR, 'filesystem': filesystem}],
                    'checks': {'fat32_bpb_and_cluster_count': 'passed', 'fsck_fat_read_only': 'passed',
                               'kernel_fat_readback_byte_exact': True, 'readme_fat_readback_byte_exact': True,
                               'diagnostic_fat_readback_byte_exact': {entry[0]: True for entry in payloads[1:]},
                               'root_payload_byte_exact': True, 'source_inputs_unchanged': True},
                    'host_tools': executables,
                    'tool_output': {'formatter': formatted.stdout.decode(errors='replace').strip(),
                                    'checker': checked.stdout.decode(errors='replace').strip(),
                                    'directory': directory.stdout.decode(errors='replace').strip()},
                    'qualification': {'production_gate': build.get('production_gate'),
                                      'remaining_rejected_port_hunks': build.get('remaining_rejected_port_hunks'),
                                      'linux_boot_pass': None, 'real_hardware_tested': False,
                                      'gnu_sysvinit_started': False, 'authenticated_shell': False},
                    'physical_devices_written': False}
                with tempfile.NamedTemporaryFile(mode='w', encoding='utf-8', newline='\n', dir=manifest.parent,
                        prefix='.' + manifest.name + '.', suffix='.tmp', delete=False) as target:
                    manifest_temporary = Path(target.name)
                    temporary_paths.append(manifest_temporary)
                    json.dump(report, target, indent=2, sort_keys=True)
                    target.write('\n')
                    target.flush()
                    os.fsync(target.fileno())
                # Each filename is published atomically. Hard-link creation also
                # refuses a destination created by another process in the meantime.
                # Consumers must verify the manifest hash; two names are not one transaction.
                regular_path(output, must_exist=False)
                regular_path(manifest, must_exist=False)
                os.link(disk_temporary, output)
                os.link(manifest_temporary, manifest)
                return report
    finally:
        if not source.closed:
            source.close()
        for path in temporary_paths:
            path.unlink(missing_ok=True)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--rootfs', type=Path, required=True)
    parser.add_argument('--kernel', type=Path, required=True)
    parser.add_argument('--kernel-config', type=Path, required=True)
    parser.add_argument('--kernel-report', type=Path, required=True)
    parser.add_argument('--output', type=Path, default=DEFAULT_OUTPUT)
    parser.add_argument('--manifest', type=Path)
    parser.add_argument('--root-size-mib', type=int, default=64, help='explicit root partition size; boot partition remains 64 MiB')
    from video import BOOT_MODES
    parser.add_argument('--video-mode', choices=tuple(name for name, spec in BOOT_MODES.items() if spec), default='576p')
    for name in ('ntsc', 'cache'):
        parser.add_argument(f'--{name}-kernel', type=Path)
        parser.add_argument(f'--{name}-config', type=Path)
        parser.add_argument(f'--{name}-report', type=Path)
    args = parser.parse_args(argv)
    diagnostics = {}
    for name in ('ntsc', 'cache'):
        triplet = tuple(getattr(args, name + '_' + part) for part in ('kernel', 'config', 'report'))
        if any(path is not None for path in triplet):
            if any(path is None for path in triplet):
                parser.error(f'--{name}-kernel, --{name}-config and --{name}-report must be supplied together')
            diagnostics[name] = triplet
    try:
        report = build_image(args.rootfs, args.kernel, args.kernel_config, args.kernel_report,
                             args.output, args.manifest, video_mode=args.video_mode,
                             root_size_mib=args.root_size_mib, **diagnostics)
    except (DiskImageError, OSError, ValueError, subprocess.SubprocessError) as error:
        print(f'hardware USB image: {error}', file=sys.stderr)
        return 1
    print(f"Hardware image: {report['disk']['path']} ({report['disk']['bytes']} bytes); launch mass:/EELINUX.ELF")
    print('Regular image file only; no physical device was flashed; hardware boot remains unverified.')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
