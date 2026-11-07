"""Exercise the actual finalizer without the removed c_rehash program."""
from pathlib import Path
import os,shutil,subprocess,sys,tempfile,unittest

ROOT=Path(__file__).resolve().parents[1]

@unittest.skipUnless(sys.platform=='linux' and shutil.which('make') and shutil.which('openssl'),
                     'Linux OpenSSL and Make required')
class CertificateFinalization(unittest.TestCase):
    def test_real_bundle_and_hash_links_use_supported_openssl_command(self):
        with tempfile.TemporaryDirectory() as temporary:
            work=Path(temporary);target=work/'target';host=work/'host';build=work/'build'
            source=target/'usr/share/ca-certificates';source.mkdir(parents=True)
            (host/'bin').mkdir(parents=True);build.mkdir()
            # A host install with only the supported openssl executable.
            (host/'bin/openssl').symlink_to(shutil.which('openssl'))
            for name in ('a','b'):
                subprocess.run(['openssl','req','-x509','-newkey','rsa:2048','-nodes',
                    '-keyout',str(work/(name+'.key')),'-out',str(source/(name+'.crt')),
                    '-days','1','-subj','/CN=eeLinux disposable '+name],check=True,
                    stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
            expected=b''.join(path.read_bytes() for path in sorted(source.glob('*.crt')))
            makefile=work/'Makefile'
            makefile.write_text(f'BR2_EXTERNAL_EELINUX_PATH := {ROOT/"userland"}\n'
                f'TARGET_DIR := {target}\nHOST_DIR := {host}\nBUILD_DIR := {build}\nINSTALL := install\n'
                f'include {ROOT/"userland/external.mk"}\n'
                'finalize:\n\t$(CA_CERTIFICATES_GEN_BUNDLE)\n')
            result=subprocess.run(['make','--no-print-directory','-f',str(makefile),'finalize'],
                                  capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            certificates=target/'etc/ssl/certs'
            self.assertEqual((certificates/'ca-certificates.crt').read_bytes(),expected)
            self.assertEqual(len(list(certificates.glob('*.0'))),2,
                             result.stdout+result.stderr+'\n'+repr(list(certificates.iterdir())))
            for path in certificates.glob('*.0'):
                self.assertTrue(path.is_symlink())
                self.assertTrue(path.resolve().is_relative_to(source))
            self.assertFalse((host/'bin/c_rehash').exists())

if __name__=='__main__':unittest.main()
