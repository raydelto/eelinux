"""Build the pinned current GNU development suite for R5900 Linux.

This supplements the qualified desktop in an isolated sysroot. Package archives
are fetched by the exact configs/native-tools-latest.json URL and checksum.
Completed packages are resumable, with their source and configure options saved.
"""
from pathlib import Path
import argparse,hashlib,json,os,re,shutil,subprocess,tarfile
from userland import validate_elf
from source_cache import locked_archive

ROOT=Path(__file__).resolve().parents[1]
DEFAULT_WORK=Path.home()/'.cache/eelinux/native-tools-latest'
PREFIX=str(Path.home()/'.local/eelinux-linux-latest/bin/mipsr5900el-unknown-linux-gnu-')
TUPLE='mipsr5900el-unknown-linux-gnu'
CPU=['-march=r5900','-mabi=32','-msoft-float','-mno-llsc','-mfix-r5900']
ORDER=['libffi','readline','xz','bash','bc','coreutils','cpio','diffutils','ed','findutils',
       'gawk','grep','gzip','sed','tar','patch','make','m4','bison','flex','perl',
       'autoconf','automake','libtool','pkgconf','nano','vim','screen','wget','lynx','gdb','python3']

def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def edit_text(path,text):
    if path.read_text()==text:return
    mode=path.stat().st_mode
    path.chmod(mode|0o200)
    path.write_text(text)
    path.chmod(mode)
def merge(source,dest):
    # cp handles overlapping shared-library symlinks without dereferencing them.
    dest.mkdir(parents=True,exist_ok=True)
    subprocess.run(['cp','-af','--reflink=auto',str(source)+'/.' ,str(dest)],check=True)

def audit_install(install):
    for directory,dirs,files in os.walk(install,followlinks=False):
        for filename in files:
            path=Path(directory)/filename
            if path.is_symlink():continue
            with path.open('rb') as stream:magic=stream.read(4)
            if magic==b'\x7fELF':validate_elf(path)

def extract(archive,dest,*,overlay=False):
    dest.mkdir(parents=True,exist_ok=True)
    with tarfile.open(archive) as stream:
        roots={m.name.split('/')[0] for m in stream.getmembers() if m.name not in ('.','')}
        if len(roots)!=1:raise RuntimeError('Archive must contain exactly one source root: '+str(archive))
        prefix=next(iter(roots))+'/'
        members=[]
        for member in stream.getmembers():
            if not member.name.startswith(prefix):continue
            member.name=member.name[len(prefix):]
            if member.name:members.append(member)
        stream.extractall(dest,members=members,filter='data')

def run(argv,cwd,env,log):
    log.write('\nCOMMAND '+repr(argv)+'\n');log.flush()
    subprocess.run(list(map(str,argv)),cwd=cwd,env=env,stdout=log,stderr=subprocess.STDOUT,check=True)

