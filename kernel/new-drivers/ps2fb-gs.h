/* SPDX-License-Identifier: GPL-2.0 */
/* Included by ps2fb.c after its console and GIF helpers.
 * Context 2 isolates rendering state from the tiled console's context 1.
 * This is a primitive command engine, not a Mesa/GLX implementation.
 */
static u32 ps2gs_depth_base(struct fb_info *info, u32 *bytes)
{
	struct ps2fb_par *par = info->par;
	u32 i, font_end = 0, base;

	*bytes = ALIGN(info->var.xres_virtual, 64) *
		ALIGN(info->var.yres_virtual, 64) * 2;
	if (*bytes > GS_MEMORY_SIZE)
		return 0;
	base = GS_MEMORY_SIZE - *bytes;
	/* Font blocks are GS-swizzled; counting linear bytes is insufficient. */
	for (i = 0; i <= par->gs_font_blocks; ++i) {
		u32 block = block_address_for_index(par->cb.block_count + i, info);
		font_end = max(font_end, (block + 1) * 256);
	}
	if (ALIGN(font_end, 8192) > base)
		return 0;
	return base;
}

static void ps2gs_caps(struct fb_info *info, struct ps2gs_caps *caps)
{
	struct ps2fb_par *par = info->par;
	unsigned long flags;

	memset(caps, 0, sizeof(*caps));
	caps->abi = PS2GS_ABI_VERSION;
	caps->features = PS2GS_HW_FILL | PS2GS_HW_COPY | PS2GS_HW_TRIANGLE |
		PS2GS_HW_UPLOAD;
	caps->width = info->var.xres;
	caps->height = info->var.yres;
	caps->bits_per_pixel = info->var.bits_per_pixel;
	caps->max_commands = PS2GS_MAX_COMMANDS;
	caps->depth_base = ps2gs_depth_base(info, &caps->depth_bytes);
	if (caps->depth_base)
		caps->features |= PS2GS_HW_DEPTH16;
	else
		caps->depth_bytes = 0;
	spin_lock_irqsave(&par->lock, flags);
	caps->completed_batches = par->gs_stats.completed_batches;
	caps->fills = par->gs_stats.fills;
	caps->copies = par->gs_stats.copies;
	caps->triangles = par->gs_stats.triangles;
	caps->depth_clears = par->gs_stats.depth_clears;
	caps->timeouts = par->gs_stats.timeouts;
	spin_unlock_irqrestore(&par->lock, flags);
}

static bool ps2gs_zero_vertex(const struct ps2gs_vertex *v)
{
	return !(v->x | v->y | v->z | v->rgba);
}

static bool ps2gs_rect(const struct ps2gs_vertex *v, u32 w, u32 h,
			 const struct ps2gs_caps *caps)
{
	return !(v->x & 15) && !(v->y & 15) && w && h &&
		(v->x >> 4) < caps->width && (v->y >> 4) < caps->height &&
		w <= caps->width - (v->x >> 4) &&
		h <= caps->height - (v->y >> 4);
}

static int ps2gs_validate(const struct ps2gs_command *c,
			 const struct ps2gs_caps *caps)
{
	u32 i, align = caps->bits_per_pixel == 16 ? 4 : 2;

	if (c->flags & ~PS2GS_DEPTH_TEST)
		return -EOPNOTSUPP;
	if ((c->flags || c->op == PS2GS_CLEAR_DEPTH) &&
	    !(caps->features & PS2GS_HW_DEPTH16))
		return -ENOSPC;
	switch (c->op) {
	case PS2GS_FILL:
		if (c->flags || c->v[0].z || !ps2gs_zero_vertex(&c->v[1]) ||
		    !ps2gs_zero_vertex(&c->v[2]))
			return -EINVAL;
		return ps2gs_rect(&c->v[0], c->width, c->height, caps) ? 0 : -EINVAL;
	case PS2GS_COPY:
		if (c->flags || c->v[0].z || c->v[0].rgba ||
		    c->v[1].z || c->v[1].rgba || !ps2gs_zero_vertex(&c->v[2]) ||
		    !ps2gs_rect(&c->v[0], c->width, c->height, caps) ||
		    !ps2gs_rect(&c->v[1], c->width, c->height, caps))
			return -EINVAL;
		/* GS BITBLTBUF format constraints: reject, never CPU-copy. */
		if (c->width % align)
			return -EOPNOTSUPP;
		return 0;
	case PS2GS_TRIANGLE:
		if (c->width || c->height)
			return -EINVAL;
		for (i = 0; i < 3; ++i)
			if (c->v[i].x > caps->width * 16 ||
			    c->v[i].y > caps->height * 16 || c->v[i].z > 65535)
				return -EINVAL;
		return 0;
	case PS2GS_CLEAR_DEPTH:
		return !(c->flags | c->width | c->height) &&
			ps2gs_zero_vertex(&c->v[0]) && ps2gs_zero_vertex(&c->v[1]) &&
			ps2gs_zero_vertex(&c->v[2]) ? 0 : -EINVAL;
	default:
		return -EOPNOTSUPP;
	}
}

