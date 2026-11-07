"""Build native GS wide integer or binary32 arithmetic diagnostics."""
from pathlib import Path
import argparse,hashlib,json,os,subprocess,sys
ROOT=Path(__file__).resolve().parents[1]
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--work',type=Path,required=True);parser.add_argument('--toolchain-prefix',required=True)
    parser.add_argument('--sysroot',type=Path,required=True);parser.add_argument('--mesa-generated',type=Path,required=True)
    parser.add_argument('--case',choices=('wide-product','float-multiply','float-divide','float-add','float-subtract','saturate-rgba8','wide-bilinear','order-blends','piecewise'),default='wide-product')
    args=parser.parse_args();args.work.mkdir(parents=True,exist_ok=True)
    os.environ['PS2_MESA_GENERATED']=str(args.mesa_generated.resolve());sys.path.insert(0,str(ROOT/'tests'))
    from test_ps2_copy import copy_flags
    sources=['graphics/'+name+'.c' for name in ('ps2_gs_client','ps2_gs_surface','ps2_gs_render',
        'ps2_gs_texture','ps2_gs_transfer','ps2_gs_logic','ps2_gs_lut')]
    sources+=['mesa/ps2/ps2_rop.c','mesa/ps2/ps2_filter.c','mesa/ps2/ps2_multiply.c','mesa/ps2/ps2_divide.c',
        'mesa/ps2/ps2_wide.c','desktop/ps2-gs-'+('float-multiply' if args.case.startswith('float-') else args.case)+'-test.c']
    if args.case=='wide-bilinear':sources+=['mesa/ps2/ps2_wide_filter.c']
    flags=['-march=r5900','-mabi=32','-msoft-float','-mno-llsc','-mfix-r5900','--sysroot='+str(args.sysroot),
        '-std=gnu17','-Os','-Wall','-Wextra','-Werror','-DUSE_GCC_ATOMIC_BUILTINS',
        '-ffunction-sections','-fdata-sections',*copy_flags()]
    if args.case=='float-divide':flags+=['-DPS2_FLOAT_DIVIDE=1']
    if args.case in ('float-add','float-subtract'):flags+=['-DPS2_FLOAT_OPERATION='+str(2 if args.case=='float-add' else 3)]
    binary=args.work/('ps2-gs-'+args.case+'-test');log=args.work/'build.log'
    with log.open('w') as output:
        subprocess.run([args.toolchain_prefix+'gcc',*flags,*(str(ROOT/p) for p in sources),
            '-Wl,--gc-sections','-latomic',*(['-lpixman-1'] if args.case in ('saturate-rgba8','wide-bilinear') else []),
            '-o',str(binary)],check=True,stdout=output,stderr=output)
    inputs={p:sha(ROOT/p) for p in sources}
    for folder in ('graphics','mesa/ps2','kernel/new-drivers'):
        for path in (ROOT/folder).glob('*.h'):inputs[path.relative_to(ROOT).as_posix()]=sha(path)
    report={'result':'native diagnostic built; execution pending','expected_byte_channel_checks':417792 if args.case=='piecewise' else 626688 if args.case=='order-blends' else 147456 if args.case=='wide-product' else 32768 if args.case=='saturate-rgba8' else 16128 if args.case=='wide-bilinear' else 98304,
        'arithmetic_case':args.case,'expected_u24_products':24576 if args.case=='wide-product' else None,
        'expected_float_products':24576 if args.case=='float-multiply' else None,
        'expected_float_quotients':24576 if args.case=='float-divide' else None,
        'expected_float_sums':24576 if args.case=='float-add' else None,
        'expected_float_differences':24576 if args.case=='float-subtract' else None,
        'binary_sha256':sha(binary),'log_sha256':sha(log),'source_inputs':inputs,
        'physical_hardware_tested':False,'full_2D_complete':False,
        'scope':'GS register arithmetic primitive; no additional XRender operator advertised'}
    (args.work/'build.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({k:v for k,v in report.items() if k!='source_inputs'}))
if __name__=='__main__':main()
