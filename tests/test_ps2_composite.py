"""Native nearest compositing compared with the independent pixman renderer."""
from pathlib import Path
import shutil, subprocess, tempfile, unittest
from test_ps2_rop import ROOT, ROP_SOURCES, headers, copy_flags, XORG
ROP_SOURCES=list(dict.fromkeys([*ROP_SOURCES,'mesa/ps2/ps2_divide.c','mesa/ps2/ps2_wide.c','mesa/ps2/ps2_wide_filter.c']))
CASES = ('borrowed-byte-glyphs','borrowed-word-glyphs','packed10-alpha-write-guards','packed10-alpha-write','packed10-alpha-write-bilinear','packed10-alpha-write-convolution','packed10-alpha-write-alias','packed10-alpha-write-procedural','packed10-alpha-read-procedural','packed10-alpha-read-guards','packed10-alpha-read','packed10-alpha-read-bilinear','packed10-alpha-read-convolution','packed10-alpha-read-alias','packed10-convolution-preflight','packed10-convolution-float','packed10-convolution-guards','packed10-convolution','packed10-convolution-masks','packed10-convolution-alias','packed10-convolution-edges','packed10-operator-guards','packed10-operators','packed10-operators-bilinear','packed10-operators-masks','packed10-operators-alias','packed10-operators-gradients','packed10-operators-solids','packed10-quantize','packed10-guards','packed10-basic','packed10-bilinear','packed10-masks','packed10-alias','packed10-gradients','packed10-solids','packed10-codec','gradient-guards','gradient-opacity','gradient-opacity-convolution','gradient-linear-degenerate','gradient-radial-degenerate','gradient-radial-contained','gradient-radial-empty','gradient-conical-centre','gradient-radial','gradient-conical','gradient-radial-dispatch-narrow','gradient-radial-dispatch-wide','gradient-conical-dispatch-narrow','gradient-conical-dispatch-wide','gradient-linear','gradient-byte','gradient-dispatch-narrow','gradient-dispatch-wide','src', 'over', 'rgb555', 'normal', 'pad', 'reflect', 'scale', 'rotate',
         'shear', 'projective', 'negative-q', 'mask-src', 'mask-over', 'mask-transform',
         'alias', 'front', 'preflight', 'noop', 'allocation', 'failure', 'exa', 'boundaries',
         'exa-transform', 'exa-mask', 'alpha-src', 'alpha-over', 'alpha-555-src',
         'alpha-555-over', 'alpha-ramps-src', 'alpha-ramps-over', 'alpha-555-ramps-over',
         'solid-src','solid-over','solid-555-src','solid-555-over','porter-duff','porter-duff-555',
         'bilinear-a1','bilinear-a8','bilinear-a1-555','bilinear-a8-555','coverage-exa',
         'colour-filter','colour-filter-555','colour-filter-mask-a1','colour-filter-mask-a8',
         'colour-filter-mask-linear','colour-filter-mask-linear-555',
         'convolution','convolution-555','convolution-mask-a1','convolution-mask-a8','convolution-mask-555','filter-guards',
         'formats','formats-555','formats-32-555','formats-555-32','exa-swapped-source','exa-swapped-destination',
         'rgba','rgba-opaque-source','rgba-opaque-destination',
         'bgra','bgra-source','bgra-destination','bgrx','bgrx-source','bgrx-destination',
         'coverage-to-colour','coverage-to-colour-555','coverage-to-colour-argb',
         'coverage-from-colour','coverage-from-colour-555','coverage-from-colour-argb',
         'coverage-core','coverage-core-images','coverage-core-readback','coverage-core-uploads',
         'component-alpha','component-alpha-555','component-alpha-exa','rgba-alias-boundaries',
         'component-alpha-coverage','component-alpha-coverage-555',
         'coverage-ca-from-colour','coverage-ca-from-colour-555','coverage-ca-from-colour-argb',
         'coverage-a8-copyplane','rgb444-codec','rgb444-exa','byte-colour-codec','byte-colour-exa',
         'word-colour-codec','word-colour-exa',
         'alpha-map-source','alpha-map-mask','alpha-map-destination','alpha-map-combined',
         'alpha-map-combined-555','alpha-map-combined-swapped','alpha-map-combined-rotated',
         'alpha-map-exa','alpha-map-aliases','alpha-map-guards',
         'divide-bytes','divide-wide','divide-guards','alpha-map-window-exa','alpha-map-raw-alias',
         'wide-product','wide-product-guards','unorm-float','float-multiply','float-multiply-guards',
         'float-divide','float-divide-guards','float-add','float-add-guards','float-subtract','float-subtract-guards','float-unorm',
         'saturate-rgba8','saturate-rgba8-guards','saturate-dispatch','saturate-exa','saturate-byte-exa','saturate-word-exa',
         'wide-bilinear','wide-bilinear-guards','saturate-bilinear-byte-exa','saturate-bilinear-word-exa',
         'saturate-float','saturate-float-guards','saturate-bilinear-dispatch','saturate-bilinear-exa','saturate-opaque-extent',
         'saturate-bilinear-raw-masks','saturate-coverage','saturate-coverage-argb',
         'saturate-coverage-555','saturate-coverage-ca-argb','saturate-coverage-aliases',
         'saturate-alpha-float','saturate-alpha-float-guards',
         'saturate-solid-native','saturate-solid-packed','saturate-solid-coverage','saturate-solid-guards',
         'saturate-map-source','saturate-map-mask','saturate-map-combined','saturate-map-destination','saturate-map-555',
         'saturate-map-native-exa','saturate-map-packed-exa','saturate-map-coverage-exa','saturate-map-guards',
         'shifted-map-native','shifted-map-packed','shifted-map-coverage','shifted-map-basic','shifted-map-boundaries',
         'convolution-float','convolution-float-guards','convolution-dispatch','convolution-exa',
         'convolution-map-native','convolution-map-packed','convolution-map-coverage',
         'format-alias-byte','format-alias-word','format-alias-boundaries','format-alias-guards',
         'native15-alias','native15-alias-boundaries','native15-alias-guards','native15-codec','native15-codec-guards',
         'backing-alias-byte','backing-alias-word','window-alias-native15','window-alias-word','window-alias-boundaries',
         'screen-rgba','screen-bgra','screen-opaque','screen-native555','screen-byte','screen-word','screen-coverage',
         'screen-map','screen-alias-byte','screen-alias-word','screen-alias-native15','screen-solid',
         'exclusion-rgba','exclusion-bgra','exclusion-opaque','exclusion-native555','exclusion-byte','exclusion-word',
         'exclusion-coverage','exclusion-map','exclusion-alias-byte','exclusion-alias-word','exclusion-alias-native15',
         'exclusion-solid','exclusion-bytes','exclusion-guards',
         'multiply-rgba','multiply-bgra','multiply-opaque','multiply-native555','multiply-byte','multiply-word',
         'multiply-coverage','multiply-map','multiply-alias-byte','multiply-alias-word','multiply-alias-native15','multiply-solid',
         'darken-rgba','darken-bgra','darken-opaque','darken-native555','darken-byte','darken-word','darken-coverage','darken-map','darken-alias-byte','darken-alias-word','darken-alias-native15','darken-solid','lighten-rgba','lighten-bgra','lighten-opaque','lighten-native555','lighten-byte','lighten-word','lighten-coverage','lighten-map','lighten-alias-byte','lighten-alias-word','lighten-alias-native15','lighten-solid','difference-rgba','difference-bgra','difference-opaque','difference-native555','difference-byte','difference-word','difference-coverage','difference-map','difference-alias-byte','difference-alias-word','difference-alias-native15','difference-solid',
         'order-blends-bytes','order-blends-guards',
         'overlay-rgba','overlay-bgra','overlay-opaque','overlay-native555','overlay-byte','overlay-word','overlay-coverage','overlay-map','overlay-alias-byte','overlay-alias-word','overlay-alias-native15','overlay-solid','hardlight-rgba','hardlight-bgra','hardlight-opaque','hardlight-native555','hardlight-byte','hardlight-word','hardlight-coverage','hardlight-map','hardlight-alias-byte','hardlight-alias-word','hardlight-alias-native15','hardlight-solid','piecewise-bytes','piecewise-guards',
         'djclear-rgba','djclear-bgra','djclear-opaque','djclear-native555','djclear-byte','djclear-word','djclear-coverage','djclear-map','djclear-alias-byte','djclear-alias-word','djclear-alias-native15','djclear-solid','djsrc-rgba','djsrc-bgra','djsrc-opaque','djsrc-native555','djsrc-byte','djsrc-word','djsrc-coverage','djsrc-map','djsrc-alias-byte','djsrc-alias-word','djsrc-alias-native15','djsrc-solid','djdst-rgba','djdst-bgra','djdst-opaque','djdst-native555','djdst-byte','djdst-word','djdst-coverage','djdst-map','djdst-alias-byte','djdst-alias-word','djdst-alias-native15','djdst-solid','cjclear-rgba','cjclear-bgra','cjclear-opaque','cjclear-native555','cjclear-byte','cjclear-word','cjclear-coverage','cjclear-map','cjclear-alias-byte','cjclear-alias-word','cjclear-alias-native15','cjclear-solid','cjsrc-rgba','cjsrc-bgra','cjsrc-opaque','cjsrc-native555','cjsrc-byte','cjsrc-word','cjsrc-coverage','cjsrc-map','cjsrc-alias-byte','cjsrc-alias-word','cjsrc-alias-native15','cjsrc-solid','cjdst-rgba','cjdst-bgra','cjdst-opaque','cjdst-native555','cjdst-byte','cjdst-word','cjdst-coverage','cjdst-map','cjdst-alias-byte','cjdst-alias-word','cjdst-alias-native15','cjdst-solid','djoverrev-rgba','djoverrev-bgra','djoverrev-opaque','djoverrev-native555','djoverrev-byte','djoverrev-word','djoverrev-coverage','djoverrev-map','djoverrev-alias-byte','djoverrev-alias-word','djoverrev-alias-native15','djoverrev-solid','djoverrev-opaque-source','djoverrev-solid16-native','djoverrev-solid16-packed','djoverrev-solid16-coverage','pd-basic-solid-clear','pd-basic-solid-src','pd-basic-solid-dst','pd-guards','pd-rounding','djover-rgba','djover-bgra','djover-opaque','djover-native555','djover-byte','djover-word','djover-coverage','djover-map','djover-alias-byte','djover-alias-word','djover-alias-native15','djover-solid','djin-rgba','djin-bgra','djin-opaque','djin-native555','djin-byte','djin-word','djin-coverage','djin-map','djin-alias-byte','djin-alias-word','djin-alias-native15','djin-solid','djinrev-rgba','djinrev-bgra','djinrev-opaque','djinrev-native555','djinrev-byte','djinrev-word','djinrev-coverage','djinrev-map','djinrev-alias-byte','djinrev-alias-word','djinrev-alias-native15','djinrev-solid','djout-rgba','djout-bgra','djout-opaque','djout-native555','djout-byte','djout-word','djout-coverage','djout-map','djout-alias-byte','djout-alias-word','djout-alias-native15','djout-solid','djoutrev-rgba','djoutrev-bgra','djoutrev-opaque','djoutrev-native555','djoutrev-byte','djoutrev-word','djoutrev-coverage','djoutrev-map','djoutrev-alias-byte','djoutrev-alias-word','djoutrev-alias-native15','djoutrev-solid','djatop-rgba','djatop-bgra','djatop-opaque','djatop-native555','djatop-byte','djatop-word','djatop-coverage','djatop-map','djatop-alias-byte','djatop-alias-word','djatop-alias-native15','djatop-solid','djatoprev-rgba','djatoprev-bgra','djatoprev-opaque','djatoprev-native555','djatoprev-byte','djatoprev-word','djatoprev-coverage','djatoprev-map','djatoprev-alias-byte','djatoprev-alias-word','djatoprev-alias-native15','djatoprev-solid','djxor-rgba','djxor-bgra','djxor-opaque','djxor-native555','djxor-byte','djxor-word','djxor-coverage','djxor-map','djxor-alias-byte','djxor-alias-word','djxor-alias-native15','djxor-solid','cjover-rgba','cjover-bgra','cjover-opaque','cjover-native555','cjover-byte','cjover-word','cjover-coverage','cjover-map','cjover-alias-byte','cjover-alias-word','cjover-alias-native15','cjover-solid','cjoverrev-rgba','cjoverrev-bgra','cjoverrev-opaque','cjoverrev-native555','cjoverrev-byte','cjoverrev-word','cjoverrev-coverage','cjoverrev-map','cjoverrev-alias-byte','cjoverrev-alias-word','cjoverrev-alias-native15','cjoverrev-solid','cjin-rgba','cjin-bgra','cjin-opaque','cjin-native555','cjin-byte','cjin-word','cjin-coverage','cjin-map','cjin-alias-byte','cjin-alias-word','cjin-alias-native15','cjin-solid','cjinrev-rgba','cjinrev-bgra','cjinrev-opaque','cjinrev-native555','cjinrev-byte','cjinrev-word','cjinrev-coverage','cjinrev-map','cjinrev-alias-byte','cjinrev-alias-word','cjinrev-alias-native15','cjinrev-solid','cjout-rgba','cjout-bgra','cjout-opaque','cjout-native555','cjout-byte','cjout-word','cjout-coverage','cjout-map','cjout-alias-byte','cjout-alias-word','cjout-alias-native15','cjout-solid','cjoutrev-rgba','cjoutrev-bgra','cjoutrev-opaque','cjoutrev-native555','cjoutrev-byte','cjoutrev-word','cjoutrev-coverage','cjoutrev-map','cjoutrev-alias-byte','cjoutrev-alias-word','cjoutrev-alias-native15','cjoutrev-solid','cjatop-rgba','cjatop-bgra','cjatop-opaque','cjatop-native555','cjatop-byte','cjatop-word','cjatop-coverage','cjatop-map','cjatop-alias-byte','cjatop-alias-word','cjatop-alias-native15','cjatop-solid','cjatoprev-rgba','cjatoprev-bgra','cjatoprev-opaque','cjatoprev-native555','cjatoprev-byte','cjatoprev-word','cjatoprev-coverage','cjatoprev-map','cjatoprev-alias-byte','cjatoprev-alias-word','cjatoprev-alias-native15','cjatoprev-solid','cjxor-rgba','cjxor-bgra','cjxor-opaque','cjxor-native555','cjxor-byte','cjxor-word','cjxor-coverage','cjxor-map','cjxor-alias-byte','cjxor-alias-word','cjxor-alias-native15','cjxor-solid','pd-ratio-guards','dodge-rgba','dodge-bgra','dodge-opaque','dodge-native555','dodge-byte','dodge-word','dodge-coverage','dodge-map','dodge-alias-byte','dodge-alias-word','dodge-alias-native15','dodge-solid','dodge-solid16-native','dodge-solid16-packed','dodge-solid16-coverage','burn-rgba','burn-bgra','burn-opaque','burn-native555','burn-byte','burn-word','burn-coverage','burn-map','burn-alias-byte','burn-alias-word','burn-alias-native15','burn-solid','burn-solid16-native','burn-solid16-packed','burn-solid16-coverage','float-branches','float-branch-guards','dodge-burn-guards','dodge-burn-registers','float-sqrt','float-signed','float-softlight-guards','softlight-rgba','softlight-bgra','softlight-opaque','softlight-native555','softlight-byte','softlight-word','softlight-coverage','softlight-map','softlight-alias-byte','softlight-alias-word','softlight-alias-native15','softlight-solid','softlight-solid16-native','softlight-solid16-packed','softlight-solid16-coverage','softlight-registers','softlight-guards','hsl-hue-rgba','hsl-hue-bgra','hsl-hue-opaque','hsl-hue-native555','hsl-hue-byte','hsl-hue-word','hsl-hue-coverage','hsl-hue-map','hsl-hue-alias-byte','hsl-hue-alias-word','hsl-hue-alias-native15','hsl-hue-solid','hsl-hue-solid16-native','hsl-hue-solid16-packed','hsl-hue-solid16-coverage','hsl-saturation-rgba','hsl-saturation-bgra','hsl-saturation-opaque','hsl-saturation-native555','hsl-saturation-byte','hsl-saturation-word','hsl-saturation-coverage','hsl-saturation-map','hsl-saturation-alias-byte','hsl-saturation-alias-word','hsl-saturation-alias-native15','hsl-saturation-solid','hsl-saturation-solid16-native','hsl-saturation-solid16-packed','hsl-saturation-solid16-coverage','hsl-color-rgba','hsl-color-bgra','hsl-color-opaque','hsl-color-native555','hsl-color-byte','hsl-color-word','hsl-color-coverage','hsl-color-map','hsl-color-alias-byte','hsl-color-alias-word','hsl-color-alias-native15','hsl-color-solid','hsl-color-solid16-native','hsl-color-solid16-packed','hsl-color-solid16-coverage','hsl-luminosity-rgba','hsl-luminosity-bgra','hsl-luminosity-opaque','hsl-luminosity-native555','hsl-luminosity-byte','hsl-luminosity-word','hsl-luminosity-coverage','hsl-luminosity-map','hsl-luminosity-alias-byte','hsl-luminosity-alias-word','hsl-luminosity-alias-native15','hsl-luminosity-solid','hsl-luminosity-solid16-native','hsl-luminosity-solid16-packed','hsl-luminosity-solid16-coverage','hsl-registers','hsl-boundaries','hsl-tiny','hsl-guards','float-routing','float-routing-compare','float-routing-guards','float-tiny')
