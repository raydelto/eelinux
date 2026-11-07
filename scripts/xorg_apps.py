"""Resolve and build native desktop applications from userland/package/x11r7."""
from pathlib import Path
import argparse,concurrent.futures,hashlib,json,os,re,shutil,subprocess,tarfile,urllib.request
from datetime import datetime,timezone
from source_cache import locked_archive
ROOT=Path(__file__).resolve().parents[1]
RECIPES=ROOT/'userland/package/x11r7'
LOCK=ROOT/'userland/xorg-apps-sources.json'
INDEX='https://xorg.freedesktop.org/archive/individual/app/'
EXCLUDE={
    **dict.fromkeys(('igt-gpu-tools','intel-gpu-tools','xf86dga','xvidtune','grandr'),'non-PS2 GPU/display tools'),
    **dict.fromkeys(('xdbedizzy','xphelloworld'),'extension test program'),
    **dict.fromkeys(('mkcfm','mkcomposecache','showfont','scripts'),'obsolete or unnecessary font/remote-session tool'),
    **dict.fromkeys(('xsetmode','xsetpointer'),'obsolete device configuration; xinput is retained'),
    'xinput_calibrator':'touchscreen calibration is unnecessary for this project',
    'transset':'requires a compositor that this desktop does not use',
    'xmh':'requires the absent MH mail backend',
    **dict.fromkeys(('compiz','xcompmgr'),'requires unfinished GLX/compositing'),
    **dict.fromkeys(('constype','xbacklight','xdriinfo','xgamma'),'hardware features unavailable in the native PS2 DDX'),
    **dict.fromkeys(('xfs','xfsinfo','fslsfonts','fstobdf','lbxproxy','proxymngr','xfindproxy','xfwp','rstart','xrx'),'obsolete remote proxy/font-server service'),
    **dict.fromkeys(('xplsprinters','xprehashprinterlist','xawprint','xprint','xtrap'),'obsolete Xprint/XTrap service'),
    'xdm':'unused graphical login service; GNU console login and startx are retained',
    'mkfontdir':'superseded by mkfontscale',
    'luit':'legacy encoding filter; UTF-8 xterm is used',
}
GUI={'beforelight','bitmap','editres','ico','oclock','viewres','xbiff','xgc',
     'xcalc','xclipboard','xclock','xconsole','xditview','xedit','xeyes','xfd',
     'xfontsel','xkill','xload','xlogo','xmag','xman','xmessage','xmore','xsm','xwud','xterm',
     'xkbvleds','xkbwatch'}
APPLETS={'oclock','xbiff','xclock','xeyes','xload','xlogo','xkbvleds','xkbwatch'}
MENU_HIDDEN={'bdftopcf','fonttosfnt','xcursorgen','sessreg','rendercheck','x11perf'}
def install_menu_entries(staging):
    entries=staging/'usr/share/applications';entries.mkdir(parents=True,exist_ok=True)
    lock=json.loads(LOCK.read_text());programs=[]
    for record in lock['applications']:
        name=record['program']
        names={'rgb':['showrgb'],'xkbutils':['xkbbell','xkbvleds','xkbwatch'],
               'xinit':['xinit','startx'],'xterm':['xterm','resize','uxterm']}.get(name,[name])
        for program in names:
            path=staging/'usr/bin'/program
            if not path.is_file():continue
            if program in MENU_HIDDEN:
                programs.append({'program':program,'version':record['version'],'desktop_entry':None})
                continue
            flags={'xclock':' -title Clock -analog -update 60 -fg black -bg white -hd black -hl black -geometry 144x144',
                   'xfd':' -fn fixed','xmessage':' -center "PS2 X.Org desktop"',
                   'xterm':' -name ps2-terminal','twm':' -version','xrandr':' --query',
                   'xgc':' -fn 6x10 -geometry 700x520+4+24'}.get(program,'')
            text='[Desktop Entry]\nType=Application\nName='+program+'\nExec='+program+flags+'\nTryExec='+program+'\n'
            text+='Terminal='+('false' if program in GUI else 'true')+'\nCategories=Utility;\n'
            text+='X-PS2-Menu='+('Applets' if program in APPLETS else 'Tools' if program not in GUI else 'Accessories')+'\n'
            (entries/('ps2-'+program+'.desktop')).write_text(text)
            programs.append({'program':program,'version':record['version'],'desktop_entry':'ps2-'+program+'.desktop'})
    destination=staging/'usr/share/eelinux';destination.mkdir(parents=True,exist_ok=True)
    (destination/'xorg-applications.json').write_text(json.dumps({'date':lock['date'],'applications':programs,
        'excluded_modules':lock['excluded'],'GS_only_rendering':True},indent=2)+'\n')
    return programs
