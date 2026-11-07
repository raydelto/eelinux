"""Generate target UTF-8 locale data with the matching native GNU libc tools."""
from pathlib import Path
import argparse,gzip,hashlib,json,os,shutil,subprocess,tempfile

def selected_emulator(work=None):
    from emulator_userland import selected_emulator as select
    directory=Path(work or os.environ.get('EELINUX_QEMU_WORK',
        str(Path.home()/'.cache/eelinux-linux/emulator-userland')))
    if not (directory/'build-report.json').is_file():
        raise RuntimeError('UTF-8 locales require the pinned R5900 QEMU; run '
            'make emulator-userland-build and set EELINUX_QEMU_WORK to its directory')
    return select(directory)[0]

def install_x11(target,sysroot,work):
    """Restore libX11's matching locale definitions after Buildroot's purge."""
    source=Path(sysroot)/'usr/share/X11/locale'
    if not (source/'en_US.UTF-8/XLC_LOCALE').is_file():
        raise RuntimeError('X11 SDK is missing its UTF-8 locale definitions')
    destination=Path(target)/'usr/share/X11/locale'
    shutil.copytree(source,destination,dirs_exist_ok=True,symlinks=True)
    files=sorted(p.relative_to(target).as_posix() for p in destination.rglob('*') if p.is_file())
    hashes={p:hashlib.sha256((Path(target)/p).read_bytes()).hexdigest() for p in files}
    Path(work).mkdir(parents=True,exist_ok=True)
    (Path(work)/'X11-locales.json').write_text(json.dumps({'result':'installed',
        'source':str(source),'files':hashes,'bytes':sum((Path(target)/p).stat().st_size for p in files)},indent=2)+'\n')
    return files

def build(target,sysroot,work,emulator,runtime=None):
    target=Path(target);sysroot=Path(sysroot);work=Path(work);work.mkdir(parents=True,exist_ok=True)
    source=sysroot/'usr/share/i18n';tool=sysroot/'usr/bin/localedef'
    assert tool.is_file() and (source/'locales/en_US').is_file() and (source/'locales/C').is_file()
    charmap=work/'UTF-8';charmap.write_bytes(gzip.decompress((source/'charmaps/UTF-8.gz').read_bytes()))
    env=dict(os.environ,LC_ALL='C',I18NPATH=str(source))
    command=[str(emulator),'-cpu','R5900','-L',str(runtime or target),str(tool)]
    version=subprocess.check_output(command+['--version'],env=env,text=True).splitlines()[0]
    outputs=[]
    with tempfile.TemporaryDirectory(prefix='compile-',dir=work) as temporary, (work/'localedef.log').open('w') as log:
        generated=[]
        for name,input_name in (('C.utf8','C'),('en_US.utf8','en_US')):
            output=Path(temporary)/name;output.mkdir()
            subprocess.run(command+['--no-archive','-f',str(charmap),'-i',input_name,str(output)],
                env=env,stdout=log,stderr=log,check=True)
            generated.append((name,output))
        for name,output in generated:
            for compiled in sorted(p for p in output.rglob('*') if p.is_file()):
                destination=target/'usr/lib/locale'/name/compiled.relative_to(output)
                destination.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(compiled,destination)
                outputs.append(destination.relative_to(target).as_posix())
    sha=lambda p:hashlib.file_digest(p.open('rb'),'sha256').hexdigest()
    report={'result':'generated','compiler':version,'target':'R5900 little endian GNU libc',
        'locales':['C.utf8','en_US.utf8'],'localedef_sha256':sha(tool),
        'source_inputs':{name:sha(source/name) for name in ('locales/C','locales/en_US','charmaps/UTF-8.gz')},
        'files':{p:sha(target/p) for p in outputs},'bytes':sum((target/p).stat().st_size for p in outputs)}
    (work/'locales.json').write_text(json.dumps(report,indent=2)+'\n')
    return outputs

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--staging',type=Path,required=True)
    parser.add_argument('--sysroot',type=Path,required=True)
    parser.add_argument('--work',type=Path,required=True)
    parser.add_argument('--emulator-work',type=Path)
    parser.add_argument('--x11-sysroot',type=Path,help='SDK with matching libX11 locale definitions')
    args=parser.parse_args()
    files=build(args.staging.resolve(),args.sysroot.resolve(),args.work.resolve(),
        selected_emulator(args.emulator_work))
    if args.x11_sysroot:
        files+=install_x11(args.staging.resolve(),args.x11_sysroot.resolve(),args.work.resolve())
    print('Installed C.UTF-8 and en_US.UTF-8 locale data: '+str(len(files))+' files')

if __name__=='__main__':main()
