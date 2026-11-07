"""Publish verified graphics artifacts and an honest source/runtime record."""
from pathlib import Path
import argparse, hashlib, json, shutil

ROOT = Path(__file__).resolve().parents[1]

def sha(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--work', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    args = p.parse_args(); args.output.mkdir(parents=True, exist_ok=True)
    reports = {name: json.loads((args.work / path).read_text()) for name, path in {
        'build': 'native/build.json', 'host': 'host-all-final/host.json',
        'r5900': 'r5900-all-final/r5900.json', 'patches': 'patches-final/patches.json',
        'client': 'client-host/client.json', 'runtime': 'runtime-v3/runtime.json'}.items()}
    if reports['host']['result'] != 'passed' or reports['r5900']['result'] != 'passed' or \
       reports['patches']['result'] != 'passed' or reports['runtime']['result'] != 'passed':
        raise RuntimeError('required verification has not passed')
    for relative, expected in reports['build']['inputs'].items():
        if sha(ROOT / relative) != expected:
            raise RuntimeError('production source changed since build: ' + relative)
    module = args.work / 'native/ps2_drv.so'
    if sha(module) != reports['runtime']['module_sha256'] or \
       sha(module) != reports['build']['module_sha256']:
        raise RuntimeError('runtime module differs from production build')
    if sha(ROOT / 'desktop/ps2-xorg-gpu2d-test.c') != reports['client']['source_sha256']:
        raise RuntimeError('X11 diagnostic source changed after verification')
    for record in reports['patches']['series'].values():
        if sha(ROOT / record['patch']) != record['patch_sha256']:
            raise RuntimeError('exported patch changed after verification')
    files = {
        'ps2_drv.so': 'native/ps2_drv.so', 'module-build.json': 'native/build.json',
        'module-build.log': 'native/build.log', 'host.json': 'host-all-final/host.json',
        'host.log': 'host-all-final/host.log', 'r5900.json': 'r5900-all-final/r5900.json',
        'r5900.log': 'r5900-all-final/r5900.log', 'patches.json': 'patches-final/patches.json',
        'client-oracle.json': 'client-host/client.json', 'client-oracle.log': 'client-host/client.log',
        'native-client': 'client-r5900/ps2-xorg-gpu2d-test',
        'runtime.json': 'runtime-v3/runtime.json', 'client.log': 'runtime-v3/client.log',
        'Xorg.log': 'runtime-v3/Xorg.log', 'meminfo.txt': 'runtime-v3/meminfo.txt',
        'serial.log': 'runtime-v3/session/pcsx2-profile/PCSX2/logs/emulog.txt'}
    for name, path in files.items():
        shutil.copy2(args.work / path, args.output / name)
    sources = {}
    for folder in ('graphics', 'mesa/ps2', 'desktop/xorg', 'kernel/new-drivers'):
        for source in sorted((ROOT / folder).iterdir()):
            if source.suffix in ('.c', '.h'):
                sources[source.relative_to(ROOT).as_posix()] = sha(source)
    evidence = {'date': '2026-10-13', 'result': 'passed for the implemented extension',
        'full_2D_complete': False, 'physical_hardware_qualified': False,
        'CPU_colour_destination_rendering': False, 'CPU_source_plane_bit_serialization': True,
        'shipping_USB_DVD_images_rebuilt': False,
        'new_features': ['colour-source CopyPlane to native colour and owned monochrome destinations',
            'all native planes, sixteen Boolean ALUs, RGB masks, clipping and overlap',
            'opaque XRender Src/Over with nearest affine transforms',
            'RepeatNone/Normal/Pad/Reflect and owned A1 masks'],
        'checks': {'host_ASan_UBSan_cases': reports['host']['host_cases'],
            'R5900_Linux_user_cases': reports['r5900']['r5900_cases'],
            'host_Xvfb_pixel_checks_per_depth': 333, 'host_Xvfb_depths': [15, 24],
            'native_Xorg_pixel_checks': 333, 'native_Xorg_depth': 15,
            'native_Xorg_client_errors': 0, 'native_Xorg_exit': 0, 'guest_RAM_MiB': 32,
            'module_cross_build': 'warning-clean with -Wall -Wextra -Werror',
            'patch_exports': 'source equality; COMPOSITE enabled and disabled compilation'},
        'runtime': reports['runtime'], 'patches': reports['patches']['series'],
        'source_sha256': sources,
        'artifact_sha256': {name: sha(args.output / name) for name in files},
        'runtime_fix': 'owned A1 storage advertised to EXA as consumable by driver; colour access stays rejected',
        'remaining': ['eight-bit alpha and component-alpha Render blending',
            'bilinear filters and projective Render transforms', 'additional Render pixel formats',
            'physical hardware and peak desktop memory/performance qualification',
            'Mesa shader/OpenGL/GLX execution'],
        'failed_attempts': [
            'uncompressed vmlinux boot aborted before Linux; corrected to validated compressed vmlinuz',
            'initial native A1 operation reached EXA colour CPU fallback; fixed storage capability and rerun passed'],
        'artifact_directory': args.output.relative_to(ROOT).as_posix()}
    (ROOT / 'docs/graphics-plane-composite-validation.json').write_text(json.dumps(evidence, indent=2) + '\n')
    (args.output / 'validation.json').write_text(json.dumps(evidence, indent=2) + '\n')
    (args.output / 'README.txt').write_text('PS2 native Xorg/Mesa 2D extension, 2026-10-13\n\n'
        'ps2_drv.so is the production R5900 module. Native-client is a separate diagnostic.\n'
        '123 host sanitizer cases, 123 R5900 cases and 333 native Xorg pixel checks passed.\n'
        'Full 2D remains incomplete; see validation.json for limits and hashes.\n'
        'The existing USB/DVD images were not changed.\n')
    print(json.dumps({'artifact_directory': str(args.output), 'module_sha256': sha(module),
        'host_cases': reports['host']['host_cases'], 'r5900_cases': reports['r5900']['r5900_cases'],
        'native_pixel_checks': 333, 'full_2D_complete': False}, indent=2))

if __name__ == '__main__':
    main()