def sha(path,algorithm='sha256'):return hashlib.new(algorithm,path.read_bytes()).hexdigest()
def fetch(url,path):
    if not path.exists():
        path.parent.mkdir(parents=True,exist_ok=True)
        urls=[url,url.replace('https://xorg.freedesktop.org/archive/','https://www.x.org/releases/')]
        for candidate in dict.fromkeys(urls):
            try:
                with urllib.request.urlopen(candidate,timeout=25) as response:data=response.read()
                temporary=path.with_suffix(path.suffix+'.part');temporary.write_bytes(data)
                with tarfile.open(temporary) as archive:archive.getmembers()
                temporary.replace(path);break
            except (OSError,tarfile.TarError):
                if candidate==urls[-1]:raise
    return path
def resolve():
    listing=urllib.request.urlopen(INDEX,timeout=90).read().decode()
    latest={}
    for name,version,extension in re.findall(r'href="([a-zA-Z0-9_-]+)-(\d+(?:\.\d+)+)\.tar\.(xz|bz2|gz)"',listing):
        tag=tuple(map(int,version.split('.')))+(dict(gz=0,bz2=1,xz=2)[extension],)
        if name not in latest or tag>latest[name][0]:latest[name]=(tag,version,extension)
    assert len(latest)>50,'Incomplete official X.Org application index'
    sources=[];excluded={}
    def one(item,base=INDEX,kind='application'):
        name,(_,version,extension)=item
        filename=f'{name}-{version}.tgz' if name=='xterm' else f'{name}-{version}.tar.{extension}'
        archive=fetch(base+filename,ROOT/'userland/sources'/filename)
        folder=('xapp_' if kind=='application' else 'xlib_' if kind=='library' else 'xutil_')+name
        recipe_dir=RECIPES/folder;recipe_dir.mkdir(parents=True,exist_ok=True)
        record={'schema':1,'name':'x11r7/'+folder,'kind':kind,'program':name,'version':version,
            'engine':'eeLinux native X.Org application builder; scripts/xorg_apps.py',
            'source':'sources/'+filename,'upstream':base+filename,
            'source_hashes':[{'algorithm':a,'digest':sha(archive,a),'filename':filename} for a in ('sha256','sha512')],
            'target':'R5900 Linux o32 soft float','runtime_qualified':False}
        (recipe_dir/'recipe.json').write_text(json.dumps(record,indent=2)+'\n')
        (recipe_dir/(folder+'.hash')).write_text('# Calculated from the official HTTPS release archive on 2026-10-09\n'+
            ''.join(f"{p['algorithm']}  {p['digest']}  {filename}\n" for p in record['source_hashes']))
        with tarfile.open(archive) as tar:
            entries=tar.getnames();engine='autotools' if any(p.endswith('/configure') for p in entries) else 'meson'
        variable=folder.upper()
        license_name='GPL-3.0-or-later' if name=='groff' else 'MIT'
        recipe_text=f'# Current upstream {name}; consumed by scripts/xorg_apps.py\n{variable}_VERSION = {version}\n{variable}_SOURCE = {filename}\n{variable}_SITE = {base}\n{variable}_LICENSE = {license_name}\n{variable}_LICENSE_FILES = COPYING\n\n$(eval $({engine}-package))\n'
        (recipe_dir/(folder+'.mk')).write_text(recipe_text)
        (recipe_dir/'buildroot-recipe.txt').write_text(recipe_text)
        return record
    selected=[]
    for item in sorted(latest.items()):
        if item[0] in EXCLUDE:excluded[item[0]]=EXCLUDE[item[0]]
        else:selected.append(item)
    with concurrent.futures.ThreadPoolExecutor(max_workers=6) as pool:
        for record in pool.map(one,selected):
            sources.append(record);print(record['program']+' '+record['version'],flush=True)
    lib_index=INDEX.replace('/app/','/lib/')
    listing=urllib.request.urlopen(lib_index,timeout=90).read().decode()
    libraries=[]
    for name in ('libXmu','libXScrnSaver','libXv'):
        releases=re.findall(r'href="('+name+r')-(\d+(?:\.\d+)+)\.tar\.(xz|bz2|gz)"',listing)
        release=max(releases,key=lambda r:(tuple(map(int,r[1].split('.'))),dict(gz=0,bz2=1,xz=2)[r[2]]))
        libraries.append(one((release[0],(None,release[1],release[2])),lib_index,'library'))
    # xcursorgen needs the separately locked PNG file-format library.
    png_lock=ROOT/'userland/libpng-sources.json'
    if png_lock.exists():libraries.append(json.loads(png_lock.read_text()))
    xterm_index='https://invisible-island.net/archives/xterm/current/'
    listing=urllib.request.urlopen(xterm_index,timeout=30).read().decode()
    version=str(max(map(int,re.findall(r'href="xterm-(\d+)\.tgz"',listing))))
    sources.append(one(('xterm',(None,version,'gz')),xterm_index))
    groff_index='https://ftp.gnu.org/gnu/groff/'
    listing=urllib.request.urlopen(groff_index,timeout=30).read().decode()
    versions=re.findall(r'href="groff-(\d+(?:\.\d+)+)\.tar\.gz"',listing)
    version=max(versions,key=lambda v:tuple(map(int,v.split('.'))))
    runtime=[one(('groff',(None,version,'gz')),groff_index,'runtime dependency')]
    LOCK.write_text(json.dumps({'schema':1,'date':datetime.now(timezone.utc).date().isoformat(),'upstream_index':INDEX,
        'recipe_directory':'userland/package/x11r7','libraries':libraries,'runtime_dependencies':runtime,
        'applications':sources,'excluded':excluded},indent=2)+'\n')
    print(str(len(sources))+' current application releases locked; '+str(len(excluded))+' irrelevant modules excluded.')
