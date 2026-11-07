"""Bounded owned texture sampling after ps2-gs-rendering.patch."""
from pathlib import Path
import difflib
import subprocess
from kernel_user_vm_port import replace_once

ROOT = Path(__file__).resolve().parents[1]
FILES = {
    'include/uapi/linux/ps2-gs-texture.h': 'ps2-gs-texture.h',
    'drivers/video/fbdev/ps2fb-gs-texture-packet.h': 'ps2fb-gs-texture-packet.h',
    'drivers/video/fbdev/ps2fb-gs-texture-device.h': 'ps2fb-gs-texture-device.h',
}


def adapt_provider(text):
    text = replace_once(text, '#include "ps2fb-gs-render-packet.h"',
        '#include "ps2fb-gs-render-packet.h"\n#include "ps2fb-gs-texture-packet.h"')
    text = replace_once(text, '\t} else if (operation == PS2GS_RENDER_SUBMIT) {',
        '\t} else if (operation == PS2GS_TEXTURE_SUBMIT) {\n'
        '\t\tconst struct ps2gs_texture_operation *t = request;\n'
        '\t\tcount = ps2gs_package_texture(par->package.buffer, destination, source, t->texture, t->triangle);\n'
        '\t} else if (operation == PS2GS_RENDER_SUBMIT) {')
    text = replace_once(text, '\t\tif (operation == PS2GS_RENDER_SUBMIT) {',
        '\t\tif (operation == PS2GS_TEXTURE_SUBMIT) ++par->gs_stats.triangles;\n'
        '\t\tif (operation == PS2GS_RENDER_SUBMIT) {')
    text = replace_once(text, '#include "ps2fb-gs-render-device.h"',
        '#include "ps2fb-gs-render-device.h"\n#include "ps2fb-gs-texture-device.h"')
    text = replace_once(text, 'PS2GS_HW_DEPTH16 | PS2GS_HW_OWNED_RENDER;',
        'PS2GS_HW_DEPTH16 | PS2GS_HW_OWNED_RENDER | PS2GS_HW_TEXTURE;')
    text = replace_once(text, '\tswitch (cmd) {', '\tswitch (cmd) {\n'
        '\tcase PS2GS_TEXTURE_SUBMIT:\n\t\treturn ps2gs_texture_locked(session, info, arg);')
    return text


def changes(baseline):
    target = 'drivers/video/fbdev/ps2fb-gs-device.h'
    old = (baseline / target).read_text()
    result = [(target, old, adapt_provider(old))]
    target = 'drivers/video/fbdev/ps2fb.c'
    old = (baseline / target).read_text()
    result.append((target, old, replace_once(old, '#include <linux/ps2-gs-render.h>',
        '#include <linux/ps2-gs-render.h>\n#include <linux/ps2-gs-texture.h>')))
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
