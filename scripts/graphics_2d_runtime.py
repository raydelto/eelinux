"""Prepare a separate native Xorg pixel diagnostic image, never shipping media."""
from pathlib import Path
import argparse, hashlib, json, re, shutil, subprocess, sys
import emulator

ROOT = Path(__file__).resolve().parents[1]
CLIENT = '''#!/bin/bash
export HOME=/root
exec /usr/bin/stdbuf -oL /usr/bin/ps2-xorg-gpu2d-test
'''
PROBE = '''#!/bin/bash
export HOME=/root
mkdir -p /var/lib/ps2-xorg-test
printf 'ps2-2d: native pixel diagnostic starting\\n' > /dev/kmsg
: > /var/lib/ps2-xorg-test/client.log
/usr/bin/tail -n +1 -f /var/lib/ps2-xorg-test/client.log > /dev/kmsg &
follower=$!
/usr/bin/xinit /usr/bin/ps2-2d-client -- /etc/X11/xinit/xserverrc :0 -logfile /var/lib/ps2-xorg-test/Xorg.log -verbose 3 > /var/lib/ps2-xorg-test/client.log 2>&1
result=$?
kill "$follower"
wait "$follower" 2>/dev/null
cat /var/lib/ps2-xorg-test/client.log > /dev/kmsg
grep 'Server terminated' /var/lib/ps2-xorg-test/Xorg.log > /dev/kmsg
grep -q 'completed pixel_checks=333 errors=0' /var/lib/ps2-xorg-test/client.log || result=1
grep -q 'Server terminated successfully (0)' /var/lib/ps2-xorg-test/Xorg.log || result=1
cat /proc/meminfo > /var/lib/ps2-xorg-test/meminfo.txt
sync
printf 'ps2-2d: native pixel diagnostic exit=%s\\n' "$result" > /dev/kmsg
'''

