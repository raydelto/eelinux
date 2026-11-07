"""Build and safely strip the complete native PS2 development userland."""
from pathlib import Path
import argparse,hashlib,json,os,shutil,subprocess,sys
import audit_target_abi,native_components,native_development,native_xorg,rootfs,userland
from strip_userland import strip_tree

ROOT=Path(__file__).resolve().parents[1]
DIAGNOSTIC_NAMES={'ps2-xorg-session-test','xorg-core2d-probe','S99xorgcore2d',
    'native-sdk-check.sh','native-development-probe','S99nativedevelopment'}

def sha(path):
    with path.open('rb') as stream:return hashlib.file_digest(stream,'sha256').hexdigest()

def merge(source,target):native_development.merge(source,target)

def install_certificates(stage):
    """Finalize the locked CA payload even when only package targets were built."""
    source=stage/'usr/share/ca-certificates'
    certificates=sorted(source.rglob('*.crt'))
    if not certificates:raise RuntimeError('CA certificate payload is missing')
    destination=stage/'etc/ssl/certs';destination.mkdir(parents=True,exist_ok=True)
    bundle=destination/'ca-certificates.crt'
    contents=[]
    for certificate in certificates:
        data=certificate.read_bytes()
        if data.count(b'-----BEGIN CERTIFICATE-----')!=1:
            raise RuntimeError('Unexpected CA certificate format: '+str(certificate))
        contents.append(data.rstrip()+b'\n')
        link=destination/(certificate.stem+'.pem')
        if link.exists() or link.is_symlink():link.unlink()
        link.symlink_to(os.path.relpath(certificate,destination))
    temporary=destination/'.ca-certificates.crt.tmp'
    temporary.write_bytes(b''.join(contents));temporary.chmod(0o644)
    if bundle.exists() or bundle.is_symlink():bundle.unlink()
    subprocess.run(['openssl','rehash',str(destination)],check=True)
    temporary.replace(bundle)
    default=stage/'etc/ssl/cert.pem'
    if default.exists() or default.is_symlink():default.unlink()
    default.symlink_to('certs/ca-certificates.crt')
    return {'certificates':len(certificates),'bundle_sha256':sha(bundle),
        'default_CA_file':'/etc/ssl/cert.pem','hash_directory':'/etc/ssl/certs'}

def install_kernel_modules(stage,report_path):
    report=json.loads(report_path.read_text())
    output=Path(report['output']);source=Path(report['source'])
    if report.get('warning_count')!=0 or not report.get('clean_full_build'):
        raise RuntimeError('Kernel module installation requires a clean kernel build')
    subprocess.run(['make','-s','-C',str(source),'O='+str(output),
        'ARCH=mips','INSTALL_MOD_PATH='+str(stage),'DEPMOD=/bin/true','modules_install'],check=True)
    release=report['kernel_release'];destination=stage/'lib/modules'/release
    for name in ('build','source'):
        path=destination/name
        if path.is_symlink():path.unlink()
    installed={}
    for relative in report['loadable_modules']:
        original=output/relative;module=destination/'kernel'/relative
        if sha(original)!=sha(module):raise RuntimeError('Kernel module changed during installation: '+relative)
        installed[relative]=sha(module)
    subprocess.run(['depmod','-b',str(stage),'-F',str(output/'System.map'),release],check=True)
    firmware_path=str(report['firmware_directory'])
    if len(firmware_path)>2 and firmware_path[1:3] in (':\\',':/'):
        firmware_path='/mnt/'+firmware_path[0].lower()+'/'+firmware_path[3:].replace('\\','/')
    firmware=Path(firmware_path);manifest_path=firmware/'manifest.json'
    if sha(manifest_path)!=report['firmware_manifest_sha256']:
        raise RuntimeError('IOP firmware manifest differs from the kernel build')
    manifest=json.loads(manifest_path.read_text());firmware_hashes=manifest['firmware_sha256']
    destination=stage/'lib/firmware/ps2';destination.mkdir(parents=True,exist_ok=True)
    for name,expected in firmware_hashes.items():
        if Path(name).name!=name or sha(firmware/name)!=expected:
            raise RuntimeError('Firmware manifest mismatch: '+name)
        shutil.copy2(firmware/name,destination/name)
    shutil.copy2(manifest_path,destination/'manifest.json')
    shutil.copy2(manifest_path,stage/'usr/share/eelinux/iop-firmware-manifest.json')
    return {'kernel_release':release,'modules':installed,'kernel_report_sha256':sha(report_path),
        'IOP_firmware_manifest_sha256':sha(manifest_path),'IOP_firmware':firmware_hashes}

