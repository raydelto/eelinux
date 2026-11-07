"""Typed masked texture transfers after ps2-gs-logic.patch."""
from pathlib import Path
import difflib
import subprocess
from kernel_user_vm_port import replace_once

ROOT = Path(__file__).resolve().parents[1]
FILES = {
    'include/uapi/linux/ps2-gs-transfer.h': 'ps2-gs-transfer.h',
    'drivers/video/fbdev/ps2fb-gs-transfer-packet.h': 'ps2fb-gs-transfer-packet.h',
    'drivers/video/fbdev/ps2fb-gs-transfer-device.h': 'ps2fb-gs-transfer-device.h',
}

def adapt_provider(text):
    text = replace_once(text, '#include "ps2fb-gs-logic-packet.h"',
        '#include "ps2fb-gs-logic-packet.h"\n#include "ps2fb-gs-transfer-packet.h"')
    text = replace_once(text, '\t} else if (operation == PS2GS_SOLID_LOGIC) {',
        '\t} else if (operation == PS2GS_TEXTURE_TRANSFER) {\n'
        '\t\tconst struct ps2gs_transfer_operation *t = request;\n'
        '\t\tcount = ps2gs_package_transfer(par->package.buffer, destination, t->texture, t->triangle, t->mask, t->mode);\n'
        '\t} else if (operation == PS2GS_SOLID_LOGIC) {')
    text = replace_once(text, '#include "ps2fb-gs-logic-device.h"',
        '#include "ps2fb-gs-logic-device.h"\n#include "ps2fb-gs-transfer-device.h"')
    text = replace_once(text, 'PS2GS_HW_TEXTURE | PS2GS_HW_SOLID_LOGIC;',
        'PS2GS_HW_TEXTURE | PS2GS_HW_SOLID_LOGIC | PS2GS_HW_TEXTURE_TRANSFER;')
    return replace_once(text, '\tswitch (cmd) {', '\tswitch (cmd) {\n'
        '\tcase PS2GS_TEXTURE_TRANSFER:\n\t\treturn ps2gs_transfer_locked(session, info, arg);')

def changes(baseline):
    target = 'drivers/video/fbdev/ps2fb-gs-device.h'
    old = (baseline / target).read_text()
    result = [(target, old, adapt_provider(old))]
    target = 'drivers/video/fbdev/ps2fb.c'
    old = (baseline / target).read_text()
    result.append((target, old, replace_once(old, '#include <linux/ps2-gs-logic.h>',
        '#include <linux/ps2-gs-logic.h>\n#include <linux/ps2-gs-transfer.h>')))
    for target, local in FILES.items():
        result.append((target, '', (ROOT / 'kernel/new-drivers' / local).read_text()))
    return result

def export(baseline, patch):
    blob = ''.join(''.join(difflib.unified_diff(old.splitlines(keepends=True), new.splitlines(keepends=True),
        fromfile='a/' + name if old else '/dev/null', tofile='b/' + name))
        for name, old, new in changes(baseline))
    patch.write_text(blob)
    subprocess.run(['git', 'apply', '--check', str(patch.resolve())], cwd=baseline, check=True)
    return blob
