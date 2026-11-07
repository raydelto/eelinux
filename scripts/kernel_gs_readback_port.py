"""Native read-only GS FIFO transfers after ps2-gs-transfer.patch."""
from pathlib import Path
import difflib,subprocess
from kernel_user_vm_port import replace_once
ROOT=Path(__file__).resolve().parents[1]
FILES={'include/uapi/linux/ps2-gs-readback.h':'ps2-gs-readback.h',
    'drivers/video/fbdev/ps2fb-gs-readback-packet.h':'ps2fb-gs-readback-packet.h',
    'drivers/video/fbdev/ps2fb-gs-readback-device.h':'ps2fb-gs-readback-device.h'}
def adapt_provider(text):
    text=replace_once(text,'#include "ps2fb-gs-transfer-packet.h"',
        '#include "ps2fb-gs-transfer-packet.h"\n#include "ps2fb-gs-readback-packet.h"')
    text=replace_once(text,'#include "ps2fb-gs-transfer-device.h"',
        '#include "ps2fb-gs-transfer-device.h"\n#include "ps2fb-gs-readback-device.h"')
    text=replace_once(text,'PS2GS_HW_TEXTURE_TRANSFER;','PS2GS_HW_TEXTURE_TRANSFER | PS2GS_HW_READBACK;')
    return replace_once(text,'\tswitch (cmd) {','\tswitch (cmd) {\n'
        '\tcase PS2GS_SURFACE_DOWNLOAD:\n\t\treturn ps2gs_download_locked(session, info, arg);')
def changes(baseline):
    target='drivers/video/fbdev/ps2fb-gs-device.h';old=(baseline/target).read_text()
    result=[(target,old,adapt_provider(old))]
    target='drivers/video/fbdev/ps2fb.c';old=(baseline/target).read_text()
    result.append((target,old,replace_once(old,'#include <linux/ps2-gs-transfer.h>',
        '#include <linux/ps2-gs-transfer.h>\n#include <linux/ps2-gs-readback.h>')))
    result.extend((name,'',(ROOT/'kernel/new-drivers'/local).read_text()) for name,local in FILES.items())
    return result
def export(baseline,patch):
    blob=''.join(''.join(difflib.unified_diff(old.splitlines(keepends=True),new.splitlines(keepends=True),
        fromfile='a/'+name if old else '/dev/null',tofile='b/'+name)) for name,old,new in changes(baseline))
    patch.write_text(blob);subprocess.run(['git','apply','--check',str(patch.resolve())],cwd=baseline,check=True);return blob
