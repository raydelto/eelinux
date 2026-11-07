"""Bounded indexed GS channel arithmetic after ps2-gs-batch.patch."""
from pathlib import Path
import difflib, subprocess
from kernel_user_vm_port import replace_once
ROOT = Path(__file__).resolve().parents[1]
FILES = {'include/uapi/linux/ps2-gs-lut.h': 'ps2-gs-lut.h',
         'drivers/video/fbdev/ps2fb-gs-lut-packet.h': 'ps2fb-gs-lut-packet.h',
         'drivers/video/fbdev/ps2fb-gs-lut-device.h': 'ps2fb-gs-lut-device.h'}

def adapt_provider(text):
    text = replace_once(text, '#include "ps2fb-gs-batch-packet.h"',
        '#include "ps2fb-gs-batch-packet.h"\n#include "ps2fb-gs-lut-packet.h"')
    text = replace_once(text, '\tif (operation == PS2GS_INTERNAL_BATCH) {\n\t\tcount = ps2gs_package_batch',
        '\tif (operation == PS2GS_CHANNEL_LUT) {\n'
        '\t\tcount = ps2gs_package_lut(par->package.buffer, destination, request);\n'
        '\t} else if (operation == PS2GS_INTERNAL_BATCH) {\n\t\tcount = ps2gs_package_batch')
    text = replace_once(text, '#include "ps2fb-gs-texture-device.h"',
        '#include "ps2fb-gs-texture-device.h"\n#include "ps2fb-gs-lut-device.h"')
    text = replace_once(text, 'PS2GS_HW_SOLID_BATCH;', 'PS2GS_HW_SOLID_BATCH | PS2GS_HW_CHANNEL_LUT;')
    return replace_once(text, '\tswitch (cmd) {', '\tswitch (cmd) {\n'
        '\tcase PS2GS_CHANNEL_LUT:\n\t\treturn ps2gs_lut_locked(session, info, arg);')

def changes(baseline):
    target = 'drivers/video/fbdev/ps2fb-gs-device.h'; old = (baseline / target).read_text()
    result = [(target, old, adapt_provider(old))]
    target = 'drivers/video/fbdev/ps2fb.c'; old = (baseline / target).read_text()
    result.append((target, old, replace_once(old, '#include <linux/ps2-gs-batch.h>',
        '#include <linux/ps2-gs-batch.h>\n#include <linux/ps2-gs-lut.h>')))
    target='drivers/video/fbdev/ps2fb-gs-transfer-device.h';old=(baseline/target).read_text()
    result.append((target,old,replace_once(old,'request.mode > PS2GS_TRANSFER_SUBTRACT',
        'request.mode > PS2GS_TRANSFER_ADD_CLAMP')))
    target='include/uapi/linux/ps2-gs-transfer.h';old=(baseline/target).read_text()
    result.append((target,old,(ROOT/'kernel/new-drivers/ps2-gs-transfer.h').read_text()))
    result.extend((name, '', (ROOT / 'kernel/new-drivers' / local).read_text()) for name, local in FILES.items())
    return result

def export(baseline, patch):
    blob = ''.join(''.join(difflib.unified_diff(old.splitlines(keepends=True), new.splitlines(keepends=True),
        fromfile='a/' + name if old else '/dev/null', tofile='b/' + name)) for name, old, new in changes(baseline))
    patch.write_text(blob)
    subprocess.run(['git', 'apply', '--check', str(patch.resolve())], cwd=baseline, check=True)
    return blob
