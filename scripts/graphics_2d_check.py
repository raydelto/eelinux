"""Run graphics C fixtures on host sanitizers or R5900; retain evidence."""
from pathlib import Path
import argparse, concurrent.futures, hashlib, importlib, json, os, shutil, subprocess, sys
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
SUITES = {'copy':('test_ps2_copy','CopyArea'),
          'window-sampling': ('test_ps2_window_sampling','WindowSampling'),
          'plane': ('test_ps2_plane', 'ColourCopyPlane'),
          'raster': ('test_ps2_raster','Raster'),
          'cursor': ('test_ps2_cursor', 'NativeCursor'),
          'cursor-argb': ('test_ps2_cursor_argb', 'ARGBCursor'),
          'composite': ('test_ps2_composite', 'NativeComposite'),
          'packed-map-input': ('test_ps2_packed_map_input', 'PackedMapInput'),
          'packed-mixed': ('test_ps2_packed_mixed', 'PackedMixed'),
    'packed-front': ('test_ps2_packed_front','PackedFront'),
          'depth30': ('test_ps2_depth30','Depth30'),
          'image': ('test_ps2_image_gc', 'NativeImages'),
          'blit': ('test_ps2_blit', 'Blit'),
          'core2d': ('test_ps2_core2d', 'Core2D'),
          'rop': ('test_ps2_rop', 'ROP'),
          'gc': ('test_ps2_gc', 'GCLifecycle'),
          'solid': ('test_ps2_solid', 'SolidGC'),
          'mono': ('test_ps2_mono', 'MonochromePixmap'),
          'lut': ('test_ps2_gs_lut', 'GSLUT'),
          'map': ('test_ps2_gs_map', 'GSMap'),
          'map-blit': ('test_ps2_gs_map_blit', 'GSMapBlit'),
          'rgba-transfer': ('test_ps2_gs_rgba_transfer', 'RGBATransfer'),
          'glyph': ('test_ps2_glyph', 'Glyphs'),
          'transfer': ('test_ps2_gs_transfer', 'TextureTransfer')}

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--work', type=Path, required=True)
    parser.add_argument('--host', action='store_true')
    parser.add_argument('--jobs',type=int,choices=range(1,5),default=1,
                        help='independent model processes per suite; separate case logs retain every result')
    parser.add_argument('--model-optimization',choices=('0','1','2'),
                        help='optimize fixture code while retaining assertions and host sanitizers')
    parser.add_argument('--toolchain-prefix')
    parser.add_argument('--sysroot', type=Path)
    parser.add_argument('--qemu', type=Path)
    parser.add_argument('--mesa-generated', type=Path, required=True)
    parser.add_argument('--suites', nargs='+', choices=SUITES, default=list(SUITES))
    parser.add_argument('--cases', nargs='+', help='Run selected case names in a single suite')
    parser.add_argument('--convolution-oracle',type=Path,
                        help='isolated pinned host pixman with explicit R5900 convolution casts')
    args = parser.parse_args()
    if args.cases and len(args.suites)!=1:
        parser.error('--cases requires exactly one --suites value')
    convolution_oracle=None
    if args.convolution_oracle:
        if not args.host:parser.error('--convolution-oracle is for host models only')
        convolution_oracle=json.loads((args.convolution_oracle/'oracle.json').read_text())
        lib=Path(convolution_oracle['library_directory'])/'libpixman-1.so.0.44.2'
        if convolution_oracle['result']!='built' or hashlib.sha256(lib.read_bytes()).hexdigest()!=convolution_oracle['library_sha256']:
            raise RuntimeError('convolution oracle identity mismatch')
    if not args.host and not (args.toolchain_prefix and args.sysroot and args.qemu):
        parser.error('R5900 requires --toolchain-prefix, --sysroot and --qemu')
    args.work.mkdir(parents=True, exist_ok=True)
    os.environ['PS2_MESA_GENERATED'] = str(args.mesa_generated.resolve())
    sys.path.insert(0, str(ROOT / 'tests'))
    run = subprocess.run
    reports = [];case_logs={}
    mode = 'host' if args.host else 'r5900'
    with (args.work / (mode + '.log')).open('w', buffering=1) as log:
        def compile_fixture(argv, **kwargs):
            if argv[0]=='cc' and args.host:argv=[*argv,'-fno-sanitize-recover=all']
            if argv[0] == 'cc' and not args.host:
                argv = [args.toolchain_prefix + 'gcc', '-march=r5900', '-mabi=32', '-msoft-float',
                        '-mno-llsc', '-mfix-r5900', '--sysroot=' + str(args.sysroot),
                        *[v for v in argv[1:] if not v.startswith('-fsanitize=') and
                          v not in ('-fno-pie', '-no-pie')]]
            if argv[0] in ('cc', args.toolchain_prefix + 'gcc' if args.toolchain_prefix else ''):
                if args.model_optimization is not None:argv=[*argv,'-O'+args.model_optimization]
                kwargs.update(stdout=log, stderr=log)
            return run(argv, **kwargs)
        for name in args.suites:
            module_name, class_name = SUITES[name]
            module = importlib.import_module(module_name); fixture = getattr(module, class_name)
            with patch('subprocess.run', compile_fixture): fixture.setUpClass()
            try:
                binary = args.work / name; shutil.copy2(fixture.binary, binary)
                cases = getattr(module, 'CASES', ('text', 'changes', 'rewrap', 'lifetime',
                    'creation', 'limit', 'invalid', 'failure') if name == 'gc' else ())
                if not cases:
                    raise RuntimeError('missing cases for suite ' + name)
                if args.cases:
                    if set(args.cases)-set(cases):
                        raise RuntimeError('unknown cases: '+', '.join(sorted(set(args.cases)-set(cases))))
                    cases=tuple(dict.fromkeys(args.cases))
                def run_case(case):
                    print(f'{mode} {name}/{case}', flush=True)
                    oracle_env={}
                    if convolution_oracle and (name=='packed-front' or case in ('packed10-convolution','packed10-convolution-masks','packed10-convolution-alias','packed10-convolution-edges','packed10-alpha-read-convolution','packed10-alpha-write-convolution') or (name=='packed-mixed' and case.endswith('-2')) or (name=='depth30' and case.endswith('convolution'))):
                        oracle_env={'env':{**os.environ,'LD_LIBRARY_PATH':convolution_oracle['library_directory']}}
                    prefix = [] if args.host else [str(args.qemu), '-cpu', 'R5900', '-L', str(args.sysroot)]
                    # The division model exercises hundreds of thousands of
                    # RGB lanes through five restoring divisions each. Under
                    # Linux-user emulation its packet model takes longer than
                    # the native GS probe. Keep a bounded, case-specific limit.
                    limit=900 if case.startswith(('packed10-','gradient-','hsl-','float-routing','softlight-','float-sqrt','float-signed','dodge-','burn-','float-branch','screen-','exclusion-','multiply-','darken-','lighten-','difference-','order-blends-','overlay-','hardlight-','piecewise-','dj','cj','pd-')) or case in ('float-divide','saturate-rgba8','saturate-float','wide-bilinear','convolution-float',
                        'saturate-bilinear-byte-exa','saturate-bilinear-word-exa','saturate-bilinear-raw-masks',
                        'saturate-coverage','saturate-coverage-argb','saturate-coverage-555','saturate-coverage-ca-argb',
                        'saturate-coverage-aliases','saturate-solid-native','saturate-solid-packed','saturate-solid-coverage',
                        'saturate-map-source','saturate-map-mask','saturate-map-combined','saturate-map-destination','saturate-map-555',
                        'saturate-map-native-exa','saturate-map-packed-exa','saturate-map-coverage-exa',
                        'shifted-map-native','shifted-map-packed','shifted-map-coverage','shifted-map-basic',
                        'convolution-dispatch','convolution-exa','convolution-map-native','convolution-map-packed','convolution-map-coverage',
                        'format-alias-byte','format-alias-word','format-alias-boundaries','native15-alias','native15-alias-boundaries','native15-codec',
                        'backing-alias-byte','backing-alias-word','window-alias-native15','window-alias-word','window-alias-boundaries') else 180
                    if case in ('coverage-from-colour','coverage-from-colour-555','coverage-from-colour-argb',
                        'coverage-ca-from-colour','coverage-ca-from-colour-555','coverage-ca-from-colour-argb'):limit=900
                    # Writable maps cover 3,392 requests per full-format case
                    # and independently quantize two stores. Keep the larger
                    # R5900 packet-model workload bounded without reusing the
                    # smaller source-map-only matrix's deadline.
                    if case in ('packed10-alpha-write','packed10-alpha-write-bilinear','packed10-alpha-write-convolution',
                        'packed10-alpha-write-alias','packed10-alpha-write-procedural','write-input-alias') or name in ('packed-mixed','depth30','packed-front'):limit=1800
                    if args.jobs==1:
                        run([*prefix, str(binary), case], check=True, timeout=limit, stdout=log, stderr=log,**oracle_env)
                    else:
                        case_log=args.work/'cases'/(name+'-'+case+'.log')
                        case_log.parent.mkdir(exist_ok=True)
                        with case_log.open('w') as output:
                            run([*prefix,str(binary),case],check=True,timeout=limit,stdout=output,stderr=output,**oracle_env)
                        return case_log
                if args.jobs==1:
                    for case in cases:run_case(case)
                else:
                    # Each fixture case executes in its own process, with no
                    # shared GS model state. Publish logs in the original case
                    # order after all workers pass; preserve individual logs
                    # immediately even when a later case fails.
                    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as executor:
                        futures={case:executor.submit(run_case,case) for case in cases}
                        for case,future in futures.items():
                            path=future.result();contents=path.read_bytes()
                            log.write(contents.decode(errors='replace'))
                            case_logs[path.relative_to(args.work).as_posix()]=hashlib.sha256(contents).hexdigest()
                            (path.with_suffix('.json')).write_text(json.dumps({'case':case,'suite':name,'result':'passed',
                                'binary_sha256':hashlib.sha256(binary.read_bytes()).hexdigest(),
                                'log_sha256':case_logs[path.relative_to(args.work).as_posix()]})+'\n')
                reports.append({'suite': name, 'cases': list(cases),
                                'binary_sha256': hashlib.sha256(binary.read_bytes()).hexdigest()})
            finally: fixture.tearDownClass()
    report = {'result': 'passed', mode + '_cases': sum(len(r['cases']) for r in reports),
              'convolution_host_oracle':convolution_oracle,
              'compiler_optimization':'compiler default' if args.model_optimization is None else '-O'+args.model_optimization,
              'parallel_model_processes':args.jobs,'case_logs_sha256':case_logs,
              'suites': reports, 'physical_hardware_tested': False,
              'pixel_execution': 'wrapped GS packets and independent oracle; ' +
                  ('ASan/UBSan host' if args.host else 'Linux-user R5900'),
              'log_sha256': hashlib.sha256((args.work / (mode + '.log')).read_bytes()).hexdigest()}
    (args.work / (mode + '.json')).write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps({'result': report['result'], mode + '_cases': report[mode + '_cases']}))

if __name__ == '__main__': main()
