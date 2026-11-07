"""Build the standalone PAL DVD kernel from an already qualified native GS tree.

The base kernel artifacts are preserved. CDVD source additions are exported as
a normal patch; a private build directory contains the optical-root config.
"""
from pathlib import Path
import argparse, difflib, hashlib, json, shutil, subprocess, sys
from common import ROOT, TARGET_FLAGS
from initramfs import newc
from iop_dev9 import linker_source, validate_irx_layout
from iop_firmware import validate_irx
import kernel


def install(source):
    changes={
        'drivers/block/ps2-cdvd.c':(ROOT/'kernel/new-drivers/ps2-cdvd.c').read_text(),
        'drivers/block/ps2-cdvd-rpc.h':(ROOT/'kernel/new-drivers/ps2-cdvd-rpc.h').read_text(),
    }
    for name,addition in [
        ('drivers/block/Makefile','\nobj-$(CONFIG_PS2_CDVD) += ps2-cdvd.o\n'),
        ('drivers/block/Kconfig','\nconfig PS2_CDVD\n\tbool "PlayStation 2 read-only DVD root device"\n\tdepends on SONY_PS2\n\thelp\n\t  Native IOP DVD-video reads for a standalone eeLinux live DVD.\n'),
    ]:
        before=(source/name).read_text()
        changes[name]=before if 'CONFIG_PS2_CDVD' in before or 'config PS2_CDVD' in before else before+addition
    patch=ROOT/'kernel/patches/ps2-cdvd.patch'
    if not (source/'drivers/block/ps2-cdvd.c').exists():
        lines=[]
        for name,after in changes.items():
            path=source/name; before=path.read_text() if path.exists() else ''
            lines+=list(difflib.unified_diff(before.splitlines(True),after.splitlines(True),
                fromfile='a/'+name if path.exists() else '/dev/null',tofile='b/'+name))
        patch.write_text(''.join(lines))
        subprocess.run(['git','apply',str(patch.resolve())],cwd=source,check=True)
    else:
        for name,text in changes.items():
            if (source/name).read_text()!=text:
                raise RuntimeError('DVD kernel source changed; re-export the optical patch: '+name)
    return patch


def firmware(work,prefix):
    original=ROOT/'kernel/vendor/iopmod/snapshot'
    tree=work/'cdvd-iop'
    if not tree.exists(): shutil.copytree(original,tree)
    (tree/'module/eecdvd.c').write_bytes((ROOT/'kernel/iop-overlay/module/eecdvd.c').read_bytes())
    (tree/'include/iopmod/ps2-cdvd-rpc.h').write_bytes((ROOT/'kernel/new-drivers/ps2-cdvd-rpc.h').read_bytes())
    # Header is also reachable by the module's direct include.
    (tree/'include/ps2-cdvd-rpc.h').write_bytes((ROOT/'kernel/new-drivers/ps2-cdvd-rpc.h').read_bytes())
    (tree/'include/iopmod/module/eecdvd.h').write_text('MODULE_ID(eecdvd, 0x0100);\n')
    printk=tree/'builtin/printk.c'
    printk.write_text((original/'builtin/printk.c').read_text().replace('err < 0 ? err : length','err < 0 ? err : (int)length'))
    linker=tree/'.eelinux-iop.ld'
    layout=linker_source((original/'script/iop.ld').read_text())
    # A BSS-only 64-byte aligned service needs initialized padding included
    # in MODLOAD's file size, rather than an unaccounted gap before BSS.
    layout=layout.replace('.data : ALIGN(16) {','.data : ALIGN(64) {\n\t\tBYTE(0);')
    layout=layout.replace('.bss : ALIGN(16) {','.bss : ALIGN(64) {')
    layout=layout.replace('*(.data.*)\n\t\t. = ALIGN(16);','*(.data.*)\n\t\t. = ALIGN(64);')
    linker.write_text(layout)
    flags=('-O0 -march=r3000 -EL -msoft-float -fomit-frame-pointer -fno-pic -mno-abicalls '
        '-fno-common -ffreestanding -static -fno-strict-aliasing -nostdlib -mlong-calls '
        '-mno-gpopt -mno-shared -G0 -ffunction-sections -fdata-sections -Wall -Werror '
        '-Iinclude -I'+str(ROOT/'kernel/iop-compat'))
    subprocess.run(['make','-C',str(tree),'-j2','module/eecdvd.irx','CROSS_COMPILE='+prefix,
        'IOP_CFLAGS='+flags,'MODULE_LD='+str(linker)],check=True)
    result=tree/'module/eecdvd.irx'; validate_irx(result); validate_irx_layout(result)
    return result


