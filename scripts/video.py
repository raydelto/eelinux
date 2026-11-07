"""Build the small GNU/Linux framebuffer mode selector; no BusyBox dependency."""
from pathlib import Path
import argparse
import json
import subprocess
import tempfile
from common import ROOT, TARGET_FLAGS

MODES = json.loads((ROOT / 'configs/video-modes.json').read_text())
BOOT_MODES = {'auto': '', **{mode['id']: mode['spec'] for mode in MODES
                           if not mode.get('experimental', False)}}


def header() -> str:
    return '/* Generated from configs/video-modes.json. */\n' + '\n'.join(
        '{' + ','.join([json.dumps(m['id']), json.dumps(m['sysfs']), str(m['width']),
                       str(m['height']), str(m['hz']), str(int(m['interlaced'])),
                       str(int(m['family'] == 'rgb-sog')), str(int(m.get('experimental', False)))]) + '},'
        for m in MODES) + '\n'


def build(staging: Path, prefix: str) -> Path:
    staging.mkdir(parents=True, exist_ok=True)
    target = staging / 'usr/sbin/ps2-video'
    target.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='eelinux-video-') as folder:
        (Path(folder) / 'modes.inc').write_text(header())
        subprocess.run([prefix + 'gcc', *TARGET_FLAGS, '-Os', '-s', '-std=c11',
                        '-Wall', '-Wextra', '-Werror', '-I'+folder,
                        str(ROOT / 'video/ps2-video.c'), '-o', str(target)], check=True)
    target.chmod(0o755)
    from userland import validate_elf
    validate_elf(target)
    return target


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--staging', type=Path, required=True)
    parser.add_argument('--toolchain-prefix', required=True)
    args = parser.parse_args()
    print(build(args.staging, args.toolchain_prefix))
