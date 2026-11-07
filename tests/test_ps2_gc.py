"""Actual Xorg GC lifecycle/text hooks with mutable lower callback fixtures."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from test_ps2_glyph import ROOT, GLYPH_SOURCES, headers as glyph_headers, glyph_flags

GC_SOURCES = GLYPH_SOURCES + ['desktop/xorg/ps2_gc.c']


def headers(folder):
    glyph_headers(folder)
    text = (ROOT / 'tests/graphics/ps2-glyph.c').read_text()
    text = text.replace('int main(', 'int previous_glyph_main(')
    (Path(folder) / 'previous-glyph.c').write_text(text)


@unittest.skipUnless(shutil.which('cc'), 'native compiler required')
class GCLifecycle(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.folder = tempfile.TemporaryDirectory()
        headers(cls.folder.name)
        cls.binary = Path(cls.folder.name) / 'gc'
        subprocess.run(['cc', '-std=gnu17', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function',
            '-fsanitize=address,undefined', '-fno-pie', '-no-pie', '-DUSE_GCC_ATOMIC_BUILTINS',
            *glyph_flags(), '-I'+cls.folder.name, '-I'+str(ROOT/'tests/graphics'),
            *(str(ROOT/p) for p in GC_SOURCES), str(ROOT/'tests/graphics/ps2-gc.c'),
            *['-Wl,--wrap='+n for n in ('open', 'close', 'ioctl', 'nanosleep', 'calloc')],
            '-latomic', '-lpixman-1','-lm', '-o', str(cls.binary)], check=True)

    @classmethod
    def tearDownClass(cls):
        cls.folder.cleanup()

    def run_case(self, case):
        subprocess.run([str(self.binary), case], check=True, timeout=15)

    def test_repeated_validation_and_font_requests_retain_native_hooks(self): self.run_case('text')
    def test_gc_changes_and_clip_copy_preserve_both_gc_owners(self): self.run_case('changes')
    def test_lower_drawing_table_replacements_restore_text_hooks_and_return_values(self): self.run_case('rewrap')
    def test_shutdown_restores_callbacks_before_context_and_lower_screen_close(self): self.run_case('lifetime')
    def test_create_failure_and_private_allocation_failure_are_transactional(self): self.run_case('creation')
    def test_bounded_gc_count_rejects_extra_creation_and_cleans_all_live_gcs(self): self.run_case('limit')
    def test_malformed_text_rejects_before_native_gpu_submission(self): self.run_case('invalid')
    def test_failed_hardware_operation_cannot_be_replayed_by_revalidation(self): self.run_case('failure')


if __name__ == '__main__':
    unittest.main()
