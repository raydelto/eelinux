"""Expand the GS handle table while retaining the existing ioctl and VRAM bounds."""
from pathlib import Path
import difflib,subprocess
ROOT=Path(__file__).resolve().parents[1]
def changes(baseline):
    target='include/uapi/linux/ps2-gs-surface.h'
    old=(baseline/target).read_text();new=(ROOT/'kernel/new-drivers/ps2-gs-surface.h').read_text()
    if '#define PS2GS_MAX_SURFACES 64' not in old:raise RuntimeError('expected the verified 64-handle baseline')
    return [(target,old,new)]
def export(baseline,patch):
    blob=''.join(''.join(difflib.unified_diff(old.splitlines(keepends=True),new.splitlines(keepends=True),
        fromfile='a/'+name,tofile='b/'+name)) for name,old,new in changes(baseline))
    patch.write_text(blob)
    subprocess.run(['git','apply','--check',str(patch.resolve())],cwd=baseline,check=True)
    return blob
