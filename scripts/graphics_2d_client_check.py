"""Build the diagnostic X11 client; optionally check its oracle against Xvfb."""
from pathlib import Path
import argparse, hashlib, json, os, re, subprocess

ROOT = Path(__file__).resolve().parents[1]

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--work', type=Path, required=True)
    parser.add_argument('--toolchain-prefix')
    parser.add_argument('--sysroot', type=Path)
    parser.add_argument('--case',choices=('alpha-maps','alpha-windows','alpha-aliases','saturate','packed-saturate','bilinear-saturate','coverage-saturate','solid-saturate','map-saturate','shifted-maps','convolution-saturate','format-aliases','native15-aliases','screen','exclusion','multiply','order-blends','piecewise','pd-families','pd-aliases','disjoint-over','pd-ratios','dodge-burn','softlight','hsl','gradients','packed10','packed10-ops','packed10-convolution','packed10-alpha-read','packed10-alpha-write','packed10-mixed','depth30','formats-filters','front-formats','resources','cursor-argb','render-geometry','window-sampling','render-glyphs'),
                        help='check a focused client oracle instead of the complete matrix')
    args = parser.parse_args()
    if bool(args.toolchain_prefix) != bool(args.sysroot):
        parser.error('--toolchain-prefix and --sysroot must be supplied together')
    args.work.mkdir(parents=True, exist_ok=True)
    binary = args.work / 'ps2-xorg-gpu2d-test'
    source = ROOT / 'desktop/ps2-xorg-gpu2d-test.c'
    flags = ['-std=gnu17', '-Os', '-Wall', '-Wextra', '-Werror']
    if args.toolchain_prefix:
        compiler = args.toolchain_prefix + 'gcc'
        flags += ['-march=r5900', '-mabi=32', '-msoft-float', '-mno-llsc', '-mfix-r5900',
                  '--sysroot=' + str(args.sysroot), '-I' + str(args.sysroot / 'usr/include/pixman-1')]
    else:
        compiler = 'cc'
        flags += subprocess.check_output(['pkg-config', '--cflags', 'pixman-1'], text=True).split()
    with (args.work / 'client.log').open('w') as log:
        subprocess.run([compiler, *flags, str(source), '-o', str(binary), '-lX11', '-lXrender', '-lpixman-1'],
                       check=True, stdout=log, stderr=log)
        if not args.toolchain_prefix:
            for depth in (15, 24):
                subprocess.run(['xvfb-run', '-a', '-s', f'-screen 0 720x576x{depth} -pixdepths 15 16 24', str(binary)],
                               check=True, timeout=120, stdout=log, stderr=log,
                               env={**os.environ, 'LC_ALL': 'C','PS2_GS_ORACLE':'1',
                                    **({'PS2_GS_CASE':args.case} if args.case else {})})
    output=(args.work/'client.log').read_text()
    actual=[int(n) for n in re.findall(r'completed pixel_checks=(\d+) errors=0',output)]
    skipped=[int(n) for n in re.findall(r'oracle_A4_Render_skipped=(\d+)',output)]
    byte_skipped=[int(n) for n in re.findall(r'oracle_byte_colour_skipped=(\d+)',output)]
    glyph_formats=[int(n) for n in re.findall(r'oracle_glyph_formats=(\d+)',output)]
    macro={'alpha-maps':'PS2_GPU2D_ALPHA_MAP_CHECKS','alpha-windows':'PS2_GPU2D_ALPHA_WINDOW_CHECKS',
           'alpha-aliases':'PS2_GPU2D_ALPHA_ALIAS_CHECKS','saturate':'PS2_GPU2D_SATURATE_CHECKS',
           'packed-saturate':'PS2_GPU2D_PACKED_SATURATE_CHECKS','bilinear-saturate':'PS2_GPU2D_BILINEAR_SATURATE_CHECKS',
           'coverage-saturate':'PS2_GPU2D_COVERAGE_SATURATE_CHECKS','solid-saturate':'PS2_GPU2D_SOLID_SATURATE_CHECKS',
           'map-saturate':'PS2_GPU2D_MAP_SATURATE_CHECKS','shifted-maps':'PS2_GPU2D_SHIFTED_MAP_CHECKS',
           'convolution-saturate':'PS2_GPU2D_CONVOLUTION_SATURATE_CHECKS',
           'format-aliases':'PS2_GPU2D_FORMAT_ALIAS_CHECKS',
           'native15-aliases':'PS2_GPU2D_NATIVE15_ALIAS_CHECKS','screen':'PS2_GPU2D_SCREEN_CHECKS',
           'order-blends':'PS2_GPU2D_ORDER_BLENDS_CHECKS',
           'piecewise':'PS2_GPU2D_PIECEWISE_CHECKS',
           'pd-families':'PS2_GPU2D_PD_FAMILIES_CHECKS','pd-aliases':'PS2_GPU2D_PD_ALIASES_CHECKS','disjoint-over':'PS2_GPU2D_DISJOINT_OVER_CHECKS','pd-ratios':'PS2_GPU2D_PD_RATIOS_CHECKS','dodge-burn':'PS2_GPU2D_DODGE_BURN_CHECKS','softlight':'PS2_GPU2D_SOFTLIGHT_CHECKS','hsl':'PS2_GPU2D_HSL_CHECKS','gradients':'PS2_GPU2D_GRADIENT_CHECKS','packed10':'PS2_GPU2D_PACKED10_CHECKS','packed10-ops':'PS2_GPU2D_PACKED10_OPS_CHECKS','packed10-convolution':'PS2_GPU2D_PACKED10_CONVOLUTION_CHECKS','packed10-alpha-read':'PS2_GPU2D_PACKED10_ALPHA_READ_CHECKS','packed10-alpha-write':'PS2_GPU2D_PACKED10_ALPHA_WRITE_CHECKS','packed10-mixed':'PS2_GPU2D_PACKED10_MIXED_CHECKS','depth30':'PS2_GPU2D_DEPTH30_CHECKS','formats-filters':'PS2_GPU2D_FORMAT_FILTER_CHECKS','front-formats':'PS2_GPU2D_FRONT_FORMAT_CHECKS','resources':'PS2_GPU2D_RESOURCE_CHECKS','cursor-argb':'PS2_GPU2D_CURSOR_ARGB_CHECKS','render-geometry':'PS2_GPU2D_RENDER_GEOMETRY_CHECKS','window-sampling':'PS2_GPU2D_WINDOW_SAMPLE_CHECKS','render-glyphs':'PS2_GPU2D_RENDER_GLYPH_CHECKS',
           'multiply':'PS2_GPU2D_MULTIPLY_CHECKS',
           'exclusion':'PS2_GPU2D_EXCLUSION_CHECKS'}.get(args.case,'PS2_GPU2D_PIXEL_CHECKS')
    report = {'result': 'built' if args.toolchain_prefix else 'oracle passed on Xvfb at depths 15 and 24; available formats only' if skipped or byte_skipped or any(n!=28 for n in glyph_formats) else 'oracle passed on Xvfb at depths 15 and 24',
              'client_case':args.case,
              'native_GS_runtime_tested': False, 'physical_hardware_tested': False,
              'pixel_checks_per_depth': int(re.search(r'#define '+macro+r' (\d+)',source.read_text())[1]), 'source_sha256': hashlib.sha256(source.read_bytes()).hexdigest(),
              'binary_sha256': hashlib.sha256(binary.read_bytes()).hexdigest(),
              'source_inputs':{p.relative_to(ROOT).as_posix():hashlib.sha256(p.read_bytes()).hexdigest()
                  for p in (source,)},
              'log_sha256': hashlib.sha256((args.work / 'client.log').read_bytes()).hexdigest(),
              'actual_oracle_checks_by_depth':None if args.toolchain_prefix else dict(zip(('15','24'),actual)),
              'glyph_oracle_formats_by_depth':dict(zip(('15','24'),glyph_formats)) if glyph_formats else None,
              'native_glyph_formats_expected':28 if args.case=='render-glyphs' else None,
              'A4_Render_oracle_skipped':bool(skipped),
              'A4_Render_oracle_skipped_per_depth':skipped[0] if skipped else 0,
              'byte_colour_oracle_skipped_per_depth':sum(byte_skipped)//2 if byte_skipped else 0}
    (args.work / 'client.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report))

if __name__ == '__main__':
    main()
