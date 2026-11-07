"""Execute the actual diagnostic; verify text restoration on failure/SIGINT."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]


@unittest.skipUnless(shutil.which('cc'),'native compiler required')
class GSUploadProbe(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.folder=tempfile.TemporaryDirectory(); folder=Path(cls.folder.name)
        flags=['-std=gnu17','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',
               '-fno-pie','-I'+str(ROOT/'graphics'),'-I'+str(ROOT/'kernel/new-drivers')]
        subprocess.run(['cc',*flags,'-Dmain=ps2_probe_main','-c',
            str(ROOT/'desktop/ps2-gs-upload-test.c'),'-o',str(folder/'probe.o')],check=True)
        cls.binary=folder/'test'
        subprocess.run(['cc',*flags,'-no-pie',str(folder/'probe.o'),
            str(ROOT/'graphics/ps2_gs_client.c'),str(ROOT/'tests/graphics/ps2-gs-probe.c'),
            '-Wl,--wrap=open,--wrap=close,--wrap=ioctl,--wrap=nanosleep',
            '-o',str(cls.binary)],check=True)

    @classmethod
    def tearDownClass(cls): cls.folder.cleanup()
    def test_success_uploads_and_restores_text(self): self.run_case('success')
    def test_hardware_error_stops_without_replay_and_restores_text(self): self.run_case('failure')
    def test_interrupt_restores_text(self): self.run_case('interrupt')
    def run_case(self,case): subprocess.run([str(self.binary),case],check=True,timeout=10)


if __name__=='__main__': unittest.main()
