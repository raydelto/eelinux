#!/usr/bin/env python3
"""Create production PAL or NTSC FreeDVDBoot DVDs and a 2,000,000,000-byte USB image.

Only regular output files are accepted. No device is flashed and no disc is
burned. Standalone DVDs carry a native Rock Ridge GNU root and optical kernel.
"""
from pathlib import Path
import argparse,hashlib,json,os,re,shutil,struct,subprocess,sys,tempfile,time,urllib.parse,urllib.request
import hardware_usb,rootfs
from video import BOOT_MODES
ROOT=Path(__file__).resolve().parents[1]
LOCK=ROOT/'boot/freedvdboot-sources.json'
SLIM='Filesystems/All PS2 slims (3.10 + 3.11) - English language'
FAT={
 'fat-2.10-2.12':'Some 2.10 models and all 2.12.iso',
 'fat-3.04':'3.04 only - M+maybe other regions except J - English language.iso',
}
# Compatibility notes are region-specific: NTSC media must not repeat PAL-only claims.
COMPATIBILITY={
 'pal':{
  'slim':'All Slim models according to upstream; English language required. Physical DVD boot not yet verified.',
  'fat-2.10-2.12':'PAL SCPH-30004 R with DVD Player 2.10E (ROMGEN 0160EC20011004); SCPH-39004 is unsupported. DVD Player 2.12 support is documented for U/J/G, not confirmed for E.',
  'fat-3.04':'DVD Player 3.04, English language. PAL 3.04E is an upstream compatibility hypothesis; only M was tested upstream. Excludes J.',
 },
 'ntsc':{
  'slim':'All Slim models (DVD Player 3.10/3.11) according to upstream, including NTSC-U and NTSC-J; the console language must be set to English. Physical DVD boot not yet verified.',
  'fat-2.10-2.12':'Fat consoles with DVD Player 2.12 (U/J/G documented upstream) and some 2.10 models. This is the likely image for an NTSC-J Fat with DVD Player 2.12J. Check the DVD Player version on the browser Version screen first.',
  'fat-3.04':'DVD Player 3.04, English language; only M was tested upstream. Upstream explicitly excludes J (NTSC-J): do not use on Japanese consoles. NTSC-U 3.04U is an unverified hypothesis.',
 },
}
def region(video_mode):
 return 'ntsc' if video_mode == '480p' else 'pal'
DIAGNOSTIC_WORDS=('ps2-xorg-test','ps2-native-sdk-test','gpu2d-probe','xorg-core2d-probe',
                  'ps2-gs-test','native-development-probe','native-sdk-check')
FULL_COMMANDS='gcc g++ as ld ar nm ranlib objcopy objdump readelf strip make m4 bison flex autoconf automake libtool pkgconf nano vim screen wget lynx gdb python3 perl'.split()
USB_EXPANSION_PATHS=('expand-usb-rootfs.sh','etc/eelinux/usb-rootfs-layout',
    'etc/init.d/usb-root-expand','etc/rc.d/rcS.d/S19usb-root-expand')

def configure_usb_expansion(stage,enabled=False):
 """Keep first-boot disk writes exclusive to explicitly generated USB media."""
 for relative in USB_EXPANSION_PATHS:
  path=rootfs.guest_path(stage,'/'+Path(relative).parent.as_posix())/Path(relative).name
  if path.is_file() or path.is_symlink():path.unlink()
 if not enabled:return
 rootfs.install_file(stage,'/expand-usb-rootfs.sh',source=ROOT/'scripts/expand-usb-rootfs.sh',mode=0o700)
 rootfs.install_file(stage,'/etc/eelinux/usb-rootfs-layout',content='eeLinux USB root expansion v1\n',mode=0o600)
 rootfs.install_file(stage,'/etc/init.d/usb-root-expand',content='''#!/bin/bash
case ${1:-} in
 start)
  if [[ ! -e /.dvd/session && -x /expand-usb-rootfs.sh ]]; then
   /expand-usb-rootfs.sh </dev/null >/dev/null 2>&1 || :
  fi
  ;;
esac
exit 0
''',mode=0o700)
 rootfs.install_symlink(stage,'/etc/rc.d/rcS.d/S19usb-root-expand','../../init.d/usb-root-expand')

