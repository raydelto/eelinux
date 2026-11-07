"""Full assembly must retain the providers' enabled desktop features."""
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'scripts'))
import native_components
import userland


class NativeLibraryFeatures(unittest.TestCase):
    def test_isolated_library_build_keeps_crypto_sql_and_compression_choices(self):
        required=['BR2_PACKAGE_EELINUX_OPENSSL3_COMPAT',
                  'BR2_PACKAGE_LIBOPENSSL_ENABLE_MD4','BR2_PACKAGE_SQLITE_ENABLE_RTREE',
                  'BR2_PACKAGE_SQLITE_ENABLE_FTS3','BR2_PACKAGE_LZ4','BR2_PACKAGE_XZ']
        data=userland.manifest()
        data['profiles']['base']['symbols']+=required
        seen=[]
        def prepare(recipe,*args):
            seen.extend(userland.selected_symbols(recipe,'full'))
            raise RuntimeError('stop before compiling')
        with tempfile.TemporaryDirectory() as temporary:
            work=Path(temporary)
            with patch.object(userland,'manifest',return_value=data), \
                 patch.object(userland,'use_native_linux_path'), \
                 patch.object(userland,'toolchain_probe',return_value={}), \
                 patch.object(userland,'prepare_config',side_effect=prepare), \
                 patch.object(native_components,'seed_sources'):
                with self.assertRaisesRegex(RuntimeError,'stop before compiling'):
                    native_components.build('libraries',work/'build','cross-',1,work,work/'report.json')
        self.assertTrue(set(required)<=set(seen))
        self.assertNotIn('BR2_PACKAGE_OPENSSH',seen)
        self.assertNotIn('BR2_PACKAGE_NMAP',seen)


if __name__=='__main__':unittest.main()