def unpack_cpio(archive,destination):
    import gzip,stat
    data=archive.read_bytes()
    if data[:2]==b'\x1f\x8b':data=gzip.decompress(data)
    offset=0
    while offset<len(data):
        header=data[offset:offset+110]
        if header[:6]!=b'070701':raise RuntimeError('Unexpected initramfs format')
        values=[int(header[6+i*8:14+i*8],16) for i in range(13)]
        mode,size,namesize=values[1],values[6],values[11]
        offset+=110;name=data[offset:offset+namesize-1].decode();offset=(offset+namesize+3)&~3
        body=data[offset:offset+size];offset=(offset+size+3)&~3
        if name=='TRAILER!!!':break
        path=destination/name
        if name.startswith('/') or '..' in Path(name).parts:raise RuntimeError('Unsafe CPIO path')
        if path.resolve()!=destination.resolve() and not path.parent.resolve().is_relative_to(destination.resolve()):raise RuntimeError('CPIO parent escapes extraction directory')
        path.parent.mkdir(parents=True,exist_ok=True)
        if stat.S_ISDIR(mode):path.mkdir(exist_ok=True);path.chmod(mode&0o7777)
        elif stat.S_ISREG(mode):path.write_bytes(body);path.chmod(mode&0o7777)
        elif stat.S_ISLNK(mode):path.symlink_to(body.decode())
        elif name!='dev/console':raise RuntimeError('Unexpected special CPIO entry')


