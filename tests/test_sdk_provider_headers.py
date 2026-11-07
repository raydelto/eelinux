"""An older archive timestamp must not retain an obsolete installed SDK header."""
from pathlib import Path
from source_archives import source_archive
import os,shutil,subprocess,sys,tarfile,tempfile,time,unittest

ROOT=Path(__file__).resolve().parents[1]

@unittest.skipUnless(sys.platform=='linux' and shutil.which('make') and shutil.which('install'),
                     'Linux installation tools required')
class ProviderHeaders(unittest.TestCase):
    def test_skalibs_upgrade_replaces_newer_dated_old_headers(self):
        with tempfile.TemporaryDirectory() as temporary:
            root=Path(temporary);source=root/'source';include=source/'src/include/skalibs'
            include.mkdir(parents=True)
            with tarfile.open(source_archive('skalibs-2.16.0.0.tar.gz')) as archive:
                expected=archive.extractfile('skalibs-2.16.0.0/src/include/skalibs/netstring.h').read()
            (include/'netstring.h').write_bytes(expected)
            destinations=[root/name for name in ('staging','target','host')]
            for destination in destinations:
                folder=destination/'include/skalibs';folder.mkdir(parents=True)
                path=folder/'netstring.h';path.write_text('obsolete provider declarations\n')
                os.utime(path,(time.time()+86400,time.time()+86400))
            makefile=root/'Makefile'
            makefile.write_text(f'BR2_EXTERNAL_EELINUX_PATH := {ROOT/"userland"}\n'
                f'STAGING_DIR := {destinations[0]}\nTARGET_DIR := {destinations[1]}\nHOST_DIR := {destinations[2]}\n'
                f'INSTALL := install\ninclude {ROOT/"userland/external.mk"}\n'
                f'{source/"checked"}:\n\t$(EELINUX_SKALIBS_REFRESH_STAGING_HEADERS)\n'
                '\t$(EELINUX_SKALIBS_REFRESH_TARGET_HEADERS)\n'
                '\t$(EELINUX_HOST_SKALIBS_REFRESH_HEADERS)\n')
            result=subprocess.run(['make','--no-print-directory','-f',str(makefile)],capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            for destination in destinations:
                self.assertEqual((destination/'include/skalibs/netstring.h').read_bytes(),expected)
                self.assertIn(b'mininetstring_read',expected)

if __name__=='__main__':unittest.main()
