"""Export and apply bounded hardware rendering commands for the PS2 GS."""
from pathlib import Path
import difflib
import subprocess
from kernel_user_vm_port import replace_once

ROOT = Path(__file__).resolve().parents[1]


def adapt_framebuffer(text):
    text = replace_once(text, '#include <linux/mm.h>',
        '#include <linux/mm.h>\n#include <linux/capability.h>\n'
        '#include <linux/console.h>\n#include <linux/ps2-gs.h>')
    text = replace_once(text, '\tstruct fb_deferred_io shadow_io;',
        '\tstruct fb_deferred_io shadow_io;\n'
        '\tu32 gs_font_blocks;\n\tstruct ps2gs_caps gs_stats;')
    text = replace_once(text, '\tpar->cb.tile = cb_tile(map->width, map->height);',
        '\tpar->cb.tile = cb_tile(map->width, map->height);\n'
        '\tpar->gs_font_blocks = DIV_ROUND_UP(map->length,\n'
        '\t\tpar->cb.tile.block.cols * par->cb.tile.block.rows);')
    text = replace_once(text, '#include "ps2fb-shadow.h"',
        '#include "ps2fb-shadow.h"\n#include "ps2fb-gs.h"')
    text = replace_once(text, '\t\t.fb_mmap\t= ps2fb_shadow_mmap,',
        '\t\t.fb_ioctl\t= ps2gs_ioctl,\n\t\t.fb_mmap\t= ps2fb_shadow_mmap,')
    return text


def install(source: Path, patch: Path):
    name = 'drivers/video/fbdev/ps2fb.c'
    old = (source/name).read_text()
    new = adapt_framebuffer(old)
    changes = [(name, old, new)]
    for name, local in [('drivers/video/fbdev/ps2fb-gs.h', 'ps2fb-gs.h'),
                        ('drivers/video/fbdev/ps2fb-gs-image.h', 'ps2fb-gs-image.h'),
                        ('include/uapi/linux/ps2-gs.h', 'ps2-gs.h')]:
        changes.append((name, '', (ROOT/'kernel/new-drivers'/local).read_text()))
    blob = ''.join(''.join(difflib.unified_diff(a.splitlines(keepends=True),
        b.splitlines(keepends=True), fromfile='/dev/null' if not a else 'a/'+name,
        tofile='b/'+name)) for name,a,b in changes)
    if patch.exists() and patch.read_text() != blob:
        raise RuntimeError('Re-export from a fresh framebuffer baseline')
    patch.parent.mkdir(parents=True, exist_ok=True)
    patch.write_text(blob)
    subprocess.run(['git','apply','--check',str(patch.resolve())],cwd=source,check=True)
    subprocess.run(['git','apply',str(patch.resolve())],cwd=source,check=True)
    return {'GS_command_engine': True, 'bounded_GS_image_upload': True, 'software_rasterizer': False,
            'Mesa_driver': False, 'Xorg_accelerated_DDX': False,
            'physical_hardware_tested': False}
