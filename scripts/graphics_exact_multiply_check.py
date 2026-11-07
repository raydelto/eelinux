"""Qualify GS integer multiplication, or build its physical console diagnostic."""
from pathlib import Path
import argparse, hashlib, json, os, subprocess, sys, tempfile

ROOT = Path(__file__).resolve().parents[1]

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--work', type=Path, required=True)
    modes = parser.add_mutually_exclusive_group()
    modes.add_argument('--host', action='store_true')
    modes.add_argument('--native', action='store_true')
    parser.add_argument('--toolchain-prefix')
    parser.add_argument('--sysroot', type=Path)
    parser.add_argument('--qemu', type=Path)
    parser.add_argument('--mesa-generated', type=Path, required=True)
    args = parser.parse_args()
    if not args.host and not (args.toolchain_prefix and args.sysroot):
        parser.error('cross builds require --toolchain-prefix and --sysroot')
    if not (args.host or args.native) and not args.qemu:
        parser.error('R5900 model checks require --qemu')
    args.work.mkdir(parents=True, exist_ok=True)
    os.environ['PS2_MESA_GENERATED'] = str(args.mesa_generated.resolve())
    sys.path.insert(0, str(ROOT / 'tests'))
    from test_ps2_composite import headers, ROP_SOURCES, copy_flags, XORG
    binary = args.work / ('ps2-gs-exact-multiply-test' if args.native else 'multiply-model')
    flags = ['-std=gnu17', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function',
             '-DUSE_GCC_ATOMIC_BUILTINS', *copy_flags()]
    if args.host:
        compiler = 'cc'; flags += ['-fsanitize=address,undefined', '-fno-pie', '-no-pie']
    else:
        compiler = args.toolchain_prefix + 'gcc'
        flags += ['-march=r5900', '-mabi=32', '-msoft-float', '-mno-llsc', '-mfix-r5900',
                  '--sysroot=' + str(args.sysroot)]
    logpath = args.work / 'check.log'
    with tempfile.TemporaryDirectory() as tmp, logpath.open('w') as log:
        if args.native:
            sources = ['graphics/' + name + '.c' for name in (
                'ps2_gs_client', 'ps2_gs_surface', 'ps2_gs_render', 'ps2_gs_texture',
                'ps2_gs_transfer', 'ps2_gs_logic', 'ps2_gs_lut')]
            sources += ['mesa/ps2/ps2_rop.c', 'mesa/ps2/ps2_filter.c',
                        'mesa/ps2/ps2_multiply.c', 'desktop/ps2-gs-exact-multiply-test.c']
            subprocess.run([compiler, *flags, '-Os', '-ffunction-sections', '-fdata-sections',
                *(str(ROOT / p) for p in sources), '-Wl,--gc-sections', '-latomic',
                '-o', str(binary)], check=True, stdout=log, stderr=log)
        else:
            headers(tmp)
            (Path(tmp) / 'previous-rop.c').write_text((ROOT / 'tests/graphics/ps2-rop.c').read_text()
                .replace('int main(', 'int previous_rop_main(').replace('int __wrap_ioctl(', 'int rop_ioctl('))
            (Path(tmp) / 'previous-composite.c').write_text((ROOT / 'tests/graphics/ps2-composite.c').read_text()
                .replace('int main(', 'int previous_composite_main('))
            ref = Path(tmp) / 'reference.o'
            subprocess.run([compiler, *flags, '-DmiFillPolygon=miReferenceFillPolygon', '-c',
                str(XORG / 'mi/mipoly.c'), '-o', str(ref)], check=True, stdout=log, stderr=log)
            sources = [*ROP_SOURCES, 'tests/graphics/ps2-multiply.c']
            subprocess.run([compiler, *flags, '-I' + tmp, '-I' + str(ROOT / 'tests/graphics'),
                *(str(ROOT / p) for p in sources), str(XORG / 'mi/micopy.c'), str(ref),
                *['-Wl,--wrap=' + name for name in ('open', 'close', 'ioctl', 'nanosleep', 'calloc', 'malloc')],
                '-latomic', '-lpixman-1', '-o', str(binary)], check=True, stdout=log, stderr=log)
            runner = [] if args.host else [str(args.qemu), '-cpu', 'R5900', '-L', str(args.sysroot)]
            subprocess.run([*runner, str(binary)], check=True, timeout=180, stdout=log, stderr=log)
    report = {'result': 'native diagnostic built; console execution pending' if args.native else 'passed',
        'pixel_execution': 'physical GS diagnostic (not run)' if args.native else
            'wrapped GS packets; ' + ('ASan/UBSan host' if args.host else 'R5900 Linux-user'),
        'physical_hardware_tested': False, 'exact_multiply_channel_checks': 196608,
        'unfiltered_saturate_triples': None if args.native else 393216,
        'binary_sha256': sha(binary), 'log_sha256': sha(logpath),
        'source_inputs': {p: sha(ROOT / p) for p in sources}}
    (args.work / 'check.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps({k: v for k, v in report.items() if k != 'source_inputs'}))

if __name__ == '__main__':
    main()
