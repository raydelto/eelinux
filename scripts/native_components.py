"""Build isolated native compiler or library components using the project settings."""
from pathlib import Path
import argparse,copy,json,os,shutil,subprocess
import userland

ROOT=Path(__file__).resolve().parents[1]
LIBRARY_SYMBOLS=['BR2_PACKAGE_OPENSSL','BR2_PACKAGE_LIBOPENSSL',
    'BR2_PACKAGE_EELINUX_OPENSSL3_COMPAT','BR2_PACKAGE_LIBFFI',
    'BR2_PACKAGE_LIBOPENSSL_ENABLE_DES','BR2_PACKAGE_LIBOPENSSL_ENABLE_MD4',
    'BR2_PACKAGE_LIBOPENSSL_ENGINES','BR2_PACKAGE_LIBOPENSSL_BIN',
    'BR2_PACKAGE_SQLITE','BR2_PACKAGE_ZSTD','BR2_PACKAGE_CA_CERTIFICATES','BR2_PACKAGE_FILE',
    'BR2_PACKAGE_NCURSES','BR2_PACKAGE_NCURSES_WCHAR','BR2_PACKAGE_BZIP2','BR2_PACKAGE_GDBM',
    'BR2_PACKAGE_GMP','BR2_PACKAGE_MPFR']
LIBRARY_TARGETS=['libopenssl','eelinux-openssl3-compat','libffi','sqlite','zstd',
    'ca-certificates','file','ncurses','bzip2','gdbm','gmp','mpfr']

def component_symbols(data,kind):
    if kind=='compiler':return ['BR2_PACKAGE_EELINUX_GCC','BR2_PACKAGE_EELINUX_BINUTILS']
    # These libraries replace the desktop providers during full assembly.
    # Carry their selected features too, so that replacement keeps the crypto,
    # SQL and compression capabilities used by the command-line tools.
    selected=userland.selected_symbols(data,'desktop')
    options=[symbol for symbol in selected if
        symbol.startswith(('BR2_PACKAGE_LIBOPENSSL_','BR2_PACKAGE_SQLITE_')) or
        symbol in ('BR2_PACKAGE_LZ4','BR2_PACKAGE_LZ4_PROGS','BR2_PACKAGE_XZ','BR2_PACKAGE_ZLIB')]
    return sorted(set(LIBRARY_SYMBOLS+options))

def seed_sources(work,data,sources):
    for name,spec in data['packages'].items():
        for record in spec.get('source_hashes',[]):
            origin=sources/record['filename']
            if not origin.is_file():continue
            import hashlib
            with origin.open('rb') as stream:
                actual=hashlib.file_digest(stream,record['algorithm']).hexdigest()
            if actual!=record['digest']:raise userland.BuildError('Source checksum mismatch: '+str(origin))
            destination=work/'downloads'/name/origin.name
            destination.parent.mkdir(parents=True,exist_ok=True)
            if not destination.exists():shutil.copy2(origin,destination)

def build(kind,work,prefix,jobs,sources,report):
    userland.use_native_linux_path()
    data=copy.deepcopy(userland.manifest())
    symbols=component_symbols(data,kind)
    data['profiles']['base']['symbols']=[]
    data['profiles']['desktop']['symbols']=[]
    data['profiles']['development']['symbols']=symbols
    work=work.resolve();work.mkdir(parents=True,exist_ok=True)
    report.parent.mkdir(parents=True,exist_ok=True)
    seed_sources(work,data,sources)
    with report.with_suffix('.log').open('a',buffering=1) as log:
        saved_out=os.dup(1);saved_err=os.dup(2)
        os.dup2(log.fileno(),1);os.dup2(log.fileno(),2)
        try:
            info=userland.toolchain_probe(prefix,work,'full')
            make,output,lock=userland.prepare_config(data,info,'full',work)
            (output/'eelinux-build-lock.json').write_text(json.dumps(lock,indent=2)+'\n')
            targets=['eelinux-gcc','eelinux-binutils'] if kind=='compiler' else LIBRARY_TARGETS
            subprocess.run([*make,f'BR2_JLEVEL={jobs}',f'-j{jobs}',*targets],check=True)
            target=output/'target';staging=output/'host/mipsel-buildroot-linux-gnu/sysroot'
            if kind=='compiler':
                userland.export_development_files(target,staging,info)
                userland.import_libc_tools(target,info,'full')
                specs=target/'usr/lib/gcc'/info['tuple']/info['version']/'specs'
                shutil.copy2(ROOT/'userland/configs/native-gcc.specs',specs)
                for name in ('gcc','g++','as','ld','ar','objcopy','objdump','readelf','nm','strip','ranlib'):
                    userland.validate_elf(target/'usr/bin'/name)
            result={'result':'built','component':kind,'target':str(target),'staging':str(staging),
                'output':str(output),'toolchain':info,'lock':lock,'jobs':jobs,
                'runtime_tested':False,'physical_hardware_tested':False}
            report.write_text(json.dumps(result,indent=2)+'\n')
        finally:
            os.dup2(saved_out,1);os.dup2(saved_err,2);os.close(saved_out);os.close(saved_err)
    print('Built native '+kind+' component: '+str(report),flush=True)
    return result

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('component',choices=['compiler','libraries'])
    parser.add_argument('--work',type=Path,required=True)
    parser.add_argument('--toolchain-prefix',required=True)
    parser.add_argument('--jobs',type=int,default=2)
    parser.add_argument('--sources',type=Path,default=ROOT/'userland/sources')
    parser.add_argument('--report',type=Path,required=True)
    args=parser.parse_args()
    if args.jobs<1:parser.error('--jobs must be positive')
    build(args.component,args.work,args.toolchain_prefix,args.jobs,args.sources,args.report)

if __name__=='__main__':main()
