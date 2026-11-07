"""Stripping must retain executable resources appended beyond ELF sections."""
from pathlib import Path
import io,os,stat,struct,sys,tempfile,unittest,zipfile
from unittest.mock import patch

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'scripts'))
import rootfs,strip_userland

def elf(machine=8,fp_abi=3,contains_zip=False,payload=b''):
    identification=b'\x7fELF\x01\x01\x01'+b'\0'*9
    header=struct.pack('<16sHHIIIIIHHHHHH',identification,2,machine,1,0,52,0,
                       0x20921000,52,32,1,40,0,0)
    attributes=struct.pack('<H6B4I',0,3,0,1,0,0,fp_abi,6,0,0,0)
    segment=struct.pack('<8I',0x70000003,84,0,0,24+(len(payload) if contains_zip else 0),24,4,8)
    return header+segment+attributes+payload

def resources(compression=zipfile.ZIP_DEFLATED):
    data=io.BytesIO()
    with zipfile.ZipFile(data,'w',compression=compression) as archive:
        archive.writestr('lib/ttrace.tcl',b'package provide ttrace 3.0.7\n')
        archive.comment=b'executable resources'
    return data.getvalue()

class StripResources(unittest.TestCase):
    def test_appended_zip_is_crc_validated_and_distinguished_from_elf_contents(self):
        with tempfile.TemporaryDirectory() as temporary:
            path=Path(temporary)/'module.so';payload=resources()
            path.write_bytes(elf(payload=payload))
            result=strip_userland.appended_zip(path)
            self.assertEqual(result['offset'],108)
            self.assertEqual(result['bytes'],len(payload))
            self.assertEqual(result['members'],1)
            self.assertTrue(result['CRC_checked'])
            path.write_bytes(elf(contains_zip=True,payload=payload))
            self.assertIsNone(strip_userland.appended_zip(path))
            path.write_bytes(elf())
            self.assertIsNone(strip_userland.appended_zip(path))

    @unittest.skipIf(os.name=='nt','POSIX mode and hardlink semantics required')
    def test_skip_preserves_bytes_permissions_and_inside_and_outside_hardlinks(self):
        with tempfile.TemporaryDirectory() as temporary:
            root=Path(temporary);stage=root/'stage';stage.mkdir()
            path=stage/'module.so';path.write_bytes(elf(payload=resources()))
            path.chmod(0o4751)
            alias=stage/'alias.so';outside=root/'outside.so'
            os.link(path,alias);os.link(path,outside)
            before=path.read_bytes();inode=path.stat().st_ino
            with patch.object(strip_userland.subprocess,'run') as tools:
                result=strip_userland.strip_tree(stage,'forbidden-',root/'report.json')
            tools.assert_not_called()
            self.assertEqual(result['binary_bytes_saved'],0)
            self.assertIn('valid appended ZIP',result['skipped'][0]['reason'])
            self.assertEqual(set(result['skipped'][0]['paths']),{'module.so','alias.so'})
            for name in (path,alias,outside):
                self.assertEqual(name.read_bytes(),before)
                self.assertEqual(name.stat().st_ino,inode)
                self.assertEqual(stat.S_IMODE(name.stat().st_mode),0o4751)

    def test_zip_resources_do_not_bypass_target_abi_validation(self):
        with tempfile.TemporaryDirectory() as temporary:
            root=Path(temporary);stage=root/'stage';stage.mkdir()
            path=stage/'module.so'
            for parameters in ({'machine':62},{'fp_abi':1}):
                with self.subTest(parameters=parameters):
                    path.write_bytes(elf(**parameters,payload=resources()))
                    with patch.object(strip_userland.subprocess,'run') as tools:
                        with self.assertRaises((ValueError,rootfs.BuildError)):
                            strip_userland.strip_tree(stage,'forbidden-',root/'report.json')
                    tools.assert_not_called()

    def test_corrupt_zip_resources_fail_without_modifying_the_binary(self):
        with tempfile.TemporaryDirectory() as temporary:
            root=Path(temporary);stage=root/'stage';stage.mkdir()
            original=elf(payload=resources(zipfile.ZIP_STORED))
            corrupt=original.replace(b'package provide ttrace',b'package provide broken')
            self.assertEqual(len(corrupt),len(original))
            path=stage/'module.so';path.write_bytes(corrupt)
            with patch.object(strip_userland.subprocess,'run') as tools:
                with self.assertRaisesRegex(ValueError,'Corrupt appended ZIP member'):
                    strip_userland.strip_tree(stage,'forbidden-',root/'report.json')
            tools.assert_not_called();self.assertEqual(path.read_bytes(),corrupt)

    def test_ordinary_elf_still_takes_the_validated_strip_path(self):
        with tempfile.TemporaryDirectory() as temporary:
            root=Path(temporary);stage=root/'stage';stage.mkdir()
            path=stage/'program';path.write_bytes(elf())
            with patch.object(strip_userland,'symbols',return_value=[]), \
                 patch.object(strip_userland.subprocess,'run') as tools:
                result=strip_userland.strip_tree(stage,'target-',root/'report.json')
            self.assertEqual(tools.call_count,1)
            self.assertEqual(tools.call_args.args[0][:2],['target-strip','--strip-unneeded'])
            self.assertEqual(result['skipped'],[])
            self.assertEqual(len(result['files']),1)
            self.assertTrue(result['files'][0]['runtime_or_SDK_contract_preserved'])

if __name__=='__main__':unittest.main()