def main():
    global PREFIX
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--work',type=Path,default=DEFAULT_WORK)
    parser.add_argument('--sources',type=Path,default=ROOT/'userland/sources')
    parser.add_argument('--toolchain-prefix',default=PREFIX)
    parser.add_argument('--desktop-staging',type=Path,default=Path.home()/'.cache/eelinux/desktop-xorg/output-desktop/staging')
    parser.add_argument('--host-tools',type=Path,default=Path.home()/'.cache/eelinux/desktop-xorg/output-desktop/host')
    parser.add_argument('--dependencies-report',type=Path,default=ROOT/'build/userland/native-tools-dependencies/build.json')
    parser.add_argument('--compiler-report',type=Path,default=ROOT/'build/userland/native-compiler/build.json')
    parser.add_argument('--reconfigure',nargs='+',choices=ORDER,default=[])
    parser.add_argument('--inventory',type=Path,default=ROOT/'configs/native-tools-latest.json')
    parser.add_argument('--jobs',type=int,default=3)
    parser.add_argument('--packages',nargs='+',choices=ORDER)
    args=parser.parse_args();work=args.work.resolve();work.mkdir(parents=True,exist_ok=True)
    if args.jobs<1:parser.error('--jobs must be positive')
    PREFIX=args.toolchain_prefix
    inventory=json.loads(args.inventory.read_text());sysroot=work/'sysroot';target=work/'target'
    dependencies=json.loads(args.dependencies_report.read_text())
    compiler=json.loads(args.compiler_report.read_text())
    if not (work/'sdk-imported.json').exists():
        merge(args.desktop_staging,sysroot)
        merge(Path(dependencies['staging']),sysroot)
        for directory in ('usr','lib'):
            merge(Path(compiler['target'])/directory,sysroot/directory)
        shutil.copy2(ROOT/'userland/configs/native-gcc.specs',
                     sysroot/'usr/lib/gcc/mipsr5900el-unknown-linux-gnu/16.2.0/specs')
        for directory in ('usr','lib','etc/ssl'):
            origin=Path(dependencies['target'])/directory
            if origin.exists():merge(origin,target/directory)
        (work/'sdk-imported.json').write_text(json.dumps({'dependencies':dependencies,'compiler':compiler},indent=2)+'\n')
    host=args.host_tools
    env=os.environ.copy();env.update(LC_ALL='C',CONFIG_SHELL='/bin/bash',MAKEINFO='true',
        CC=PREFIX+'gcc --sysroot='+str(sysroot),CXX=PREFIX+'g++ --sysroot='+str(sysroot),
        AR=PREFIX+'ar',AS=PREFIX+'as',LD=PREFIX+'ld',RANLIB=PREFIX+'ranlib',STRIP=PREFIX+'strip',
        CFLAGS='-Os -g0 -std=gnu17 -D_FILE_OFFSET_BITS=64 '+' '.join(CPU),
        CXXFLAGS='-Os -g0 -D_FILE_OFFSET_BITS=64 '+' '.join(CPU),
        CPPFLAGS='-I'+str(sysroot/'usr/include'),LDFLAGS='-Wl,-rpath-link,'+str(sysroot/'usr/lib'),
        PKG_CONFIG=str(host/'bin/pkgconf'),PKG_CONFIG_SYSROOT_DIR=str(sysroot),
        PKG_CONFIG_LIBDIR=str(sysroot/'usr/lib/pkgconfig')+':'+str(sysroot/'usr/share/pkgconfig'),
        CC_FOR_BUILD='gcc',CXX_FOR_BUILD='g++',BUILD_CC='gcc',
        # Same C dialect as the target build: GCC 15+ hosts default to C23,
        # which breaks build helpers such as bash's mkbuiltins (bool typedef).
        BUILD_CFLAGS='-O2 -std=gnu17',CFLAGS_FOR_BUILD='-O2 -std=gnu17',
        FORCE_UNSAFE_CONFIGURE='1')
    env['PATH']=str(host/'bin')+':'+str(Path(PREFIX).parent)+':'+env['PATH']
    for name in args.packages or ORDER:
        spec=inventory[name]
        archive=locked_archive(spec['filename'],spec['url'],spec['sha256'],
                               work/'downloads',args.sources)
        source=work/'sources'/name;build=work/'build'/name;build.mkdir(parents=True,exist_ok=True)
        install=work/'install'/name;install.mkdir(parents=True,exist_ok=True)
        options=['--prefix=/usr','--build=x86_64-pc-linux-gnu','--host='+TUPLE,'--disable-nls']
        local=env.copy();subdir=source;makeflags=[]
        recipe=json.loads((ROOT/'userland/package'/name/'recipe.json').read_text())
        if recipe['source']!=spec:raise RuntimeError(name+': recipe source differs from inventory')
        substitutions={'sysroot':str(sysroot)}
        options += [value.format(**substitutions) for value in recipe['configure_options']]
        local.update({key:value.format(**substitutions) for key,value in recipe['configure_environment'].items()})
        if recipe['source_subdirectory']:subdir=source/recipe['source_subdirectory']
        if recipe['build_jobs']:makeflags=['-j'+str(recipe['build_jobs'])]
        record={'source':spec,'options':options,'cpu':CPU,'special_recipe':3 if name=='python3' else 2 if name in ('perl','ed') else 1}
        patches=recipe.get('patches',[])
        if patches:record['patches']=patches
        done=build/'installed.json'
        if done.exists():
            previous=json.loads(done.read_text())
            if name not in args.reconfigure:
                if previous!=record:raise RuntimeError(name+': completed inputs changed; use --reconfigure for the same source or a new work directory')
                audit_install(install)
                print(name+' '+spec['version']+' already built',flush=True);continue
            if previous['source']!=spec:raise RuntimeError(name+': source changed; use a new work directory')
            done.replace(build/('installed-before-'+sha(done)+'.json'))
        if not source.exists():extract(archive,source)
        if patches:
            applied=source/'.eelinux-patches.json'
            if applied.exists():
                if json.loads(applied.read_text())!=patches:raise RuntimeError(name+': source patches changed; use a new work directory')
            else:
                for patch in patches:
                    path=ROOT/patch['path']
                    if sha(path)!=patch['sha256']:raise RuntimeError(name+': source patch checksum mismatch')
                    strip=patch.get('strip',1)
                    if not isinstance(strip,int) or not 0<=strip<=3:raise RuntimeError('Invalid source patch strip level')
                    subprocess.run(['patch','--batch','--forward','--fuzz=0','-p'+str(strip),'-i',str(path)],cwd=source,check=True)
                applied.write_text(json.dumps(patches,indent=2)+'\n')
        print('Building '+name+' '+spec['version'],flush=True)
        with (build/'build.log').open('a',buffering=1) as log:
            if name=='python3':build_python(source,build,install,sysroot,local,args.jobs,log)
            elif name=='perl':build_perl(source,install,inventory,args.sources,local,args.jobs,log,
                                         download_cache=work/'downloads')
            else:
                # Some upstream Makefiles do not support out-of-source builds.
                cwd=subdir if recipe['in_source_build'] else build
                configure_options=options
                if name=='ed':
                    # GNU ed's configure is not Autoconf: it takes compiler
                    # settings as arguments and otherwise defaults to host gcc.
                    configure_options=['--prefix=/usr',*['%s=%s'%(key,local[key]) for key in ('CC','CFLAGS','CPPFLAGS','LDFLAGS')]]
                run([subdir/'configure',*configure_options],cwd,local,log)
                if name=='bc':
                    # The release ships bytecode for its math library. Regenerating
                    # it would execute the newly built target bc on the workstation.
                    header=build/'bc/libmath.h';header.parent.mkdir(exist_ok=True)
                    shutil.copy2(source/'bc/libmath.h',header);os.utime(header,None)
                run(['make','-j'+str(args.jobs),*makeflags],cwd,local,log)
                run(['make','DESTDIR='+str(install),'install'],cwd,local,log)
        # GNU Readline saves replaced installation files as .old when a private
        # package is reconfigured. Keep those in build evidence, not the runtime
        # library directory where ldconfig could select the obsolete SONAME.
        if name=='readline':
            for path in (install/'usr/lib').glob('lib*.old'):
                base=path.with_suffix('')
                if path.name.startswith(('libreadline.','libhistory.')) and (base.is_file() or base.with_name(base.name+'.a').is_file()):
                    path.unlink()
        # Installed scripts must use native tools and paths, never the workstation sysroot.
        for folder in ('usr/bin','usr/lib/pkgconfig','usr/share/pkgconfig'):
            for path in (install/folder).glob('*'):
                if not path.is_file() or path.is_symlink():continue
                data=path.read_bytes()
                if data.startswith(b'\x7fELF'):continue
                if data.startswith(b'#!') or path.suffix=='.pc':
                    text=data.decode(errors='strict')
                    for tool in ('gcc','g++','ar','ld','ranlib','strip'):text=text.replace(PREFIX+tool,'/usr/bin/'+tool)
                    text=text.replace('--sysroot='+str(sysroot),'').replace(str(sysroot),'')
                    edit_text(path,text)
        if name=='perl':
            for pattern in ('**/Config.pm','**/Config_heavy.pl'):
                for path in (install/'usr/lib/perl5').glob(pattern):
                    text=path.read_text()
                    for tool in ('gcc','g++','ar','ld','ranlib','strip'):
                        text=text.replace(PREFIX+tool,'/usr/bin/'+tool)
                    edit_text(path,text.replace('--sysroot='+str(sysroot),'').replace(str(sysroot),''))
        audit_install(install)
        merge(install,sysroot);merge(install,target)
        done.write_text(json.dumps(record,indent=2)+'\n')
        print(name+' '+spec['version']+' installed',flush=True)
    (work/'build.json').write_text(json.dumps({'result':'built','target':str(target),'sysroot':str(sysroot),
        'packages':{name:inventory[name] for name in ORDER if (work/'build'/name/'installed.json').exists()},
        'physical_hardware_tested':False,'runtime_tested':False},indent=2)+'\n')

