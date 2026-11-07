"""Read back native Rock Ridge roots without mounting or extracting them.

The build host needs libarchive. Every file byte, symlink target, permission
and root ownership is compared with the GNU tree supplied to genisoimage.
"""
import ctypes as C
import ctypes.util
import hashlib
import os
from pathlib import Path
import stat

def tree_manifest(root):
    entries={}
    for directory,dirs,files in os.walk(root,followlinks=False):
        for name in [None,*sorted(dirs+files)]:
            path=Path(directory) if name is None else Path(directory)/name
            relative=path.relative_to(root).as_posix()
            if relative in entries:continue
            st=path.lstat();item={'mode':st.st_mode,'uid':0,'gid':0}
            if stat.S_ISLNK(st.st_mode):item['symlink']=os.readlink(path)
            elif stat.S_ISREG(st.st_mode):
                with path.open('rb') as stream:item['sha256']=hashlib.file_digest(stream,'sha256').hexdigest()
                item['bytes']=st.st_size
            elif not stat.S_ISDIR(st.st_mode):raise RuntimeError('Unsupported optical-root entry: '+str(path))
            entries[relative]=item
    return entries

def _library():
    name=ctypes.util.find_library('archive')
    if not name:raise RuntimeError('Install libarchive on the Linux build host')
    lib=C.CDLL(name)
    signatures={
        'archive_read_new':(C.c_void_p,[]),
        'archive_read_support_format_iso9660':(C.c_int,[C.c_void_p]),
        'archive_read_support_filter_none':(C.c_int,[C.c_void_p]),
        'archive_read_open_filename':(C.c_int,[C.c_void_p,C.c_char_p,C.c_size_t]),
        'archive_read_next_header':(C.c_int,[C.c_void_p,C.POINTER(C.c_void_p)]),
        'archive_read_data':(C.c_ssize_t,[C.c_void_p,C.c_void_p,C.c_size_t]),
        'archive_read_data_skip':(C.c_int,[C.c_void_p]),
        'archive_error_string':(C.c_char_p,[C.c_void_p]),
        'archive_read_free':(C.c_int,[C.c_void_p]),
        'archive_entry_pathname':(C.c_char_p,[C.c_void_p]),
        'archive_entry_mode':(C.c_uint,[C.c_void_p]),
        'archive_entry_uid':(C.c_int64,[C.c_void_p]),
        'archive_entry_gid':(C.c_int64,[C.c_void_p]),
        'archive_entry_size':(C.c_int64,[C.c_void_p]),
        'archive_entry_symlink':(C.c_char_p,[C.c_void_p]),
        'archive_entry_hardlink':(C.c_char_p,[C.c_void_p]),
    }
    for function,(result,args) in signatures.items():
        fn=getattr(lib,function);fn.restype=result;fn.argtypes=args
    return lib

def verify_root(image,expected,prefix='eelinux/root'):
    lib=_library();archive=lib.archive_read_new();seen={};aliases={};buffer=C.create_string_buffer(1024*1024)
    def check(code):
        if code<0:raise RuntimeError('ISO9660 readback: '+(lib.archive_error_string(archive) or b'unknown error').decode())
    try:
        check(lib.archive_read_support_format_iso9660(archive));check(lib.archive_read_support_filter_none(archive))
        check(lib.archive_read_open_filename(archive,os.fsencode(image),len(buffer)))
        while True:
            entry=C.c_void_p();code=lib.archive_read_next_header(archive,C.byref(entry))
            if code==1:break
            check(code)
            name=os.fsdecode(lib.archive_entry_pathname(entry)).removeprefix('./').rstrip('/')
            if name==prefix:relative='.'
            elif name.startswith(prefix+'/'):relative=name[len(prefix)+1:]
            else:check(lib.archive_read_data_skip(archive));continue
            if relative not in expected or relative in seen:raise RuntimeError('Unexpected/duplicate ISO root entry: '+name)
            wanted=expected[relative]
            item={'mode':lib.archive_entry_mode(entry),'uid':lib.archive_entry_uid(entry),'gid':lib.archive_entry_gid(entry)}
            link=lib.archive_entry_symlink(entry);hardlink=lib.archive_entry_hardlink(entry)
            if link:item['symlink']=os.fsdecode(link)
            elif stat.S_ISREG(item['mode']):
                if hardlink:aliases[relative]=os.fsdecode(hardlink).removeprefix('./')
                else:
                    digest=hashlib.sha256();count=0
                    while True:
                        size=lib.archive_read_data(archive,buffer,len(buffer));check(size)
                        if not size:break
                        digest.update(buffer.raw[:size]);count+=size
                    item.update(bytes=count,sha256=digest.hexdigest())
            check(lib.archive_read_data_skip(archive));seen[relative]=item
            if relative not in aliases and item!=wanted:
                raise RuntimeError('ISO root content/metadata differs: '+name+'; actual='+repr(item)+'; expected='+repr(wanted))
        for relative,target in aliases.items():
            if not target.startswith(prefix+'/'):raise RuntimeError('Hardlink outside DVD root: '+target)
            target=target[len(prefix)+1:]
            # genisoimage stores shared extents; libarchive may expose aliases.
            if target not in seen or target in aliases:raise RuntimeError('Unresolved ISO hardlink: '+target)
            seen[relative].update(bytes=seen[target]['bytes'],sha256=seen[target]['sha256'])
            if seen[relative]!=expected[relative]:raise RuntimeError('ISO root hardlink differs: '+relative)
        if set(seen)!=set(expected):raise RuntimeError('Missing ISO root entries: '+str(sorted(set(expected)-set(seen))[:10]))
        return {'result':'passed','entries':len(seen),
                'regular_files':sum(stat.S_ISREG(x['mode']) for x in seen.values()),
                'symlinks':sum(stat.S_ISLNK(x['mode']) for x in seen.values()),
                'file_bytes':sum(x.get('bytes',0) for x in seen.values()),
                'all_file_bytes_permissions_symlinks_root_ownership_verified':True}
    finally:lib.archive_read_free(archive)
