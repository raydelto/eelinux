"""Compile the actual upload validator/GIF builder with real PS2 UAPI layouts."""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
SOURCE=Path(os.environ.get('PS2_KERNEL_SOURCE',
    str(Path.home()/'.cache/eelinux/sources/linux-7.2.8-ps2-gs')))


def prepare_packet_headers(folder, source=SOURCE):
    folder=Path(folder); (folder/'asm').mkdir(parents=True,exist_ok=True)
    for name in ('gif.h','gs.h','bitfield.h'):
        shutil.copyfile(source/'arch/mips/include/uapi/asm'/name,folder/'asm'/name)
    text=(ROOT/'kernel/new-drivers/ps2fb-gs-image.h').read_text()
    # Pure functions are the exact prefix of the driver, not reimplementations.
    pure,sep,_=text.partition('static int ps2gs_upload_execute(')
    assert sep and 'ps2gs_validate_upload' in pure and 'ps2gs_package_upload' in pure
    (folder/'ps2-gs-image-packet.h').write_text(pure)


@unittest.skipUnless(shutil.which('cc') and (SOURCE/'arch/mips/include/uapi/asm/gif.h').is_file(),
                    'native compiler and actual PS2 kernel UAPI required')
class GSImageKernel(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.folder=tempfile.TemporaryDirectory(); cls.binary=Path(cls.folder.name)/'image'
        prepare_packet_headers(cls.folder.name)
        subprocess.run(['cc','-std=gnu17','-Wall','-Wextra','-Werror',
            '-fsanitize=address,undefined','-fno-pie','-no-pie',
            '-I'+cls.folder.name,'-I'+str(ROOT/'kernel/new-drivers'),
            str(ROOT/'tests/graphics/ps2-gs-image.c'),'-o',str(cls.binary)],check=True)

    @classmethod
    def tearDownClass(cls): cls.folder.cleanup()
    def test_kernel_rejects_overflow_stale_mode_and_invalid_sources(self):
        subprocess.run([str(self.binary),'validation'],check=True,timeout=10)
    def test_real_gif_encodings_exact_payload_padding_and_fence(self):
        subprocess.run([str(self.binary),'packet'],check=True,timeout=10)


if __name__=='__main__': unittest.main()