static union package *ps2gs_environment(union package *p, struct fb_info *info,
				      const struct ps2gs_command *c, u32 zbase)
{
	const bool depth = c->flags & PS2GS_DEPTH_TEST;
	const bool clear = c->op == PS2GS_CLEAR_DEPTH;

	GIF_PACKAGE_AD(p) { .addr = gs_addr_frame_2,
		.data.frame_1 = { .fbw = var_to_fbw(&info->var),
			.psm = var_to_psm(&info->var, info),
			.fbmsk = clear ? 0xffffffff : 0 } };
	GIF_PACKAGE_AD(p) { .addr = gs_addr_xyoffset_2, .data.xyoffset_1 = { } };
	GIF_PACKAGE_AD(p) { .addr = gs_addr_scissor_2,
		.data.scissor_1 = { .scax1 = info->var.xres - 1,
			.scay1 = info->var.yres - 1 } };
	GIF_PACKAGE_AD(p) { .addr = gs_addr_zbuf_2,
		/* CT16 pairs with Z16; CT32 requires the alternate Z16S layout.
		 * Mixing CT32/Z16 corrupts bands on the physical GS even though
		 * DMA and FINISH complete successfully in an emulator.
		 */
		.data.zbuf = { .zbp = zbase / 8192,
			.psm = info->var.bits_per_pixel == 16 ? 2 : 0xa,
			.zmsk = depth || clear ? gs_zbuf_on : gs_zbuf_off } };
	GIF_PACKAGE_AD(p) { .addr = gs_addr_test_2,
		.data.test_1 = { .zte = gs_depth_test_on,
			.ztst = depth ? gs_depth_greater : gs_depth_pass } };
	/* PRIM context/shading bits are ignored when PRMODECONT.AC is zero.
	 * Establish the attribute source explicitly instead of inheriting console
	 * state. Local-to-local copies do not use the primitive rasterizer.
	 */
	GIF_PACKAGE_AD(p) { .addr = gs_addr_prmodecont,
		.data.prmodecont = { .ac = 1 } };
	return p;
}

static union package *ps2gs_vertex(union package *p, struct ps2gs_vertex v)
{
	GIF_PACKAGE_AD(p) { .addr = gs_addr_rgbaq,
		.data.rgbaq = { .r = v.rgba & 255, .g = (v.rgba >> 8) & 255,
			.b = (v.rgba >> 16) & 255, .a = v.rgba >> 24,
			.q = 0x3f800000 } };
	GIF_PACKAGE_AD(p) { .addr = gs_addr_xyz2,
		.data.xyz2 = { .x = v.x, .y = v.y, .z = v.z } };
	return p;
}

static int ps2gs_execute(struct fb_info *info, const struct ps2gs_command *c,
			u32 zbase)
{
	struct ps2fb_par *par = info->par;
	union package *base, *p;
	unsigned long flags;
	u32 i;
	int ret = 0;

	spin_lock_irqsave(&par->lock, flags);
	if (!gif_wait()) {
		ret = -ETIMEDOUT;
		goto out;
	}
	gs_write_csr((struct gs_csr) { .finish = 1 });
	base = par->package.buffer;
	p = base + 1;
	if (c->op == PS2GS_COPY) {
		struct fb_copyarea area = { .sx = c->v[0].x >> 4,
			.sy = c->v[0].y >> 4, .dx = c->v[1].x >> 4,
			.dy = c->v[1].y >> 4, .width = c->width, .height = c->height };
		/* Skip the helper's tag; this packet has one enclosing tag. */
		p += package_copyarea(base, &area, info) - 1;
	} else {
		p = ps2gs_environment(p, info, c, zbase);
		GIF_PACKAGE_AD(p) { .addr = gs_addr_prim,
			.data.prim = { .prim = c->op == PS2GS_TRIANGLE ?
				gs_triangle : gs_sprite, .ctxt = gs_context_2,
				.iip = c->op == PS2GS_TRIANGLE ?
				gs_gouraud_shading : gs_flat_shading } };
		if (c->op == PS2GS_TRIANGLE) {
			for (i = 0; i < 3; ++i)
				p = ps2gs_vertex(p, c->v[i]);
		} else {
			struct ps2gs_vertex v = c->v[0];
			p = ps2gs_vertex(p, v);
			v.x += (c->op == PS2GS_CLEAR_DEPTH ?
				info->var.xres : c->width) * 16;
			v.y += (c->op == PS2GS_CLEAR_DEPTH ?
				info->var.yres : c->height) * 16;
			p = ps2gs_vertex(p, v);
		}
	}
	GIF_PACKAGE_AD(p) { .addr = gs_addr_finish };
	base->gif.tag = (struct gif_tag) { .flg = gif_packed_mode,
		.reg0 = gif_reg_ad, .nreg = 1, .nloop = p - base - 1, .eop = 1 };
	gif_write(&base->gif, p - base);
	/* DMA completion alone is not a GS drawing fence. */
	if (!gif_wait()) {
		ret = -ETIMEDOUT;
		goto out;
	}
	for (i = 0; i < 1000000; ++i) {
		if (gs_read_csr().finish)
			break;
		cpu_relax();
	}
	if (i == 1000000) {
		ret = -ETIMEDOUT;
		goto out;
	}
	switch (c->op) {
	case PS2GS_FILL: ++par->gs_stats.fills; break;
	case PS2GS_COPY: ++par->gs_stats.copies; break;
	case PS2GS_TRIANGLE: ++par->gs_stats.triangles; break;
	case PS2GS_CLEAR_DEPTH: ++par->gs_stats.depth_clears; break;
	}
out:
	if (ret)
		++par->gs_stats.timeouts;
	spin_unlock_irqrestore(&par->lock, flags);
	return ret;
}

