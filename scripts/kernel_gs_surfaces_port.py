"""Export the owned GS surface extension on top of ps2-gs-commands.patch."""
from pathlib import Path
import difflib
import subprocess
from kernel_user_vm_port import replace_once

ROOT = Path(__file__).resolve().parents[1]
FILES = {
    'drivers/video/fbdev/ps2fb-gs-device.h': 'ps2fb-gs-device.h',
    'drivers/video/fbdev/ps2fb-gs-arena.h': 'ps2fb-gs-arena.h',
    'drivers/video/fbdev/ps2fb-gs-surface-packet.h': 'ps2fb-gs-surface-packet.h',
    'include/uapi/linux/ps2-gs-surface.h': 'ps2-gs-surface.h',
}

def adapt_framebuffer(text):
    text = replace_once(text, '#include <linux/ps2-gs.h>',
        '#include <linux/ps2-gs.h>\n#include <linux/ps2-gs-surface.h>\n'
        '#include <linux/miscdevice.h>\n#include <linux/kref.h>\n'
        '#include <linux/kd.h>\n#include <linux/vt_kern.h>\n#include <linux/console_struct.h>')
    text = replace_once(text, '\tstruct ps2gs_caps gs_stats;',
        '\tstruct ps2gs_caps gs_stats;\n\tatomic_t gs_owned;\n'
        '\tu64 gs_epoch;\n\tstruct ps2gs_device *gs_device;')
    text = replace_once(text, '\tpar->cb.tile = cb_tile(map->width, map->height);',
        '\t++par->gs_epoch;\n\tpar->cb.tile = cb_tile(map->width, map->height);')
    text = replace_once(text, '\t\tpar->cb.block_count = var_to_block_count(info);',
        '\t\t++par->gs_epoch;\n\t\tpar->cb.block_count = var_to_block_count(info);')
    text = replace_once(text, '\tif (changed_cb_pan_display(var))\n\t\twrite_cb_pan_display(var, info);',
        '\tif (changed_cb_pan_display(var)) {\n\t\t++par->gs_epoch;\n'
        '\t\twrite_cb_pan_display(var, info);\n\t}')
    text = replace_once(text, '#include "ps2fb-gs.h"',
        '#include "ps2fb-gs.h"\n#include "ps2fb-gs-device.h"')
    text = replace_once(text, '\tplatform_set_drvdata(pdev, info);',
        '\tplatform_set_drvdata(pdev, info);\n'
        '\terr = ps2gs_device_register(info);\n'
        '\tif (err)\n\t\tfb_warn(info, "GS surface device unavailable: %d\\n", err);')
    text = replace_once(text, '\tunregister_framebuffer(info);',
        '\tps2gs_device_unregister(info);\n\tunregister_framebuffer(info);')
    return text

def adapt_commands(text):
    return replace_once(text, '\tif (cmd == PS2GS_UPLOAD)\n',
        '\tif (atomic_read(&par->gs_owned))\n\t\treturn -EBUSY;\n'
        '\tif (cmd == PS2GS_UPLOAD)\n')

def adapt_image(text):
    text = replace_once(text, 'static size_t ps2gs_package_upload(union package *base,',
        'static size_t ps2gs_package_upload_to(union package *base,')
    text = replace_once(text, '\t\tu32 fbw, enum gs_psm psm)',
        '\t\tu32 dbp, u32 fbw, enum gs_psm psm)')
    text = replace_once(text, '.data.bitbltbuf = { .dbp = 0,',
        '.data.bitbltbuf = { .dbp = dbp,')
    text = replace_once(text, 'static int ps2gs_upload_execute(',
        'static size_t ps2gs_package_upload(union package *base,\n'
        '\t\tconst struct ps2gs_upload *u, const u8 *pixels,\n'
        '\t\tu32 fbw, enum gs_psm psm)\n{\n'
        '\treturn ps2gs_package_upload_to(base, u, pixels, 0, fbw, psm);\n}\n\n'
        'static int ps2gs_upload_execute(')
    return text

def adapt_shadow(text):
    # fbdev's fb_mmap already owns mm_lock around this callback.
    marker = '\tret = fb_deferred_io_mmap(info, vma);'
    return replace_once(text, marker,
        '\tif (atomic_read(&par->gs_owned))\n\t\treturn -EBUSY;\n'+marker)

def install(source, patch):
    changes=[]
    for name, adapt in [('ps2fb.c',adapt_framebuffer),('ps2fb-gs.h',adapt_commands),
                        ('ps2fb-gs-image.h',adapt_image),('ps2fb-shadow.h',adapt_shadow)]:
        target='drivers/video/fbdev/'+name
        old=(source/target).read_text()
        changes.append((target,old,adapt(old)))
    for target,local in FILES.items():
        changes.append((target,'',(ROOT/'kernel/new-drivers'/local).read_text()))
    blob=''.join(''.join(difflib.unified_diff(old.splitlines(keepends=True),
        new.splitlines(keepends=True),fromfile='/dev/null' if not old else 'a/'+name,
        tofile='b/'+name)) for name,old,new in changes)
    if patch.exists() and patch.read_text()!=blob:
        raise RuntimeError('Re-export from the unchanged upload baseline')
    patch.parent.mkdir(parents=True,exist_ok=True); patch.write_text(blob)
    subprocess.run(['git','apply','--check',str(patch.resolve())],cwd=source,check=True)
    subprocess.run(['git','apply',str(patch.resolve())],cwd=source,check=True)
    return {'owned_GS_surfaces':True,'software_rasterizer':False,
            'Xorg_DDX_complete':False,'Mesa_OpenGL_complete':False}
