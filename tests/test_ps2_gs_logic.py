"""Real kernel logic packets/provider; independent Boolean pixel oracle."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from test_ps2_gs_texture import ROOT, headers as texture_headers
from kernel_gs_logic_port import adapt_provider


def headers(folder):
    texture_headers(folder)
    p = Path(folder) / 'ps2fb-gs-device.h'
    p.write_text(adapt_provider(p.read_text()))


@unittest.skipUnless(shutil.which('cc'), 'native compiler required')
class SolidLogicKernel(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.folder = tempfile.TemporaryDirectory()
        headers(cls.folder.name)
        cls.binary = Path(cls.folder.name) / 'logic'
        subprocess.run(['cc', '-std=gnu17', '-Wall', '-Wextra', '-Werror',
            '-Wno-unused-function', '-Wno-unused-parameter', '-pthread',
            '-fsanitize=address,undefined', '-fno-pie', '-no-pie', '-I'+cls.folder.name,
            '-I'+str(ROOT/'kernel/new-drivers'), '-I'+str(ROOT/'tests/graphics'),
            str(ROOT/'tests/graphics/ps2-gs-logic.c'), '-o', str(cls.binary)], check=True)

    @classmethod
    def tearDownClass(cls): cls.folder.cleanup()
    def run_case(self, case): subprocess.run([str(self.binary), case], check=True, timeout=30)
    def test_all_solid_alu_results_against_independent_ct32_and_ct16_oracle(self): self.run_case('pixels')
    def test_packets_have_bounded_owned_addresses_masks_blend_and_geometry(self): self.run_case('packets')
    def test_invalid_handles_depth_roles_modes_and_requests_never_draw(self): self.run_case('validation')
    def test_stale_or_failed_session_never_replays_and_cleanup_releases_owner(self): self.run_case('failure')


if __name__ == '__main__': unittest.main()