def full_userland_inventory(stage):
 for name in FULL_COMMANDS:
  if rootfs.find_command(stage,name) is None:raise RuntimeError('Full GNU userland is required; missing '+name)
 if not (stage/'usr/include/stdio.h').is_file():raise RuntimeError('Native compiler SDK headers are required')
 programs=[]
 for directory in ('bin','sbin','usr/bin','usr/sbin'):
  for path in sorted((stage/directory).iterdir()):
   actual=rootfs.guest_path(stage,'/'+directory+'/'+path.name)
   if actual.is_file() and actual.stat().st_mode&0o111:
    programs.append({'path':'/'+directory+'/'+path.name,'sha256':sha(actual)})
 inventory={'profile':'full','programs':programs,'GNU_required_commands':rootfs.GNU_COMMANDS,
            'development_commands':FULL_COMMANDS,'SDK_headers_and_libraries_preserved':True}
 destination=stage/'usr/share/eelinux';destination.mkdir(parents=True,exist_ok=True)
 (destination/'installed-programs.json').write_text(json.dumps(inventory,indent=2)+'\n')
 return inventory
def sha(path):
 with path.open('rb') as stream:return hashlib.file_digest(stream,'sha256').hexdigest()

def install_kernel_modules(stage,report_path):
 """Install only the verified module payload built with the selected kernel."""
 if not stage.is_dir() or stage.resolve() in (Path('/'),Path.home()):
  raise RuntimeError('Kernel module installation requires a private staging tree')
 report=json.loads(report_path.read_text())
 records=report.get('modules',{})
 if not records:
  if report.get('optional_networking_modules'):raise RuntimeError('Kernel module manifest is empty')
  return []
 release=report.get('kernel_release','')
 if not re.fullmatch(r'[A-Za-z0-9_+.-]+',release) or release in ('.','..'):
  raise RuntimeError('Invalid kernel release')
 source=Path(report['modules_directory']).resolve(strict=True)
 prefix='lib/modules/'+release+'/'
 checked=[]
 for relative,expected in sorted(records.items()):
  parts=Path(relative).parts
  if not relative.startswith(prefix) or '..' in parts or '\\' in relative:
   raise RuntimeError('Unsafe kernel module manifest path: '+relative)
  path=source/relative
  if path.is_symlink() or not path.is_file() or not path.resolve().is_relative_to(source):
   raise RuntimeError('Invalid kernel module payload: '+relative)
  if sha(path)!=expected:raise RuntimeError('Kernel module checksum mismatch: '+relative)
  checked.append((relative,path))
 # The previous image may use the same release name with an older config.
 # Replace only this release's directory in the newly cloned staging tree.
 destination=rootfs.guest_path(stage,'/'+prefix.rstrip('/'))
 if not destination.resolve().is_relative_to(stage.resolve()):raise RuntimeError('Unsafe staged module directory')
 if any(path.resolve().is_relative_to(destination.resolve()) for _,path in checked):
  raise RuntimeError('Kernel module payload aliases the staged release')
 if destination.is_symlink() or destination.is_file():raise RuntimeError('Staged module path is not a directory')
 if destination.exists():shutil.rmtree(destination)
 for relative,path in checked:rootfs.install_file(stage,'/'+relative,source=path)
 return [relative for relative,_ in checked]

def system_menu(stage,work):
 """Prepare the immutable system menu with the same parser used on target."""
 with tempfile.TemporaryDirectory(prefix='twm-menu-',dir=work) as folder:
  parser=Path(folder)/'menu'
  subprocess.run(['cc','-std=gnu17','-Os','-Wall','-Wextra','-Werror',
    str(ROOT/'desktop/ps2-xdg-menu.c'),'-o',str(parser)],check=True)
  env=dict(os.environ,LANG='C',LC_ALL='C',XDG_CURRENT_DESKTOP='PS2:TWM',
    PATH=':'.join(str(stage/path) for path in ('usr/bin','usr/sbin','bin','sbin')))
  text=subprocess.check_output([str(parser),str(stage/'usr/share/applications')],env=env,text=True)
  if text.count(' f.exec ')<60:raise RuntimeError('Incomplete prepared application menu')
  (stage/'usr/share/eelinux/twm-applications.menu').write_text(text)
def new_file(path):
 path=hardware_usb.regular_path(path,must_exist=False)
 if path.exists():raise RuntimeError('Preserving existing artifact; choose a new output directory: '+str(path))
 return path
