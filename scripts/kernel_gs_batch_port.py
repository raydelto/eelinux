"""Ordered bounded GS DMA batches after ps2-gs-readback.patch."""
from pathlib import Path
import difflib
import subprocess
from kernel_user_vm_port import replace_once
ROOT = Path(__file__).resolve().parents[1]
FILES = {
    'include/uapi/linux/ps2-gs-batch.h': 'ps2-gs-batch.h',
    'drivers/video/fbdev/ps2fb-gs-batch-packet.h': 'ps2fb-gs-batch-packet.h',
    'drivers/video/fbdev/ps2fb-gs-batch-device.h': 'ps2fb-gs-batch-device.h',
}
def adapt_provider(text):
    text = replace_once(text, '#include "ps2fb-gs-transfer-packet.h"',
        '#include "ps2fb-gs-transfer-packet.h"\n#include "ps2fb-gs-batch-packet.h"')
    text = replace_once(text, '\tif (operation == PS2GS_SURFACE_FILL) {',
        '\tif (operation == PS2GS_INTERNAL_BATCH) {\n'
        '\t\tcount = ps2gs_package_batch(par->package.buffer, destination, source, request);\n'
        '\t} else if (operation == PS2GS_SURFACE_FILL) {')
    text = replace_once(text, '#include "ps2fb-gs-render-device.h"',
        '#include "ps2fb-gs-batch-device.h"\n#include "ps2fb-gs-render-device.h"')
    text = replace_once(text, 'PS2GS_HW_READBACK;', 'PS2GS_HW_READBACK | PS2GS_HW_SOLID_BATCH;')
    text = replace_once(text, '\tswitch (cmd) {', '\tswitch (cmd) {\n'
        '\tcase PS2GS_SOLID_BATCH:\n\t\treturn ps2gs_solid_batch_locked(session, info, arg);')
    text = replace_once(text, '\t\t++par->gs_stats.completed_batches;',
        '\t\t++par->gs_stats.completed_batches;\n'
        '\t\tif (operation == PS2GS_INTERNAL_BATCH) {\n'
        '\t\t\tconst struct ps2gs_draw_batch *b = request;\n'
        '\t\t\tif (b->operation == PS2GS_SOLID_LOGIC) par->gs_stats.fills += b->count;\n'
        '\t\t\tif (b->operation == PS2GS_TEXTURE_SUBMIT || b->operation == PS2GS_TEXTURE_TRANSFER)\n'
        '\t\t\t\tpar->gs_stats.triangles += b->count;\n'
        '\t\t\tif (b->operation == PS2GS_RENDER_SUBMIT) {\n'
        '\t\t\t\tconst struct ps2gs_command *c = b->requests;\n'
        '\t\t\t\tu32 j;\n'
        '\t\t\t\tfor (j = 0; j < b->count; ++j) {\n'
        '\t\t\t\t\tif (c[j].op == PS2GS_FILL) ++par->gs_stats.fills;\n'
        '\t\t\t\t\tif (c[j].op == PS2GS_TRIANGLE) ++par->gs_stats.triangles;\n'
        '\t\t\t\t\tif (c[j].op == PS2GS_CLEAR_DEPTH) ++par->gs_stats.depth_clears;\n'
        '\t\t\t\t}\n\t\t\t}\n\t\t}\n')
    return text
def adapt_render(text):
    start = text.index('\tfor (i = 0; i < request.count; ++i) {\n\t\tret = ps2gs_surface_execute')
    end = text.index('\nout:', start)
    return text[:start] + '\tret = ps2gs_execute_draw_batch(info, PS2GS_RENDER_SUBMIT, depth, color,\n\t\tcommands, request.count, NULL, 0, 0);\n\tif (ret) session->failed = ret;\n' + text[end:]
def adapt_texture(text):
    start = text.index('\tfor (i = 0; i < request.count; ++i) {\n\t\tstruct ps2gs_texture_operation')
    end = text.index('\nout:', start)
    return text[:start] + '\tret = ps2gs_execute_draw_batch(info, PS2GS_TEXTURE_SUBMIT, depth, color,\n\t\ttriangles, request.count, texture, 0, 0);\n\tif (ret) session->failed = ret;\n' + text[end:]
def adapt_transfer(text):
    start = text.index('\tret = 0;\n\tfor (i = 0; request.plane_mask')
    end = text.index('\nout:', start)
    return text[:start] + '\tret = request.plane_mask ? ps2gs_execute_draw_batch(info, PS2GS_TEXTURE_TRANSFER, NULL, color,\n\t\ttriangles, r->count, texture, request.plane_mask, request.mode) : 0;\n\tif (ret) session->failed = ret;\n' + text[end:]
def changes(baseline):
    result = []
    for name, transform in [('ps2fb-gs-device.h', adapt_provider), ('ps2fb-gs-render-device.h', adapt_render),
                            ('ps2fb-gs-texture-device.h', adapt_texture), ('ps2fb-gs-transfer-device.h', adapt_transfer)]:
        target = 'drivers/video/fbdev/' + name
        old = (baseline / target).read_text()
        result.append((target, old, transform(old)))
    target = 'drivers/video/fbdev/ps2fb.c'; old = (baseline / target).read_text()
    result.append((target, old, replace_once(old, '#include <linux/ps2-gs-readback.h>',
        '#include <linux/ps2-gs-readback.h>\n#include <linux/ps2-gs-batch.h>')))
    for target, local in FILES.items():
        result.append((target, '', (ROOT / 'kernel/new-drivers' / local).read_text()))
    return result
def export(baseline, patch):
    blob = ''.join(''.join(difflib.unified_diff(old.splitlines(keepends=True), new.splitlines(keepends=True),
        fromfile='a/' + name if old else '/dev/null', tofile='b/' + name)) for name, old, new in changes(baseline))
    patch.write_text(blob)
    subprocess.run(['git', 'apply', '--check', str(patch.resolve())], cwd=baseline, check=True)
    return blob
