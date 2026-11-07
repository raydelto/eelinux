"""Add a demand-paged GS shadow framebuffer to the qualified PS2 kernel."""
from pathlib import Path
import difflib
import subprocess
from kernel_user_vm_port import replace_once

ROOT = Path(__file__).resolve().parents[1]


def adapt_framebuffer(text):
    text = replace_once(text, '#include <linux/mm.h>',
                        '#include <linux/mm.h>\n#include <linux/highmem.h>\n#include <asm/cacheflush.h>')
    text = replace_once(text, 'struct ps2fb_par {\n\tspinlock_t lock;',
        'struct ps2fb_par {\n\tspinlock_t lock;\n'
        '\tstruct page *shadow_pages[GS_MEMORY_SIZE / PAGE_SIZE];\n'
        '\tstruct fb_deferred_io shadow_io;\n'
        '\tatomic_t shadow_maps;\n'
        '\tconst struct vm_operations_struct *shadow_vm_original;\n'
        '\tstruct vm_operations_struct shadow_vm_wrapped;')
    text = replace_once(text, 'static int init_console_buffer(',
                        '#include "ps2fb-shadow.h"\n\nstatic int init_console_buffer(')
    text = replace_once(text, '\t\t.fb_blank\t= ps2fb_blank,',
                        '\t\t.fb_mmap\t= ps2fb_shadow_mmap,\n\t\t.fb_blank\t= ps2fb_blank,')
    text = replace_once(text, '\tinfo->flags |= FBINFO_MISC_TILEBLITTING;',
                        '\tinfo->flags |= FBINFO_MISC_TILEBLITTING | FBINFO_VIRTFB;')
    text = replace_once(text, '\tinfo->fix.smem_len = GS_MEMORY_SIZE;',
        '\tinfo->fix.smem_len = GS_MEMORY_SIZE;\n'
        '\tpar->shadow_io = (struct fb_deferred_io) {\n'
        '\t\t.delay = max_t(unsigned long, 1, HZ / 30),\n'
        '\t\t.get_page = ps2fb_shadow_get_page,\n'
        '\t\t.deferred_io = ps2fb_shadow_deferred,\n\t};\n'
        '\tinfo->fbdefio = &par->shadow_io;')
    # GS allocation checks must use GS capacity, not the CPU-visible map size.
    text = text.replace('var->yres_virtual, info->fix.smem_len)',
                        'var->yres_virtual, GS_MEMORY_SIZE)')
    text = text.replace('var->bits_per_pixel, info->fix.smem_len)',
                        'var->bits_per_pixel, GS_MEMORY_SIZE)')
    text = replace_once(text, '\tvar->xoffset = 0;\n\tvar->yoffset = 0;',
        '\tvar->xres_virtual = ALIGN(var->xres_virtual, 8);\n'
        '\tvar->xoffset = 0;\n\tvar->yoffset = 0;')
    text = replace_once(text, '\tinfo->fix.line_length = var->xres_virtual * var->bits_per_pixel / 8;',
        '\tinfo->fix.line_length = var->xres_virtual * var->bits_per_pixel / 8;\n'
        '\tinfo->fix.smem_len = info->fix.line_length * var->yres_virtual;')
    text = replace_once(text, '\tif (register_framebuffer(info) < 0) {',
        '\t/* Track every possible GS page; allocate RAM only on fault. */\n'
        '\tinfo->fix.smem_len = GS_MEMORY_SIZE;\n'
        '\terr = fb_deferred_io_init(info);\n'
        '\tinfo->fix.smem_len = info->fix.line_length * info->var.yres_virtual;\n\tif (err)\n'
        '\t\tgoto err_register_framebuffer;\n'
        '\tif (register_framebuffer(info) < 0) {\n\t\tps2fb_shadow_cleanup(info);')
    # Tear down DMA and shadow storage before releasing the fb_info/par object.
    start = text.index('static void ps2fb_remove(')
    end = text.index('\nstatic struct platform_driver ps2fb_driver', start)
    text = text[:start] + '''static void ps2fb_remove(struct platform_device *pdev)
{
	struct fb_info *info = platform_get_drvdata(pdev);
	struct ps2fb_par *par = info->par;

	unregister_framebuffer(info);
	if (atomic_read(&par->shadow_maps)) {
		fb_err(info, "Framebuffer mappings remain during removal\\n");
		return;
	}
	ps2fb_shadow_cleanup(info);
	if (!gif_wait()) {
		/* Do not free an in-flight DMA packet. */
		fb_err(info, "GIF DMA still active during removal\\n");
		return;
	}
	enable_irq(IRQ_DMAC_GIF);
	fb_dealloc_cmap(&info->cmap);
	free_page((unsigned long)par->package.buffer);
	framebuffer_release(info);
}
''' + text[end:]
    return text


def install(source: Path, patch: Path):
    name = 'drivers/video/fbdev/ps2fb.c'
    old = (source/name).read_text()
    new = adapt_framebuffer(old)
    kname = 'drivers/video/fbdev/Kconfig'
    kold = (source/kname).read_text()
    knew = replace_once(kold, '\tselect FB_TILEBLITTING\n\tdefault y',
                        '\tselect FB_TILEBLITTING\n\tselect FB_DEFERRED_IO\n\tdefault y')
    hname = 'drivers/video/fbdev/ps2fb-shadow.h'
    header = (ROOT/'kernel/new-drivers/ps2fb-shadow.h').read_text()
    changes = [(name, old, new), (kname, kold, knew), (hname, '', header)]
    blob = ''.join(''.join(difflib.unified_diff(a.splitlines(keepends=True), b.splitlines(keepends=True),
        fromfile='/dev/null' if not a else 'a/'+path, tofile='b/'+path))
        for path,a,b in changes)
    if patch.exists() and patch.read_text() != blob:
        raise RuntimeError('Use a fresh baseline when exporting a changed framebuffer patch')
    patch.parent.mkdir(parents=True, exist_ok=True)
    patch.write_text(blob)
    subprocess.run(['git','apply','--check',str(patch.resolve())],cwd=source,check=True)
    subprocess.run(['git','apply',str(patch.resolve())],cwd=source,check=True)
    return {'demand_paged_GS_framebuffer': True, 'update_hz': 30,
            'hardware_Xorg_tested': False}