def clean_diagnostics(stage):
 """Remove project qualification clients, hooks and old results from a clone."""
 removed=[];stage=stage.resolve()
 for directory in ('usr/bin','usr/lib/eelinux','etc/init.d','etc/rc.d','etc/inittab.d','var/lib','root'):
  folder=stage/directory
  if not folder.is_dir():continue
  for path in sorted(folder.rglob('*'),key=lambda p:len(p.parts),reverse=True):
   name=path.name.lower()
   match=any(word in name for word in DIAGNOSTIC_WORDS) or (
       name.startswith(('ps2-','gs-','s99gs','s99xorg','xorg-core','native-sdk')) and
       any(word in name for word in ('test','probe','session','gpu2d','core2d')))
   if not match:continue
   if path.is_symlink() or path.is_file():path.unlink()
   elif path.is_dir():
    if not path.resolve().is_relative_to(stage):raise RuntimeError('Unsafe diagnostic directory')
    shutil.rmtree(path)
   removed.append(path.relative_to(stage).as_posix())
 # Remove test service commands in auxiliary init files. The normal project
 # inittab is restored by rootfs.assemble_tree during image construction.
 for path in (stage/'etc/inittab.d').glob('*'):
  if path.is_file() and any(word in path.read_text(errors='replace') for word in DIAGNOSTIC_WORDS):
   removed.append(path.relative_to(stage).as_posix());path.unlink()
 return removed
def fetch_boot(work):
 lock=json.loads(LOCK.read_text());destination=work/'freedvdboot'/lock['commit']
 for relative,expected in lock['files'].items():
  path=destination/relative
  if not path.resolve().is_relative_to(destination.resolve()):raise RuntimeError('Unsafe boot source path')
  if not path.exists():
   path.parent.mkdir(parents=True,exist_ok=True)
   url='https://raw.githubusercontent.com/CTurt/FreeDVDBoot/'+lock['commit']+'/'+urllib.parse.quote(relative)
   with urllib.request.urlopen(url,timeout=45) as response:data=response.read()
   if hashlib.sha256(data).hexdigest()!=expected:raise RuntimeError('Boot source hash mismatch: '+relative)
   temporary=path.with_suffix(path.suffix+'.part');temporary.write_bytes(data);temporary.replace(path)
  if sha(path)!=expected:raise RuntimeError('Boot source hash mismatch: '+relative)
 return destination,lock
def boot_readme(video_mode="576p"):
 return (('eeLinux NTSC desktop' if video_mode == '480p' else 'eeLinux PAL desktop')+'\n\n'
   + ('EELINUX.ELF uses 720x480p@60: it needs a component, D-terminal (D2 or higher) or HDMI adapter.\n'
      'Composite, S-Video and RGB users: launch EENTSC.ELF (interlaced 576x384i@60) instead.\n'
      if video_mode == '480p' else '')
   + 'DVD: no USB disk is required. The full system is included on this disc.\n'
   'USB: write the entire 2 GB USB image to a USB disk and boot its EELINUX.ELF.\n'
   'USB first boot silently expands the root partition and ext4 filesystem to fill the drive.\n'
   'Its one-time script removes itself after successful expansion; DVDs do not run it.\n'
   'Burn the matching ISO as a disc image to DVD and finalise it; DVD-R is recommended.\n'
   'A CD cannot boot this DVD image. Set console language to English.\n'
   'DVD starts uLaunchELF using FreeDVDBoot, without a memory-card exploit or modchip.\n'
   'DVD: select cdfs:/EELINUX.ELF in uLaunchELF. Linux mounts the system from DVD.\n'
   'DVD changes are held in a bounded RAM overlay and disappear at shutdown.\n'
   'DVD uses bounded LZ4 compressed RAM swap; large native builds need writable storage.\n'
   'Log in as root (default media password: eelinux; custom builds may override it), then run startx.\n'
   'TWM starts the classic analogue xclock. F1 opens the Applications menu. Alt+Enter opens a terminal.\n'
   'Window buttons: triangle shade, square minimize, circle maximize/restore, X close.\n'
   'Drag a window border, or hold Alt and drag with the right mouse button, to resize.\n'
   'Run ps2-tools to browse the installed command-line and networking tools.\n'
   'This is a homebrew disc, not an authenticated or licensed Sony disc.\n'
   'Fat compatibility depends on DVD Player firmware; consult the adjacent compatibility manifest.\n'
   'Physical DVD/USB boot remains to be verified on your console.\n'
   'https://github.com/CTurt/FreeDVDBoot\n')