def build_perl(source,install,inventory,cache,env,jobs,log,download_cache=None):
    cross=inventory['perl-cross']
    archive=locked_archive(cross['filename'],cross['url'],cross['sha256'],
                           download_cache or source.parent/'downloads',cache)
    if not (source/'cnf').exists():extract(archive,source,overlay=True)
    run([source/'configure','--target='+TUPLE,'--target-tools-prefix='+PREFIX,'--prefix=/usr',
         '-Dcc='+env['CC'],'-Dld='+env['CC'],'-Dccflags='+env['CFLAGS'],
         '-Dldflags='+env['LDFLAGS']+' -lm','-Dosname=linux','-Dosvers=7.2.8',
         '-Dmydomain=','-Dmyhostname=ps2','-Dperladmin=root'],source,env,log)
    run(['make','-j'+str(jobs),'all'],source,env,log)
    run(['make','DESTDIR='+str(install),'install.perl','install.sym'],source,env,log)

def build_python(source,build,install,sysroot,env,jobs,log):
    patchlevel=(source/'Include/patchlevel.h').read_text()
    version='.'.join(re.search(r'^#define\s+PY_'+part+r'_VERSION\s+(\d+)',patchlevel,re.M)[1]
                     for part in ('MAJOR','MINOR'))
    host=build/'host';host.mkdir(exist_ok=True)
    hostenv=os.environ.copy();hostenv.update(CC='gcc',CXX='g++',LC_ALL='C')
    if not (host/'python').is_file():
        run([source/'configure','--without-ensurepip','--disable-test-modules'],host,hostenv,log)
        run(['make','-j'+str(jobs)],host,hostenv,log)
    cross=build/'target';cross.mkdir(exist_ok=True)
    env=env.copy();env.update(ac_cv_file__dev_ptmx='yes',ac_cv_file__dev_ptc='no',
        ac_cv_buggy_getaddrinfo='no',ac_cv_working_tzset='yes',ac_cv_little_endian_double='yes',
        LIBFFI_CFLAGS='-I'+str(sysroot/'usr/include'),LIBFFI_LIBS='-lffi',
        BZIP2_CFLAGS='-I'+str(sysroot/'usr/include'),BZIP2_LIBS='-lbz2',
        LIBREADLINE_CFLAGS='-I'+str(sysroot/'usr/include'),LIBREADLINE_LIBS='-lreadline -lncursesw')
    run([source/'configure','--prefix=/usr','--build=x86_64-pc-linux-gnu','--host='+TUPLE,
        '--with-build-python='+str(host/'python'),'--enable-shared','--without-ensurepip',
        '--disable-test-modules','--with-openssl='+str(sysroot/'usr'),'--with-readline=readline',
        '--with-system-expat'],cross,env,log)
    run(['make','-j'+str(jobs)],cross,env,log)
    run(['make','DESTDIR='+str(install),'COMPILEALL_OPTS=-j'+str(jobs),'install'],cross,env,log)
    for module in ('_ssl','_hashlib','_ctypes','_sqlite3','_curses','readline','_bz2','_lzma','_zstd','_gdbm'):
        if not list((install/('usr/lib/python'+version+'/lib-dynload')).glob(module+'.*.so')):
            raise RuntimeError('Required Python standard-library extension missing: '+module)
    for path in (install/('usr/lib/python'+version)).glob('_sysconfigdata*.py'):
        text=path.read_text().replace(env['CC'],'gcc').replace(env['CXX'],'g++')
        for tool in ('gcc','g++','ar','ld','ranlib','strip'):text=text.replace(PREFIX+tool,tool)
        text=text.replace('--sysroot='+str(sysroot),'').replace(str(sysroot),'')
        edit_text(path,text)

if __name__=='__main__':main()
