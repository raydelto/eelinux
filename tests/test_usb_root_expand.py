"""Exercise the real first-boot shell logic with private sysfs and mocked tools."""
from pathlib import Path
import json,os,re,shlex,shutil,signal,struct,subprocess,sys,tempfile,time,unittest
try:
    import fcntl
except ImportError:
    fcntl=None

ROOT=Path(__file__).resolve().parents[1]
UUID='e30eebbb-9122-536d-b9b4-0950c22e8c24'
MOCK=r'''#!/usr/bin/python3
from pathlib import Path
import json,os,struct,sys,time
name=Path(sys.argv[0]).name;args=sys.argv[1:]
base=Path(os.environ['USB_FIXTURE']);statepath=base/'state.json'
s=json.loads(statepath.read_text());s.setdefault('calls',[]).append([name,*args])
def save():statepath.write_text(json.dumps(s))
def output(value):print(value);save();sys.exit(0)
def fail():save();sys.exit(1)
disk=str(base/'dev/sda');root=str(base/'dev/sda2');boot=str(base/'dev/sda1')
if name=='id':output(s.get('uid',0))
if name=='stat':
    node=args[-1];assert node in (disk,root,boot),args
    if args[1]=='%F':output('regular file' if s.get('nonblock')==node.rsplit('/',1)[-1] else 'block special file')
    assert args[1]=='%t:%T';output({'sda':'8:0','sda1':'8:1','sda2':s.get('root_hex','8:2')}[Path(node).name])
if name=='findmnt':
    fields=args[args.index('--output')+1]
    if fields=='MAJ:MIN':output(s.get('mount_number','8:2'))
    if fields=='MAJ:MIN,FSTYPE,OPTIONS':output(s.get('mount_number','8:2')+' '+s.get('fstype','ext4')+' '+s.get('mount_options','rw,noatime'))
    assert fields=='MAJ:MIN,TARGET'
    output('8:2 /'+ ('\n8:2 /ambiguous' if s.get('duplicate_mount') else '')+ ('\n8:0 /whole-disk' if s.get('whole_mount') else ''))
if name=='blockdev':
    if args[0]=='--getss':output(s.get('sector_size',512))
    assert args[0]=='--getsz'
    if args[-1]==disk:output(s['disk_sectors'])
    assert args[-1]==root;output((base/'sys/devices/usb1/1-1/block/sda/sda2/size').read_text().strip())
if name=='sfdisk':
    assert args[-1]==disk
    if '--dump' in args:
        output('label: '+s.get('table_label','dos')+'\nlabel-id: '+s.get('disk_id','0x584c6565')+
            '\ndevice: '+disk+'\nunit: sectors\nsector-size: 512\n\n'+
            boot+' : start= '+str(s.get('boot_start',2048))+', size= '+str(s.get('boot_sectors',131072))+', type='+s.get('boot_type','c')+'\n'+
            root+' : start= '+str(s.get('root_start',133120))+', size= '+str(s['table_sectors'])+', type='+s.get('root_type','83')+
            ('\n'+str(base/'dev/sda3')+' : start= 20000000, size= 1000, type=83' if s.get('trailing_mbr') else ''))
    for flag in ('--lock=nonblock','--no-reread','--no-tell-kernel','--wipe-partitions','--wipe','--partno'):assert flag in args,args
    assert args[args.index('--wipe')+1]=='never' and args[args.index('--wipe-partitions')+1]=='never'
    assert args[args.index('--partno')+1]=='2'
    text=sys.stdin.read();assert text=='start=133120, size='+str(s['disk_sectors']-133120)+'\n',text
    if s.get('table_fail') and not s.get('partial_table_success'):fail()
    s['table_sectors']=s['disk_sectors']-133120
    data=bytearray(Path(disk).read_bytes());struct.pack_into('<I',data,474,s['table_sectors'])
    if s.get('corrupt_boot'):data[446]^=1
    Path(disk).write_bytes(data)
    if s.get('table_fail'):fail()
    save();sys.exit(0)
if name=='partx':
    assert args==['--update','--nr','2',disk],args
    if s.get('partx_fail'):fail()
    (base/'sys/devices/usb1/1-1/block/sda/sda2/size').write_text(str(s['table_sectors'])+'\n')
    save();sys.exit(0)
if name=='blkid':
    assert args[-1]==root and args[:2]==['-p','-s'],args
    output({'TYPE':s.get('blkid_type','ext4'),'UUID':s.get('uuid','e30eebbb-9122-536d-b9b4-0950c22e8c24'),'LABEL':s.get('label','EELINUX_ROOT')}[args[2]])
if name=='dumpe2fs':
    assert args==['-h',root]
    output('Filesystem features: has_journal ext_attr '+s.get('features','resize_inode')+'\nBlock count: '+str(s['fs_blocks'])+'\nBlock size: '+str(s.get('fs_block_size',4096)))
if name=='resize2fs':
    assert args==[root],args
    console=base/'proc/sys/kernel/printk'
    try:s['printk_during_resize']=console.read_text()
    except (OSError,ValueError):s['printk_during_resize']=None
    s['resize_started']=True;save()
    if s.get('resize_wait'):time.sleep(30)
    if s.get('resize_fail'):fail()
    s['fs_blocks']=s['table_sectors']//8-(1 if s.get('incomplete_resize') else 0)
    if s.get('change_uuid_after_resize'):s['uuid']='changed'
    save();sys.exit(0)
if name=='rm':
    expected=[str(base/'etc/rc.d/rcS.d/S19usb-root-expand'),str(base/'etc/init.d/usb-root-expand'),str(base/'etc/eelinux/usb-rootfs-layout'),str(base/'expand-usb-rootfs.sh')]
    assert args==['-f','--',*expected],args
    for filename in expected:Path(filename).unlink(missing_ok=True)
    save();sys.exit(0)
raise AssertionError((name,args))
'''