def dvd_inputs(args):
 for name in ('dvd_kernel','dvd_kernel_config','dvd_kernel_report'):
  if getattr(args,name,None) is None:raise RuntimeError('--'+name.replace('_','-')+' is required for standalone DVDs')
  setattr(args,name,hardware_usb.regular_path(getattr(args,name),must_exist=True))
 report=json.loads(args.dvd_kernel_report.read_text());config=args.dvd_kernel_config.read_text()
 if report.get('root_device')!='/dev/ps2dvd' or report.get('USB_required') is not False:
  raise RuntimeError('DVD kernel must implement the standalone optical root path')
 if report['image_sha256']!=sha(args.dvd_kernel) or report['config_sha256']!=sha(args.dvd_kernel_config):
  raise RuntimeError('DVD kernel/report hash mismatch')
 if report.get('boot_mode')!='native Rock Ridge ISO9660 /eelinux/root, RAM overlay':
  raise RuntimeError('Rebuild the DVD kernel for the native Rock Ridge ISO9660 root')
 for feature in ('PS2_CDVD','ISO9660_FS','OVERLAY_FS','TMPFS_XATTR','FB_PS2','ZRAM','ZRAM_BACKEND_LZ4'):
  if 'CONFIG_'+feature+'=y\n' not in config:raise RuntimeError('DVD kernel needs '+feature)
 if report.get('video_mode') != args.video_mode or 'video=ps2fb:'+BOOT_MODES[args.video_mode] not in config:
  raise RuntimeError('DVD kernel video mode does not match requested media')
 if args.staging is None:raise RuntimeError('--staging with complete GNU userland is required for native DVDs')
 return report

def prepare_stage(args,folder,module_report=None):
 stage=folder/'stage'
 subprocess.run(['cp','-a','--reflink=auto',str(args.staging.resolve()),str(stage)],check=True)
 if module_report is not None:
  install_kernel_modules(stage,module_report)
 removed=clean_diagnostics(stage)
 configure_usb_expansion(stage,False)
 obsolete=stage/'usr/lib/eelinux/fbcheck'
 if obsolete.is_file():obsolete.unlink();removed.append('usr/lib/eelinux/fbcheck')
 for path in (stage/'usr/lib/xorg/modules/drivers').glob('*'):
  if path.name!='ps2_drv.so' and (path.is_file() or path.is_symlink()):path.unlink()
 from strip_userland import strip_tree
 if args.toolchain_prefix:strip_tree(stage,args.toolchain_prefix,args.work/'media-strip-report.json')
 inventory=full_userland_inventory(stage)
 import xorg_apps
 programs=xorg_apps.install_menu_entries(stage);system_menu(stage,args.work)
 if len(programs)<60 or not (stage/'usr/bin/ps2-xdg-menu').is_file():
  raise RuntimeError('Build the complete X.Org application payload and desktop helpers first')
 if not (stage/'usr/lib/xorg/modules/drivers/ps2_drv.so').is_file():raise RuntimeError('Native PS2 DDX is missing')
 if not (stage/'usr/share/X11/app-defaults/XCalc').is_file():
  raise RuntimeError('Rebuild X.Org with target app-defaults paths before packaging')
 return stage,removed,inventory,programs

def password_hash(args):
 if args.root_password_hash_file is not None:
  value=args.root_password_hash_file.read_text().strip()
 else:
  # A private build-host cache lets separate Slim/Fat targets reuse the same
  # root filesystem without putting the password hash in release reports.
  cache=args.work/'media-root-password.sha512'
  if not cache.exists():
   value=subprocess.run(['openssl','passwd','-6','-stdin'],input='eelinux\n',
     text=True,capture_output=True,check=True).stdout.strip()
   with cache.open('x') as stream:stream.write(value+'\n')
   cache.chmod(0o600)
  value=cache.read_text().strip()
 if not re.fullmatch(r'\$6\$(?:rounds=[0-9]+\$)?[./A-Za-z0-9]{1,16}\$[./A-Za-z0-9]{86}',value):
  raise RuntimeError('Root password file must contain one SHA-512 crypt hash')
 return value

def audit_tree(tree):
 if (tree/'THIS_IS_NOT_YOUR_ROOT_FILESYSTEM').exists():
  raise RuntimeError('Buildroot staging marker leaked into production root')
 for path in tree.rglob('*'):
  name=path.name.lower()
  if any(word in name for word in DIAGNOSTIC_WORDS) or (
     name.startswith(('ps2-','gs-','s99gs','s99xorg')) and
     any(word in name for word in ('test','probe','gpu2d','core2d'))):
   raise RuntimeError('Diagnostic leaked into production tree: '+str(path.relative_to(tree)))
 if (tree/'swapfile').exists():raise RuntimeError('A DVD must not contain the USB swapfile')