def build(args):
    if sys.platform!='linux':raise RuntimeError('Build the DVD kernel on Linux/WSL')
    args.work.mkdir(parents=True,exist_ok=True);args.output_dir.mkdir(parents=True,exist_ok=True)
    source_audit=kernel.inspect_source(args.source,'7.2.8')
    networking_sha=kernel.networking_config_sha256()
    gs_files=kernel.native_gs_files(args.source)
    reference=getattr(args,'gs_reference_output',None)
    gs_reference=kernel.verify_gs_reference(args.source,reference) if reference else None
    patch=install(args.source)
    fw=firmware(args.work,args.iop_prefix)
    config=kernel.with_networking_config((args.base_output/'.config').read_text())
    import re
    oldinit=Path(re.search(r'^CONFIG_INITRAMFS_SOURCE="([^"]+)"',config,re.M)[1])
    init=args.work/'initramfs'
    if not init.exists():
        init.mkdir();unpack_cpio(oldinit,init)
    subprocess.run([args.cross_prefix+'gcc',*TARGET_FLAGS,'-Os','-static','-s','-Wall','-Wextra','-Werror',
        str(ROOT/'boot/early-root.c'),'-o',str(init/'init')],check=True)
    (init/'init').chmod(0o755);(init/'etc/eelinux-root').write_text('/dev/ps2dvd\n')
    (init/'lib/firmware/ps2').mkdir(parents=True,exist_ok=True)
    shutil.copy2(fw,init/'lib/firmware/ps2/eecdvd.irx')
    cpio=args.work/'dvd-root.cpio';cpio.write_bytes(newc(init,console=True))
    out=args.work/'kernel'
    if not out.exists():subprocess.run(['cp','-a','--reflink=auto',str(args.base_output),str(out)],check=True)
    make=['make','-C',str(args.source),'O='+str(out),'ARCH=mips','CROSS_COMPILE='+args.cross_prefix]
    values={'BLK_DEV':'y','PS2_CDVD':'y',
        'ISO9660_FS':'y','OVERLAY_FS':'y','TMPFS_XATTR':'y',
        'ZRAM':'y','ZRAM_BACKEND_LZ4':'y','ZRAM_DEF_COMP_LZ4':'y',
        'INITRAMFS_SOURCE':'"'+str(cpio)+'"',
        'CMDLINE':'"console=ttyS0,115200 console=tty0 root=/dev/ps2dvd ro rootwait video=ps2fb:720x576p@50"'}
    for name,value in values.items():
        config=re.sub(r'^(?:CONFIG_'+name+r'=.*|# CONFIG_'+name+r' is not set)\n','',config,flags=re.M)
        config+='CONFIG_'+name+'='+value+'\n'
    (out/'.config').write_text(config)
    subprocess.run(make+['olddefconfig'],check=True)
    kernel.verify_config(out/'.config')
    kernel.verify_networking_config(out/'.config')
    log=args.output_dir/'kernel-build.log'
    with log.open('w',buffering=1) as stream:
        subprocess.run(make+['-j'+str(args.jobs),'W=1','KCFLAGS=-Werror','vmlinux','vmlinuz','modules'],stdout=stream,stderr=subprocess.STDOUT,check=True)
        kernel.verify_networking_config(out/'.config')
        if kernel.networking_config_sha256()!=networking_sha or kernel.native_gs_files(args.source)!=gs_files:
            raise RuntimeError('Kernel profile or native GS sources changed while compiling; rebuild before publishing')
        modules=args.output_dir/'modules'
        release,module_files=kernel.install_module_payload(make,out,modules,stream)
    image=args.output_dir/'EELINUX.ELF';shutil.copy2(out/'vmlinuz',image)
    shutil.copy2(out/'.config',args.output_dir/'kernel.config')
    report={'schema':2,'purpose':'Standalone PAL DVD kernel; native Rock Ridge ISO9660 GNU root',
        'kernel_version':'7.2.8','root_device':'/dev/ps2dvd','video_mode':'576p',
        'image_sha256':kernel.sha256(image),'image_bytes':image.stat().st_size,
        'config_sha256':kernel.sha256(out/'.config'),'layout':kernel.boot_layout(out/'vmlinux',out/'vmlinuz'),
        'source_audit':source_audit,'base_kernel_sha256':kernel.sha256(args.base_output/'vmlinuz'),
        'native_gs_source_files':gs_files,'graphics_reference':gs_reference,
        'kernel_release':release,'modules_directory':str(modules.resolve()),'modules':module_files,
        'networking_fragment_sha256':networking_sha,
        'optional_networking_modules':True,'networking_services_started_at_boot':False,
        'cdvd_patch_sha256':kernel.sha256(patch),'cdvd_firmware_sha256':kernel.sha256(fw),
        'cdvd_firmware_source_sha256':kernel.sha256(ROOT/'kernel/iop-overlay/module/eecdvd.c'),
        'early_root_source_sha256':kernel.sha256(ROOT/'boot/early-root.c'),
        'boot_mode':'native Rock Ridge ISO9660 /eelinux/root, RAM overlay',
        'read_cache_sectors':8,'RPC_transfer_bytes':64+8*2048,
        'RAM_swap':'16 MiB logical zram; maximum 4 MiB compressed storage; LZ4',
        'USB_required':False,'physical_boot_image_tested':False,'emulator_boot_qualified':False}
    (args.output_dir/'kernel-report.json').write_text(json.dumps(report,indent=2)+'\n')
    print('Built standalone DVD kernel: '+str(image),flush=True)


def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('source','base-output','work','output-dir'):p.add_argument('--'+name,type=Path,required=True)
    p.add_argument('--cross-prefix',required=True);p.add_argument('--iop-prefix',required=True)
    p.add_argument('--gs-reference-output',type=Path,help='Require identical native GS sources to this preserved kernel build')
    p.add_argument('--jobs',type=int,default=2)
    build(p.parse_args())
if __name__=='__main__':main()
