"""On-demand networking selections must survive Kconfig without changing boot drivers."""
from pathlib import Path
import hashlib
import json
import os
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
import kernel


class ModularNetworking(unittest.TestCase):
    def test_single_source_preserves_every_pre_consolidation_option(self):
        # Semantic snapshots of the two source files before consolidation.
        # Update deliberately if kernel choices are changed in a future task.
        expected = {
            'baseline': (218, '4546a99f01d18bdac9d19232f0e15a0e340b5ed8af21d0e98d3d37931fa8fd97'),
            'networking': (262, '1c549ddabd266ef2d47a1e24c08b8b7fd89da62a9b2d913f9040c0d629b8df93'),
            'combined': (480, 'cf7a2e00c713a21e4a6855930d3a139028088333c778370750280fba827b6b3d'),
        }
        values = {
            'baseline': kernel.config_values_text(kernel.base_kernel_config()),
            'networking': kernel.networking_config_values(),
            'combined': kernel.config_values(kernel.KERNEL_CONFIG),
        }
        for section, (count, digest) in expected.items():
            with self.subTest(section=section):
                self.assertEqual(len(values[section]), count)
                payload = json.dumps(values[section], sort_keys=True, separators=(',', ':')).encode()
                self.assertEqual(hashlib.sha256(payload).hexdigest(), digest)
        self.assertFalse((ROOT / 'configs/kernel-ps2.config').exists())
        self.assertFalse((ROOT / 'configs/kernel-networking.config').exists())
        self.assertEqual(kernel.KERNEL_CONFIG, ROOT / 'configs/kernel.config')
        # The existing USB/DVD provenance field still hashes the same section.
        self.assertEqual(kernel.networking_config_sha256(),
                         'ac2497b4c361fa95932896d58b0c6a4428c75ef662d23b639609a06e8adf7838')

    def test_source_rejects_duplicate_options_and_broken_section_boundaries(self):
        source = kernel.KERNEL_CONFIG.read_text()
        cases = (
            source + '\nCONFIG_EXT4_FS=y\n',
            source + '\n# CONFIG_NETFILTER is not set\n',
            source.replace(kernel.NETWORKING_BEGIN, '# missing section'),
            source.replace(kernel.NETWORKING_BEGIN, '# TEMP NETWORKING BOUNDARY')
                  .replace(kernel.NETWORKING_END, kernel.NETWORKING_BEGIN)
                  .replace('# TEMP NETWORKING BOUNDARY', kernel.NETWORKING_END),
        )
        with tempfile.TemporaryDirectory() as folder:
            config = Path(folder) / 'kernel.config'
            for content in cases:
                with self.subTest(content=content[-80:]):
                    config.write_text(content)
                    with patch.object(kernel, 'KERNEL_CONFIG', config), self.assertRaises(RuntimeError):
                        kernel.networking_config_values()

    def test_qualified_usb_and_dvd_configs_keep_unrelated_profile_choices(self):
        requested = kernel.networking_config_values()
        for medium, root_device in (('USB', '/dev/sda2'), ('DVD', '/dev/ps2dvd')):
            original = {
                'LOCALVERSION': '"-qualified-' + medium.lower() + '"',
                'CMDLINE': '"root=' + root_device + ' video=ps2fb:720x576p@50"',
                'INITRAMFS_SOURCE': '"/qualified/' + medium.lower() + '.cpio"',
                'HZ_100': 'n', 'HZ_250': 'y', 'USB_STORAGE': 'y', 'FB_PS2': 'y',
                'PS2_CDVD': 'y' if medium == 'DVD' else 'n',
                'ISO9660_FS': 'y' if medium == 'DVD' else 'n',
                'OVERLAY_FS': 'y' if medium == 'DVD' else 'n',
                'ZRAM': 'y' if medium == 'DVD' else 'n',
                'TUN': 'y', 'NETFILTER': 'n',
            }
            config = ''.join('CONFIG_' + key + '=' + value + '\n'
                             for key, value in original.items())
            with self.subTest(medium=medium):
                merged = kernel.with_networking_config(config)
                self.assertEqual(kernel.config_values_text(merged), {**original, **requested})
                directives = [line for line in merged.splitlines()
                              if line.startswith(('CONFIG_', '# CONFIG_'))]
                self.assertEqual(len(directives), len(kernel.config_values_text(merged)))

    def test_merge_replaces_old_disables_and_keeps_boot_paths(self):
        source = ('CONFIG_PS2_SMAP=y\nCONFIG_USB_STORAGE=y\nCONFIG_CMDLINE="unchanged"\n'
                  '# CONFIG_IPV6 is not set\nCONFIG_NETFILTER=n\nCONFIG_TUN=y\n')
        merged = kernel.with_networking_config(source)
        self.assertIn('CONFIG_PS2_SMAP=y\n', merged)
        self.assertIn('CONFIG_USB_STORAGE=y\n', merged)
        self.assertIn('CONFIG_CMDLINE="unchanged"\n', merged)
        self.assertNotIn('# CONFIG_IPV6 is not set', merged)
        self.assertNotIn('CONFIG_NETFILTER=n', merged)
        self.assertNotIn('CONFIG_TUN=y', merged)
        self.assertEqual(merged.count('CONFIG_IPV6='), 1)
        self.assertIn('CONFIG_IPV6=y\n', merged)
        self.assertIn('CONFIG_TUN=m\n', merged)

    def test_discarded_module_and_accidental_builtin_are_rejected(self):
        expected = kernel.networking_config_text()
        with tempfile.TemporaryDirectory() as folder:
            config = Path(folder) / '.config'
            config.write_text(expected)
            kernel.verify_networking_config(config)
            for replacement in ('CONFIG_TUN=y', '# CONFIG_TUN is not set'):
                with self.subTest(replacement=replacement):
                    config.write_text(expected.replace('CONFIG_TUN=m', replacement))
                    with self.assertRaisesRegex(RuntimeError, 'CONFIG_TUN: required m'):
                        kernel.verify_networking_config(config)

    def test_optional_families_do_not_enter_early_root_module_list(self):
        requested = kernel.networking_config_values()
        for name in ('TUN', 'WIREGUARD', 'OVPN', 'NF_TABLES', 'NF_CONNTRACK',
                     'BRIDGE', 'VLAN_8021Q', 'CIFS', 'NFS_FS', 'FUSE_FS',
                     'CFG80211', 'MAC80211', 'RT2800USB', 'RTL8XXXU'):
            with self.subTest(name=name):
                self.assertEqual(requested[name], 'm')
        self.assertEqual(requested['IPV6'], 'y')
        self.assertEqual(requested['SECCOMP'], 'y')
        self.assertEqual(requested['SECCOMP_FILTER'], 'y')
        for name in ('FS_POSIX_ACL', 'EXT4_FS_POSIX_ACL', 'EXT4_FS_SECURITY',
                     'TMPFS_XATTR', 'TMPFS_POSIX_ACL'):
            with self.subTest(name=name):
                self.assertEqual(requested[name], 'y')
        self.assertTrue(set(kernel.EARLY).isdisjoint(
            {'ipv6', 'wireguard', 'nf_tables', 'nf_conntrack', 'cfg80211', 'mac80211', 'cifs', 'nfs'}))
        self.assertEqual(requested['NFSD'], 'n')
        self.assertEqual(requested['SMB_SERVER'], 'n')

    @unittest.skipIf(os.name == 'nt', 'build-source symlinks require POSIX')
    def test_graphics_reference_catches_a_lost_protocol_header(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            source, reference, output = root / 'source', root / 'reference', root / 'output'
            for tree in (source, reference):
                (tree / 'drivers/video/fbdev').mkdir(parents=True)
                (tree / 'drivers/video/fbdev/ps2fb-gs-device.h').write_text('native device\n')
                (tree / 'include/linux').mkdir(parents=True)
                (tree / 'include/linux/ps2-gs-map-blit.h').write_text('channel map protocol\n')
            output.mkdir()
            (output / 'source').symlink_to(reference)
            (output / 'vmlinuz').write_bytes(b'preserved reference identity')
            self.assertTrue(kernel.verify_gs_reference(source, output)['native_GS_source_comparison_passed'])
            (source / 'include/linux/ps2-gs-map-blit.h').unlink()
            with self.assertRaisesRegex(RuntimeError, 'Native GS sources differ'):
                kernel.verify_gs_reference(source, output)

    @unittest.skipIf(os.name == 'nt', 'module-directory symlinks require POSIX')
    def test_module_install_rejects_a_release_symlink_before_removing_files(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            output, modules, outside = root / 'output', root / 'modules', root / 'outside'
            (output / 'include/config').mkdir(parents=True)
            (output / 'include/config/kernel.release').write_text('7.2.8-test\n')
            (modules / 'lib/modules').mkdir(parents=True)
            outside.mkdir()
            (outside / 'keep').write_text('unrelated data\n')
            (modules / 'lib/modules/7.2.8-test').symlink_to(outside)
            with self.assertRaisesRegex(RuntimeError, 'must not be a symlink'):
                kernel.install_module_payload([], output, modules, None)
            self.assertEqual((outside / 'keep').read_text(), 'unrelated data\n')

    @unittest.skipIf(os.name == 'nt', 'sticky directory permissions require POSIX')
    def test_media_initramfs_unpack_preserves_directory_and_file_modes(self):
        from initramfs import newc
        from kernel_dvd import unpack_cpio
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            original, restored = root / 'original', root / 'restored'
            (original / 'tmp').mkdir(parents=True)
            (original / 'tmp').chmod(0o1777)
            (original / 'init').write_text('static init fixture\n')
            (original / 'init').chmod(0o750)
            archive = root / 'initramfs.cpio'
            archive.write_bytes(newc(original))
            restored.mkdir()
            unpack_cpio(archive, restored)
            self.assertEqual((restored / 'tmp').stat().st_mode & 0o7777, 0o1777)
            self.assertEqual((restored / 'init').stat().st_mode & 0o7777, 0o750)
            self.assertEqual((restored / 'init').read_text(), 'static init fixture\n')


if __name__ == '__main__':
    unittest.main()