def extract_boot_files(original,tree):
 """Expose the same upstream files in the new ISO directory view."""
 for name in subprocess.check_output(['isoinfo','-i',str(original),'-f'],text=True).splitlines():
  if not name.startswith('/VIDEO_TS/') or not name.endswith(';1'):continue
  destination=tree/name[1:-2];destination.parent.mkdir(parents=True,exist_ok=True)
  destination.write_bytes(subprocess.check_output(['isoinfo','-i',str(original),'-x',name]))

def image_file_hash(image,name):
 process=subprocess.Popen(['isoinfo','-i',str(image),'-x',name],stdout=subprocess.PIPE)
 digest=hashlib.sha256();count=0
 for data in iter(lambda:process.stdout.read(1048576),b''):digest.update(data);count+=len(data)
 process.stdout.close()
 if process.wait():raise RuntimeError('ISO readback failed: '+name)
 return digest.hexdigest(),count

def fat_append(original,session,output):
 """Preserve all exploit/UDF sectors, changing only the ISO9660 PVD.

 The appended filesystem has absolute extents generated with -C. The DVD
 player retains its original UDF view and fixed-offset exploit payload;
 uLaunchELF/Linux see the new full ISO9660 filesystem through sector 16.
 """
 before=original.read_bytes()
 if len(before)%2048 or before[16*2048:16*2048+7]!=b'\x01CD001\x01':
  raise RuntimeError('Unexpected pinned Fat ISO layout')
 with session.open('rb') as stream:stream.seek(16*2048);pvd=stream.read(2048)
 if pvd[:7]!=b'\x01CD001\x01':raise RuntimeError('Missing appended ISO9660 descriptor')
 # -C without -M creates absolute file extents but reports only the new
 # session's size in the PVD. The standalone disc must report its full size,
 # or strict ISO readers reject the appended root as out of bounds.
 if session.stat().st_size%2048:raise RuntimeError('Unaligned appended ISO session')
 sectors=(len(before)+session.stat().st_size)//2048
 pvd=bytearray(pvd);struct.pack_into('<I',pvd,80,sectors);struct.pack_into('>I',pvd,84,sectors)
 with output.open('wb') as stream:
  stream.write(before)
  with session.open('rb') as appended:shutil.copyfileobj(appended,stream,1048576)
  stream.seek(16*2048);stream.write(pvd)
 with output.open('rb') as stream:prefix=stream.read(len(before))
 if prefix[:16*2048]!=before[:16*2048] or prefix[17*2048:]!=before[17*2048:]:
  raise RuntimeError('Fat exploit sectors were changed')
 return {'preserved_prefix_bytes':len(before),'only_modified_prefix_sector':16,
         'original_prefix_sha256':hashlib.sha256(before).hexdigest(),
         'ISO9660_volume_sectors':sectors,
         'exploit_and_UDF_sectors_byte_exact':True}

def dvd_fallback(args,primary):
 """Validate the interlaced EENTSC.ELF DVD kernel carried on 480p media."""
 if args.video_mode != '480p':return None
 paths=(args.dvd_fallback_kernel,args.dvd_fallback_config,args.dvd_fallback_report)
 if any(path is None for path in paths):raise RuntimeError('480p media requires an interlaced DVD fallback kernel/config/report')
 from copy import copy
 fallback=copy(args)
 fallback.dvd_kernel,fallback.dvd_kernel_config,fallback.dvd_kernel_report=paths
 fallback.video_mode='ntsc'
 report=dvd_inputs(fallback)
 if report.get('kernel_release') != primary.get('kernel_release') or report.get('modules') != primary.get('modules'):
  raise RuntimeError('DVD fallback modules differ from primary kernel')
 args.dvd_fallback_kernel,args.dvd_fallback_config,args.dvd_fallback_report=(
  fallback.dvd_kernel,fallback.dvd_kernel_config,fallback.dvd_kernel_report)
 return report
