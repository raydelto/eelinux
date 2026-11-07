"""Package a clean 1 GiB PS2 development disk from an audited full userland."""
from pathlib import Path
import argparse,hashlib,json,shutil,subprocess,sys
import hardware_usb,rootfs
from userland_full import DIAGNOSTIC_NAMES

ROOT=Path(__file__).resolve().parents[1]
DISK_MIB=1024
ROOT_MIB=959  # 1 MiB partition alignment + 64 MiB FAT32 + 959 MiB ext4.

def sha(path):
    with path.open('rb') as stream:return hashlib.file_digest(stream,'sha256').hexdigest()

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--staging',type=Path,required=True)
    parser.add_argument('--kernel-directory',type=Path,required=True)
    parser.add_argument('--full-build-report',type=Path,required=True)
    parser.add_argument('--root-password-hash-file',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--swap-mib',type=int,default=256)
    args=parser.parse_args();stage=args.staging.resolve(strict=True);output=args.output.resolve()
    if output.exists():raise RuntimeError('Output directory already exists; preserve previous images')
    build=json.loads(args.full_build_report.read_text())
    if build['staging']!=str(stage):raise RuntimeError('Stage does not match the full build report')
    for directory in ('usr/bin','usr/lib/eelinux','etc/init.d','etc/rc.d/rcS.d'):
        for path in (stage/directory).glob('*'):
            if (path.name.startswith('ps2-gs') and path.name.endswith('-test') or
                path.name in DIAGNOSTIC_NAMES):
                raise RuntimeError('Diagnostic payload in personal staging: '+str(path))
    for relative in ('var/lib/ps2-xorg-test','var/lib/ps2-native-sdk-test'):
        if (stage/relative).exists():raise RuntimeError('Prior test logs in personal staging: '+relative)
    kernel=json.loads((args.kernel_directory/'kernel-report.json').read_text())
    if kernel.get('warning_count')!=0 or not kernel.get('clean_full_build'):
        raise RuntimeError('A clean, zero-warning kernel report is required')
    config=(args.kernel_directory/'kernel.config').read_text()
    if args.swap_mib and 'CONFIG_SWAP=y\n' not in config:raise RuntimeError('Kernel lacks swap support')
    output.mkdir(parents=True)
    for name in ('EELINUX.ELF','kernel.config','kernel-report.json'):
        shutil.copy2(args.kernel_directory/name,output/name)
    subprocess.run([sys.executable,str(ROOT/'scripts/rootfs.py'),'--staging',str(stage),
        '--output-dir',str(output/'rootfs'),'--filesystem','ext4','--size-mib',str(ROOT_MIB),
        '--swap-file-mib',str(args.swap_mib),'--hostname','ps2','--video-mode','576p',
        '--root-password-hash-file',str(args.root_password_hash_file)],check=True)
    # Use the normal USB packager's checked kernel/config/hash and filesystem gates.
    disk=output/'ps2-linux-development-clean-1g.img'
    readme='''eeLinux PS2 development image

Disk size: 1 GiB (1,024 MiB); FAT32 boot partition plus ext4 GNU/Linux root.
Launch mass:/EELINUX.ELF with your existing Tuna/wLaunchELF setup.
Root is /dev/sda2. Video defaults to 720x576p at 50 Hz.
Login: root / eelinux. Change the password with passwd after logging in.
Normal console login; run startx when you want Xorg/TWM and xterm.
No automatic graphics tests or diagnostic clients are installed.
The development suite includes GCC/G++, Binutils, the glibc SDK, GNU build
tools, nano, Vim, Lynx, Python and certificates. Temporary compiler files use
/var/tmp; disk-backed swap is enabled. Use make -j1 on the console.
Physical boot and full 2D/GLX coverage require separate qualification.
'''
    (output/'README.txt').write_text(readme)
    report=hardware_usb.build_image(output/'rootfs/rootfs.ext4',output/'EELINUX.ELF',
        output/'kernel.config',output/'kernel-report.json',disk,video_mode='576p',
        root_size_mib=ROOT_MIB,readme_bytes=readme.encode())
    if disk.stat().st_size!=DISK_MIB*1024*1024:raise RuntimeError('Disk is not exactly 1 GiB')
    shutil.copy2(args.full_build_report,output/'userland-build.json')
    stripping=Path(build['strip_report']);shutil.copy2(stripping,output/'strip-report.json')
    result={'result':'packaged; exact-image runtime qualification pending','disk':report['disk'],
        'kernel_sha256':sha(output/'EELINUX.ELF'),'userland_build_sha256':sha(args.full_build_report),
        'root_MiB':ROOT_MIB,'swap_MiB':args.swap_mib,'diagnostic_clients_installed':False,
        'automatic_Xorg_test_service':False,'normal_login':True,'manual_startx':True,
        'kernel_build_warning_count':0,'latest_packages':build['latest_packages'],
        'physical_hardware_tested':False}
    (output/'development-image.json').write_text(json.dumps(result,indent=2)+'\n')
    print('Clean 1 GiB image: '+str(disk),flush=True)

if __name__=='__main__':main()
