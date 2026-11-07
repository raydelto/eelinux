"""Shrink a staged PS2 userland while retaining its runtime and SDK contracts."""
from pathlib import Path
import argparse,hashlib,json,os,shutil,stat,struct,subprocess,tempfile,zipfile
from rootfs import elf_info

def sha(path):
    with path.open('rb') as stream:return hashlib.file_digest(stream,'sha256').hexdigest()

def symbols(path,prefix,*,dynamic=False):
    command=[prefix+'nm','--extern-only']
    if dynamic:command+=['--dynamic','--defined-only']
    result=subprocess.run([*command,str(path)],capture_output=True,text=True,check=True)
    # Addresses and archive member labels can change when debug sections go away.
    return sorted(line.split()[-2:] for line in result.stdout.splitlines()
                  if len(line.split())>=2 and len(line.split()[-2])==1)

def elf_identity(path):
    with path.open('rb') as stream:header=stream.read(52)
    # Keep the class/endianness/version, type, machine, entry and MIPS flags.
    return (header[:7],*struct.unpack_from('<HHII',header,16),struct.unpack_from('<I',header,36)[0])

def elf_file_extent(path):
    """Last byte referenced by ELF32 headers, segments or file-backed sections."""
    size=path.stat().st_size
    with path.open('rb') as stream:
        header=stream.read(52)
        if len(header)!=52 or header[:6]!=b'\x7fELF\x01\x01':
            raise ValueError('Expected little-endian ELF32: '+str(path))
        values=struct.unpack('<16sHHIIIIIHHHHHH',header)
        phoff,shoff=values[5:7];ehsize,phsize,phnum,shsize,shnum=values[8:13]
        extent=max(52,ehsize)
        if shoff and (not shnum or phnum==0xffff):
            if shsize<40 or shoff+40>size:raise ValueError('Invalid extended ELF section table: '+str(path))
            stream.seek(shoff);section0=struct.unpack('<10I',stream.read(40))
            if not shnum:shnum=section0[5]
            if phnum==0xffff:phnum=section0[7]
        for offset,count,entry_size,minimum in ((phoff,phnum,phsize,32),(shoff,shnum,shsize,40)):
            if count and (entry_size<minimum or offset+count*entry_size>size):
                raise ValueError('Invalid ELF file table: '+str(path))
            if count:extent=max(extent,offset+count*entry_size)
        for index in range(phnum):
            stream.seek(phoff+index*phsize);segment=struct.unpack('<8I',stream.read(32))
            end=segment[1]+segment[4]
            if end>size:raise ValueError('Truncated ELF segment: '+str(path))
            extent=max(extent,end)
        for index in range(shnum):
            stream.seek(shoff+index*shsize);section=struct.unpack('<10I',stream.read(40))
            if section[1] in (0,8):continue  # SHT_NULL / SHT_NOBITS have no file data.
            end=section[4]+section[5]
            if end>size:raise ValueError('Truncated ELF section: '+str(path))
            extent=max(extent,end)
    return extent

def appended_zip(path):
    """Recognize and CRC-check a ZIP stored beyond the ELF's referenced bytes."""
    size=path.stat().st_size
    with path.open('rb') as stream:
        stream.seek(max(0,size-65557));tail=stream.read()
    at=tail.rfind(b'PK\x05\x06')
    if at<0 or at+22>len(tail) or at+22+struct.unpack_from('<H',tail,at+20)[0]!=len(tail):return None
    try:
        with zipfile.ZipFile(path) as archive:
            entries=archive.infolist()
            start=min((entry.header_offset for entry in entries),default=archive.start_dir)
            if start<elf_file_extent(path):return None
            try:
                bad=archive.testzip()
            except (OSError,RuntimeError,zipfile.BadZipFile) as error:
                raise ValueError('Unreadable appended ZIP payload: '+str(path)) from error
            if bad:raise ValueError('Corrupt appended ZIP member '+bad+': '+str(path))
            return {'offset':start,'bytes':size-start,'members':len(entries),
                    'uncompressed_bytes':sum(entry.file_size for entry in entries),'CRC_checked':True}
    except zipfile.BadZipFile:return None