@unittest.skipUnless(sys.platform=='linux' and shutil.which('flock'),'Linux shell and flock required')
class UsbRootExpansion(unittest.TestCase):
    def setUp(self):
        self.temporary=tempfile.TemporaryDirectory(prefix='usb-expand-test-')
        self.base=Path(self.temporary.name);base=self.base
        self.disk_sys=base/'sys/devices/usb1/1-1/block/sda'
        self.root_sys=self.disk_sys/'sda2'
        for relative in ('dev','sys/dev/block','sys/bus/usb','run/lock','etc/init.d','etc/rc.d/rcS.d','etc/eelinux','proc/sys/kernel'):
            (base/relative).mkdir(parents=True,exist_ok=True)
        for path in (self.disk_sys,self.disk_sys/'sda1',self.root_sys):
            (path/'holders').mkdir(parents=True)
        for path,values in ((self.disk_sys,{'dev':'8:0','ro':'0'}),
                (self.disk_sys/'sda1',{'dev':'8:1','partition':'1','start':'2048','size':'131072','ro':'0'}),
                (self.root_sys,{'dev':'8:2','partition':'2','start':'133120','size':'8388608','ro':'0'})):
            for name,value in values.items():(path/name).write_text(value+'\n')
        (base/'sys/devices/usb1/subsystem').symlink_to(base/'sys/bus/usb')
        for number,path in (('8:0',self.disk_sys),('8:2',self.root_sys)):(base/'sys/dev/block'/number).symlink_to(path)
        self.marker=base/'etc/eelinux/usb-rootfs-layout';self.marker.write_text('eeLinux USB root expansion v1\n')
        self.printk=base/'proc/sys/kernel/printk';self.printk.write_text('7\t4\t1\t7\n')
        self.hook=base/'etc/init.d/usb-root-expand';self.hook.write_text('dispatcher\n')
        self.link=base/'etc/rc.d/rcS.d/S19usb-root-expand';self.link.symlink_to('../../init.d/usb-root-expand')
        data=bytearray(512);data[440:444]=b'eeLX';data[510:512]=b'\x55\xaa'
        struct.pack_into('<B3sB3sII',data,446,0,b'\0'*3,0x0c,b'\0'*3,2048,131072)
        struct.pack_into('<B3sB3sII',data,462,0,b'\0'*3,0x83,b'\0'*3,133120,8388608)
        (base/'dev/sda').write_bytes(data)
        (base/'dev/sda1').write_bytes(b'boot-partition-data')
        (base/'dev/sda2').write_bytes(b'root-filesystem-data')
        self.state_path=base/'state.json'
        self.state_path.write_text(json.dumps({'disk_sectors':33554432,'table_sectors':8388608,'fs_blocks':1048576,'calls':[]}))
        source=(ROOT/'scripts/expand-usb-rootfs.sh').read_text()
        for name,path in {'SYSFS_ROOT':base/'sys','DEV_ROOT':base/'dev','MARKER':self.marker,
                'SCRIPT_PATH':base/'expand-usb-rootfs.sh','HOOK_PATH':self.hook,'HOOK_LINK':self.link,
                'LOCK_FILE':base/'run/lock/eelinux-usb-root-expand.lock','DVD_MARKER':base/'dvd-session',
                'PRINTK_FILE':self.printk}.items():
            source,count=re.subn('^'+name+'=.*$',name+'='+shlex.quote(str(path)),source,flags=re.M)
            self.assertEqual(count,1,name)
        self.script=base/'expand-usb-rootfs.sh';self.script.write_text(source)
        tools=base/'tools';tools.mkdir();mock=tools/'mock';mock.write_text(MOCK);mock.chmod(0o755)
        for name in ('id','stat','findmnt','blockdev','sfdisk','partx','blkid','dumpe2fs','resize2fs','rm'):(tools/name).symlink_to(mock.name)
        self.environment={**os.environ,'USB_FIXTURE':str(base),'PATH':str(tools)+':/usr/sbin:/usr/bin:/sbin:/bin'}
        self.before_prefix=(base/'dev/sda').read_bytes()[:462]

    def tearDown(self):self.temporary.cleanup()
    def state(self):return json.loads(self.state_path.read_text())
    def update(self,**changes):
        state=self.state();state.update(changes);self.state_path.write_text(json.dumps(state))
    def run_script(self,success):
        before={path:path.exists() or path.is_symlink() for path in (self.marker,self.hook,self.link,self.script)}
        result=subprocess.run(['/bin/sh',str(self.script)],capture_output=True,env=self.environment,timeout=15)
        self.assertEqual(result.stdout,b'');self.assertEqual(result.stderr,b'')
        self.assertEqual(result.returncode==0,success,self.state())
        self.assertEqual((self.base/'dev/sda1').read_bytes(),b'boot-partition-data')
        self.assertEqual((self.base/'dev/sda2').read_bytes(),b'root-filesystem-data')
        for path,present in before.items():self.assertEqual(path.exists() or path.is_symlink(),False if success else present,str(path))
        return self.state()
    def mutations(self,state):
        return [call[0] for call in state['calls'] if call[0] in ('partx','resize2fs') or call[0]=='sfdisk' and '--dump' not in call]

    def test_grows_only_partition_two_online_and_removes_dispatcher_after_verification(self):
        state=self.run_script(True)
        self.assertEqual(self.mutations(state),['sfdisk','partx','resize2fs'])
        self.assertEqual(state['table_sectors'],state['disk_sectors']-133120)
        self.assertEqual(state['fs_blocks'],state['table_sectors']//8)
        self.assertEqual((self.base/'dev/sda').read_bytes()[:462],self.before_prefix)

    def test_already_full_partition_and_filesystem_need_no_block_mutation(self):
        full=self.state()['disk_sectors']-133120
        self.update(table_sectors=full,fs_blocks=full//8)
        (self.root_sys/'size').write_text(str(full)+'\n')
        self.assertEqual(self.mutations(self.run_script(True)),[])

    def test_partition_success_kernel_update_failure_retries_without_repartitioning(self):
        self.update(partx_fail=True);first=self.run_script(False)
        self.assertEqual(self.mutations(first),['sfdisk','partx'])
        self.update(partx_fail=False,calls=[])
        self.assertEqual(self.mutations(self.run_script(True)),['partx','resize2fs'])

    def test_failed_partition_command_may_have_committed_and_retries_safely(self):
        self.update(table_fail=True,partial_table_success=True)
        self.assertEqual(self.mutations(self.run_script(False)),['sfdisk'])
        self.update(table_fail=False,calls=[])
        self.assertEqual(self.mutations(self.run_script(True)),['partx','resize2fs'])

    def test_filesystem_resize_failure_retries_after_persistent_partition_growth(self):
        self.update(resize_fail=True);self.run_script(False)
        self.assertEqual(self.printk.read_text().split(),['7','4','1','7'])
        self.assertEqual(self.state()['printk_during_resize'].split(),['5','4','1','7'])
        self.update(resize_fail=False,calls=[])
        self.assertEqual(self.mutations(self.run_script(True)),['resize2fs'])

    def test_console_values_are_restored_after_success_and_existing_quiet_level_is_kept(self):
        self.printk.write_text('4\t3\t1\t6\n')
        state=self.run_script(True)
        self.assertEqual(state['printk_during_resize'].split(),['4','3','1','6'])
        self.assertEqual(self.printk.read_text().split(),['4','3','1','6'])

    def test_unsafe_console_configuration_does_not_prevent_growth(self):
        self.printk.write_text('invalid console state\n')
        state=self.run_script(True)
        self.assertEqual(self.mutations(state),['sfdisk','partx','resize2fs'])
        self.assertEqual(self.printk.read_text(),'invalid console state\n')

    def test_absent_sysctl_printk_interface_does_not_prevent_growth_or_get_created(self):
        self.printk.unlink()
        state=self.run_script(True)
        self.assertEqual(self.mutations(state),['sfdisk','partx','resize2fs'])
        self.assertIsNone(state['printk_during_resize']);self.assertFalse(self.printk.exists())

    def test_linked_console_interface_is_left_untouched(self):
        saved=self.base/'unrelated-console';saved.write_text('7 4 1 7\n')
        self.printk.unlink();self.printk.symlink_to(saved)
        state=self.run_script(True)
        self.assertEqual(state['printk_during_resize'],'7 4 1 7\n')
        self.assertEqual(saved.read_text(),'7 4 1 7\n')

    @unittest.skipIf(getattr(os,'geteuid',lambda:0)()==0,'permission rejection requires an unprivileged fixture process')
    def test_readonly_console_interface_does_not_prevent_growth(self):
        self.printk.chmod(0o400)
        state=self.run_script(True)
        self.assertEqual(state['printk_during_resize'].split(),['7','4','1','7'])
        self.assertEqual(self.printk.read_text().split(),['7','4','1','7'])

    @unittest.skipIf(getattr(os,'geteuid',lambda:0)()==0,'permission rejection requires an unprivileged fixture process')
    def test_unreadable_console_interface_does_not_prevent_growth(self):
        self.printk.chmod(0)
        state=self.run_script(True)
        self.assertIsNone(state['printk_during_resize'])
        self.printk.chmod(0o600)
        self.assertEqual(self.printk.read_text().split(),['7','4','1','7'])

    def test_absent_console_and_resize_failure_keep_retry_files(self):
        self.printk.unlink();self.update(resize_fail=True)
        self.run_script(False)
        self.assertFalse(self.printk.exists())

    def test_console_is_restored_when_resize_is_interrupted(self):
        self.update(resize_wait=True)
        process=subprocess.Popen(['/bin/sh',str(self.script)],stdout=subprocess.PIPE,stderr=subprocess.PIPE,env=self.environment)
        try:
            deadline=time.monotonic()+8
            while time.monotonic()<deadline:
                if self.state().get('resize_started'):break
                time.sleep(0.03)
            self.assertTrue(self.state().get('resize_started'))
            process.send_signal(signal.SIGTERM)
            output,errors=process.communicate(timeout=5)
            self.assertNotEqual(process.returncode,0)
            self.assertEqual(output,b'');self.assertEqual(errors,b'')
            self.assertEqual(self.printk.read_text().split(),['7','4','1','7'])
            self.assertTrue(self.script.exists());self.assertTrue(self.marker.exists())
            self.assertEqual(self.state()['fs_blocks'],1048576)
        finally:
            if process.poll() is None:process.kill();process.wait()

    def test_corrupt_boot_prefix_stops_before_kernel_or_filesystem_changes(self):
        self.update(corrupt_boot=True)
        self.assertEqual(self.mutations(self.run_script(False)),['sfdisk'])

    def test_resize_success_requires_actual_final_size_and_unchanged_uuid(self):
        self.update(incomplete_resize=True);self.run_script(False)
        self.update(incomplete_resize=False,calls=[])
        self.assertEqual(self.mutations(self.run_script(True)),['resize2fs'])

    def test_uuid_change_after_resize_keeps_retry_files(self):
        self.update(change_uuid_after_resize=True);self.run_script(False)

    def test_wrong_geometry_filesystem_mount_or_device_is_rejected_before_writes(self):
        cases=({'uid':1000},{'disk_id':'0x12345678'},{'table_label':'gpt'},
            {'boot_start':4096},{'boot_sectors':262144},{'boot_type':'83'},
            {'root_start':135168},{'root_type':'c'},{'trailing_mbr':True},
            {'fstype':'overlay'},{'blkid_type':'ext3'},{'uuid':'unrelated'},
            {'label':'OTHER_ROOT'},{'features':'ext_attr'},{'fs_block_size':1024},
            {'mount_options':'ro,noatime'},{'duplicate_mount':True},{'whole_mount':True},
            {'nonblock':'sda'},{'nonblock':'sda2'},{'root_hex':'8:3'},
            {'sector_size':4096},{'disk_sectors':4294967297},{'disk_sectors':1000000},
            {'fs_blocks':999999999})
        original=self.state()
        for changes in cases:
            with self.subTest(changes=changes):
                self.state_path.write_text(json.dumps({**original,**changes,'calls':[]}))
                self.assertEqual(self.mutations(self.run_script(False)),[])

    def test_missing_usb_marker_dvd_or_usb_identity_is_rejected(self):
        self.marker.unlink()
        self.assertEqual(self.mutations(self.run_script(False)),[])
        self.marker.write_text('eeLinux USB root expansion v1\n\n')
        self.assertEqual(self.mutations(self.run_script(False)),[])
        self.marker.write_text('eeLinux USB root expansion v1\n');self.update(calls=[])
        (self.base/'dvd-session').write_text('DVD')
        self.assertEqual(self.mutations(self.run_script(False)),[])
        (self.base/'dvd-session').unlink();self.update(calls=[])
        (self.base/'sys/devices/usb1/subsystem').unlink()
        self.assertEqual(self.mutations(self.run_script(False)),[])

    def test_virtual_or_loop_image_backing_device_is_rejected(self):
        virtual=self.base/'sys/devices/virtual/block/loop0/loop0p2';virtual.mkdir(parents=True)
        link=self.base/'sys/dev/block/8:2';link.unlink();link.symlink_to(virtual)
        self.assertEqual(self.mutations(self.run_script(False)),[])

    def test_extra_sysfs_partition_holders_or_readonly_disk_is_rejected(self):
        extra=self.disk_sys/'sda3';extra.mkdir();(extra/'partition').write_text('3\n')
        self.assertEqual(self.mutations(self.run_script(False)),[])
        shutil.rmtree(extra);self.update(calls=[])
        holder=self.root_sys/'holders/dm-0';holder.write_text('held')
        self.assertEqual(self.mutations(self.run_script(False)),[])
        holder.unlink();self.update(calls=[]);(self.disk_sys/'ro').write_text('1\n')
        self.assertEqual(self.mutations(self.run_script(False)),[])

    def test_script_lock_prevents_concurrent_expansion(self):
        with (self.base/'run/lock/eelinux-usb-root-expand.lock').open('w') as lock:
            fcntl.flock(lock,fcntl.LOCK_EX|fcntl.LOCK_NB)
            self.assertEqual(self.mutations(self.run_script(False)),[])

if __name__=='__main__':unittest.main()
