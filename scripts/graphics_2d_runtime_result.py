"""Extract and verify native pixel logs from a stopped disposable session."""
from pathlib import Path
import argparse, hashlib, json, re, struct, subprocess
from emulator_process import process_identity

def sha(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--work', type=Path, required=True)
    p.add_argument('--session',type=Path,help='separate Windows session using the prepared artifacts')
    args = p.parse_args(); session = args.session or args.work / 'session'
    prepared = json.loads((args.work / 'prepared.json').read_text())
    process = json.loads((session / 'running-process.json').read_text())
    if process.get('active') and args.session:
        raise RuntimeError('stop the external session before extracting its disk')
    current = process_identity(process['pid'])
    if current is not None and current == process.get('identity'):
        raise RuntimeError('stop this session before extracting its disk')
    fs = args.work / 'result.ext4'
    with (session / 'rootfs-usb.img').open('rb') as source, fs.open('wb') as target:
        mbr = source.read(512); entry = mbr[462:478]
        if mbr[510:512] != b'\x55\xaa' or entry[4] != 0x83:
            raise ValueError('expected diagnostic root in MBR partition 2')
        start, sectors = struct.unpack_from('<II', entry, 8)
        source.seek(start * 512); remaining = sectors * 512
        while remaining:
            chunk = source.read(min(remaining, 1024 * 1024))
            if not chunk:
                raise ValueError('truncated diagnostic root')
            target.write(chunk); remaining -= len(chunk)
    # A stopped emulator can leave committed ext4 metadata in the journal.
    # Replay only the extracted copy before debugfs reads it; preserve the disk.
    recovered = subprocess.run(['/usr/sbin/e2fsck', '-E', 'journal_only', '-y', str(fs)],
                               capture_output=True, text=True)
    (args.work / 'journal-replay.log').write_text(recovered.stdout + recovered.stderr)
    if recovered.returncode not in (0, 1):
        raise RuntimeError('extracted diagnostic journal recovery failed')
    prepared = json.loads((args.work / 'prepared.json').read_text())
    session_record=json.loads((session/'session.json').read_text())
    for path,key in (('/usr/lib/xorg/modules/drivers/ps2_drv.so','module_sha256'),
                     ('/usr/bin/ps2-xorg-gpu2d-test','client_sha256')):
        extracted=subprocess.run(['/usr/sbin/debugfs','-R','cat '+path,str(fs)],check=True,capture_output=True)
        if hashlib.sha256(extracted.stdout).hexdigest()!=prepared[key]:
            raise RuntimeError('guest artifact identity mismatch: '+path)
    logs = {}
    files=('meminfo.txt','lut.log') if prepared.get('pre_only') else (
        'client.log','Xorg.log','meminfo.txt',*(( 'lut.log',) if prepared.get('pre_client_sha256') else ()))
    for name in files:
        result = subprocess.run(['/usr/sbin/debugfs', '-R', 'cat /var/lib/ps2-xorg-test/' + name,
                                 str(fs)], check=True, capture_output=True, text=True)
        if not result.stdout or 'not found' in result.stderr or 'checksum' in result.stderr:
            raise RuntimeError('diagnostic log unavailable: ' + name + ': ' + result.stderr)
        (args.work / name).write_text(result.stdout); logs[name] = sha(args.work / name)
    if prepared.get('pre_only'):
        serial=session/'pcsx2-profile/PCSX2/logs/emulog.txt';serial_text=serial.read_text()
        probe=(args.work/'lut.log').read_text()
        kind=prepared.get('pre_case') or 'exact-multiply'
        exact=re.search(r'^ps2-'+re.escape(kind)+r': pixel_checks=(\d+) exit=0$',probe,re.M)
        rendered=re.search(r'GL_RENDERER:\s*([^\n]+)',serial_text)
        host_renderer=rendered[1].strip() if rendered else None
        record=json.loads((session/'session.json').read_text())
        expected=prepared.get('expected_pre_checks') or 196608
        if not exact or int(exact[1])!=expected or 'ps2-2d: primitive diagnostic exit=0' not in serial_text:
            raise RuntimeError('native arithmetic diagnostic did not pass')
        extracted=subprocess.run(['/usr/sbin/debugfs','-R','cat /usr/bin/ps2-gs-lut-test',str(fs)],check=True,capture_output=True)
        if hashlib.sha256(extracted.stdout).hexdigest()!=prepared['pre_client_sha256']:
            raise RuntimeError('guest arithmetic diagnostic identity mismatch')
        host_gpu=record.get('gs_renderer')=='opengl' and bool(host_renderer and not re.search(
            r'llvmpipe|softpipe|software|Microsoft Basic|SwiftShader',host_renderer,re.I))
        report={'result':'passed','native_'+kind.replace('-','_')+'_channel_checks':int(exact[1]),
            'native_Xorg_tested':False,'physical_hardware_tested':False,'full_2D_complete':False,
            'guest_RAM_MiB':32,'host_GL_renderer':host_renderer,'host_GPU_acceleration_verified':host_gpu,
            'emulator_host_GS_renderer':record.get('gs_renderer'),
            'guest_software_raster_fallback':False,'kernel_sha256':prepared['diagnostic_kernel_sha256'],
            'pre_client_sha256':prepared['pre_client_sha256'],'serial_sha256':sha(serial),'logs_sha256':logs,
            'extracted_copy_journal_replayed':True,
            'scope':'native GS '+kind+' diagnostic under PCSX2; not physical PS2 proof'}
        if kind=='wide-product':
            report['native_wide_product_byte_channel_checks']=report.pop('native_wide_product_channel_checks')
            report['native_wide_product_u24_products']=int(exact[1])//6
        if kind=='float-multiply':
            report['native_float_multiply_byte_channel_checks']=report.pop('native_float_multiply_channel_checks')
            report['native_float_products']=int(exact[1])//4
        if kind=='float-divide':
            report['native_float_divide_byte_channel_checks']=report.pop('native_float_divide_channel_checks')
            report['native_float_quotients']=int(exact[1])//4
        if kind in ('float-add','float-subtract'):
            key='native_'+kind.replace('-','_')
            report[key+'_byte_channel_checks']=report.pop(key+'_channel_checks')
            report['native_float_sums' if kind=='float-add' else 'native_float_differences']=int(exact[1])//4
        if kind=='saturate-rgba8':
            report['native_saturate_rgba8_pixels']=int(exact[1])//4
        if kind=='wide-bilinear':
            report['native_wide_bilinear_float_results']=int(exact[1])//4
        (args.work/'runtime.json').write_text(json.dumps(report,indent=2)+'\n')
        print(json.dumps(report,indent=2));return
    client = (args.work / 'client.log').read_text(); xorg = (args.work / 'Xorg.log').read_text()
    serial = session / 'pcsx2-profile/PCSX2/logs/emulog.txt'
    serial_text=serial.read_text()
    host_gl=re.search(r'GL_RENDERER:\s*([^\n]+)',serial_text)
    host_renderer=host_gl[1].strip() if host_gl else None
    host_gpu=bool(host_renderer and not re.search(
        r'llvmpipe|softpipe|software|Microsoft Basic|SwiftShader',host_renderer,re.I))
    completed=re.search(r'completed pixel_checks=(\d+) errors=0\b',client)
    if not completed or (prepared.get('expected_pixel_checks') and int(completed[1])!=prepared['expected_pixel_checks']) or \
       'Server terminated successfully (0)' not in xorg or \
       'ps2-2d: native pixel diagnostic exit=0' not in serial_text:
        raise RuntimeError('native client/server diagnostic did not pass')
    lut_checks=0
    map_checks=0
    additional={}
    if prepared.get('pre_client_sha256'):
        pre_binary=subprocess.run(['/usr/sbin/debugfs','-R','cat /usr/bin/ps2-gs-lut-test',str(fs)],check=True,capture_output=True)
        if hashlib.sha256(pre_binary.stdout).hexdigest()!=prepared['pre_client_sha256']:
            raise RuntimeError('guest pre-client diagnostic identity mismatch')
        cursor=re.search(r'ps2-cursor-argb: pixel_checks=(\d+) image_checks=22 exit=0\b',(args.work/'lut.log').read_text())
        if cursor and (prepared.get('pre_case')!='cursor-argb' or int(cursor[1])!=45056):raise RuntimeError('native ARGB cursor diagnostic identity/count mismatch')
        if cursor:additional.update(native_cursor_argb_pixel_checks=int(cursor[1]),native_cursor_argb_image_checks=22)
        lut=re.search(r'ps2-lut: pixel_checks=(\d+) exit=0\b',(args.work/'lut.log').read_text())
        exact=re.search(r'ps2-exact-multiply: pixel_checks=(\d+) exit=0\b',(args.work/'lut.log').read_text())
        ordered=re.search(r'ps2-order-blends: pixel_checks=(\d+) exit=0\b',(args.work/'lut.log').read_text())
        pieces=re.search(r'ps2-piecewise: pixel_checks=(\d+) exit=0\b',(args.work/'lut.log').read_text())
        if pieces and (prepared.get('pre_case')!='piecewise' or int(pieces[1])!=prepared.get('expected_pre_checks')):
            raise RuntimeError('native piecewise diagnostic identity/count mismatch')
        if not lut and not exact and not ordered and not pieces and not cursor:raise RuntimeError('native GS pre-client diagnostic did not pass')
        if ordered and (prepared.get('pre_case')!='order-blends' or int(ordered[1])!=prepared.get('expected_pre_checks')):
            raise RuntimeError('native ordered-blend diagnostic identity/count mismatch')
        lut_checks=int(lut[1]) if lut else 0
        mapped=re.search(r'ps2-map: pixel_checks=(\d+) exit=0\b',(args.work/'lut.log').read_text())
        if mapped:map_checks=int(mapped[1])
        for label in ('map-blit','rgba-solid','alpha-read','rgba-copy','rgba-multiply'):
            checked=re.search(r'ps2-'+label+r': pixel_checks=(\d+) exit=0\b',(args.work/'lut.log').read_text())
            if checked:additional['native_'+label.replace('-','_')+'_pixel_checks']=int(checked[1])
        if exact:additional['native_exact_multiply_channel_checks']=int(exact[1])
        if ordered:additional['native_order_blends_channel_checks']=int(ordered[1])
        if pieces:additional['native_piecewise_channel_checks']=int(pieces[1])
    depth = re.search(r'checks passed depth=(\d+)', client)
    report = {'result': 'passed', 'native_pixel_checks': int(completed[1]), 'client_errors': 0,
        'native_LUT_pixel_checks':lut_checks,
        'native_MAP_pixel_checks':map_checks,
        **additional,
        'Xorg_exit_code': 0, 'diagnostic_exit_code': 0, 'depth': int(depth[1]) if depth else None,
        'guest_RAM_MiB': 32, 'physical_hardware_tested': False, 'full_2D_complete': False,
        'CPU_colour_destination_rendering': False,
        'CPU_coverage_pixel_arithmetic':prepared.get('CPU_coverage_pixel_arithmetic'),
        'source_plane_bit_serialization_on_CPU': True,
        'render_checks_against_pixman':len(re.findall(r'^ps2-gpu2d: Render |^ps2-gpu2d: solid source |^ps2-gpu2d: glyph |^ps2-gpu2d: alpha maps |^ps2-gpu2d: alpha window depth=\d+ role=|^ps2-gpu2d: alpha raw alias |^ps2-gpu2d: raw colour |^ps2-gpu2d: wide Saturate ',client,re.M)),
        'kernel_sha256': prepared['diagnostic_kernel_sha256'],
        'module_sha256': prepared['module_sha256'], 'client_sha256': prepared['client_sha256'],
        'pre_client_sha256':prepared.get('pre_client_sha256'),
        'prepared_rootfs_sha256': prepared['rootfs_sha256'], 'logs_sha256': logs,
        'serial_sha256': sha(serial),
        'extracted_copy_journal_replayed': True,
        'scope': 'actual native Xorg pixel protocol; '+(prepared.get('client_case') or 'full diagnostic')+'; no new TWM/xterm interactive qualification',
        'client_case':prepared.get('client_case'),
        'shard_count':prepared.get('shard_count',1),'shard_index':prepared.get('shard_index',0),
        'emulator_host_renderer': ('PCSX2 GS software renderer; native guest packets, no guest software renderer'
            if session_record.get('gs_renderer')=='software' else
            'PCSX2 OpenGL hardware mode with host '+(host_renderer or 'unidentified renderer')),
        'host_GL_renderer':host_renderer,
        'host_GPU_acceleration_verified':host_gpu and session_record.get('gs_renderer')=='opengl',
        'boot_observation': ('root mounts and pixel session passes; boot reports disk I/O errors'
            if 'I/O error, dev sda' in serial_text else 'root mounts and pixel session passes; no disk I/O errors in serial log')}
    (args.work / 'runtime.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))

if __name__ == '__main__':
    main()