@unittest.skipUnless(shutil.which('cc'), 'native compiler required')
class NativeComposite(unittest.TestCase):
    def test_strided_GS_coverage_roundtrip_boundaries_and_submission_budget(self):self.run_case('coverage-strips')
    def test_readonly_protocol_colour_byte_glyphs(self):self.run_case('borrowed-byte-glyphs')
    def test_readonly_protocol_colour_word_glyphs(self):self.run_case('borrowed-word-glyphs')
    @classmethod
    def setUpClass(cls):
        cls.folder = tempfile.TemporaryDirectory(); headers(cls.folder.name)
        cls.binary = Path(cls.folder.name) / 'composite'
        (Path(cls.folder.name) / 'previous-rop.c').write_text(
            (ROOT / 'tests/graphics/ps2-rop.c').read_text()
            .replace('int main(', 'int previous_rop_main(')
            .replace('int __wrap_ioctl(', 'int rop_ioctl('))
        common = ['cc', '-std=gnu17', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function',
                  '-fsanitize=address,undefined', '-fno-pie', '-no-pie',
                  '-DUSE_GCC_ATOMIC_BUILTINS', *copy_flags()]
        ref = Path(cls.folder.name) / 'reference.o'
        subprocess.run([*common, '-DmiFillPolygon=miReferenceFillPolygon', '-c',
                        str(XORG / 'mi/mipoly.c'), '-o', str(ref)], check=True)
        subprocess.run([*common, '-I' + cls.folder.name, '-I' + str(ROOT / 'tests/graphics'),
                        *(str(ROOT / p) for p in ROP_SOURCES), str(XORG / 'mi/micopy.c'), str(ref),
                        str(ROOT / 'tests/graphics/ps2-composite.c'),
                        *['-Wl,--wrap=' + n for n in ('open', 'close', 'ioctl', 'nanosleep', 'calloc', 'malloc')],
                        '-latomic', '-lpixman-1', '-lm', '-o', str(cls.binary)], check=True)
    @classmethod
    def tearDownClass(cls): cls.folder.cleanup()
    def run_case(self, name): subprocess.run([str(self.binary), name], check=True, timeout=120)
for _case in CASES:
    setattr(NativeComposite, 'test_' + _case.replace('-', '_'), lambda self, n=_case: self.run_case(n))
if __name__ == '__main__': unittest.main()
