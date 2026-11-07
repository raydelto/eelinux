"""Owned CT32 RGBA replacement with typed geometry and full-bit masks."""
from pathlib import Path
import difflib, subprocess
from kernel_user_vm_port import replace_once

ROOT = Path(__file__).resolve().parents[1]
FILES = {'include/uapi/linux/ps2-gs-rgba-transfer.h': 'ps2-gs-rgba-transfer.h',
         'drivers/video/fbdev/ps2fb-gs-rgba-transfer-packet.h': 'ps2fb-gs-rgba-transfer-packet.h',
         'drivers/video/fbdev/ps2fb-gs-rgba-transfer-device.h': 'ps2fb-gs-rgba-transfer-device.h'}

def adapt_provider(text):
    text=replace_once(text,'#include "ps2fb-gs-map-blit-packet.h"',
        '#include "ps2fb-gs-map-blit-packet.h"\n#include "ps2fb-gs-rgba-transfer-packet.h"')
    text=replace_once(text,'\tif (operation == PS2GS_CHANNEL_MAP_BLIT) {',
        '\tif (operation == PS2GS_TEXTURE_RGBA_TRANSFER) {\n'
        '\t\tconst struct ps2gs_transfer_operation *op = request;\n'
        '\t\tcount = ps2gs_package_rgba_transfer(par->package.buffer, destination,\n'
        '\t\t\top->texture, op->triangle, op->mask, op->mode);\n'
        '\t} else if (operation == PS2GS_CHANNEL_MAP_BLIT) {')
    text=replace_once(text,'#include "ps2fb-gs-map-blit-device.h"',
        '#include "ps2fb-gs-map-blit-device.h"\n#include "ps2fb-gs-rgba-transfer-device.h"')
    text=replace_once(text,'PS2GS_HW_CHANNEL_MAP_BLIT;',
        'PS2GS_HW_CHANNEL_MAP_BLIT | PS2GS_HW_RGBA_TRANSFER;')
    return replace_once(text,'\tswitch (cmd) {','\tswitch (cmd) {\n'
        '\tcase PS2GS_TEXTURE_RGBA_TRANSFER:\n\t\treturn ps2gs_rgba_transfer_locked(session, info, arg);')

def changes(baseline):
    target='drivers/video/fbdev/ps2fb-gs-device.h';old=(baseline/target).read_text()
    result=[(target,old,adapt_provider(old))]
    target='drivers/video/fbdev/ps2fb.c';old=(baseline/target).read_text()
    result.append((target,old,replace_once(old,'#include <linux/ps2-gs-map-blit.h>',
        '#include <linux/ps2-gs-map-blit.h>\n#include <linux/ps2-gs-rgba-transfer.h>')))
    result.extend((name,'',(ROOT/'kernel/new-drivers'/local).read_text()) for name,local in FILES.items())
    return result

def export(baseline,patch):
    blob=''.join(''.join(difflib.unified_diff(old.splitlines(keepends=True),new.splitlines(keepends=True),
        fromfile='a/'+name if old else '/dev/null',tofile='b/'+name)) for name,old,new in changes(baseline))
    patch.write_text(blob)
    subprocess.run(['git','apply','--check',str(patch.resolve())],cwd=baseline,check=True)
    return blob
