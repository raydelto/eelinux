"""Expose the locked native-package archives and package index in userland/."""
from pathlib import Path
import hashlib,json,shutil
ROOT=Path(__file__).resolve().parents[1]

def digest(path,algorithm='sha256'):
    with path.open('rb') as stream:return hashlib.file_digest(stream,algorithm).hexdigest()

def main():
    cache=Path.home()/'.cache/eelinux'
    latest=json.loads((ROOT/'configs/native-tools-latest.json').read_text())
    toolchain=json.loads((ROOT/'toolchain/source-lock.json').read_text())
    sources=ROOT/'userland/sources';sources.mkdir(parents=True,exist_ok=True)
    archives={}
    for folder in (cache/'native-tools-sources',cache/'desktop-xorg/downloads',
                   cache/'native-tools-dependencies/downloads',cache/'native-gcc16/downloads',
                   cache/'userland-latest/xorg-builder-check/mesa',cache/'mesa-xlib',
                   Path.home()/'.cache/eelinux-linux-latest/toolchain/downloads'):
        if folder.exists():
            for path in folder.rglob('*'):
                if path.is_file():archives.setdefault(path.name,[]).append(path)
    index={}
    def copy_archive(filename,expected,algorithm):
        local=sources/filename
        if local.exists() and digest(local,algorithm)==expected:return local
        match=next((p for p in archives.get(filename,[]) if digest(p,algorithm)==expected),None)
        if match is None:return None
        temp=local.with_name(local.name+'.part');shutil.copy2(match,temp)
        if digest(temp,algorithm)!=expected:raise RuntimeError('Source copy checksum mismatch: '+filename)
        temp.replace(local);return local
    for name,spec in latest.items():
        local=copy_archive(spec['filename'],spec['sha256'],'sha256')
        if local is None:raise RuntimeError('Missing verified source: '+name)
        installed=cache/'native-tools-latest/build'/name/'installed.json'
        index[name]={'version':spec['version'],'source':local.relative_to(ROOT/'userland').as_posix(),
            'sha256':spec['sha256'],'upstream':spec['url'],'recipe':'package/'+name+'/recipe.json',
            'state':'built' if installed.exists() else 'source verified; build pending',
            'runtime_qualified':False}
    for spec in toolchain['sources']:
        name=spec['name'];filename=spec['url'].rsplit('/',1)[1]
        local=copy_archive(filename,spec['digest'],spec['algorithm'])
        if local is None:raise RuntimeError('Missing compiler source: '+name)
        index[name]={'version':spec['version'],'source':local.relative_to(ROOT/'userland').as_posix(),
            'sha256':digest(local),'upstream':spec['url'],
            'recipe':'package/'+name+'/recipe.json',
            'state':'built','runtime_qualified':False}
    # Retain the wider desktop/runtime recipe catalog and expose any cached,
    # checksum-matching archive without confusing its older pins with latest.
    manifest=json.loads((ROOT/'configs/packages.json').read_text())
    for name,spec in manifest['packages'].items():
        if name in index or name.startswith('eelinux-') and name[8:] in index:continue
        records=[]
        for entry in spec.get('source_hashes',[]):
            local=copy_archive(entry['filename'],entry['digest'],entry['algorithm'])
            if local:records.append(local.relative_to(ROOT/'userland').as_posix())
        index[name]={'version':spec['version'],'sources':records,'recipe':spec.get('recipe'),
                     'state':'baseline recipe; see configs/packages.json for selection',
                     'runtime_qualified':False}
        if (ROOT/'userland/package'/name/'recipe.json').is_file():
            index[name]['recipe']='package/'+name+'/recipe.json'
    mesa=json.loads((ROOT/'userland/mesa-sources.json').read_text())
    local=copy_archive('mesa-'+mesa['version']+'.tar.xz',mesa['sha256'],'sha256')
    if local is None:raise RuntimeError('Missing verified Mesa source archive')
    folder=ROOT/'userland/package/mesa';folder.mkdir(parents=True,exist_ok=True)
    (folder/'recipe.json').write_text(json.dumps({'schema':1,'name':'mesa',
        'engine':'scripts/native_xorg.py','source':mesa,'source_archive':'../../sources/'+local.name,
        'scope':'Shared native GS components in the PS2 DDX; no complete OpenGL/GLX driver or software renderer'},indent=2)+'\n')
    index['mesa']={'version':mesa['version'],'source':local.relative_to(ROOT/'userland').as_posix(),
        'sha256':mesa['sha256'],'recipe':'package/mesa/recipe.json','state':'native GS components built; full OpenGL/GLX pending',
        'runtime_qualified':False}
    payload={'schema':1,'date':'2026-10-05','target':'R5900 Linux, little endian o32 soft float',
        'application_CFLAGS':['-march=r5900','-mabi=32','-msoft-float','-mno-llsc','-mfix-r5900'],
        'glibc_2_44_atomic_policy':'R5900 with Linux LL/SC trap emulation',
        'IOP_firmware_CPU':'Separate R3000 IOP; not R5900 GNU userland',
        'purpose':'Package sources and recipes. A built state does not imply image or hardware qualification.',
        'native_build_output':str(cache/'native-tools-latest/target'),
        'compiler_build_output':str(cache/'native-gcc16/output-full/target'),
        'packages':dict(sorted(index.items()))}
    (ROOT/'userland/package-index.json').write_text(json.dumps(payload,indent=2)+'\n')
    print(str(len(index))+' package entries indexed; verified local source archives in userland/sources.',flush=True)

if __name__=='__main__':main()