def strip_tree(root,prefix,report):
    root=root.resolve(strict=True)
    if not root.is_dir() or root==Path('/') or root==Path.home():
        raise ValueError('A dedicated staged userland directory is required')
    groups={};skipped=[]
    for directory,dirs,files in os.walk(root,followlinks=False):
        for name in files:
            path=Path(directory)/name
            if path.is_symlink() or not path.is_file():continue
            relative=path.relative_to(root).as_posix()
            if relative.startswith('lib/firmware/') or relative.endswith('.ko'):
                skipped.append({'path':relative,'reason':'hashed IOP firmware or kernel module; preserved byte-for-byte'})
                continue
            with path.open('rb') as stream:header=stream.read(52)
            if header.startswith(b'!<thin>\n'):
                raise ValueError('Installed thin archive requires external members: '+relative)
            archive=header.startswith(b'!<arch>\n')
            if not archive and not header.startswith(b'\x7fELF'):continue
            if not archive and (len(header)<52 or header[4:6]!=b'\x01\x01' or struct.unpack_from('<H',header,18)[0]!=8):
                raise ValueError('Non-MIPS/o32 ELF in staged userland: '+relative)
            if not archive and struct.unpack_from('<H',header,16)[0] not in (1,2,3):
                raise ValueError('Unsupported installed ELF type: '+relative)
            metadata=path.stat();key=(metadata.st_dev,metadata.st_ino)
            groups.setdefault(key,{'paths':[],'archive':archive,'elf_type':None if archive else struct.unpack_from('<H',header,16)[0]})['paths'].append(path)
    records=[];before_total=after_total=0
    for item in groups.values():
        paths=item['paths'];path=paths[0];metadata=path.stat();mode=stat.S_IMODE(metadata.st_mode)
        before_size=metadata.st_size;before_hash=sha(path)
        sdk=item['archive'] or item['elf_type']==1
        before_contract=None if item['archive'] else elf_info(path)
        before_identity=None if item['archive'] else elf_identity(path)
        resource_zip=None if item['archive'] else appended_zip(path)
        if resource_zip is not None:
            skipped.append({'path':path.relative_to(root).as_posix(),
                'paths':[p.relative_to(root).as_posix() for p in paths],
                'reason':'ELF has a valid appended ZIP payload; executable resources preserved byte-for-byte',
                'appended_zip':resource_zip,'sha256':before_hash,'bytes':before_size,
                'permissions_preserved':True,'hardlink_aliases_preserved':True,
                'runtime_or_SDK_contract_preserved':True})
            before_total+=before_size;after_total+=before_size
            continue
        before_symbols=symbols(path,prefix,dynamic=not sdk)
        with tempfile.TemporaryDirectory(prefix='.eelinux-strip-',dir=path.parent) as folder:
            candidate=Path(folder)/path.name;shutil.copy2(path,candidate);candidate.chmod(mode|0o200)
            subprocess.run([prefix+'strip',*(['-D'] if item['archive'] else []),
                '--strip-debug' if sdk else '--strip-unneeded',str(candidate)],check=True,
                           stdout=subprocess.DEVNULL,stderr=subprocess.PIPE)
            if item['archive']:
                subprocess.run([prefix+'ranlib','-D',str(candidate)],check=True)
            else:
                if elf_info(candidate)!=before_contract:raise RuntimeError('ELF runtime dependency changed: '+str(path))
                if elf_identity(candidate)!=before_identity:raise RuntimeError('ELF ABI or entry point changed: '+str(path))
            if before_symbols is not None and symbols(candidate,prefix,dynamic=not sdk)!=before_symbols:
                raise RuntimeError('Runtime export or SDK symbol changed: '+str(path))
            after_size=candidate.stat().st_size;after_hash=sha(candidate)
            if after_size>before_size:raise RuntimeError('Strip increased a staged binary: '+str(path))
            if before_hash!=after_hash:
                candidate.chmod(mode);os.utime(candidate,ns=(metadata.st_atime_ns,metadata.st_mtime_ns))
                # Replacing a private candidate avoids changing any outside hardlink.
                # Recreate the original aliases within this stage only.
                os.replace(candidate,path)
                for alias in paths[1:]:
                    assert alias.parent.resolve().is_relative_to(root)
                    alias.unlink();os.link(path,alias)
            records.append({'paths':[p.relative_to(root).as_posix() for p in paths],
                'method':'debug only; SDK symbols retained' if sdk else 'unneeded symbols; runtime exports retained',
                'before_bytes':before_size,'after_bytes':after_size,'before_sha256':before_hash,'after_sha256':after_hash,
                'permissions_preserved':True,'hardlink_aliases_preserved':True,'runtime_or_SDK_contract_preserved':True})
            before_total+=before_size;after_total+=after_size
    payload={'result':'passed','staging':str(root),'files':records,'skipped':skipped,
        'unique_binary_bytes_before':before_total,'unique_binary_bytes_after':after_total,
        'binary_bytes_saved':before_total-after_total,'source_and_build_trees_modified':False}
    report.parent.mkdir(parents=True,exist_ok=True);report.write_text(json.dumps(payload,indent=2)+'\n')
    print('Safely stripped '+str(len(records))+' unique binaries/archives; saved '+str(before_total-after_total)+' bytes.',flush=True)
    return payload

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--staging',type=Path,required=True)
    parser.add_argument('--toolchain-prefix',required=True)
    parser.add_argument('--report',type=Path,required=True)
    args=parser.parse_args();strip_tree(args.staging,args.toolchain_prefix,args.report)

if __name__=='__main__':main()