def sha(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()

def main():
    p = argparse.ArgumentParser(description=__doc__)
    for name in ('work', 'base-staging', 'native-staging', 'client', 'kernel'):
        p.add_argument('--' + name, type=Path, required=True)
    p.add_argument('--guest-root', choices=('sda1', 'sda2'), default='sda2',
                   help='match the supplied kernel command line; use the validated compressed boot ELF')
    p.add_argument('--pre-client',type=Path,help='run an additional native GS pixel diagnostic before Xorg')
    p.add_argument('--pre-only',action='store_true',help='run only the supplied native GS diagnostic')
    p.add_argument('--pre-case',choices=('exact-multiply','divide','wide-product','float-multiply','float-divide','float-add','float-subtract','saturate-rgba8','wide-bilinear','order-blends','piecewise','cursor-argb'),default='exact-multiply',
                   help='identity of a focused pre-only arithmetic diagnostic')
    p.add_argument('--client-case',choices=('alpha-maps','alpha-windows','alpha-aliases','saturate','packed-saturate','bilinear-saturate','coverage-saturate','solid-saturate','map-saturate','shifted-maps','convolution-saturate','format-aliases','native15-aliases','screen','exclusion','multiply','order-blends','piecewise','pd-families','pd-aliases','disjoint-over','pd-ratios','dodge-burn','softlight','hsl','gradients','packed10','packed10-ops','packed10-convolution','packed10-alpha-read','packed10-alpha-write','packed10-mixed','depth30','formats-filters','front-formats','resources','cursor-argb','render-geometry','window-sampling','render-glyphs'),help='run a focused native Xorg suite')
    p.add_argument('--shard-count',type=int,default=1,help='partition focused PD requests across independent native sessions')
    p.add_argument('--shard-index',type=int,default=0)
    args = p.parse_args()
    if not 1<=args.shard_count<=16 or not 0<=args.shard_index<args.shard_count:p.error('invalid shard range')
    if (args.shard_count!=1 or args.shard_index) and args.client_case not in ('disjoint-over','pd-ratios','dodge-burn','softlight','hsl','gradients','packed10','packed10-ops','packed10-convolution','packed10-alpha-read','packed10-alpha-write','packed10-mixed','depth30','formats-filters','front-formats'):p.error('shards require --client-case disjoint-over, pd-ratios or dodge-burn')
    if args.pre_only and not args.pre_client:p.error('--pre-only requires --pre-client')
    if sys.platform != 'linux':
        p.error('prepare using Linux/WSL')
    args.work.mkdir(parents=True, exist_ok=True)
    tree = args.work / 'tree'
    shutil.copytree(args.base_staging, tree, symlinks=True)
    for relative in ('etc/X11/xinit/xserverrc', 'etc/X11/xorg.conf'):
        target = tree / relative; target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(ROOT / 'rootfs' / relative, target)
        target.chmod(0o755 if relative.endswith('xserverrc') else 0o644)
    for relative in ('usr/lib/xorg/modules/drivers/ps2_drv.so', 'usr/lib/eelinux/ps2-xorg-prepare'):
        target = tree / relative; target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(args.native_staging / relative, target)
    shutil.copy2(args.client, tree / 'usr/bin/ps2-xorg-gpu2d-test')
    macro={'alpha-maps':'PS2_GPU2D_ALPHA_MAP_CHECKS','alpha-windows':'PS2_GPU2D_ALPHA_WINDOW_CHECKS',
        'alpha-aliases':'PS2_GPU2D_ALPHA_ALIAS_CHECKS','saturate':'PS2_GPU2D_SATURATE_CHECKS',
        'packed-saturate':'PS2_GPU2D_PACKED_SATURATE_CHECKS','bilinear-saturate':'PS2_GPU2D_BILINEAR_SATURATE_CHECKS',
        'coverage-saturate':'PS2_GPU2D_COVERAGE_SATURATE_CHECKS','solid-saturate':'PS2_GPU2D_SOLID_SATURATE_CHECKS',
        'map-saturate':'PS2_GPU2D_MAP_SATURATE_CHECKS','shifted-maps':'PS2_GPU2D_SHIFTED_MAP_CHECKS',
        'convolution-saturate':'PS2_GPU2D_CONVOLUTION_SATURATE_CHECKS',
           'format-aliases':'PS2_GPU2D_FORMAT_ALIAS_CHECKS',
           'native15-aliases':'PS2_GPU2D_NATIVE15_ALIAS_CHECKS','screen':'PS2_GPU2D_SCREEN_CHECKS',
           'order-blends':'PS2_GPU2D_ORDER_BLENDS_CHECKS',
           'piecewise':'PS2_GPU2D_PIECEWISE_CHECKS',
           'pd-families':'PS2_GPU2D_PD_FAMILIES_CHECKS','pd-aliases':'PS2_GPU2D_PD_ALIASES_CHECKS','disjoint-over':'PS2_GPU2D_DISJOINT_OVER_CHECKS','pd-ratios':'PS2_GPU2D_PD_RATIOS_CHECKS','dodge-burn':'PS2_GPU2D_DODGE_BURN_CHECKS','softlight':'PS2_GPU2D_SOFTLIGHT_CHECKS','hsl':'PS2_GPU2D_HSL_CHECKS','gradients':'PS2_GPU2D_GRADIENT_CHECKS','packed10':'PS2_GPU2D_PACKED10_CHECKS','packed10-ops':'PS2_GPU2D_PACKED10_OPS_CHECKS','packed10-convolution':'PS2_GPU2D_PACKED10_CONVOLUTION_CHECKS','packed10-alpha-read':'PS2_GPU2D_PACKED10_ALPHA_READ_CHECKS','packed10-alpha-write':'PS2_GPU2D_PACKED10_ALPHA_WRITE_CHECKS','packed10-mixed':'PS2_GPU2D_PACKED10_MIXED_CHECKS','depth30':'PS2_GPU2D_DEPTH30_CHECKS','formats-filters':'PS2_GPU2D_FORMAT_FILTER_CHECKS','front-formats':'PS2_GPU2D_FRONT_FORMAT_CHECKS','resources':'PS2_GPU2D_RESOURCE_CHECKS','cursor-argb':'PS2_GPU2D_CURSOR_ARGB_CHECKS','render-geometry':'PS2_GPU2D_RENDER_GEOMETRY_CHECKS','window-sampling':'PS2_GPU2D_WINDOW_SAMPLE_CHECKS','render-glyphs':'PS2_GPU2D_RENDER_GLYPH_CHECKS',
           'multiply':'PS2_GPU2D_MULTIPLY_CHECKS',
           'exclusion':'PS2_GPU2D_EXCLUSION_CHECKS'}.get(
        args.client_case,'PS2_GPU2D_PIXEL_CHECKS')
    expected=int(re.search(r'#define '+macro+r' (\d+)',(ROOT/'desktop/ps2-xorg-gpu2d-test.c').read_text())[1])
    client_script=CLIENT
    if args.client_case:client_script=CLIENT.replace('export HOME=/root',
        'export HOME=/root\nexport PS2_GS_CASE='+args.client_case)
    if args.shard_count>1:
        expected=2*((expected//2+args.shard_count-1-args.shard_index)//args.shard_count) if args.client_case in ('packed10-alpha-write','packed10-mixed','depth30') else (expected+args.shard_count-1-args.shard_index)//args.shard_count
        client_script=client_script.replace('export HOME=/root','export HOME=/root\nexport PS2_GS_SHARD_COUNT='+str(args.shard_count)+'\nexport PS2_GS_SHARD_INDEX='+str(args.shard_index))
    probe=PROBE.replace('pixel_checks=333','pixel_checks='+str(expected))
    if args.pre_client:
        shutil.copy2(args.pre_client,tree/'usr/bin/ps2-gs-lut-test')
        probe=probe.replace("printf 'ps2-2d: native pixel diagnostic starting", 
            ': > /var/lib/ps2-xorg-test/lut.log\n'
            '/usr/bin/tail -n +1 -f /var/lib/ps2-xorg-test/lut.log > /dev/kmsg &\n'
            'pre_follower=$!\n'
            '/usr/bin/stdbuf -oL /usr/bin/ps2-gs-lut-test > /var/lib/ps2-xorg-test/lut.log 2>&1\n'
            'pre_result=$?\nkill "$pre_follower"\nwait "$pre_follower" 2>/dev/null\n'
            'cat /var/lib/ps2-xorg-test/lut.log > /dev/kmsg\n'
            'if [ "$pre_result" != 0 ]; then sync; exit "$pre_result"; fi\n'
            "printf 'ps2-2d: native pixel diagnostic starting")
    if args.pre_only:
        probe=probe.replace("printf 'ps2-2d: native pixel diagnostic starting",
            'cat /proc/meminfo > /var/lib/ps2-xorg-test/meminfo.txt\n'
            'sync\n'
            "printf 'ps2-2d: primitive diagnostic exit=0\\n' > /dev/kmsg\n"
            'exit 0\n'
            "printf 'ps2-2d: native pixel diagnostic starting")
    for relative, content in (('usr/bin/ps2-2d-client', client_script), ('usr/bin/ps2-2d-probe', probe)):
        target = tree / relative; target.write_text(content); target.chmod(0o755)
    # The base is a diagnostic stage, with no automatic X session. Keep its
    # system initialization and run just this client once in runlevel 3.
    init = tree / 'etc/inittab'
    entries=[line for line in init.read_text().splitlines() if not line.startswith('x2:3:wait:/usr/bin/ps2-2d-probe')]
    init.write_text('\n'.join(entries) + '\nx2:3:wait:/usr/bin/ps2-2d-probe\n')
    (args.work / 'config.json').write_text(json.dumps({'epoch': 1791849600,
        'size_mib': 256, 'filesystem': 'ext4'}))
    subprocess.run(['fakeroot', '--', sys.executable, str(ROOT / 'scripts/rootfs.py'),
                    '--_worker', str(args.work)], check=True)
    kernel = args.work / 'kernel-diagnostic.elf'
    shutil.copy2(args.kernel, kernel)
    emulator.validate_kernel(kernel)
    subprocess.run([sys.executable, str(ROOT / 'scripts/emulator.py'), 'prepare',
                    '--directory', str(args.work / 'session'), '--host', 'linux',
                    '--kernel', str(kernel), '--rootfs', str(args.work / 'rootfs.ext4')], check=True)
    if args.guest_root == 'sda2':
        # Move the regular image's root partition entry into MBR slot 2.
        # The empty first slot needs no filesystem; root data stays at 1 MiB.
        session = args.work / 'session'; disk = session / 'rootfs-usb.img'
        with disk.open('r+b') as stream:
            mbr = bytearray(stream.read(512)); mbr[462:478] = mbr[446:462]; mbr[446:462] = bytes(16)
            stream.seek(0); stream.write(mbr)
        disk_hash = sha(disk)
        manifest_path = session / 'rootfs-usb.img.json'
        manifest = json.loads(manifest_path.read_text()); manifest['disk']['sha256'] = disk_hash
        manifest['required_guest_root'] = '/dev/sda2'; manifest['root_partition_slot'] = 2
        manifest_path.write_text(json.dumps(manifest, indent=2) + '\n')
        session_path = session / 'session.json'; record = json.loads(session_path.read_text())
        record['prepared_disk_sha256'] = disk_hash; record['required_guest_root'] = '/dev/sda2'
        session_path.write_text(json.dumps(record, indent=2) + '\n')
    report = {'result': 'prepared; runtime not yet executed', 'shipping_media_modified': False,
        'original_kernel_sha256': sha(args.kernel), 'diagnostic_kernel_sha256': sha(kernel),
        'kernel_change': 'supplied kernel; identity verified', 'guest_root': '/dev/' + args.guest_root,
        'pre_client_sha256':sha(args.pre_client) if args.pre_client else None,
        'module_sha256': sha(tree / 'usr/lib/xorg/modules/drivers/ps2_drv.so'),
        'client_sha256': sha(args.client), 'rootfs_sha256': sha(args.work / 'rootfs.ext4')}
    report['expected_pixel_checks']=expected
    report['pre_only']=args.pre_only
    report['pre_case']=args.pre_case if args.pre_only or args.pre_case in ('order-blends','piecewise','cursor-argb') else None
    report['expected_pre_checks']={'exact-multiply':196608,'divide':24576,'wide-product':147456,'float-multiply':98304,'float-divide':98304,'float-add':98304,'float-subtract':98304,'saturate-rgba8':32768,'wide-bilinear':16128,'order-blends':626688,'piecewise':417792,'cursor-argb':45056}[args.pre_case] if args.pre_only or args.pre_case in ('order-blends','piecewise','cursor-argb') else None
    report['client_case']=args.client_case
    report['shard_count']=args.shard_count;report['shard_index']=args.shard_index
    report['CPU_coverage_pixel_arithmetic']=False
    (args.work / 'prepared.json').write_text(json.dumps(report, indent=2) + '\n')

if __name__ == '__main__':
    main()