def build(work,sysroot,prefix,staging,jobs,only=None):
    import userland
    userland.use_native_linux_path();work.mkdir(parents=True,exist_ok=True)
    env=dict(os.environ)
    env.update(CC=prefix+'gcc --sysroot='+str(sysroot),AR=prefix+'ar',RANLIB=prefix+'ranlib',STRIP=prefix+'strip',
        CXX=prefix+'g++ --sysroot='+str(sysroot),CXXFLAGS='-Os -std=gnu++17 -march=r5900 -mabi=32 -msoft-float -mno-llsc -mfix-r5900',
        PKG_CONFIG='pkg-config',PKG_CONFIG_SYSROOT_DIR=str(sysroot),PKG_CONFIG_LIBDIR=str(sysroot/'usr/lib/pkgconfig')+':'+str(sysroot/'usr/share/pkgconfig'),
        CFLAGS='-Os -std=gnu17 -march=r5900 -mabi=32 -msoft-float -mno-llsc -mfix-r5900',
        LDFLAGS='-Wl,-rpath-link,'+str(sysroot/'usr/lib'),LIBS='-lm',
        ac_cv_func_malloc_0_nonnull='yes',ac_cv_func_realloc_0_nonnull='yes')
    env['ac_cv_file__etc_man_conf']='no'
    env['ac_cv_file__etc_man_config']='no'
    env['ac_cv_file__etc_manpath_config']='no'
    env['ac_cv_file__usr_share_misc_man_conf']='no'
    env['ac_cv_file__usr_share_man_man7_attributes_7']='no'
    locked=json.loads(LOCK.read_text());results=[]
    ordered=sorted(locked['applications'],key=lambda item:(item['program']!='xbitmaps',item['program']))
    selected=set(only or [])
    known={entry['program'] for entry in locked.get('libraries',[])+locked.get('runtime_dependencies',[])+ordered}
    if selected-known:raise ValueError('Unknown X.Org build selection: '+', '.join(sorted(selected-known)))
    for entry in locked.get('libraries',[])+locked.get('runtime_dependencies',[])+ordered:
        if selected and entry['program'] not in selected:continue
        name=entry['program'];recipe=ROOT/'userland/package'/entry['name']/'recipe.json'
        record=json.loads(recipe.read_text());assert record==entry
        source_lock=next(item for item in record['source_hashes'] if item['algorithm']=='sha256')
        archive=locked_archive(source_lock['filename'],record['upstream'],source_lock['digest'],
                               work/'downloads',ROOT/'userland/sources')
        package=work/name;package.mkdir(exist_ok=True)
        source=package/(name+'-'+record['version'])
        if not source.exists():
            with tarfile.open(archive) as tar:tar.extractall(package,filter='data')
        patches=sorted(recipe.parent.glob('*.patch'))
        patch_inputs={p.name:sha(p) for p in patches}
        patch_stamp=package/'source-patches.json'
        if patch_stamp.exists():
            if json.loads(patch_stamp.read_text())!=patch_inputs:
                raise RuntimeError(name+': patches changed; use a fresh --work directory')
        else:
            for patch in patches:
                subprocess.run(['patch','--batch','--forward','-p1','--dry-run','-i',str(patch)],cwd=source,check=True,capture_output=True)
                subprocess.run(['patch','--batch','--forward','-p1','-i',str(patch)],cwd=source,check=True,capture_output=True)
            patch_stamp.write_text(json.dumps(patch_inputs,indent=2)+'\n')
        log_path=package/'build.log';stamp=package/'installed.json'
        tag={'recipe_sha256':sha(recipe),'staging':str(staging),'compiler':prefix,'sysroot':str(sysroot),
             'builder_sha256':sha(Path(__file__)),'patches':patch_inputs,'environment':{k:env[k] for k in ('CFLAGS','CXXFLAGS','LIBS')}}
        if stamp.exists() and json.loads(stamp.read_text())['inputs']==tag:
            results.append(json.loads(stamp.read_text()));print(name+': cached',flush=True);continue
        print(name+': building '+record['version'],flush=True)
        with log_path.open('w',buffering=1) as log:
            if (source/'configure').exists():
                help_text=subprocess.check_output([str(source/'configure'),'--help'],text=True)
                options=['--prefix=/usr','--sysconfdir=/etc','--localstatedir=/var','--host=mipsr5900el-unknown-linux-gnu','--build=x86_64-pc-linux-gnu','--disable-static']
                if '--with-appdefaultdir' in help_text:options+=['--with-appdefaultdir=/usr/share/X11/app-defaults']
                if name=='groff':options+=['--without-x']
                if name=='libpng':
                    # R5900 MMI is not the Loongson MMI or MIPS MSA ISA.
                    for simd in ('mips-msa','mips-mmi'):
                        if '--enable-'+simd in help_text:options+=['--enable-'+simd+'=no']
                for feature in ('xrender','xft','present'):
                    if '--with-'+feature in help_text or '--without-'+feature in help_text:options.append('--without-'+feature)
                    elif '--enable-'+feature in help_text or '--disable-'+feature in help_text:options.append('--disable-'+feature)
                subprocess.run([str(source/'configure'),*options],cwd=source,env=env,stdout=log,stderr=log,check=True)
                host_options=[]
                if name=='groff':
                    # Runtime manuals and macro/font data are retained. Omit
                    # processed upstream examples, which execute target groff
                    # and require a host PostScript/HTML publication toolchain.
                    variables=re.findall(r'^(\w+_DATA)\s*=',(source/'Makefile').read_text(),re.M)
                    host_options=[v+'=' for v in variables if
                        (v.startswith('nodist_') and ('example' in v or 'doc' in v)) or
                        v in ('momhtml_DATA','htmlpic_DATA')]+['PDFDOCFILE=']
                    with (source/'Makefile').open('a') as generated:
                        generated.write('\n# PS2 runtime omits processed publication examples.\n'
                            'install_mom install_doc_htmldoc install_doc_gnu_eps install_doc_examples:\n\t@:\n')
                subprocess.run(['make','-j'+str(jobs),*host_options],cwd=source,env=env,stdout=log,stderr=log,check=True)
                payload=package/'payload'
                if payload.exists():
                    if payload.resolve().parent!=package.resolve():raise RuntimeError('Unsafe package payload')
                    shutil.rmtree(payload)
                payload.mkdir()
                subprocess.run(['make',*host_options,'DESTDIR='+str(payload),'install'],cwd=source,env=env,stdout=log,stderr=log,check=True)
                subprocess.run(['cp','-a',str(payload)+'/.',str(staging)],check=True)
                if entry.get('kind')=='library' or name=='xbitmaps':
                    subprocess.run(['make','DESTDIR='+str(sysroot),'install'],cwd=source,env=env,stdout=log,stderr=log,check=True)
            else:raise RuntimeError(name+': upstream build-system integration required; see '+str(source))
        result={'program':name,'version':record['version'],'result':'built','inputs':tag,'build_log_sha256':sha(log_path),
                'installed_files':sorted(p.relative_to(payload).as_posix() for p in payload.rglob('*') if p.is_file() or p.is_symlink())}
        stamp.write_text(json.dumps(result,indent=2)+'\n');results.append(result)
    (work/'applications-build.json').write_text(json.dumps({'result':'built','applications':results,'staging':str(staging),'runtime_qualified':False},indent=2)+'\n')
    install_menu_entries(staging)
def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('command',choices=('resolve','build'))
    parser.add_argument('--work',type=Path);parser.add_argument('--sysroot',type=Path);parser.add_argument('--toolchain-prefix')
    parser.add_argument('--staging',type=Path);parser.add_argument('--jobs',type=int,default=4)
    parser.add_argument('--only',nargs='+',help='build selected locked applications/libraries into an isolated staging tree')
    args=parser.parse_args()
    if args.command=='resolve':resolve()
    else:
        assert args.work and args.sysroot and args.toolchain_prefix and args.staging
        build(args.work.resolve(),args.sysroot.resolve(),args.toolchain_prefix,args.staging.resolve(),args.jobs,args.only)
if __name__=='__main__':main()