#include "ps2fb-gs-image.h"

static int ps2gs_ioctl_locked(struct fb_info *info, unsigned int cmd, unsigned long arg)
{
	struct ps2fb_par *par = info->par;
	struct ps2gs_caps caps;
	struct ps2gs_submit request;
	struct ps2gs_command *commands;
	u32 i;
	int ret = 0;
	unsigned long flags;

	if (cmd != PS2GS_GET_CAPS && cmd != PS2GS_SUBMIT && cmd != PS2GS_UPLOAD)
		return -ENOTTY;
	ps2gs_caps(info, &caps);
	if (cmd == PS2GS_GET_CAPS)
		return copy_to_user((void __user *)arg, &caps, sizeof(caps)) ? -EFAULT : 0;
	if (cmd == PS2GS_UPLOAD)
		return ps2gs_upload_locked(info, arg, &caps);
	if (!capable(CAP_SYS_RAWIO))
		return -EPERM;
	if (copy_from_user(&request, (void __user *)arg, sizeof(request)))
		return -EFAULT;
	if (request.abi != caps.abi || request.reserved || !request.count ||
	    request.count > PS2GS_MAX_COMMANDS)
		return -EINVAL;
	if (request.width != caps.width || request.height != caps.height ||
	    request.bits_per_pixel != caps.bits_per_pixel)
		return -ESTALE;
	if (request.commands >> 32)
		return -EFAULT;
	commands = memdup_user(u64_to_user_ptr(request.commands),
		request.count * sizeof(*commands));
	if (IS_ERR(commands))
		return PTR_ERR(commands);
	/* mmap uses mm_lock, not info->lock. Count unfaulted VMAs too. */
	mutex_lock(&info->mm_lock);
	if (atomic_read(&par->shadow_maps)) {
		ret = -EBUSY;
		goto out;
	}
	flush_delayed_work(&info->deferred_work);
	/* Validate the entire batch before the first hardware side effect. */
	for (i = 0; i < request.count; ++i) {
		ret = ps2gs_validate(&commands[i], &caps);
		if (ret)
			goto out;
	}
	for (i = 0; i < request.count; ++i) {
		ret = ps2gs_execute(info, &commands[i], caps.depth_base);
		if (ret)
			goto out;
		cond_resched();
	}
	spin_lock_irqsave(&par->lock, flags);
	++par->gs_stats.completed_batches;
	spin_unlock_irqrestore(&par->lock, flags);
out:
	mutex_unlock(&info->mm_lock);
	kfree(commands);
	return ret;
}

static int ps2gs_ioctl(struct fb_info *info, unsigned int cmd, unsigned long arg)
{
	int ret;

	if (cmd != PS2GS_GET_CAPS && cmd != PS2GS_SUBMIT && cmd != PS2GS_UPLOAD)
		return -ENOTTY;
	/* fb ioctl already owns info->lock. A blocking console_lock here would
	 * invert the mode-setting lock order; trylock makes contention retryable.
	 * It also keeps font uploads and console pan changes out of a batch.
	 */
	if (!console_trylock())
		return -EAGAIN;
	ret = ps2gs_ioctl_locked(info, cmd, arg);
	console_unlock();
	return ret;
}
