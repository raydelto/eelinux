"""Shipping media must preserve boot payloads and reject incomplete userland."""
import argparse,json,os,shutil,struct,subprocess,sys,tempfile,unittest
from pathlib import Path
from unittest.mock import patch
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'scripts'))
import ps2_media,iso9660

class ProductionMedia(unittest.TestCase):
    def test_usb_first_boot_service_is_private_silent_and_removed_from_other_media(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);stage=root/'stage';stage.mkdir()
            source=root/'project/scripts';source.mkdir(parents=True)
            (source/'expand-usb-rootfs.sh').write_text('#!/bin/bash\nexit 0\n')
            kept=stage/'etc/kept';kept.parent.mkdir();kept.write_text('preserved')
            with patch.object(ps2_media,'ROOT',root/'project'):
                ps2_media.configure_usb_expansion(stage,True)
            self.assertEqual((stage/'expand-usb-rootfs.sh').stat().st_mode&0o777,0o700)
            self.assertEqual((stage/'etc/eelinux/usb-rootfs-layout').read_text(),'eeLinux USB root expansion v1\n')
            hook=stage/'etc/init.d/usb-root-expand'
            self.assertIn('>/dev/null 2>&1 || :',hook.read_text())
            link=stage/'etc/rc.d/rcS.d/S19usb-root-expand'
            self.assertTrue(link.is_symlink());self.assertEqual(link.resolve(),hook)
            ps2_media.configure_usb_expansion(stage,False)
            self.assertTrue(kept.exists())
            for relative in ps2_media.USB_EXPANSION_PATHS:
                path=stage/relative
                self.assertFalse(path.exists() or path.is_symlink(),relative)

    def test_optical_modules_match_kernel_manifest_and_replace_only_the_guest_release(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);stage=root/'stage';payload=root/'payload'
            relative='lib/modules/7.2.8-eelinux-ps2/kernel/net/tun.ko'
            module=payload/relative;module.parent.mkdir(parents=True);module.write_bytes(b'module fixture')
            stale=stage/'lib/modules/7.2.8-eelinux-ps2/stale.ko'
            stale.parent.mkdir(parents=True);stale.write_bytes(b'old')
            other=stage/'lib/modules/another-release/kept.ko'
            other.parent.mkdir();other.write_bytes(b'preserved')
            report=root/'kernel.json'
            record={'kernel_release':'7.2.8-eelinux-ps2','modules_directory':str(payload),
                    'modules':{relative:ps2_media.sha(module)},'optional_networking_modules':True}
            report.write_text(json.dumps(record))
            self.assertEqual(ps2_media.install_kernel_modules(stage,report),[relative])
            self.assertEqual((stage/relative).read_bytes(),module.read_bytes())
            self.assertFalse(stale.exists());self.assertTrue(other.exists())
            module.write_bytes(b'changed')
            with self.assertRaisesRegex(RuntimeError,'checksum mismatch'):ps2_media.install_kernel_modules(stage,report)
            record['modules']={'lib/modules/7.2.8-eelinux-ps2/../../outside':ps2_media.sha(module)}
            report.write_text(json.dumps(record))
            with self.assertRaisesRegex(RuntimeError,'Unsafe kernel module'):ps2_media.install_kernel_modules(stage,report)

    def test_separate_media_targets_reuse_private_default_password_and_reject_invalid_hash(self):
        with tempfile.TemporaryDirectory() as folder:
            args=argparse.Namespace(work=Path(folder),root_password_hash_file=None)
            first=ps2_media.password_hash(args);second=ps2_media.password_hash(args)
            self.assertEqual(first,second)
            self.assertEqual((args.work/'media-root-password.sha512').stat().st_mode&0o777,0o600)
            args.root_password_hash_file=args.work/'bad';args.root_password_hash_file.write_text('invalid\n')
            with self.assertRaisesRegex(RuntimeError,'SHA-512 crypt'):ps2_media.password_hash(args)
    def test_fat_append_preserves_fixed_payload_and_replaces_only_primary_descriptor(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);old=root/'old.iso';session=root/'session.iso';out=root/'full.iso'
            before=bytearray(b'A'*(32*2048));before[16*2048:16*2048+7]=b'\x01CD001\x01'
            appended=bytearray(b'B'*(24*2048));appended[16*2048:16*2048+7]=b'\x01CD001\x01'
            old.write_bytes(before);session.write_bytes(appended)
            report=ps2_media.fat_append(old,session,out)
            result=out.read_bytes()
            self.assertEqual(old.read_bytes(),before)
            self.assertEqual(result[:16*2048],before[:16*2048])
            descriptor=bytearray(appended[16*2048:17*2048])
            struct.pack_into('<I',descriptor,80,56);struct.pack_into('>I',descriptor,84,56)
            self.assertEqual(result[16*2048:17*2048],descriptor)
            self.assertEqual(result[17*2048:len(before)],before[17*2048:])
            self.assertEqual(result[len(before):],appended)
            self.assertTrue(report['exploit_and_UDF_sectors_byte_exact'])

    def test_dvd_requires_native_kernel_and_staging_without_usb_image(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);kernel=root/'kernel.elf';kernel.write_bytes(b'kernel')
            config=root/'config';config.write_text(''.join('CONFIG_'+f+'=y\n' for f in
                ('PS2_CDVD','ISO9660_FS','OVERLAY_FS','TMPFS_XATTR','FB_PS2','ZRAM','ZRAM_BACKEND_LZ4')))
            report=root/'report.json';report.write_text(json.dumps({'root_device':'/dev/ps2dvd',
                'USB_required':False,'image_sha256':'checked','config_sha256':'checked',
                'boot_mode':'native Rock Ridge ISO9660 /eelinux/root, RAM overlay'}))
            args=argparse.Namespace(dvd_kernel=kernel,dvd_kernel_config=config,dvd_kernel_report=report,
                staging=None,output_dir=root)
            with patch.object(ps2_media,'sha',return_value='checked'):
                with self.assertRaisesRegex(RuntimeError,'complete GNU userland'):ps2_media.dvd_inputs(args)
                args.staging=root
                self.assertFalse(ps2_media.dvd_inputs(args)['USB_required'])
                config.write_text(config.read_text().replace('CONFIG_ZRAM=y\n',''))
                with self.assertRaisesRegex(RuntimeError,'DVD kernel needs ZRAM'):ps2_media.dvd_inputs(args)
                config.write_text(config.read_text()+'CONFIG_ZRAM=y\n')
                old=json.loads(report.read_text());old['boot_mode']='loop-mounted USB image'
                report.write_text(json.dumps(old))
                with self.assertRaisesRegex(RuntimeError,'native Rock Ridge'):ps2_media.dvd_inputs(args)

    @unittest.skipUnless(shutil.which('genisoimage'),'Linux ISO builder required')
    def test_rock_ridge_preserves_gnu_links_secure_permissions_and_all_file_bytes(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder)/'root';root.mkdir();(root/'etc').mkdir();(root/'bin').mkdir()
            (root/'tmp').mkdir();(root/'tmp').chmod(0o1777)
            shadow=root/'etc/shadow';shadow.write_text('qualification fixture\n');shadow.chmod(0o600)
            command=root/'bin/tool';command.write_bytes(bytes(range(256))*10);command.chmod(0o755)
            os.link(command,root/'bin/alias');(root/'bin/sh').symlink_to('/bin/tool')
            (root/'etc/Autoridad_de_Certificacion_Firmaprofesional_CIF_A62634068.pem').symlink_to('../../../usr/share/ca-certificates/mozilla/Autoridad_de_Certificacion_Firmaprofesional_CIF_A62634068.crt')
            image=Path(folder)/'system.iso';expected=iso9660.tree_manifest(root)
            subprocess.run(['genisoimage','-quiet','-R','-udf','-no-split-symlink-components','-no-split-symlink-fields',
                '-uid','0','-gid','0','-o',str(image),
                '-graft-points','eelinux/root='+str(root)],check=True)
            proof=iso9660.verify_root(image,expected)
            self.assertTrue(proof['all_file_bytes_permissions_symlinks_root_ownership_verified'])
            shadow.write_text('changed\n')
            with self.assertRaisesRegex(RuntimeError,'differs'):iso9660.verify_root(image,iso9660.tree_manifest(root))

if __name__=='__main__':unittest.main()
