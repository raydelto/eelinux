"""Validate prospective GS display allocation and retain palette/font space."""
from pathlib import Path
import difflib
import subprocess
from kernel_user_vm_port import replace_once


def adapt_framebuffer(text):
    text=replace_once(text,'\treturn (xres_virtual * yres_virtual * bits_per_pixel) / 8;', '''	const u32 fbw = DIV_ROUND_UP(xres_virtual, GS_FB_PAGE_WIDTH);

	/* Include the actual GS block/stride padding, not just visible pixels. */
	if (bits_per_pixel == 16)
		return gs_psm_ct16_block_count(fbw, yres_virtual) * GS_BLOCK_SIZE;
	if (bits_per_pixel == 32)
		return gs_psm_ct32_block_count(fbw, yres_virtual) * GS_BLOCK_SIZE;
	return GS_MEMORY_SIZE + 1;''')
    text=replace_once(text,'\tvar->xoffset = 0;\n\tvar->yoffset = 0;', '''	/* Bound virtual geometry before GS allocation arithmetic. */
	if (var->xres_virtual > 2048 || var->yres_virtual > 2048)
		return -EINVAL;
	var->xoffset = 0;
	var->yoffset = 0;''')
    before='''	err = ps2fb_check_var(var, info);
	spin_unlock_irqrestore(&par->lock, flags);

	if (!err && info->tileops)
		if (info->tileops->fb_get_tilemax(info) < 256)
			err = -ENOMEM;'''
    text = replace_once(text,before,'''	err = ps2fb_check_var(var, info);
	if (!err && info->tileops) {
		const u32 blocks = display_buffer_size(var->xres_virtual,
			var->yres_virtual, var->bits_per_pixel) / GS_BLOCK_SIZE;
		const u32 tiles = par->cb.tile.block.cols * par->cb.tile.block.rows;
		/* Initial font geometry is 8x8; later use the installed tile size.
		 * Check the proposed allocation rather than the old active mode.
		 */
		const u32 font_blocks = DIV_ROUND_UP(256, tiles ? tiles : 8);

		if (blocks + PALETTE_BLOCK_COUNT + font_blocks > GS_BLOCK_COUNT)
			err = -ENOMEM;
	}
	spin_unlock_irqrestore(&par->lock, flags);''')
    return replace_once(text, '\tsp.smode1.gcont = gs_gcont_ycrcb;',
                        '\tsp.smode1.gcont = bc ? gs_gcont_ycrcb : gs_gcont_rgbyc;')


def install(source: Path, patch: Path):
    adapters=(('drivers/video/fbdev/ps2fb.c',adapt_framebuffer),
              ('init/main.c',lambda text:replace_once(text,
                'ps2: exception and TLB-entry kernel',
                'ps2: exception guards and video modes kernel')))
    changes=[(name,(source/name).read_text(),fn((source/name).read_text())) for name,fn in adapters]
    blob=''.join(''.join(difflib.unified_diff(a.splitlines(keepends=True),b.splitlines(keepends=True),
        fromfile='a/'+name,tofile='b/'+name)) for name,a,b in changes).encode()
    if patch.exists() and patch.read_bytes()!=blob:
        raise RuntimeError('Preserve reviewed GS allocation patch')
    patch.parent.mkdir(parents=True,exist_ok=True);patch.write_bytes(blob)
    subprocess.run(['git','apply','--check',str(patch.resolve())],cwd=source,check=True)
    subprocess.run(['git','apply',str(patch.resolve())],cwd=source,check=True)
    return {'padded_GS_allocation_checked':True,'prospective_font_and_palette_checked':True,
            'virtual_geometry_bounded':True,'VESA_RGB_output':True,'physical_modes_tested':False}
