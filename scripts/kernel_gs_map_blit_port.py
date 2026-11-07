"""Bounded channel mapping between arbitrary owned CT32 surfaces."""
from pathlib import Path
import difflib, subprocess
from kernel_user_vm_port import replace_once
ROOT = Path(__file__).resolve().parents[1]
FILES = {'include/uapi/linux/ps2-gs-map-blit.h': 'ps2-gs-map-blit.h',
         'drivers/video/fbdev/ps2fb-gs-map-blit-packet.h': 'ps2fb-gs-map-blit-packet.h',
         'drivers/video/fbdev/ps2fb-gs-map-blit-device.h': 'ps2fb-gs-map-blit-device.h'}

def adapt_provider(text):
    text=replace_once(text,'#include "ps2fb-gs-map-packet.h"',
        '#include "ps2fb-gs-map-packet.h"\n#include "ps2fb-gs-map-blit-packet.h"')
    text=replace_once(text,'\tif (operation == PS2GS_CHANNEL_MAP) {',
        '\tif (operation == PS2GS_CHANNEL_MAP_BLIT) {\n'
        '\t\tcount = ps2gs_package_map_blit(par->package.buffer, destination, request);\n'
        '\t} else if (operation == PS2GS_CHANNEL_MAP) {')
    text=replace_once(text,'#include "ps2fb-gs-map-device.h"',
        '#include "ps2fb-gs-map-device.h"\n#include "ps2fb-gs-map-blit-device.h"')
    text=replace_once(text,'PS2GS_HW_CHANNEL_MAP;', 'PS2GS_HW_CHANNEL_MAP | PS2GS_HW_CHANNEL_MAP_BLIT;')
    return replace_once(text,'\tswitch (cmd) {','\tswitch (cmd) {\n'
        '\tcase PS2GS_CHANNEL_MAP_BLIT:\n\t\treturn ps2gs_map_blit_locked(session, info, arg);')

def changes(baseline):
    target='drivers/video/fbdev/ps2fb-gs-device.h';old=(baseline/target).read_text()
    result=[(target,old,adapt_provider(old))]
    target='drivers/video/fbdev/ps2fb.c';old=(baseline/target).read_text()
    result.append((target,old,replace_once(old,'#include <linux/ps2-gs-map.h>',
        '#include <linux/ps2-gs-map.h>\n#include <linux/ps2-gs-map-blit.h>')))
    result.extend((name,'',(ROOT/'kernel/new-drivers'/local).read_text()) for name,local in FILES.items())
    return result

def export(baseline,patch):
    blob=''.join(''.join(difflib.unified_diff(old.splitlines(keepends=True),new.splitlines(keepends=True),
        fromfile='a/'+name if old else '/dev/null',tofile='b/'+name)) for name,old,new in changes(baseline))
    patch.write_text(blob)
    subprocess.run(['git','apply','--check',str(patch.resolve())],cwd=baseline,check=True)
    return blob
