"""Check partitioned image readback and fail-before-format input constraints.

The small ELF/report fixtures are format checks, not runnable kernels or boot
evidence. The native integration test formats a real ext2 fixture and reads
the published FAT filesystem independently with mtools/fsck.
"""
from __future__ import annotations

import hashlib
import contextlib
import io
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
from hardware_usb import build_image, fat32_info, main, tool
from emulator_disk import DiskImageError


def sha(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def elf_fixture():
    ident = b'\x7fELF\x01\x01\x01' + b'\0' * 9
    header = struct.pack('<16sHHIIIIIHHHHHH', ident, 2, 8, 1, 0x80800000,
                         52, 0, 0x20921000, 52, 32, 1, 0, 0, 0)
    segment = struct.pack('<8I', 1, 84, 0x80800000, 0x00800000, 4, 4, 5, 4)
    return header + segment + b'\0' * 4


def native_tools_available():
    if os.name == 'nt':
        return False
    try:
        for name in ('mkfs.fat', 'fsck.fat', 'mcopy', 'mtype', 'mdir', 'mkfs.ext2'):
            tool(name)
    except DiskImageError:
        return False
    return True


class HardwareUSBPackaging(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.folder = Path(self.temp.name)
        self.rootfs = self.folder / 'rootfs.ext2'
        # Sparse superblock fixture for tests that must reject before formatting.
        with self.rootfs.open('wb') as stream:
            stream.truncate(64 * 1024 * 1024)
            block = bytearray(1024)
            struct.pack_into('<I', block, 4, 65536)
            struct.pack_into('<I', block, 20, 1)
            struct.pack_into('<H', block, 56, 0xEF53)
            struct.pack_into('<I', block, 76, 1)
            struct.pack_into('<III', block, 92, 0, 2, 3)
            stream.seek(1024)
            stream.write(block)
        self.kernel = self.folder / 'kernel.elf'
        self.kernel.write_bytes(elf_fixture())
        self.config = self.folder / 'kernel.config'
        self.config.write_text('CONFIG_CMDLINE="console=tty0 root=/dev/sda2 rootfstype=ext2 rw video=ps2fb:576x460i@50"\n')
        self.report = self.folder / 'kernel.json'
        self.write_report()
        self.output = self.folder / 'hardware.img'

    def write_report(self, **changes):
        data = {'image_sha256': sha(self.kernel), 'image_bytes': self.kernel.stat().st_size,
                'config_sha256': sha(self.config), 'root_device': '/dev/sda2',
                'layout': {'decompressor_overlap_check': 'passed', 'ram_bytes': 32 * 1024 * 1024},
                'initramfs_unpacked_bytes': 128, 'production_gate': 'closed',
                'remaining_rejected_port_hunks': 64}
        data.update(changes)
        self.report.write_text(json.dumps(data))

    def tearDown(self):
        self.temp.cleanup()

    def diagnostic(self, mode, cache=False):
        name = 'cache' if cache else 'ntsc'
        elf, config, report = (self.folder / (name + suffix) for suffix in ('.elf', '.config', '.json'))
        elf.write_bytes(elf_fixture() + name.encode())
        config.write_text(self.config.read_text().replace('576x460i@50',
                          '576x384i@60' if mode == 'ntsc' else '576x460i@50'))
        data = {'image_sha256': sha(elf), 'image_bytes': elf.stat().st_size,
                'config_sha256': sha(config), 'root_device': '/dev/sda2',
                'layout': {'decompressor_overlap_check': 'passed', 'ram_bytes': 32 * 1024 * 1024},
                'initramfs_unpacked_bytes': 128, 'production_gate': 'closed',
                'remaining_rejected_port_hunks': 64}
        if cache:
            data['compiled_cache_checks'] = {
                'writeback_before_instruction_invalidate_before_kernel_jump': True,
                'sync_immediately_before_after_cache': True,
                'no_cache_instruction_at_page_tail': True,
                'function_alignment_bytes': 64, 'both_cache_ways': True}
        report.write_text(json.dumps(data))
        return elf, config, report

    def build(self, **changes):
        args = dict(rootfs=self.rootfs, kernel=self.kernel, config=self.config,
                    kernel_report=self.report, output=self.output, video_mode='pal')
        args.update(changes)
        return build_image(**args)

    def test_wrong_root_video_and_build_hash_fail_before_formatter(self):
        original = self.config.read_text()
        scenarios = (
            ('root report', original, {'root_device': '/dev/sda1'}),
            ('root config', original.replace('/dev/sda2', '/dev/sda1'), {}),
            ('NTSC video', original.replace('576x460i@50', '576x384i@60'), {}),
            ('duplicate root', original.replace('root=/dev/sda2', 'root=/dev/sda2 root=/dev/sda1'), {}),
            ('kernel hash', original, {'image_sha256': '0' * 64}),
            ('config hash', original, {'config_sha256': '0' * 64}),
            ('unchecked layout', original, {'layout': {'ram_bytes': 32 * 1024 * 1024}}),
            ('oversized early archive', original, {'initramfs_unpacked_bytes': 4 * 1024 * 1024 + 1}),
        )
        for name, config, report in scenarios:
            with self.subTest(name=name):
                self.config.write_text(config)
                self.write_report(**report)
                with patch('hardware_usb.tool', side_effect=AssertionError('formatter discovery must not run')):
                    with self.assertRaises(DiskImageError):
                        self.build()
                self.assertFalse(self.output.exists())
                self.assertFalse(Path(str(self.output) + '.json').exists())

    def test_existing_images_manifests_and_aliases_are_preserved(self):
        self.output.write_bytes(b'keep previous image')
        with self.assertRaises(DiskImageError):
            self.build()
        self.assertEqual(self.output.read_bytes(), b'keep previous image')
        fresh = self.folder / 'fresh.img'
        manifest = Path(str(fresh) + '.json')
        manifest.write_bytes(b'keep previous manifest')
        with self.assertRaises(DiskImageError):
            self.build(output=fresh)
        self.assertEqual(manifest.read_bytes(), b'keep previous manifest')
        for changes in ({'output': self.rootfs}, {'output': fresh, 'manifest': self.kernel},
                        {'output': fresh, 'manifest': fresh}, {'kernel_report': self.config}):
            with self.subTest(changes=changes), self.assertRaises(DiskImageError):
                self.build(**changes)
        linked = self.folder / 'linked.img'
        try:
            os.link(self.kernel, linked)
        except (OSError, NotImplementedError):
            self.skipTest('hard links unavailable')
        with self.assertRaises(DiskImageError):
            self.build(output=linked)
        self.assertEqual(self.kernel.read_bytes(), elf_fixture())

    def test_diagnostic_root_modes_hashes_and_cache_proof_fail_before_format(self):
        scenarios = ('NTSC uses PAL', 'NTSC wrong root', 'NTSC ELF hash',
                     'cache uses NTSC', 'cache proof absent', 'cache missing sync',
                     'diagnostic input alias', 'incomplete triplet')
        for name in scenarios:
            with self.subTest(name=name):
                ntsc = self.diagnostic('ntsc')
                cache = self.diagnostic('pal', cache=True)
                kwargs = {'ntsc': ntsc, 'cache': cache}
                if name == 'NTSC uses PAL':
                    ntsc[1].write_text(self.config.read_text())
                elif name == 'NTSC wrong root':
                    ntsc[1].write_text(ntsc[1].read_text().replace('/dev/sda2', '/dev/sda1'))
                elif name == 'cache uses NTSC':
                    cache[1].write_text(cache[1].read_text().replace('576x460i@50', '576x384i@60'))
                elif name == 'diagnostic input alias':
                    kwargs['ntsc'] = (self.kernel, *ntsc[1:])
                elif name == 'incomplete triplet':
                    kwargs['ntsc'] = ntsc[:2]
                for triplet in (ntsc, cache):
                    data = json.loads(triplet[2].read_text())
                    data['config_sha256'] = sha(triplet[1])
                    if name == 'NTSC ELF hash' and triplet == ntsc:
                        data['image_sha256'] = '0' * 64
                    if name == 'cache proof absent' and triplet == cache:
                        del data['compiled_cache_checks']
                    if name == 'cache missing sync' and triplet == cache:
                        data['compiled_cache_checks']['sync_immediately_before_after_cache'] = False
                    triplet[2].write_text(json.dumps(data))
                with patch('hardware_usb.tool', side_effect=AssertionError('formatter must not run')):
                    with self.assertRaises(DiskImageError):
                        self.build(**kwargs)
                self.assertFalse(self.output.exists())

    def test_cli_incomplete_diagnostic_triplet_rejects_before_opening_inputs(self):
        args = ['--rootfs', str(self.rootfs), '--kernel', str(self.kernel),
                '--kernel-config', str(self.config), '--kernel-report', str(self.report),
                '--ntsc-kernel', str(self.kernel), '--output', str(self.output)]
        with patch('hardware_usb.build_image', side_effect=AssertionError('must not open inputs')):
            with contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit) as error:
                main(args)
        self.assertEqual(error.exception.code, 2)

    def test_devices_and_symlink_paths_fail_before_formatter(self):
        paths = ({'rootfs': Path(os.devnull)}, {'output': Path(os.devnull)},
                 {'output': Path(r'\\.\PhysicalDrive0')}, {'config': self.folder})
        for changes in paths:
            with self.subTest(changes=changes), self.assertRaises((DiskImageError, OSError)):
                self.build(**changes)
        linked = self.folder / 'kernel-link'
        try:
            linked.symlink_to(self.kernel)
        except (OSError, NotImplementedError):
            self.skipTest('symlinks unavailable')
        ancestor = self.folder / 'directory-link'
        ancestor.symlink_to(self.folder, target_is_directory=True)
        with patch('hardware_usb.tool', side_effect=AssertionError('formatter discovery must not run')):
            for changes in ({'kernel': linked}, {'output': linked}, {'output': ancestor / 'image.img'}):
                with self.subTest(changes=changes), self.assertRaises(DiskImageError):
                    self.build(**changes)

    def test_fat32_label_without_fat32_cluster_count_is_rejected(self):
        image = self.folder / 'fake.fat'
        boot = bytearray(512)
        struct.pack_into('<H', boot, 11, 512)
        boot[13] = 1
        struct.pack_into('<H', boot, 14, 32)
        boot[16] = 2
        struct.pack_into('<III', boot, 28, 2048, 2048, 8)
        struct.pack_into('<I', boot, 44, 2)
        boot[82:90] = b'FAT32   '
        boot[510:512] = b'\x55\xaa'
        with image.open('wb') as stream:
            stream.write(boot)
            stream.truncate(1024 * 1024)
        with self.assertRaisesRegex(DiskImageError, 'cluster count'):
            fat32_info(image)

    def test_explicit_root_size_is_bounded_and_must_match_before_formatting(self):
        with patch('hardware_usb.tool', side_effect=AssertionError('formatter must not run')):
            for size in (True, 0, 63, 4097, 128):
                with self.subTest(size=size), self.assertRaises(DiskImageError):
                    self.build(root_size_mib=size)
        self.assertFalse(self.output.exists())

    @unittest.skipUnless(native_tools_available(), 'requires Linux dosfstools, mtools and e2fsprogs')
    def test_sector_aligned_trailing_padding_is_verified_and_copied(self):
        from emulator_hardware_disk import copy_hardware_disk
        subprocess.run([tool('mkfs.ext2'),'-q','-F','-b','1024',str(self.rootfs)],check=True,capture_output=True)
        expected=(65+64)*1024*1024+512
        report=self.build(disk_size_bytes=expected)
        self.assertEqual(self.output.stat().st_size,expected)
        self.assertEqual(report['disk']['trailing_padding_bytes'],512)
        proof=copy_hardware_disk(self.output,self.rootfs,self.kernel,self.folder/'copy.img')
        self.assertEqual(proof['source']['sha256'],sha(self.rootfs))
        with self.output.open('r+b') as file:file.seek(-1,2);file.write(b'x')
        with self.assertRaisesRegex(DiskImageError,'padding'):
            copy_hardware_disk(self.output,self.rootfs,self.kernel,self.folder/'bad.img')

    @unittest.skipUnless(native_tools_available(), 'requires Linux dosfstools, mtools and e2fsprogs')
    def test_128_mib_desktop_root_preserves_boot_geometry_and_exact_copy(self):
        from emulator_hardware_disk import copy_hardware_disk
        with self.rootfs.open('r+b') as stream:
            stream.truncate(128 * 1024 * 1024)
        subprocess.run([tool('mkfs.ext2'), '-q', '-F', '-b', '1024', str(self.rootfs)],
                       check=True, capture_output=True, timeout=30)
        report = self.build(root_size_mib=128)
        with self.output.open('rb') as stream:
            mbr = stream.read(512)
        entries = [struct.unpack_from('<B3sB3sII', mbr, 446 + 16 * i) for i in (0, 1)]
        self.assertEqual([(e[2], e[4], e[5]) for e in entries],
                         [(0x0c, 2048, 131072), (0x83, 133120, 262144)])
        self.assertEqual(self.output.stat().st_size, 202375168)
        copied = self.folder / 'desktop-emulator.img'
        proof = copy_hardware_disk(self.output, self.rootfs, self.kernel, copied)
        self.assertEqual(proof['partition']['sectors'], 262144)
        self.assertEqual(sha(copied), report['disk']['sha256'])
        self.assertEqual(proof['source']['sha256'], sha(self.rootfs))

    @unittest.skipUnless(native_tools_available(), 'requires Linux dosfstools, mtools and e2fsprogs')
    def test_ext4_image_matches_kernel_and_survives_hardware_copy(self):
        from emulator_hardware_disk import copy_hardware_disk
        subprocess.run([tool('mke2fs'), '-q', '-F', '-t', 'ext4', '-b', '4096',
                        '-I', '256', '-J', 'size=4', '-O',
                        'none,has_journal,extent,filetype,sparse_super,large_file,dir_index,extra_isize,metadata_csum',
                        str(self.rootfs)], check=True, capture_output=True, timeout=30)
        with patch('hardware_usb.tool', side_effect=AssertionError('must reject before formatting')):
            with self.assertRaisesRegex(DiskImageError, 'filesystem setting'):
                self.build()
        self.config.write_text(self.config.read_text().replace('rootfstype=ext2', 'rootfstype=ext4'))
        evidence = json.loads(self.report.read_text())
        evidence['config_sha256'] = sha(self.config)
        self.report.write_text(json.dumps(evidence))
        report = self.build()
        self.assertEqual(report['partitions'][1]['filesystem']['type'], 'ext4')
        copied = self.folder / 'ext4-emulator.img'
        proof = copy_hardware_disk(self.output, self.rootfs, self.kernel, copied)
        self.assertEqual(proof['filesystem']['type'], 'ext4')
        self.assertEqual(sha(copied), report['disk']['sha256'])

    @unittest.skipUnless(native_tools_available(), 'requires Linux dosfstools, mtools and e2fsprogs')
    def test_independent_mbr_and_published_fat_root_readback(self):
        subprocess.run([tool('mkfs.ext2'), '-F', '-b', '1024', str(self.rootfs)],
                       check=True, capture_output=True, timeout=30)
        ntsc = self.diagnostic('ntsc')
        cache = self.diagnostic('pal', cache=True)
        inputs = [self.rootfs, self.kernel, self.config, self.report, *ntsc, *cache]
        before = {p: (sha(p), p.stat().st_size, p.stat().st_mtime_ns) for p in inputs}
        report = self.build(ntsc=ntsc, cache=cache)
        with self.output.open('rb') as disk:
            mbr = disk.read(512)
            self.assertEqual(mbr[510:512], b'\x55\xaa')
            entries = [struct.unpack_from('<B3sB3sII', mbr, 446 + 16 * i) for i in (0, 1)]
            self.assertEqual([(e[0], e[2], e[4], e[5]) for e in entries],
                             [(0, 0x0C, 2048, 131072), (0, 0x83, 133120, 131072)])
            self.assertEqual(mbr[478:510], bytes(32))
            self.assertEqual(self.output.stat().st_size, (133120 + 131072) * 512)
            fat = self.folder / 'published.fat'
            disk.seek(entries[0][4] * 512)
            with fat.open('wb') as target:
                remaining = entries[0][5] * 512
                while remaining:
                    chunk = disk.read(min(1024 * 1024, remaining))
                    self.assertTrue(chunk)
                    target.write(chunk)
                    remaining -= len(chunk)
            disk.seek(entries[1][4] * 512)
            root_hash = hashlib.sha256()
            remaining = entries[1][5] * 512
            while remaining:
                chunk = disk.read(min(1024 * 1024, remaining))
                self.assertTrue(chunk)
                root_hash.update(chunk)
                remaining -= len(chunk)
            self.assertEqual(root_hash.hexdigest(), before[self.rootfs][0])
        # Read the actual published filesystem, not the packager's temporary file.
        subprocess.run([tool('fsck.fat'), '-n', str(fat)], check=True, capture_output=True, timeout=30)
        extracted = self.folder / 'readback.elf'
        subprocess.run([tool('mcopy'), '-i', str(fat), '::/EELINUX.ELF', str(extracted)],
                       check=True, capture_output=True, timeout=30)
        self.assertEqual(extracted.read_bytes(), self.kernel.read_bytes())
        for name, triplet in (('EENTSC.ELF', ntsc), ('EECACHE.ELF', cache)):
            copy = self.folder / ('readback-' + name)
            subprocess.run([tool('mcopy'), '-i', str(fat), '::/' + name, str(copy)],
                           check=True, capture_output=True, timeout=30)
            self.assertEqual(copy.read_bytes(), triplet[0].read_bytes())
        readme = subprocess.run([tool('mtype'), '-i', str(fat), '::/README.TXT'],
                                check=True, capture_output=True, timeout=30).stdout
        self.assertIn(b'/dev/sda2', readme)
        self.assertIn(b'PAL 50 Hz', readme)
        self.assertIn(b'EENTSC.ELF', readme)
        self.assertIn(b'EECACHE.ELF', readme)
        self.assertEqual([entry['fat_path'] for entry in report['diagnostics']], ['EENTSC.ELF', 'EECACHE.ELF'])
        self.assertEqual(report['checks']['diagnostic_fat_readback_byte_exact'],
                         {'EENTSC.ELF': True, 'EECACHE.ELF': True})
        self.assertFalse(report['kernel']['cache_handoff_probe'])
        self.assertTrue(report['diagnostics'][1]['cache_handoff_probe'])
        # The updated hardware image selects the PAL cache probe as EELINUX,
        # with only NTSC as an extra; verify that published layout independently.
        updated = self.folder / 'updated.img'
        update_report = self.build(kernel=cache[0], config=cache[1], kernel_report=cache[2],
                                   output=updated, ntsc=ntsc)
        update_copy = self.folder / 'updated-main.elf'
        subprocess.run([tool('mcopy'), '-i', str(updated) + '@@1048576',
                        '::/EELINUX.ELF', str(update_copy)],
                       check=True, capture_output=True, timeout=30)
        self.assertEqual(update_copy.read_bytes(), cache[0].read_bytes())
        self.assertTrue(update_report['kernel']['cache_handoff_probe'])
        self.assertEqual([entry['fat_path'] for entry in update_report['diagnostics']], ['EENTSC.ELF'])
        self.assertEqual(update_report['disk']['sha256'], sha(updated))
        self.assertEqual(report['disk']['sha256'], sha(self.output))
        self.assertEqual(json.loads(Path(str(self.output) + '.json').read_text()), report)
        for path in inputs:
            self.assertEqual((sha(path), path.stat().st_size, path.stat().st_mtime_ns), before[path])
        self.assertFalse(list(self.folder.glob('*.tmp')))


if __name__ == '__main__':
    unittest.main()