def clean_diagnostics(stage):
    removed=[]
    for directory in ('usr/bin','usr/lib/eelinux','etc/init.d','etc/rc.d/rcS.d'):
        for path in (stage/directory).glob('*'):
            name=path.name
            diagnostic=(name.startswith('ps2-gs') and name.endswith('-test') or
                name in DIAGNOSTIC_NAMES or
                name.startswith('S99gs') or name.startswith('gs-') and name.endswith('-probe'))
            if diagnostic and (path.is_file() or path.is_symlink()):
                removed.append(path.relative_to(stage).as_posix());path.unlink()
    for relative in ('var/lib/ps2-xorg-test','var/lib/ps2-native-sdk-test'):
        path=stage/relative
        if path.exists():
            if path.is_symlink() or not path.resolve().is_relative_to(stage.resolve()):
                raise RuntimeError('Unsafe diagnostic output path: '+relative)
            shutil.rmtree(path);removed.append(relative)
    return removed

def assemble(stage,desktop,compiler,dependencies,tools,prefix):
    stage=stage.resolve()
    if stage.exists():raise RuntimeError('Use a new staging directory; completed stages are preserved: '+str(stage))
    stage.parent.mkdir(parents=True,exist_ok=True);merge(desktop,stage)
    for origin in (Path(compiler['target']),Path(dependencies['target']),Path(tools['target'])):
        for relative in ('usr','lib','etc/ssl'):
            if (origin/relative).exists():merge(origin/relative,stage/relative)
    # Export headers, crt objects and archives from the combined, matching SDK.
    userland.export_development_files(stage,Path(tools['sysroot']),compiler['toolchain'])
    for relative in ('usr/lib/pkgconfig','usr/share/pkgconfig'):
        origin=Path(tools['sysroot'])/relative
        if origin.is_dir():merge(origin,stage/relative)
    shutil.copy2(ROOT/'userland/configs/native-gcc.specs',
        stage/'usr/lib/gcc/mipsr5900el-unknown-linux-gnu/16.2.0/specs')
    # Boot scripts use /bin paths. Redirect previous providers to the latest
    # /usr/bin installation instead of leaving an older executable selected.
    for new in (Path(tools['target'])/'usr/bin').iterdir():
        for directory in ('bin','sbin'):
            old=stage/directory/new.name
            if old.is_file() or old.is_symlink():
                old.unlink();old.symlink_to(os.path.relpath('/usr/bin/'+new.name,'/'+directory))
    for folder in ('usr/lib/pkgconfig','usr/share/pkgconfig'):
        for path in (stage/folder).glob('*.pc'):
            if path.is_symlink():continue
            text=path.read_text()
            for base in (tools['sysroot'],dependencies['staging'],str(desktop)):
                text=text.replace(base,'')
            native_development.edit_text(path,text)
    (stage/'var/tmp').mkdir(parents=True,exist_ok=True);(stage/'var/tmp').chmod(0o1777)
    return clean_diagnostics(stage)

