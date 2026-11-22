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
                ('PS2_CDVD','ISO9660_FS','OVERLAY_FS','TMPFS_XATTR','FB_PS2','ZRAM','ZRAM_BACKEND_LZ4'))+'CONFIG_CMDLINE="video=ps2fb:720x576p@50"\n')
            report=root/'report.json';report.write_text(json.dumps({'root_device':'/dev/ps2dvd',
                'USB_required':False,'video_mode':'576p','image_sha256':'checked','config_sha256':'checked',
                'boot_mode':'native Rock Ridge ISO9660 /eelinux/root, RAM overlay'}))
            args=argparse.Namespace(dvd_kernel=kernel,dvd_kernel_config=config,dvd_kernel_report=report,
                staging=None,output_dir=root,video_mode='576p')
            with patch.object(ps2_media,'sha',return_value='checked'):
                with self.assertRaisesRegex(RuntimeError,'complete GNU userland'):ps2_media.dvd_inputs(args)
                args.staging=root
                args.video_mode='480p'
                with self.assertRaisesRegex(RuntimeError,'video mode'):ps2_media.dvd_inputs(args)
                args.video_mode='576p'
                self.assertFalse(ps2_media.dvd_inputs(args)['USB_required'])
                config.write_text(config.read_text().replace('CONFIG_ZRAM=y\n',''))
                with self.assertRaisesRegex(RuntimeError,'DVD kernel needs ZRAM'):ps2_media.dvd_inputs(args)
                config.write_text(config.read_text()+'CONFIG_ZRAM=y\n')
                old=json.loads(report.read_text());old['boot_mode']='loop-mounted USB image'
                report.write_text(json.dumps(old))
                with self.assertRaisesRegex(RuntimeError,'native Rock Ridge'):ps2_media.dvd_inputs(args)

    @staticmethod
    def dvd_kernel_triplet(root,name,video_mode,spec,modules=None):
        """Write a minimal DVD kernel/config/report triplet that dvd_inputs accepts."""
        folder=root/name;folder.mkdir()
        kernel=folder/'EELINUX.ELF';kernel.write_bytes(b'kernel-'+name.encode())
        config=folder/'kernel.config';config.write_text(''.join('CONFIG_'+f+'=y\n' for f in
            ('PS2_CDVD','ISO9660_FS','OVERLAY_FS','TMPFS_XATTR','FB_PS2','ZRAM','ZRAM_BACKEND_LZ4'))+
            'CONFIG_CMDLINE="root=/dev/ps2dvd video=ps2fb:'+spec+'"\n')
        report=folder/'kernel-report.json';report.write_text(json.dumps({'root_device':'/dev/ps2dvd',
            'USB_required':False,'video_mode':video_mode,'image_sha256':ps2_media.sha(kernel),
            'config_sha256':ps2_media.sha(config),'boot_mode':'native Rock Ridge ISO9660 /eelinux/root, RAM overlay',
            'kernel_release':'7.2.8-eelinux-ps2','modules':modules if modules is not None else {'net/a.ko':'abc'}}))
        return kernel,config,report

    def test_dvd_inputs_accept_480p_kernel(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder)
            kernel,config,report=self.dvd_kernel_triplet(root,'dvd-480p','480p','720x480p@60')
            args=argparse.Namespace(dvd_kernel=kernel,dvd_kernel_config=config,dvd_kernel_report=report,
                staging=root,output_dir=root,video_mode='480p')
            self.assertEqual(ps2_media.dvd_inputs(args)['video_mode'],'480p')
            # A 576p report or a PAL command line must not pass as 480p media.
            args.video_mode='576p'
            with self.assertRaisesRegex(RuntimeError,'video mode'):ps2_media.dvd_inputs(args)
            args.video_mode='480p'
            config.write_text(config.read_text().replace('720x480p@60','720x576p@50'))
            data=json.loads(report.read_text());data['config_sha256']=ps2_media.sha(config);report.write_text(json.dumps(data))
            with self.assertRaisesRegex(RuntimeError,'video mode'):ps2_media.dvd_inputs(args)

    def test_dvd_fallback_is_required_interlaced_and_module_compatible(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder)
            kernel,config,report=self.dvd_kernel_triplet(root,'dvd-480p','480p','720x480p@60')
            fb_kernel,fb_config,fb_report=self.dvd_kernel_triplet(root,'dvd-ntsc','ntsc','576x384i@60')
            args=argparse.Namespace(dvd_kernel=kernel,dvd_kernel_config=config,dvd_kernel_report=report,
                dvd_fallback_kernel=fb_kernel,dvd_fallback_config=fb_config,dvd_fallback_report=fb_report,
                staging=root,output_dir=root,video_mode='480p')
            primary=ps2_media.dvd_inputs(args)
            self.assertEqual(ps2_media.dvd_fallback(args,primary)['video_mode'],'ntsc')
            # PAL media carries no fallback and ignores the arguments.
            args.video_mode='576p'
            self.assertIsNone(ps2_media.dvd_fallback(args,primary))
            args.video_mode='480p'
            # Mismatched modules.
            data=json.loads(fb_report.read_text());data['modules']={'net/a.ko':'different'};fb_report.write_text(json.dumps(data))
            with self.assertRaisesRegex(RuntimeError,'DVD fallback modules differ'):ps2_media.dvd_fallback(args,primary)
            # A 480p kernel cannot stand in for the interlaced fallback.
            args.dvd_fallback_kernel,args.dvd_fallback_config,args.dvd_fallback_report=kernel,config,report
            with self.assertRaisesRegex(RuntimeError,'video mode'):ps2_media.dvd_fallback(args,primary)
            # Missing fallback.
            args.dvd_fallback_report=None
            with self.assertRaisesRegex(RuntimeError,'requires an interlaced DVD fallback'):ps2_media.dvd_fallback(args,primary)

    def test_ntsc_dvd_compatibility_has_no_pal_only_claims(self):
        for variant in ('slim',*ps2_media.FAT):
            ntsc=ps2_media.COMPATIBILITY['ntsc'][variant]
            self.assertNotIn('PAL',ntsc)
            self.assertNotEqual(ntsc,ps2_media.COMPATIBILITY['pal'][variant])
        self.assertIn('English',ps2_media.COMPATIBILITY['ntsc']['slim'])
        self.assertIn('2.12J',ps2_media.COMPATIBILITY['ntsc']['fat-2.10-2.12'])
        self.assertIn('excludes J',ps2_media.COMPATIBILITY['ntsc']['fat-3.04'])
        self.assertEqual(ps2_media.region('480p'),'ntsc');self.assertEqual(ps2_media.region('576p'),'pal')
        self.assertIn('EENTSC.ELF',ps2_media.boot_readme('480p'))
        self.assertNotIn('EENTSC.ELF',ps2_media.boot_readme('576p'))

    def test_ntsc_usb_fallback_must_match_kernel_modules(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder)
            primary=root/'primary.json'; fallback=root/'fallback.json'
            record={'kernel_release':'7.2.8','modules':{'module.ko':'abc'}}
            primary.write_text(json.dumps(record));fallback.write_text(json.dumps(record))
            args=argparse.Namespace(video_mode='480p',kernel_report=primary,
                fallback_kernel=root/'EENTSC.ELF',fallback_config=root/'fallback.config',
                fallback_report=fallback)
            self.assertEqual(ps2_media.usb_fallback(args)[0],args.fallback_kernel)
            record['modules']['module.ko']='different'
            fallback.write_text(json.dumps(record))
            with self.assertRaisesRegex(RuntimeError,'modules differ'):ps2_media.usb_fallback(args)
            args.fallback_config=None
            with self.assertRaisesRegex(RuntimeError,'requires an interlaced'):ps2_media.usb_fallback(args)
            args.video_mode='576p'
            self.assertIsNone(ps2_media.usb_fallback(args))

    @staticmethod
    def vmg(mask):
        return b'DVDVIDEO-VMG'+bytes(0x23-12)+bytes([mask])+bytes(64)

    def test_freedvdboot_trees_must_be_region_free_dvd_video(self):
        with tempfile.TemporaryDirectory() as folder:
            tree=Path(folder);(tree/'VIDEO_TS').mkdir()
            ifo=tree/'VIDEO_TS/VIDEO_TS.IFO';ifo.write_bytes(self.vmg(0))
            self.assertEqual(ps2_media.verify_freedvdboot_tree(tree),0)
            ifo.write_bytes(self.vmg(0x02))  # bit 1 forbids region 2 (Japan)
            with self.assertRaisesRegex(RuntimeError,r'blocked DVD regions \[2\]'):ps2_media.verify_freedvdboot_tree(tree)
            ifo.write_bytes(self.vmg(0x01))  # region 1 (US/Canada)
            with self.assertRaisesRegex(RuntimeError,r'\[1\]'):ps2_media.verify_freedvdboot_tree(tree)
            ifo.write_bytes(b'DVDVIDEO-VTS'+bytes(64))
            with self.assertRaisesRegex(RuntimeError,'not a DVD-Video VMG'):ps2_media.verify_freedvdboot_tree(tree)
            ifo.write_bytes(self.vmg(0));(tree/'SYSTEM.CNF').write_text('BOOT2 = cdrom0:\\X.ELF;1\r\n')
            with self.assertRaisesRegex(RuntimeError,'must not contain SYSTEM.CNF'):ps2_media.verify_freedvdboot_tree(tree)

    @staticmethod
    def pinned_freedvdboot():
        """Hash-verified pinned FreeDVDBoot files, cached between runs; None when offline."""
        work=Path(os.environ.get('EELINUX_FREEDVDBOOT_CACHE',Path.home()/'.cache/eelinux/test-freedvdboot'))
        try:
            work.mkdir(parents=True,exist_ok=True)
            return ps2_media.fetch_boot(work)[0]
        except OSError:
            return None

    def test_real_pinned_freedvdboot_payloads_are_region_free(self):
        source=self.pinned_freedvdboot()
        if source is None:self.skipTest('pinned FreeDVDBoot files are not cached and the network is unavailable')
        ifo=(source/ps2_media.SLIM/'VIDEO_TS/VIDEO_TS.IFO').read_bytes()
        self.assertEqual(ifo[0x21],0x11)  # DVD-Video specification 1.1: confirms the VMGI_MAT layout
        self.assertEqual(ps2_media.dvd_region_mask(ifo),0)
        self.assertEqual(ps2_media.verify_freedvdboot_tree(source/ps2_media.SLIM),0)
        if not shutil.which('isoinfo'):self.skipTest('isoinfo required for the prebuilt Fat ISOs')
        for name in ps2_media.FAT.values():
            with tempfile.TemporaryDirectory() as folder:
                ps2_media.extract_boot_files(source/'PREBUILT ISOs'/name,Path(folder))
                self.assertEqual(ps2_media.verify_freedvdboot_tree(Path(folder)),0,name)

    def test_test_unit_discs_are_opt_in_and_boot_the_selected_kernel(self):
        self.assertEqual(ps2_media.dvd_variants('all','480p'),['slim','fat-2.10-2.12','fat-3.04'])
        self.assertEqual(ps2_media.dvd_variants('test-unit','480p'),['test-unit','test-unit-interlaced'])
        self.assertEqual(ps2_media.dvd_variants('test-unit','576p'),['test-unit'])
        with self.assertRaisesRegex(RuntimeError,'needs 480p'):ps2_media.dvd_variants('test-unit-interlaced','576p')
        self.assertEqual(ps2_media.system_cnf('EELINUX.ELF','480p'),
            'BOOT2 = cdrom0:\\EELINUX.ELF;1\r\nVER = 1.00\r\nVMODE = NTSC\r\n')
        self.assertIn('VMODE = PAL\r\n',ps2_media.system_cnf('EELINUX.ELF','576p'))
        self.assertIn('cdrom0:\\EENTSC.ELF;1',ps2_media.system_cnf('EENTSC.ELF','480p'))
        with self.assertRaisesRegex(RuntimeError,'Unsupported'):ps2_media.system_cnf('../X.ELF','480p')
        for region,variants in (('ntsc',ps2_media.TEST_UNIT),('pal',['test-unit'])):
            for variant in variants:
                self.assertIn('Retail',ps2_media.COMPATIBILITY[region][variant])
        self.assertNotIn('test-unit-interlaced',ps2_media.COMPATIBILITY['pal'])
        with tempfile.TemporaryDirectory() as folder:
            (Path(folder)/'eelinux-ntsc-test-unit.iso').write_bytes(b'iso')
            ps2_media.write_checksums(Path(folder))
            self.assertIn('eelinux-ntsc-test-unit.iso',(Path(folder)/'SHA256SUMS').read_text())

    @unittest.skipUnless(shutil.which('genisoimage') and shutil.which('isoinfo'),'Linux ISO tools required')
    def test_test_unit_iso_has_system_cnf_both_kernels_and_no_freedvdboot(self):
        with tempfile.TemporaryDirectory() as folder:
            folder=Path(folder);root=folder/'root';(root/'etc').mkdir(parents=True)
            (root/'etc/hostname').write_text('eelinux\n')
            kernel=folder/'dvd.elf';kernel.write_bytes(b'\x7fELF480p'*100)
            fallback=folder/'dvd-ntsc.elf';fallback.write_bytes(b'\x7fELFntsc'*100)
            manifest=folder/'system.json';manifest.write_text('{}\n')
            args=argparse.Namespace(work=folder/'work',output_dir=folder/'out',variant='test-unit',video_mode='480p',
                dvd_kernel=kernel,dvd_fallback_kernel=fallback)
            args.work.mkdir();args.output_dir.mkdir()
            with patch.object(ps2_media,'fetch_boot',side_effect=AssertionError('no FreeDVDBoot fetch')):
                report=ps2_media.build_dvds(args,{},root,iso9660.tree_manifest(root),manifest)
            images={item['variant']:item for item in report['images']}
            self.assertEqual(sorted(images),['test-unit','test-unit-interlaced'])
            for variant,elf in ps2_media.TEST_UNIT.items():
                image=Path(images[variant]['path'])
                names=subprocess.check_output(['isoinfo','-i',str(image),'-f'],text=True).split()
                self.assertFalse([name for name in names if name.startswith('/VIDEO_TS')])
                cnf=subprocess.check_output(['isoinfo','-i',str(image),'-x','/SYSTEM.CNF;1'])
                self.assertEqual(cnf,ps2_media.system_cnf(elf,'480p').encode())
                self.assertEqual(images[variant]['boot']['BOOT2'],'cdrom0:\\'+elf+';1')
                self.assertFalse(images[variant]['boot']['retail_consoles'])
            saved=json.loads((args.output_dir/'dvd-compatibility.json').read_text())
            self.assertEqual(len(saved['images']),2)

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
