"""Check the personal image's installed payload, swap and normal boot policy."""
from pathlib import Path
import argparse,hashlib,json,subprocess,tempfile
import rootfs
from userland_full import DIAGNOSTIC_NAMES

def sha(path):
    with path.open('rb') as stream:return hashlib.file_digest(stream,'sha256').hexdigest()

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--directory',type=Path,required=True)
    args=parser.parse_args();directory=args.directory.resolve(strict=True)
    build=json.loads((directory/'userland-build.json').read_text());stage=Path(build['staging'])
    image=directory/'rootfs/rootfs.ext4';report=json.loads((directory/'development-image.json').read_text())
    disk=directory/'ps2-linux-development-clean-1g.img'
    if disk.stat().st_size!=1073741824 or sha(disk)!=report['disk']['sha256']:
        raise RuntimeError('Personal disk size/hash mismatch')
    def debug(command):
        return subprocess.run(['debugfs','-R',command,str(image)],capture_output=True,text=True,check=True).stdout
    directories={}
    for name in ('/usr/bin','/usr/lib/eelinux','/etc/init.d','/etc/rc.d/rcS.d','/var/lib'):
        listing=debug('ls -p '+name)
        names=[line.split('/')[5] for line in listing.splitlines() if line.startswith('/')]
        if any(n in DIAGNOSTIC_NAMES or n in ('ps2-native-sdk-test','ps2-xorg-test') or
            n.startswith('ps2-gs') and n.endswith('-test') for n in names):
            raise RuntimeError('Diagnostic payload in personal filesystem: '+name)
        directories[name]=names
    expected={name for name in json.loads((rootfs.REPO/'rootfs/etc/rc.d/runlevels.json').read_text())['S']}
    boot=set(directories['/etc/rc.d/rcS.d'])-{'.','..','.keep'}
    if boot!=expected:raise RuntimeError('Unexpected early boot service: '+str(boot-expected))
    swap=debug('stat /swapfile')
    if ('Mode:  0600' not in swap or 'Size: 268435456' not in swap or 'Blockcount: 524288' not in swap):
        raise RuntimeError('Swap allocation/permissions mismatch')
    if debug('cat /etc/default/swap').strip()!='SWAP_DEVICE=/swapfile':
        raise RuntimeError('Swap boot configuration mismatch')
    profile=debug('cat /etc/profile')
    if 'export TMPDIR=${TMPDIR:-/var/tmp}' not in profile:
        raise RuntimeError('Compiler temporary directory default is not disk-backed')
    files={};version=build['compiler'];tuple_name='mipsr5900el-unknown-linux-gnu'
    required=['/usr/bin/'+name for name in ('gcc','g++','as','ld','ed','python3.14','nano','vim','lynx','gdb')]
    required += [f'/usr/libexec/gcc/{tuple_name}/{version}/{name}' for name in ('cc1','cc1plus')]
    required += [f'/usr/lib/gcc/{tuple_name}/{version}/specs',
        '/usr/lib/xorg/modules/drivers/ps2_drv.so','/etc/ssl/certs/ca-certificates.crt',
        '/usr/lib/libffi.so.8.5.0','/usr/lib/libc.a',
        f'/usr/lib/gcc/{tuple_name}/{version}/libstdc++.a',
        f'/usr/lib/gcc/{tuple_name}/{version}/libgcc_eh.a']
    required += ['/lib/firmware/ps2/'+name for name in build['kernel_modules']['IOP_firmware']]
    with tempfile.TemporaryDirectory(prefix='eelinux-image-check-',dir='/var/tmp') as temporary:
        destination=Path(temporary)/'payload'
        for name in required:
            source=rootfs.guest_path(stage,name)
            if not source.is_file():raise RuntimeError('Missing source payload: '+name)
            relative='/'+source.relative_to(stage).as_posix()
            destination.unlink(missing_ok=True)
            debug('dump '+relative+' '+str(destination))
            if not destination.is_file() or sha(destination)!=sha(source):
                raise RuntimeError('Installed payload mismatch: '+name)
            files[name]=sha(destination)
    result={'result':'passed','scope':'offline clean personal image payload/boot/swap audit',
        'disk_sha256':report['disk']['sha256'],'disk_bytes':disk.stat().st_size,
        'early_boot_services':sorted(boot),'diagnostic_boot_hooks':False,'diagnostic_clients_installed':False,
        'swap_MiB':256,'swap_fully_allocated':True,'compiler_TMPDIR':'/var/tmp',
        'installed_payload_sha256':files,'physical_hardware_tested':False}
    (directory/'offline-verification.json').write_text(json.dumps(result,indent=2)+'\n')
    print('Clean image payload, normal boot hooks and allocated swap verified.',flush=True)

if __name__=='__main__':main()