def dvds(args):
 args.work.mkdir(parents=True,exist_ok=True);args.output_dir.mkdir(parents=True,exist_ok=True)
 dvd_kernel=dvd_inputs(args)
 dvd_fallback(args,dvd_kernel)
 import iso9660
 with tempfile.TemporaryDirectory(prefix='production-dvd-root-',dir=args.work) as folder:
  folder=Path(folder)
  stage,removed,inventory,programs=prepare_stage(args,folder,args.dvd_kernel_report)
  tree=folder/'root'
  rootfs.assemble_tree(stage,tree,'eelinux',True,password_hash(args),args.video_mode,0)
  audit_tree(tree)
  expected=iso9660.tree_manifest(tree)
  manifest={'schema':1,'layout':'native Rock Ridge ISO9660 /eelinux/root',
    'userland_profile':'full','installed_program_paths':len(inventory['programs']),
    'xorg_applications':programs,'development_commands':FULL_COMMANDS,
    'SDK_headers_and_libraries_preserved':True,'tests_and_diagnostics_installed':False,
    'removed_diagnostics':removed,'native_driver_sha256':sha(stage/'usr/lib/xorg/modules/drivers/ps2_drv.so'),
    'clock':'windowed classic analogue xclock; GS core drawing','entries':expected}
  manifest_path=args.output_dir/'dvd-rootfs-manifest.json'
  if manifest_path.exists() and json.loads(manifest_path.read_text())!=manifest:
   old=json.loads(manifest_path.read_text())
   changed=[name for name,item in manifest['entries'].items() if old.get('entries',{}).get(name)!=item]
   raise RuntimeError('Mixed native DVD root revisions; changed paths='+str(changed[:10]))
  manifest_path.write_text(json.dumps(manifest,indent=2)+'\n')
  return build_dvds(args,dvd_kernel,tree,expected,manifest_path)

