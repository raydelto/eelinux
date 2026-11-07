"""Publish a verified graphics extension without changing historical evidence or media."""
from pathlib import Path
import argparse, json, re, shutil
from graphics_2d_publish import ROOT, sha

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--work',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True)
    p.add_argument('--extension',choices=('alpha','filters','formats','rgba','coverage','rgba-transfer','bgra'),default='alpha')
    p.add_argument('--failed-opengl',type=Path,
        help='retain a failed OpenGL comparison of the same kernel and GS probe')
    for name in ('native','host','r5900','patches','client-host','client-r5900','runtime','kernel'):
        p.add_argument('--'+name,required=True)
    args=p.parse_args()
    args.work=args.work.resolve();args.output=args.output.resolve()
    paths={name:args.work/getattr(args,name.replace('-','_')) for name in
        ('native','host','r5900','patches','client-host','client-r5900','runtime','kernel')}
    records={name:json.loads((paths[name]/file).read_text()) for name,file in
        [('native','build.json'),('host','host.json'),('r5900','r5900.json'),
         ('patches','patches.json'),('client-host','client.json'),('client-r5900','client.json'),
         ('runtime','runtime.json'),('kernel','kernel.json')]}
    for name in ('host','r5900','patches','runtime'):
        if records[name]['result']!='passed':raise RuntimeError(name+' verification did not pass')
    if records['native']['diagnostic_logging']:raise RuntimeError('publish the production module without diagnostic logging')
    for relative,expected in records['native']['inputs'].items():
        if sha(ROOT/relative)!=expected:raise RuntimeError('source changed after build: '+relative)
    if records['native']['module_sha256']!=records['runtime']['module_sha256'] or \
        sha(paths['native']/'ps2_drv.so')!=records['native']['module_sha256']:
        raise RuntimeError('production module differs from native execution')
    if records['kernel']['kernel_sha256']!=records['runtime']['kernel_sha256'] or \
        sha(paths['kernel']/'kernel/vmlinuz')!=records['kernel']['kernel_sha256']:
        raise RuntimeError('kernel differs from native execution')
    client_source=sha(ROOT/'desktop/ps2-xorg-gpu2d-test.c')
    if any(records[name]['source_sha256']!=client_source for name in ('client-host','client-r5900')):
        raise RuntimeError('client source changed after verification')
    if records['client-r5900']['binary_sha256']!=records['runtime']['client_sha256'] or \
        records['client-host']['pixel_checks_per_depth']!=records['runtime']['native_pixel_checks']:
        raise RuntimeError('client execution differs from oracle')
    if sha(args.work/'ps2-gs-lut-test')!=records['runtime']['pre_client_sha256']:
        raise RuntimeError('lookup diagnostic differs from native execution')
    patches=dict(records['patches']['series'])
    for record in patches.values():
        if sha(ROOT/record['patch'])!=record['patch_sha256']:raise RuntimeError('export changed after verification')
    kernel_patch=ROOT/('kernel/patches/ps2-gs-rgba-transfer.patch' if args.extension in ('rgba-transfer','bgra') else
        'kernel/patches/ps2-gs-map-blit.patch' if args.extension in ('rgba','coverage') else
        'kernel/patches/ps2-gs-map.patch' if args.extension=='formats' else 'kernel/patches/ps2-gs-lut.patch')
    if sha(kernel_patch)!=records['kernel']['patch_sha256']:raise RuntimeError('kernel patch changed after build')
    if args.extension=='formats' and records['runtime'].get('native_MAP_pixel_checks')!=294912:
        raise RuntimeError('RGBA channel mapping diagnostic did not pass')
    if args.extension in ('rgba','coverage','rgba-transfer','bgra'):
        for field,count in (('native_MAP_pixel_checks',294912),('native_map_blit_pixel_checks',1105920),
                            ('native_rgba_solid_pixel_checks',491520),('native_alpha_read_pixel_checks',5063)):
            if records['runtime'].get(field)!=count:raise RuntimeError(field+' diagnostic did not pass')
        client_log=(paths['runtime']/'client.log').read_text()
        if 'ARGB format index=4 red=0 green=8 blue=16 alpha=24' not in client_log or \
            'ARGB format index=5 red=16 green=8 blue=0 alpha=24' not in client_log:
            raise RuntimeError('native protocol did not exercise both ARGB layouts')
    if args.extension in ('rgba-transfer','bgra'):
        for field,count in (('native_rgba_copy_pixel_checks',276480),('native_rgba_multiply_pixel_checks',737280)):
            if records['runtime'].get(field)!=count:raise RuntimeError(field+' diagnostic did not pass')
    if args.extension=='bgra':
        for layout in ('index=6 red=8 green=16 blue=24 alpha=0',
                       'index=7 red=8 green=16 blue=24 alpha=0',
                       'index=8 red=0 green=8 blue=16 alpha=0',
                       'index=9 red=16 green=8 blue=0 alpha=0'):
            if layout not in client_log:raise RuntimeError('native BGRA/RGBX layout missing: '+layout)
    renderer_comparison=None
    if args.failed_opengl:
        failed=args.failed_opengl.resolve()
        comparison=json.loads((failed/'prepared.json').read_text())
        if comparison['diagnostic_kernel_sha256']!=records['runtime']['kernel_sha256'] or \
            comparison['pre_client_sha256']!=records['runtime']['pre_client_sha256']:
            raise RuntimeError('renderer comparison must use the same kernel and GS probe')
        profile=failed/'session/pcsx2-profile/PCSX2'
        if not re.search(r'^Renderer\s*=\s*12\s*$',(profile/'inis/PCSX2.ini').read_text(),re.M):
            raise RuntimeError('failed comparison was not the OpenGL renderer')
        serial=profile/'logs/emulog.txt'
        mismatch=re.search(r'ps2-rgba-solid: mismatch [^\n]+',serial.read_text())
        if not mismatch:raise RuntimeError('failed comparison has no raw32 alpha mismatch')
        renderer_comparison={'result':'OpenGL renderer failed raw32 alpha; GS software renderer passed',
            'kernel_sha256':comparison['diagnostic_kernel_sha256'],
            'pre_client_sha256':comparison['pre_client_sha256'],
            'failure':mismatch[0],
            'scope':('same native GS probe, kernel and Xorg module' if comparison['module_sha256']==records['runtime']['module_sha256'] else
                'same native GS probe and kernel; the Xorg modules are different revisions'),
            'physical_hardware_tested':False,'OpenGL_host_renderer_qualified':False,
            'cause':'unresolved; passing the GS software renderer does not establish physical behavior'}
    args.output.mkdir(parents=True,exist_ok=True)
    files={'ps2_drv.so':paths['native']/'ps2_drv.so','kernel-vmlinuz.elf':paths['kernel']/'kernel/vmlinuz',
        'native-client':paths['client-r5900']/'ps2-xorg-gpu2d-test','native-lut-client':args.work/'ps2-gs-lut-test',
        'module-build.json':paths['native']/'build.json','module-build.log':paths['native']/'build.log',
        'host.json':paths['host']/'host.json','host.log':paths['host']/'host.log',
        'r5900.json':paths['r5900']/'r5900.json','r5900.log':paths['r5900']/'r5900.log',
        'patches.json':paths['patches']/'patches.json','kernel-build.json':paths['kernel']/'kernel.json',
        'kernel-build.log':paths['kernel']/'kernel.log','client-oracle.json':paths['client-host']/'client.json',
        'client-oracle.log':paths['client-host']/'client.log','runtime.json':paths['runtime']/'runtime.json',
        'client.log':paths['runtime']/'client.log','Xorg.log':paths['runtime']/'Xorg.log',
        'lut.log':paths['runtime']/'lut.log','meminfo.txt':paths['runtime']/'meminfo.txt',
        'serial.log':paths['runtime']/'session/pcsx2-profile/PCSX2/logs/emulog.txt'}
    if renderer_comparison:
        files['opengl-comparison-serial.log']=serial
        files['opengl-comparison-prepared.json']=failed/'prepared.json'
    for name,path in files.items():shutil.copy2(path,args.output/name)
    evidence={'date':'2026-10-13','result':'passed for the implemented '+args.extension+' extension',
        'requested_completion_scope':'all XRender formats and filters',
        'full_2D_complete':False,'physical_hardware_qualified':False,'shipping_USB_DVD_images_rebuilt':False,
        'CPU_colour_destination_rendering':False,'CPU_mask_coverage_processing':True,
        'features':records['native']['new_2D_features']+['A8 Render glyphs'],
        'checks':{'host_ASan_UBSan_cases':records['host']['host_cases'],
            'R5900_Linux_user_cases':records['r5900']['r5900_cases'],
            'Xvfb_pixel_checks_per_depth':records['client-host']['pixel_checks_per_depth'],'Xvfb_depths':[15,24],
            'native_Xorg_pixel_checks':records['runtime']['native_pixel_checks'],
            'native_LUT_pixel_checks':records['runtime']['native_LUT_pixel_checks'],
            'native_MAP_pixel_checks':records['runtime'].get('native_MAP_pixel_checks',0),
            **{k:v for k,v in records['runtime'].items() if k in
                ('native_map_blit_pixel_checks','native_rgba_solid_pixel_checks','native_alpha_read_pixel_checks',
                 'native_rgba_copy_pixel_checks','native_rgba_multiply_pixel_checks')},
            'module_warning_flags':['-Wall','-Wextra','-Werror'],'kernel_W':1,'kernel_KCFLAGS':'-Werror'},
        'runtime':records['runtime'],'kernel_build':records['kernel'],'patches':patches,
        **({'renderer_comparison':renderer_comparison} if renderer_comparison else {}),
        'kernel_patch':{'path':kernel_patch.relative_to(ROOT).as_posix(),'sha256':sha(kernel_patch),
            'prerequisite':'ps2-gs-map-blit.patch' if args.extension in ('rgba-transfer','bgra') else
                'ps2-gs-map.patch' if args.extension in ('rgba','coverage') else
                'ps2-gs-lut.patch' if args.extension=='formats' else 'ps2-gs-batch.patch'},
        'remaining':records['native']['remaining_2D']+['extended Render blend operators and alpha maps',
            'Mesa shader/OpenGL/GLX execution'],
        'memory_limits':{'source_coverage_storage_bytes':1048576,'source_coverage_pixmaps':1024,
            'composite_plan_pixels':1048576,'composite_plan_bytes':8388608 if args.extension in ('rgba','coverage','rgba-transfer','bgra') else
                6291456 if args.extension in ('filters','formats') else 5242880,
            'alpha_GS_workspace_bytes':65536,
            **({'filtered_GS_workspace_bytes':147456 if args.extension in ('rgba','coverage','rgba-transfer','bgra') else 131072,
                'filtered_CPU_plan_bytes':5267456 if args.extension in ('rgba','coverage','rgba-transfer','bgra') else 3170304,
                'convolution_parameter_bytes_per_picture':1048576,'convolution_tap_work_per_picture':16777216}
               if args.extension in ('filters','formats','rgba','coverage','rgba-transfer','bgra') else {}),
            **({'ARGB_source_alpha_snapshot_bytes':1048576,'raw32_ROP_GS_workspace_bytes':81920,
                'isolated_alpha_readback_GS_workspace_bytes':16384} if args.extension in ('rgba','coverage','rgba-transfer','bgra') else {}),
            **({'raw32_solid_GS_workspace_bytes':24576} if args.extension in ('rgba-transfer','bgra') else {}),
            **({'prepared_colour_to_coverage_snapshot_bytes':1048576,
                'coverage_to_colour_request_bytes':1048576,'coverage_black_source_GS_bytes':8192}
               if args.extension in ('coverage','rgba-transfer','bgra') else {})},
        'memory_measurement':'meminfo captured after Xorg exits; peak desktop usage is not measured',
        'source_sha256':{str(f.relative_to(ROOT)).replace('\\','/'):sha(f) for folder in
            ('graphics','mesa/ps2','desktop/xorg','kernel/new-drivers') for f in sorted((ROOT/folder).iterdir())
            if f.suffix in ('.c','.h')},
        'artifact_sha256':{name:sha(args.output/name) for name in files},
        'artifact_directory':args.output.relative_to(ROOT).as_posix()}
    data=json.dumps(evidence,indent=2)+'\n'
    (ROOT/('docs/graphics-'+args.extension+'-validation.json')).write_text(data)
    (args.output/'validation.json').write_text(data)
    (args.output/'README.txt').write_text('Native PS2 Xorg/Mesa '+args.extension+' extension, 2026-10-13\n\n'
        +('ps2_drv.so uses CHANNEL_LUT, CHANNEL_MAP, CHANNEL_MAP_BLIT and typed RGBA transfer; the kernel includes all four.\n' if args.extension in ('rgba-transfer','bgra') else
          'ps2_drv.so uses CHANNEL_LUT, CHANNEL_MAP and CHANNEL_MAP_BLIT; the kernel includes all three.\n' if args.extension in ('rgba','coverage') else
          'ps2_drv.so uses CHANNEL_LUT and CHANNEL_MAP; kernel-vmlinuz.elf includes both.\n' if args.extension=='formats' else
          'ps2_drv.so requires the kernel CHANNEL_LUT extension; kernel-vmlinuz.elf includes it.\n')+
        'The native clients are diagnostics and are separate from the production module.\n'
        f"{records['host']['host_cases']} host sanitizer cases and {records['r5900']['r5900_cases']} R5900 cases passed.\n"
        f"{records['runtime']['native_pixel_checks']} native Xorg checks and {records['runtime']['native_LUT_pixel_checks']} native lookup pixel comparisons passed.\n"
        +(f"{records['runtime'].get('native_MAP_pixel_checks',0)} native RGBA mapping comparisons passed.\n" if args.extension in ('formats','rgba','coverage','rgba-transfer','bgra') else '')
        +(f"{records['runtime']['native_map_blit_pixel_checks']} rectangle-map, {records['runtime']['native_rgba_solid_pixel_checks']} raw32-solid and {records['runtime']['native_alpha_read_pixel_checks']} isolated-alpha comparisons passed.\n" if args.extension in ('rgba','coverage','rgba-transfer','bgra') else '')
        +(f"{records['runtime']['native_rgba_copy_pixel_checks']} typed RGBA copy and {records['runtime']['native_rgba_multiply_pixel_checks']} native RGB multiply comparisons passed.\n" if args.extension in ('rgba-transfer','bgra') else '')
        +('Full XRender formats and Mesa OpenGL/GLX remain incomplete.\n' if args.extension in ('filters','formats','rgba','coverage','rgba-transfer','bgra') else
          'Full XRender formats/filters and Mesa OpenGL/GLX remain incomplete.\n')+
        'This is an emulator-verified candidate; shipping USB/DVD images are unchanged.\n')
    if renderer_comparison:
        with (args.output/'README.txt').open('a') as readme:
            readme.write('Qualification used the PCSX2 GS software host renderer. The OpenGL host renderer failed a raw32 alpha comparison with the same kernel and GS probe; its cause remains unresolved.\n')
    print(json.dumps({'artifacts':str(args.output),'checks':evidence['checks'],'full_2D_complete':False},indent=2))

if __name__=='__main__':main()