def plan(args):
    inventory=json.loads((ROOT/'configs/native-tools-latest.json').read_text())
    print(json.dumps({'profile':'full','settings':{'work':str(args.work),'staging':str(args.sysroot),
        'toolchain_prefix':args.toolchain_prefix,'jobs':args.jobs},'steps':[
        'Build the selected base and Xorg desktop with configs/packages.json',
        'Build native GCC/G++, binutils and required libraries with the same Linux toolchain',
        'Build the locked current tools from userland/package/*/recipe.json',
        'Build the production PS2 DDX and Mesa GS components',
        'Merge the complete SDK, remove diagnostics, strip safely and audit the result'],
        'latest_packages':{name:spec['version'] for name,spec in inventory.items()},
        'runtime_and_hardware_qualification':'Separate checks; plan does not certify a build'},indent=2))

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--work',type=Path,required=True);parser.add_argument('--sysroot',type=Path,required=True)
    parser.add_argument('--toolchain-prefix',required=True);parser.add_argument('--jobs',type=int,default=2)
    parser.add_argument('--profile',choices=['full'],default='full');parser.add_argument('--plan',action='store_true')
    parser.add_argument('--sources',type=Path,default=ROOT/'userland/sources')
    parser.add_argument('--kernel-report',type=Path,help='Install matching modules from an existing clean kernel build')
    # Explicit reuse keeps historical build locks intact; default builds are fresh.
    for name in ('desktop-report','compiler-report','dependencies-report','tools-report'):
        parser.add_argument('--reuse-'+name,type=Path)
    args=parser.parse_args()
    if args.jobs<1:parser.error('--jobs must be positive')
    if args.plan:plan(args);return
    userland.use_native_linux_path();work=args.work.resolve();work.mkdir(parents=True,exist_ok=True)
    if args.sysroot.exists():raise RuntimeError('Output staging already exists; preserve it and choose a new --sysroot')
    desktop_report=args.reuse_desktop_report
    if desktop_report is None:
        userland.build(argparse.Namespace(work=work/'desktop',sysroot=work/'desktop-stage',
            profile='desktop',toolchain_prefix=args.toolchain_prefix,jobs=args.jobs))
        desktop_report=work/'desktop/output-desktop/eelinux-build-report.json'
    desktop=json.loads(desktop_report.read_text());desktop_output=desktop_report.parent
    desktop_stage=Path(desktop['output_sysroot'])
    compiler_report=args.reuse_compiler_report or work/'compiler.json'
    if args.reuse_compiler_report is None:
        native_components.build('compiler',work/'compiler',args.toolchain_prefix,args.jobs,args.sources,compiler_report)
    dependencies_report=args.reuse_dependencies_report or work/'libraries.json'
    if args.reuse_dependencies_report is None:
        native_components.build('libraries',work/'libraries',args.toolchain_prefix,args.jobs,args.sources,dependencies_report)
    compiler=json.loads(compiler_report.read_text());dependencies=json.loads(dependencies_report.read_text())
    if compiler['result']!='built' or dependencies['result']!='built':raise RuntimeError('Incomplete component build')
    tools_report=args.reuse_tools_report
    if tools_report is None:
        command=[sys.executable,str(ROOT/'scripts/native_development.py'),'--work',str(work/'tools'),
            '--sources',str(args.sources),'--jobs',str(args.jobs),'--toolchain-prefix',args.toolchain_prefix,
            '--desktop-staging',str(desktop_output/'staging'),'--host-tools',str(desktop_output/'host'),
            '--compiler-report',str(compiler_report),'--dependencies-report',str(dependencies_report)]
        subprocess.run(command,check=True);tools_report=work/'tools/build.json'
    tools=json.loads(tools_report.read_text())
    expected=json.loads((ROOT/'configs/native-tools-latest.json').read_text())
    for name in native_development.ORDER:
        if tools.get('packages',{}).get(name)!=expected[name]:raise RuntimeError('Incomplete or stale tool build: '+name)
    removed=assemble(args.sysroot,desktop_stage,compiler,dependencies,tools,args.toolchain_prefix)
    certificates=install_certificates(args.sysroot)
    modules=install_kernel_modules(args.sysroot,args.kernel_report) if args.kernel_report else None
    driver=native_xorg.build(work/'xorg-native',args.toolchain_prefix,Path(tools['sysroot']),
        desktop_output/'host',args.jobs,args.sysroot)
    import xorg_apps
    xorg_apps.build(work/'xorg-applications',Path(tools['sysroot']).resolve(),
        args.toolchain_prefix,args.sysroot,args.jobs)
    strip_report=work/'strip-report.json'
    stripping=strip_tree(args.sysroot,args.toolchain_prefix,strip_report)
    locale_files=userland.build_utf8_locales(args.sysroot,{'sysroot':tools['sysroot']},work,Path(tools['sysroot']))
    loader_cache=userland.build_loader_cache(args.sysroot,work)
    ABI_report=work/'ABI-report.json';ABI=audit_target_abi.audit(args.sysroot,ABI_report)
    commands=rootfs.check_staging(args.sysroot,True)
    required=sorted({name for names in rootfs.TOOL_COVERAGE['development_tools'].values()
                     for name in names.split()})
    for name in required:
        if rootfs.find_command(args.sysroot,name) is None:raise RuntimeError('Missing native tool: '+name)
    result={'result':'built and stripped; runtime qualification pending','staging':str(args.sysroot.resolve()),
        'settings':{'work':str(work),'toolchain_prefix':args.toolchain_prefix,'jobs':args.jobs},
        'input_reports':{str(p):sha(p) for p in (desktop_report,compiler_report,dependencies_report,tools_report)},
        'latest_packages':{name:expected[name]['version'] for name in native_development.ORDER},
        'compiler':compiler['toolchain']['version'],'glibc':'2.44','binutils':'2.47',
        'native_gcc_specs_sha256':sha(ROOT/'userland/configs/native-gcc.specs'),
        'native_driver':driver,'strip_report':str(strip_report),'binary_bytes_saved':stripping['binary_bytes_saved'],
        'native_loader_cache_files':loader_cache,
        'native_UTF8_locale_files':locale_files,
        'target_ABI_report':str(ABI_report),'target_ABI_report_sha256':sha(ABI_report),
        'target_ELFs_checked':ABI['unique_ELF_files'],'SDK_archive_members_checked':ABI['SDK_ELF_members'],
        'CA_certificates':certificates,
        'kernel_modules':modules,
        'runtime_commands':commands,'development_commands':required,'diagnostics_removed':removed,
        'diagnostic_clients_installed':False,'physical_hardware_tested':False}
    (work/'build.json').write_text(json.dumps(result,indent=2)+'\n')
    print('Complete native userland staged at '+str(args.sysroot)+'; report '+str(work/'build.json'),flush=True)

if __name__=='__main__':main()
