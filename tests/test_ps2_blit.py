"""Native Gallium blit and EXA composite callbacks, independent pixel checks."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from test_ps2_copy_exa import ROOT, EXA_COPY_SOURCES, XORG, copy_flags
from test_ps2_resource_copy import headers as copy_headers

BLIT_SOURCES = EXA_COPY_SOURCES + ['desktop/xorg/ps2_blit_exa.c']
CASES = ('scale', 'flips', 'scissor', 'copy', 'preflight', 'unsupported',
         'shared', 'exa', 'lifetime', 'failure', 'installation')
def headers(folder):
    copy_headers(folder)
    p = Path(folder)/'previous-glyph.c'
    p.write_text(p.read_text().replace('Bool dixRegisterPrivateKey(', 'Bool original_register_private('))

@unittest.skipUnless(shutil.which('cc'), 'native compiler required')
class Blit(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.folder = tempfile.TemporaryDirectory(); headers(cls.folder.name)
        cls.binary = Path(cls.folder.name)/'blit'
        subprocess.run(['cc', '-std=gnu17', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function',
            '-fsanitize=address,undefined', '-fno-pie', '-no-pie', '-DUSE_GCC_ATOMIC_BUILTINS',
            *copy_flags(), '-I'+cls.folder.name, '-I'+str(ROOT/'tests/graphics'),
            *(str(ROOT/p) for p in BLIT_SOURCES), str(XORG/'mi/micopy.c'),
            str(ROOT/'tests/graphics/ps2-blit.c'),
            *['-Wl,--wrap='+n for n in ('open','close','ioctl','nanosleep','calloc')],
            '-latomic', '-lpixman-1','-lm', '-o', str(cls.binary)], check=True)
    @classmethod
    def tearDownClass(cls): cls.folder.cleanup()
    def run_case(self, name): subprocess.run([str(self.binary), name], check=True, timeout=30)
    def test_nearest_up_down_anisotropic_and_npot_rgb_formats(self): self.run_case('scale')
    def test_horizontal_vertical_double_flips_and_cropped_sources(self): self.run_case('flips')
    def test_scissor_preserves_original_sampling_and_empty_clip_queue(self): self.run_case('scissor')
    def test_unscaled_odd_width_and_overlapping_copies_use_native_batch(self): self.run_case('copy')
    def test_complete_geometry_storage_and_scissor_preflight_precedes_flush(self): self.run_case('preflight')
    def test_unsupported_format_filter_mask_depth_layers_and_owners_do_not_draw(self): self.run_case('unsupported')
    def test_mask_noops_and_blits_preserve_mesa_bindings_and_references(self): self.run_case('shared')
    def test_actual_exa_composite_callback_front_owned_and_self_copy(self): self.run_case('exa')
    def test_closed_context_and_reverse_composite_cleanup_release_once(self): self.run_case('lifetime')
    def test_submission_failure_sticks_without_replay_or_extra_pixels(self): self.run_case('failure')
    def test_installation_picture_validation_and_cleanup_are_transactional(self): self.run_case('installation')
if __name__ == '__main__': unittest.main()
