"""Audit installed PS2 ELF files and SDK archive members, excluding IOP firmware."""
from pathlib import Path
import argparse,hashlib,json,os,tempfile
import userland

def audit(stage,report):
    stage=stage.resolve(strict=True);seen=set();records=[];archives=0;members=0
    with tempfile.TemporaryDirectory(prefix='eelinux-abi-',dir='/var/tmp') as temporary:
        object_path=Path(temporary)/'member.o'
        for directory,dirs,files in os.walk(stage,followlinks=False):
            for name in files:
                path=Path(directory)/name
                if path.is_symlink() or not path.is_file():continue
                relative=path.relative_to(stage).as_posix()
                if relative.startswith('lib/firmware/') or relative.endswith('.ko'):continue
                stat=path.stat();identity=(stat.st_dev,stat.st_ino)
                if identity in seen:continue
                seen.add(identity)
                with path.open('rb') as stream:
                    magic=stream.read(8)
                    if magic[:4]==b'\x7fELF':
                        userland.validate_elf(path);count=1;kind='ELF'
                    elif magic==b'!<arch>\n':
                        count=0;kind='SDK archive';archives+=1
                        while True:
                            header=stream.read(60)
                            if not header:break
                            if len(header)!=60 or header[58:]!=b'`\n':
                                raise RuntimeError('Invalid archive header: '+relative)
                            size=int(header[48:58].strip());data=stream.read(size)
                            if len(data)!=size:raise RuntimeError('Truncated archive: '+relative)
                            if size&1:stream.read(1)
                            if header[:16].startswith(b'#1/'):
                                data=data[int(header[:16][3:].strip()):]
                            if data[:4]!=b'\x7fELF':continue  # Archive symbol/name tables.
                            object_path.write_bytes(data);userland.validate_elf(object_path);count+=1
                        # glibc keeps empty compatibility archives after merging
                        # libpthread/libdl/libanl into libc.
                        if not count and stat.st_size!=8:
                            raise RuntimeError('Archive contains no target objects: '+relative)
                        members+=count
                    elif magic==b'!<thin>\n':raise RuntimeError('Installed thin archive: '+relative)
                    else:continue
                with path.open('rb') as stream:digest=hashlib.file_digest(stream,'sha256').hexdigest()
                records.append({'path':relative,'kind':kind,'objects_checked':count,'sha256':digest})
    payload={'result':'passed','stage':str(stage),'scope':'little-endian MIPS o32, R5900-compatible ISA, soft-float or ABI-independent assembly',
        'unique_ELF_files':sum(r['kind']=='ELF' for r in records),'SDK_archives':archives,
        'SDK_ELF_members':members,'application_CFLAGS':userland.CPU_FLAGS,
        'glibc_2_44_atomic_policy':'R5900 with LL/SC handled by the existing Linux trap emulation policy',
        'IOP_firmware':'Separate IOP CPU; excluded and preserved by its firmware manifest',
        'files':records,'physical_hardware_tested':False}
    report.parent.mkdir(parents=True,exist_ok=True);report.write_text(json.dumps(payload,indent=2)+'\n')
    print(f'Target ABI audit passed: {payload["unique_ELF_files"]} ELFs and {members} members in {archives} SDK archives.',flush=True)
    return payload

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--staging',type=Path,required=True);parser.add_argument('--report',type=Path,required=True)
    args=parser.parse_args();audit(args.staging,args.report)

if __name__=='__main__':main()
