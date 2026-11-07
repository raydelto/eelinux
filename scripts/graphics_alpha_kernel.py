"""Clone the verified kernel sources and build the new typed lookup extension."""
from pathlib import Path
import argparse, hashlib, json, shutil, subprocess
import kernel_gs_lut_port, kernel_gs_map_port, kernel_gs_map_blit_port
import kernel_gs_rgba_transfer_port
import kernel_gs_capacity_port
ROOT = Path(__file__).resolve().parents[1]
def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--base-build',type=Path,required=True)
    p.add_argument('--base-config',type=Path)
    p.add_argument('--extension',choices=('lut','map','map-blit','rgba-transfer','surface-capacity'),default='lut')
    p.add_argument('--work',type=Path,required=True)
    p.add_argument('--toolchain-prefix',required=True)
    p.add_argument('--jobs',type=int,default=4)
    args=p.parse_args();args.work.mkdir(parents=True,exist_ok=True)
    source=args.work/'source';out=args.work/'kernel'
    if source.exists() or out.exists():raise RuntimeError('use a fresh work directory')
    port={'lut':kernel_gs_lut_port,'map':kernel_gs_map_port,'map-blit':kernel_gs_map_blit_port,
          'rgba-transfer':kernel_gs_rgba_transfer_port,'surface-capacity':kernel_gs_capacity_port}[args.extension]
    patch=ROOT/('kernel/patches/ps2-gs-'+args.extension+'.patch')
    port.export(args.base_build/'source',patch)
    subprocess.run(['cp','-a','--reflink=auto',str((args.base_build/'source').resolve()),str(source)],check=True)
    out.mkdir();shutil.copy2(args.base_config or args.base_build/'.config',out/'.config')
    for name,old,new in port.changes(source):
        path=source/name;path.parent.mkdir(parents=True,exist_ok=True);path.write_text(new)
    make=['make','-C',str(source),'O='+str(out),'ARCH=mips',
          'CROSS_COMPILE='+args.toolchain_prefix,'W=1','KCFLAGS=-Werror']
    with (args.work/'kernel.log').open('w',buffering=1) as log:
        subprocess.run([*make,'olddefconfig'],stdout=log,stderr=log,check=True)
        subprocess.run([*make,'-j'+str(args.jobs),'vmlinux','vmlinuz'],stdout=log,stderr=log,check=True)
    image=out/'vmlinuz'
    report={'result':'built','kernel_sha256':hashlib.sha256(image.read_bytes()).hexdigest(),
        'patch_sha256':hashlib.sha256(patch.read_bytes()).hexdigest(), 'extension':args.extension,
        'W':1,'KCFLAGS':'-Werror','native_pixel_tested':False,'physical_hardware_tested':False}
    (args.work/'kernel.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))
if __name__=='__main__':main()
