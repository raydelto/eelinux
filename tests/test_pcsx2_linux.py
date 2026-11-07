"""Prepare only reviewed USB edits and reject source drift before PCSX2 builds."""
from pathlib import Path
import argparse,json,shutil,subprocess,sys,tempfile,unittest
from unittest.mock import patch

ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT/'scripts'))
import emulator_pcsx2 as linux

@unittest.skipUnless(shutil.which('git'),'git required')
class LinuxSourceGuard(unittest.TestCase):
    def setUp(self):
        self.temporary=tempfile.TemporaryDirectory();self.root=Path(self.temporary.name)
        self.source=self.root/'source';(self.source/'pcsx2/USB/usb-msd').mkdir(parents=True)
        self.mmu=self.source/'pcsx2/MMU.cpp';self.mmu.write_bytes(b'int mode = 0;\nint stable = 1;\n')
        self.usb=self.source/'pcsx2/USB/usb-msd/usb-msd.cpp'
        self.usb.write_bytes(b'int last = count;\nint untouched = 1;\n')
        for command in (['init'],['config','core.autocrlf','false'],['config','user.name','Fixture'],
                        ['config','user.email','fixture@example.invalid'],['add','.'],['commit','-m','fixture']):
            self.git(*command)
        revision=linux.capture(['git','-C',self.source,'rev-parse','HEAD']).strip()
        mmu_patch=self.root/'mmu.patch'
        mmu_patch.write_bytes(b'--- a/pcsx2/MMU.cpp\n+++ b/pcsx2/MMU.cpp\n@@ -1,2 +1,2 @@\n-int mode = 0;\n+int mode = 1;\n int stable = 1;\n')
        self.capacity=self.root/'usb.patch'
        self.capacity.write_bytes(b'--- a/pcsx2/USB/usb-msd/usb-msd.cpp\n+++ b/pcsx2/USB/usb-msd/usb-msd.cpp\n@@ -1,2 +1,2 @@\n-int last = count;\n+int last = count - 1;\n int untouched = 1;\n')
        self.git('-c','core.autocrlf=false','apply',str(mmu_patch))
        self.data={'source_revision':revision,'mmu_patch':'mmu.patch','mmu_patch_sha256':linux.sha256(mmu_patch),
                   'modified_source_files':['pcsx2/MMU.cpp'],
                   'additional_patches':[{'name':'capacity','path':'usb.patch','sha256':linux.sha256(self.capacity),
                                         'source_files':['pcsx2/USB/usb-msd/usb-msd.cpp']}]}
        self.manifest=self.root/'manifest.json';self.manifest.write_text(json.dumps(self.data))
        self.patches=[patch.object(linux,'ROOT',self.root),patch.object(linux,'MANIFEST',self.manifest),patch.object(linux,'linux_only')]
        for item in self.patches:item.start()

    def tearDown(self):
        for item in reversed(self.patches):item.stop()
        self.temporary.cleanup()

    def git(self,*args):
        return subprocess.run(['git','-C',str(self.source),*args],check=True,capture_output=True)

    def prepare(self,name='prepared.json'):
        return linux.prepare_patches(argparse.Namespace(source=self.source,report=self.root/name))

    def test_explicit_prepare_is_idempotent_and_keeps_original_mmu_bytes(self):
        mmu_bytes=self.mmu.read_bytes()
        before=linux.source_guard(self.source,self.data,allow_unapplied=True)
        self.assertFalse(before['additional_patches'][0]['applied'])
        with self.assertRaisesRegex(RuntimeError,'use prepare-patches'):
            linux.source_guard(self.source,self.data)
        first=self.prepare();second=self.prepare('second.json')
        self.assertEqual(first['patches_applied'],['usb.patch']);self.assertEqual(second['patches_applied'],[])
        self.assertEqual(self.usb.read_bytes(),b'int last = count - 1;\nint untouched = 1;\n')
        self.assertEqual(self.mmu.read_bytes(),mmu_bytes)
        self.assertEqual(len(first['mmu_source_sha256']),1)
        self.assertEqual(len(first['reviewed_source_sha256']),2)
        self.assertTrue(first['additional_patches'][0]['applied'])
        self.assertEqual(first['additional_patches'][0]['sha256'],linux.sha256(self.capacity))
        self.assertFalse(first['compiled']);self.assertFalse(first['qualified_runtime_modified'])

    def test_unprepared_build_rejects_without_applying_patch_or_creating_output(self):
        before=self.usb.read_bytes();output=self.root/'build'
        args=argparse.Namespace(source=self.source,output=output,qt=self.root/'qt',dependencies=self.root/'deps',jobs=1,report=None)
        with patch.object(linux.shutil,'which',return_value='/verified/tool'):
            with self.assertRaisesRegex(RuntimeError,'use prepare-patches'):linux.build(args)
        self.assertEqual(self.usb.read_bytes(),before);self.assertFalse(output.exists())

    def test_unrelated_same_file_edits_are_rejected_before_prepare(self):
        self.usb.write_bytes(self.usb.read_bytes()+b'// unrelated\n')
        before=self.usb.read_bytes()
        with self.assertRaisesRegex(RuntimeError,'byte-exact reviewed'):self.prepare()
        self.assertEqual(self.usb.read_bytes(),before)
        self.assertFalse((self.root/'prepared.json').exists())

    def test_mmu_and_unrecorded_source_drift_are_rejected(self):
        self.mmu.write_bytes(self.mmu.read_bytes()+b'// unrelated\n')
        with self.assertRaisesRegex(RuntimeError,'MMU source differs'):self.prepare()
        self.mmu.write_bytes(b'int mode = 1;\nint stable = 1;\n')
        (self.source/'foreign.cpp').write_bytes(b'unreviewed\n')
        with self.assertRaisesRegex(RuntimeError,'changes beyond'):self.prepare()

    def test_patch_hash_mismatch_and_post_prepare_drift_are_rejected(self):
        self.data['additional_patches'][0]['sha256']='0'*64
        with self.assertRaisesRegex(RuntimeError,'patch differs'):linux.source_guard(self.source,self.data)
        self.data['additional_patches'][0]['sha256']=linux.sha256(self.capacity)
        self.prepare()
        self.usb.write_bytes(self.usb.read_bytes()+b'// post-prepare tampering\n')
        with self.assertRaisesRegex(RuntimeError,'byte-exact reviewed'):linux.source_guard(self.source,self.data)

    def test_additional_patch_cannot_create_undeclared_files(self):
        self.capacity.write_bytes(self.capacity.read_bytes()+b'--- /dev/null\n+++ b/foreign.cpp\n@@ -0,0 +1 @@\n+unreviewed\n')
        self.data['additional_patches'][0]['sha256']=linux.sha256(self.capacity)
        with self.assertRaisesRegex(RuntimeError,'undeclared source'):
            linux.source_guard(self.source,self.data,allow_unapplied=True)
        self.assertFalse((self.source/'foreign.cpp').exists())
        self.assertEqual(self.usb.read_bytes(),b'int last = count;\nint untouched = 1;\n')

    def test_additional_patch_cannot_bypass_the_original_mmu_scope(self):
        self.capacity.write_bytes(self.capacity.read_bytes()+b'--- a/pcsx2/MMU.cpp\n+++ b/pcsx2/MMU.cpp\n@@ -1,2 +1,2 @@\n int mode = 1;\n-int stable = 1;\n+int stable = 2;\n')
        self.data['additional_patches'][0]['sha256']=linux.sha256(self.capacity)
        with self.assertRaisesRegex(RuntimeError,'unexpectedly changed reviewed MMU'):
            linux.source_guard(self.source,self.data,allow_unapplied=True)
        self.assertEqual(self.mmu.read_bytes(),b'int mode = 1;\nint stable = 1;\n')

class PinnedManifest(unittest.TestCase):
    def test_existing_mmu_pin_and_windows_capacity_provenance_remain_separate(self):
        data=json.loads((ROOT/'emulator/pcsx2-linux-build.json').read_text())
        self.assertEqual(data['mmu_patch_sha256'],'3483e1fad391cbe50fdf4e6dab2d6b63ca1bc024d0d9248ff51457e8cb41df85')
        self.assertEqual(len(data['modified_source_files']),15)
        extra=data['additional_patches'];self.assertEqual(len(extra),1)
        self.assertEqual(extra[0]['sha256'],linux.sha256(ROOT/extra[0]['path']))
        self.assertEqual(extra[0]['source_files'],['pcsx2/USB/usb-msd/usb-msd.cpp'])
        windows=json.loads((ROOT/'emulator/pcsx2-windows-build.json').read_text())
        portable=(ROOT/windows['windows_portability_patch']).read_text()
        self.assertIn('+\t\t\t\tlbas = s->file_size / LBA_BLOCK_SIZE - 1;',portable)
        self.assertEqual(windows['windows_portability_patch_sha256'],linux.sha256(ROOT/windows['windows_portability_patch']))

if __name__=='__main__':unittest.main()