def build_dvds(args,dvd_kernel,root,expected,manifest_path):
 import iso9660
 source,lock=fetch_boot(args.work)
 variants=['slim',*FAT] if args.variant=='all' else list(FAT) if args.variant=='fat' else [args.variant]
 report={'schema':3,'video':BOOT_MODES[args.video_mode],'USB_required':False,'standalone_DVD':True,'official_Sony_disc':False,
  'root_layout':'native Rock Ridge ISO9660 /eelinux/root','embedded_USB_image':False,
  'rootfs_manifest_sha256':sha(manifest_path),'rootfs_manifest':str(manifest_path),
  'DVD_kernel_sha256':sha(args.dvd_kernel),'fallback_kernel_sha256':sha(args.dvd_fallback_kernel) if args.video_mode == '480p' else None,'writable_state':'8 MiB RAM overlay; discarded at shutdown',
  'RAM_swap':'16 MiB logical zram; maximum 4 MiB compressed storage; LZ4',
  'FreeDVDBoot_commit':lock['commit'],'tests_and_diagnostics_installed':False,'physical_boot_qualified':False,'images':[]}
 for variant in variants:
  output=new_file(args.output_dir/('eelinux-'+region(args.video_mode)+'-'+variant+'.iso'))
  if not shutil.which('genisoimage'):raise RuntimeError('Install genisoimage on the Linux build host')
  preservation={}
  with tempfile.TemporaryDirectory(prefix='eelinux-full-dvd-',dir=args.work) as folder:
    tree=Path(folder)/'tree';tree.mkdir()
    original=None
    if variant=='slim':shutil.copytree(source/SLIM/'VIDEO_TS',tree/'VIDEO_TS')
    else:
     original=source/'PREBUILT ISOs'/FAT[variant]
     extract_boot_files(original,tree)
    shutil.copy2(args.dvd_kernel,tree/'EELINUX.ELF')
    if args.video_mode == '480p':shutil.copy2(args.dvd_fallback_kernel,tree/'EENTSC.ELF')
    (tree/'README.TXT').write_text(boot_readme(args.video_mode))
    shutil.copy2(ROOT/'userland/xorg-apps-sources.json',tree/'XORGAPPS.JSON')
    (tree/'eelinux').mkdir();shutil.copy2(manifest_path,tree/'eelinux/system.json')
    temporary=output.with_suffix('.iso.part')
    session=Path(folder)/'session.iso'
    # Build and inspect on the native Linux work filesystem. isoinfo reads in
    # 2 KiB sectors, which is unnecessarily slow over WSL's Windows filesystem.
    candidate=Path(folder)/'verified.iso' if original else session
    # Keep long GNU symlink targets in one Rock Ridge SL field. Some readers
    # lose a slash when an SL field is split across continuation areas.
    command=['genisoimage','-quiet','-R','-no-split-symlink-components','-no-split-symlink-fields',
      '-uid','0','-gid','0','-iso-level','2',
      '-V',('EELINUX_NTSC_DESKTOP' if args.video_mode == '480p' else 'EELINUX_PAL_DESKTOP'),'-o',str(session)]
    if original:command+=['-C','0,'+str(original.stat().st_size//2048)]
    else:command+=['-udf']
    subprocess.run(command+['-graft-points',str(tree),'eelinux/root='+str(root)],check=True)
    if original:preservation=fat_append(original,session,candidate)
    # Inspect files inside ISO9660 and compare every unchanged exploit payload.
    for payload in (tree/'VIDEO_TS').iterdir():
     data=subprocess.check_output(['isoinfo','-i',str(candidate),'-x','/VIDEO_TS/'+payload.name+';1'])
     if hashlib.sha256(data).hexdigest()!=sha(payload):raise RuntimeError('DVD payload readback mismatch')
    if image_file_hash(candidate,'/EELINUX.ELF;1')!=(sha(args.dvd_kernel),args.dvd_kernel.stat().st_size):
     raise RuntimeError('DVD kernel readback mismatch')
    if args.video_mode == '480p' and image_file_hash(candidate,'/EENTSC.ELF;1')!=(sha(args.dvd_fallback_kernel),args.dvd_fallback_kernel.stat().st_size):
     raise RuntimeError('DVD fallback kernel readback mismatch')
    root_readback=iso9660.verify_root(candidate,expected)
    if candidate.stat().st_size>4_700_000_000:raise RuntimeError('Standalone image exceeds single-layer DVD capacity')
    shutil.copyfile(candidate,temporary)
    if sha(temporary)!=sha(candidate):raise RuntimeError('Published ISO copy differs from verified image')
    # Windows indexing/scanning can retain a short-lived handle after readback.
    for attempt in range(8):
     try:temporary.replace(output);break
     except PermissionError:
      if attempt==7:raise
      time.sleep(0.25)
  compatibility=COMPATIBILITY[region(args.video_mode)][variant]
  method='Standalone DVD: uLaunchELF cdfs:/EELINUX.ELF; direct Rock Ridge GNU root; writable RAM overlay'
  report['images'].append({'variant':variant,'path':str(output),'bytes':output.stat().st_size,
      'sha256':sha(output),'compatibility':compatibility,'method':method,'payload_readback_byte_exact':True,
      'USB_required':False,'embedded_USB_image':False,'root_readback':root_readback,'preservation':preservation})
  save_dvd_report(args,report,lock,manifest_path)
  print('Created '+str(output),flush=True)
 return report

def save_dvd_report(args,report,lock,manifest_path):
 previous=args.output_dir/'dvd-compatibility.json'
 if previous.exists():
  old=json.loads(previous.read_text())
  if old['FreeDVDBoot_commit']!=lock['commit'] or old.get('rootfs_manifest_sha256')!=sha(manifest_path) or \
     old.get('DVD_kernel_sha256')!=report['DVD_kernel_sha256'] or old.get('fallback_kernel_sha256')!=report['fallback_kernel_sha256']:
   raise RuntimeError('Mixed boot source/kernel/system revisions')
  current={item['variant']:item for item in old['images']}
  current.update({item['variant']:item for item in report['images']})
  report['images']=list(current.values())
 previous.write_text(json.dumps(report,indent=2)+'\n')
 (args.output_dir/'BOOT-README.txt').write_text(boot_readme(args.video_mode))
def usb_fallback(args):
 if args.video_mode != '480p':return None
 paths=(args.fallback_kernel,args.fallback_config,args.fallback_report)
 if any(path is None for path in paths):raise RuntimeError('480p media requires an interlaced USB fallback kernel/config/report')
 primary=json.loads(args.kernel_report.read_text())
 fallback=json.loads(args.fallback_report.read_text())
 if fallback.get('kernel_release') != primary.get('kernel_release') or fallback.get('modules') != primary.get('modules'):
  raise RuntimeError('USB fallback modules differ from primary kernel')
 return paths

def usb(args):
 if args.staging is None:raise RuntimeError('--staging is required for a USB image')
 for name in ('kernel','kernel_config','kernel_report'):
  if getattr(args,name,None) is None:raise RuntimeError('--'+name.replace('_','-')+' is required for USB media')
 args.work.mkdir(parents=True,exist_ok=True);args.output_dir.mkdir(parents=True,exist_ok=True)
 output=new_file(args.output_dir/('eelinux-'+region(args.video_mode)+'-desktop-2gb-usb.img'))
 with tempfile.TemporaryDirectory(prefix='production-stage-',dir=args.work) as folder:
  stage,removed,inventory,programs=prepare_stage(args,Path(folder),args.kernel_report)
  configure_usb_expansion(stage,True)
  filesystem=Path(folder)/'filesystem'
  command=[sys.executable,str(ROOT/'scripts/rootfs.py'),'--staging',str(stage),'--output-dir',str(filesystem),
    '--size-mib','1842','--hostname','eelinux','--video-mode',args.video_mode,'--swap-file-mib',str(args.swap_mib),
    '--usb-auto-expand']
  password_file=args.root_password_hash_file
  if password_file is None:
   password_file=Path(folder)/'root-password.sha512'
   password_file.write_text(password_hash(args)+'\n')
   password_file.chmod(0o600)
  command+=['--root-password-hash-file',str(password_file)]
  subprocess.run(command,check=True)
  # Audit the assembled tar, after all rootfs overlays and runlevel links.
  import tarfile
  with tarfile.open(filesystem/'rootfs.tar') as archive:
   for member in archive:
    name=Path(member.name).name.lower()
    if any(word in name for word in DIAGNOSTIC_WORDS) or (
       name.startswith(('ps2-','gs-','s99gs','s99xorg')) and
       any(word in name for word in ('test','probe','gpu2d','core2d'))):
     raise RuntimeError('Diagnostic leaked into production image: '+member.name)
  report=hardware_usb.build_image(filesystem/'rootfs.ext4',args.kernel,args.kernel_config,args.kernel_report,
    output,video_mode=args.video_mode,ntsc=usb_fallback(args),root_size_mib=1842,disk_size_bytes=2_000_000_000,
    readme_bytes=boot_readme(args.video_mode).replace('\n','\r\n').encode())
  report.update(purpose='Production '+args.video_mode+' X.Org desktop for Slim and supported Fat models',
    tests_and_diagnostics_installed=False,removed_diagnostics=removed,
    xorg_applications=programs,clock='windowed classic analogue xclock; 60-second update; GS core drawing',
    native_driver_sha256=sha(stage/'usr/lib/xorg/modules/drivers/ps2_drv.so'),
    userland_profile='full',installed_program_paths=len(inventory['programs']),
    development_commands=FULL_COMMANDS,SDK_headers_and_libraries_preserved=True,
    swap_mib=args.swap_mib,physical_boot_qualified=False,
    first_boot_root_expansion={'enabled':True,'script':'/expand-usb-rootfs.sh',
      'source_sha256':sha(ROOT/'scripts/expand-usb-rootfs.sh'),'silent':True,'self_removes_after_success':True})
  Path(str(output)+'.json').write_text(json.dumps(report,indent=2)+'\n')
 print('Created '+str(output)+' (exactly 2,000,000,000 bytes)',flush=True)
 return report
def main():
 parser=argparse.ArgumentParser(description=__doc__)
 parser.add_argument('command',choices=['usb','dvd','all'])
 parser.add_argument('--variant',choices=['all','slim','fat',*FAT],default='all')
 for name in ('output-dir','work'):parser.add_argument('--'+name,type=Path,required=True)
 parser.add_argument('--staging',type=Path);parser.add_argument('--root-password-hash-file',type=Path)
 for name in ('kernel','kernel-config','kernel-report','dvd-kernel','dvd-kernel-config','dvd-kernel-report'):parser.add_argument('--'+name,type=Path)
 parser.add_argument('--video-mode',choices=('576p','480p'),default='576p')
 for name in ('fallback-kernel','fallback-config','fallback-report','dvd-fallback-kernel','dvd-fallback-config','dvd-fallback-report'):parser.add_argument('--'+name,type=Path)
 parser.add_argument('--toolchain-prefix')
 parser.add_argument('--swap-mib',type=int,default=128);args=parser.parse_args()
 args.work=args.work.resolve();args.output_dir=args.output_dir.resolve();args.work.mkdir(parents=True,exist_ok=True)
 if args.command in ('usb','all'):usb(args)
 if args.command in ('dvd','all'):dvds(args)
 write_checksums(args.output_dir)

def write_checksums(directory):
 names=[f'eelinux-{region}-{suffix}' for region in ('pal','ntsc') for suffix in ('desktop-2gb-usb.img','slim.iso','fat-2.10-2.12.iso','fat-3.04.iso')]
 lines=[]
 for name in names:
  path=directory/name
  if path.exists():
   hardware_usb.regular_path(path,must_exist=True)
   lines.append(sha(path)+'  '+name+'\n')
 (directory/'SHA256SUMS').write_text(''.join(lines))
if __name__=='__main__':main()
