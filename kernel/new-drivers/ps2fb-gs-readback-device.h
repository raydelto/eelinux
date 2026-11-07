/* SPDX-License-Identifier: GPL-2.0 */
#define PS2GS_VIF1_STAT 0x10003c00U
#define PS2GS_VIF1_CHCR 0x10009000U
#define PS2GS_VIF1_FDR (1U << 23)
#define PS2GS_VIF1_FQC (31U << 24)
/* Programmed reads avoid DMA ownership and cache hazards on timeout. Each
 * FIFO operation is a native aligned 128-bit R5900 access. */
static void ps2gs_read_fifo(void *destination)
{
	void *fifo = (void *)CKSEG1ADDR(0x10005000U);
	__asm__ __volatile__(".set push\n.set arch=r5900\nlq $8, 0(%1)\nsq $8, 0(%0)\n.set pop\n"
		: : "r"(destination), "r"(fifo) : "$8", "memory");
}
static int ps2gs_download_execute(struct fb_info *info,
		const struct ps2gs_buffer *buffer, const struct ps2gs_surface_download *r, u8 *pixels)
{
	struct ps2fb_par *par = info->par;
	unsigned long flags;
	u32 budget = 1000000, offset;
	int ret = 0;
	spin_lock_irqsave(&par->lock, flags);
	if ((inl(PS2GS_VIF1_CHCR) & (1U << 8)) ||
	    (inl(PS2GS_VIF1_STAT) & (PS2GS_VIF1_FDR | PS2GS_VIF1_FQC))) {
		ret = -EBUSY;
		goto out;
	}
	if (!gif_wait()) {
		ret = -ETIMEDOUT;
		goto out;
	}
	gs_write_csr((struct gs_csr) { .finish = 1 });
	gif_write(&par->package.buffer->gif, ps2gs_package_download(par->package.buffer, buffer, r));
	if (!gif_wait()) {
		ret = -ETIMEDOUT;
		goto out;
	}
	while (!gs_read_csr().finish && --budget)
		cpu_relax();
	if (!budget) {
		ret = -ETIMEDOUT;
		goto out;
	}
	outl(PS2GS_VIF1_FDR, PS2GS_VIF1_STAT);
	gs_write_busdir((struct gs_busdir) { .dir = gs_dir_local_to_host });
	for (offset = 0; offset < r->bytes; offset += 16) {
		while (!(inl(PS2GS_VIF1_STAT) & PS2GS_VIF1_FQC) && --budget)
			cpu_relax();
		if (!budget) {
			ret = -ETIMEDOUT;
			break;
		}
		ps2gs_read_fifo(pixels + offset);
	}
	/* Restore send direction on every path after switching, before releasing
	 * the console/GIF locks. Failed sessions cannot submit another packet. */
	gs_write_busdir((struct gs_busdir) { .dir = gs_dir_host_to_local });
	outl(0, PS2GS_VIF1_STAT);
out:
	if (ret == -ETIMEDOUT)
		++par->gs_stats.timeouts;
	spin_unlock_irqrestore(&par->lock, flags);
	return ret;
}
static int ps2gs_download_locked(struct ps2gs_session *session,
		struct fb_info *info, void __user *arg)
{
	struct ps2gs_surface_download request;
	struct ps2gs_buffer front, *buffer;
	u8 *pixels;
	int ret;
	if (copy_from_user(&request, arg, sizeof(request)))
		return -EFAULT;
	buffer = ps2gs_session_buffer(session, info, request.handle, &front);
	if (!buffer || ps2gs_depth_role(session, request.handle))
		return -ENOENT;
	ret = ps2gs_validate_download(buffer, &request);
	if (ret)
		return ret;
	if (request.pixels >> 32 || !request.pixels ||
	    request.pixels > U32_MAX - (request.bytes - 1))
		return -EFAULT;
	pixels = kzalloc(request.bytes, GFP_KERNEL);
	if (!pixels)
		return -ENOMEM;
	ret = ps2gs_download_execute(info, buffer, &request, pixels);
	if (ret == -ETIMEDOUT)
		session->failed = ret;
	if (!ret && copy_to_user(u64_to_user_ptr(request.pixels), pixels, request.bytes))
		ret = -EFAULT;
	kfree(pixels);
	return ret;
}
