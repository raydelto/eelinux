"""Owned triangle/depth extension after ps2-gs-surfaces.patch."""
from pathlib import Path
import difflib
import subprocess
from kernel_user_vm_port import replace_once

ROOT = Path(__file__).resolve().parents[1]
FILES = {
    'include/uapi/linux/ps2-gs-render.h': 'ps2-gs-render.h',
    'drivers/video/fbdev/ps2fb-gs-render-packet.h': 'ps2fb-gs-render-packet.h',
    'drivers/video/fbdev/ps2fb-gs-render-device.h': 'ps2fb-gs-render-device.h',
}


def adapt_provider(text):
    text = replace_once(text, '#include "ps2fb-gs-surface-packet.h"',
        '#include "ps2fb-gs-surface-packet.h"\n#include "ps2fb-gs-render-packet.h"')
    text = replace_once(text, '\tu64 epoch;', '\tu32 depth_color_bpp[PS2GS_MAX_SURFACES];\n\tu64 epoch;')
    text = replace_once(text, '\tif (handle)\n\t\treturn ps2gs_arena_lookup(&session->arena, handle);',
        '\tif (handle) {\n\t\tstruct ps2gs_buffer *b = ps2gs_arena_lookup(&session->arena, handle);\n'
        '\t\treturn b && !session->depth_color_bpp[b - session->arena.buffers] ? b : NULL;\n\t}')
    text = replace_once(text, '\t} else {\n\t\tconst struct ps2gs_surface_upload *u = request;',
        '\t} else if (operation == PS2GS_RENDER_SUBMIT) {\n'
        '\t\tcount = ps2gs_package_render(par->package.buffer, destination, source, request);\n'
        '\t} else {\n\t\tconst struct ps2gs_surface_upload *u = request;')
    text = replace_once(text, '\t\tif (operation == PS2GS_SURFACE_COPY)\n\t\t\t++par->gs_stats.copies;',
        '\t\tif (operation == PS2GS_SURFACE_COPY)\n\t\t\t++par->gs_stats.copies;\n'
        '\t\tif (operation == PS2GS_RENDER_SUBMIT) {\n'
        '\t\t\tconst struct ps2gs_command *c = request;\n'
        '\t\t\tif (c->op == PS2GS_FILL) ++par->gs_stats.fills;\n'
        '\t\t\tif (c->op == PS2GS_TRIANGLE) ++par->gs_stats.triangles;\n'
        '\t\t\tif (c->op == PS2GS_CLEAR_DEPTH) ++par->gs_stats.depth_clears;\n\t\t}')
    text = replace_once(text, 'static int ps2gs_surface_ioctl_locked(',
        '#include "ps2fb-gs-render-device.h"\n\nstatic int ps2gs_surface_ioctl_locked(')
    text = replace_once(text, '\t\t/* This owned interface currently exposes color storage/transfers. The\n'
        '\t\t * legacy framebuffer triangle/depth engine is a separate ABI endpoint;\n'
        '\t\t * do not claim those operations on a file that cannot submit them. */',
        '\t\t/* Explicitly allocated Z16/Z16S depth and bounded owned rendering. */')
    text = replace_once(text, '\t\t\tPS2GS_HW_SURFACES;',
        '\t\t\tPS2GS_HW_SURFACES | PS2GS_HW_TRIANGLE | PS2GS_HW_DEPTH16 | PS2GS_HW_OWNED_RENDER;')
    text = replace_once(text, '\tswitch (cmd) {',
        '\tswitch (cmd) {\n\tcase PS2GS_DEPTH_CREATE:\n'
        '\t\treturn ps2gs_depth_create_locked(session, info, arg);\n'
        '\tcase PS2GS_RENDER_SUBMIT:\n\t\treturn ps2gs_render_locked(session, info, arg);')
    text = replace_once(text, '\t\treturn ps2gs_arena_destroy(&session->arena, destroy.handle);',
        '\t\treturn ps2gs_destroy_owned(session, destroy.handle);')
    return text


def changes(baseline):
    target = 'drivers/video/fbdev/ps2fb-gs-device.h'
    old = (baseline / target).read_text()
    result = [(target, old, adapt_provider(old))]
    target = 'drivers/video/fbdev/ps2fb.c'
    old = (baseline / target).read_text()
    result.append((target, old, replace_once(old, '#include <linux/ps2-gs-surface.h>',
        '#include <linux/ps2-gs-surface.h>\n#include <linux/ps2-gs-render.h>')))
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
