"""Build a modular-tool USB kernel from a preserved native R5900 kernel build.

The source and original kernel stay unchanged. The USB configuration and real
embedded initramfs come from the supplied USB reference; a completed compatible
build directory may supply cached objects in a separate output directory.
"""
from pathlib import Path
import argparse
import json
import re
import shutil
import subprocess
import sys

from common import ROOT, TARGET_FLAGS
from initramfs import newc
import kernel
from kernel_dvd import unpack_cpio
from video import BOOT_MODES


def build(args):
    if sys.platform != 'linux':
        raise RuntimeError('Build the USB kernel on Linux/WSL')
    if args.jobs < 1:
        raise RuntimeError('--jobs must be positive')
    args.work.mkdir(parents=True, exist_ok=True)
    args.output_dir.mkdir(parents=True, exist_ok=True)
    reference = kernel.verify_gs_reference(args.source, args.base_output)
    networking_sha = kernel.networking_config_sha256()
    source_audit = kernel.inspect_source(args.source, '7.2.8')
    config = kernel.with_networking_config((args.base_output / '.config').read_text())
    # Use the compiled reference archive, rather than a possibly stale external
    # CONFIG_INITRAMFS_SOURCE path retained from an earlier build workspace.
    archives = [args.base_output / 'usr' / name for name in
                ('initramfs_inc_data', 'initramfs_data.cpio.gz', 'initramfs_data.cpio')]
    archive = next((p for p in archives if p.is_file()), None)
    if archive is None:
        raise RuntimeError('The USB reference has no compiled initramfs payload')
    kernel.initramfs_archive_bytes(args.base_output)
    init = args.work / 'initramfs'
    if not init.exists():
        init.mkdir()
        unpack_cpio(archive, init)
    if any(init.rglob('*.ko')):
        raise RuntimeError('The USB reference must use the required built-in boot drivers; use kernel.py to refresh an early-module initramfs')
    subprocess.run([args.cross_prefix + 'gcc', *TARGET_FLAGS, '-Os', '-static', '-s',
                    '-Wall', '-Wextra', '-Werror', str(ROOT / 'boot/early-root.c'),
                    '-o', str(init / 'init')], check=True)
    (init / 'init').chmod(0o755)
    kernel.validate_static_init(init / 'init')
    (init / 'etc').mkdir(exist_ok=True)
    (init / 'etc/eelinux-root').write_text('/dev/sda2\n')
    payload = newc(init, console=True)
    if len(payload) > 4 * 1024 * 1024:
        raise RuntimeError('USB early initramfs exceeds 4 MiB')
    cpio = args.work / 'usb-root.cpio'
    cpio.write_bytes(payload)
    out = args.work / 'kernel'
    if not out.exists():
        origin = args.reuse_build_output or args.base_output
        if origin.resolve() == out.resolve():
            raise RuntimeError('Preserve the reference build; use a fresh output')
        subprocess.run(['cp', '-a', '--reflink=auto', str(origin), str(out)], check=True)
    values = {'INITRAMFS_SOURCE': json.dumps(str(cpio)),
              'CMDLINE': json.dumps(kernel.kernel_command_line('/dev/sda2', args.video_mode))}
    for name, value in values.items():
        config = re.sub(r'^(?:CONFIG_' + name + r'=.*|# CONFIG_' + name + r' is not set)\n',
                        '', config, flags=re.M)
        config += 'CONFIG_' + name + '=' + value + '\n'
    (out / '.config').write_text(config)
    make = ['make', '-C', str(args.source), 'O=' + str(out), 'ARCH=mips',
            'CROSS_COMPILE=' + args.cross_prefix]
    subprocess.run(make + ['olddefconfig'], check=True)
    kernel.verify_config(out / '.config')
    kernel.verify_networking_config(out / '.config')
    modules = args.output_dir / 'modules'
    log = args.output_dir / 'kernel-build.log'
    with log.open('w', buffering=1) as stream:
        subprocess.run(make + ['-j' + str(args.jobs), 'W=1', 'KCFLAGS=-Werror',
                              'vmlinux', 'vmlinuz', 'modules'], stdout=stream,
                       stderr=subprocess.STDOUT, check=True)
        kernel.verify_networking_config(out / '.config')
        if (kernel.networking_config_sha256() != networking_sha or
                kernel.native_gs_files(args.source) != reference['source_files']):
            raise RuntimeError('Kernel profile or native GS sources changed while compiling; rebuild before publishing')
        release, files = kernel.install_module_payload(make, out, modules, stream)
    layout = kernel.boot_layout(out / 'vmlinux', out / 'vmlinuz')
    unpacked = kernel.initramfs_archive_bytes(out)
    image = args.output_dir / 'EELINUX.ELF'
    shutil.copy2(out / 'vmlinuz', image)
    shutil.copy2(out / '.config', args.output_dir / 'kernel.config')
    report = {'schema': 2, 'purpose': 'Native R5900 USB kernel with optional tool modules',
              'kernel_version': '7.2.8', 'kernel_release': release, 'root_device': '/dev/sda2',
              'video_mode': args.video_mode, 'image_sha256': kernel.sha256(image),
              'image_bytes': image.stat().st_size, 'config_sha256': kernel.sha256(out / '.config'),
              'layout': layout, 'initramfs_unpacked_bytes': unpacked,
              'early_modules': [],
              'reference_initramfs_sha256': kernel.sha256(archive),
              'source_audit': source_audit, 'graphics_reference': reference,
              'base_kernel_sha256': kernel.sha256(args.base_output / 'vmlinuz'),
              'native_gs_source_files': kernel.native_gs_files(args.source),
              'early_root_source_sha256': kernel.sha256(ROOT / 'boot/early-root.c'),
              'networking_fragment_sha256': networking_sha,
              'modules_directory': str(modules.resolve()), 'modules': files,
              'optional_networking_modules': True, 'networking_services_started_at_boot': False,
              'physical_hardware_tested': False, 'emulator_boot_qualified': False}
    (args.output_dir / 'kernel-report.json').write_text(json.dumps(report, indent=2) + '\n')
    print('Built native USB kernel: ' + str(image), flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('source', 'base-output', 'work', 'output-dir'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--reuse-build-output', type=Path,
                        help='Reuse compiled objects from a compatible build without changing its config')
    parser.add_argument('--cross-prefix', required=True)
    parser.add_argument('--video-mode', choices=BOOT_MODES, default='576p')
    parser.add_argument('--jobs', type=int, default=2)
    build(parser.parse_args())


if __name__ == '__main__':
    main()
