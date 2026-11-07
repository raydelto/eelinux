"""Native EXA solid ALUs, Mesa queue ordering and hardware failure lifetime."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from test_ps2_pipe import ROOT, PIPE_SOURCES, headers as pipe_headers, render_flags
LOGIC_SOURCES = PIPE_SOURCES + ['mesa/ps2/ps2_logic.c', 'desktop/xorg/ps2_logic_exa.c']


def headers(folder):
    pipe_headers(folder)
    text = (ROOT/'tests/graphics/ps2-pipe.c').read_text()
    text = text.replace('int __wrap_ioctl(', 'int pipe_ioctl(').replace('int main(', 'int previous_logic_pipe_main(')
    (Path(folder)/'previous-logic-pipe.c').write_text(text)


@unittest.skipUnless(shutil.which('cc'), 'native compiler required')
class LogicBackends(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.folder = tempfile.TemporaryDirectory(); headers(cls.folder.name)
        cls.binary = Path(cls.folder.name) / 'logic'
        subprocess.run(['cc', '-std=gnu17', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function',
            '-fsanitize=address,undefined', '-fno-pie', '-no-pie', '-DUSE_GCC_ATOMIC_BUILTINS',
            *render_flags(), '-I'+cls.folder.name, '-I'+str(ROOT/'tests/graphics'),
            *(str(ROOT/p) for p in LOGIC_SOURCES), str(ROOT/'tests/graphics/ps2-logic.c'),
            *['-Wl,--wrap='+n for n in ('open','close','ioctl','nanosleep')], '-latomic', '-lpixman-1','-lm', '-o', str(cls.binary)], check=True)

    @classmethod
    def tearDownClass(cls): cls.folder.cleanup()
    def run_case(self, case): subprocess.run([str(self.binary), case], check=True, timeout=20)
    def test_transport_pre_effect_retries_bounds_and_sticky_hardware_failure(self): self.run_case('transport')
    def test_exa_all_solid_alus_masks_and_shared_mesa_fence(self): self.run_case('exa')
    def test_mesa_resource_roles_queue_order_and_target_depth_restoration(self): self.run_case('mesa')
    def test_feature_absence_and_invalid_resources_reject_before_submission(self): self.run_case('unsupported')
    def test_callback_restoration_precedes_deferred_context_close(self): self.run_case('lifetime')
    def test_failed_exa_work_is_not_replayed_after_cleanup(self): self.run_case('failure')


if __name__ == '__main__': unittest.main()
