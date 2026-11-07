/* SPDX-License-Identifier: GPL-2.0 */
/* CPU-visible, demand-paged GS framebuffer. Included by ps2fb.c after the
 * existing GIF helpers. GS local memory is never mapped into userspace.
 */
#define PS2FB_SHADOW_PAGES (GS_MEMORY_SIZE / PAGE_SIZE)

static struct page *ps2fb_shadow_get_page(struct fb_info *info,
					unsigned long offset)
{
	struct ps2fb_par *par = info->par;
	struct page *page, *new;
	u32 index = offset >> PAGE_SHIFT;

	if (offset >= info->fix.smem_len || index >= PS2FB_SHADOW_PAGES)
		return NULL;
	page = READ_ONCE(par->shadow_pages[index]);
	if (!page) {
		new = alloc_page(GFP_KERNEL | __GFP_ZERO);
		if (!new)
			return NULL;
		page = cmpxchg(&par->shadow_pages[index], NULL, new);
		if (page)
			__free_page(new);
		else
			page = new;
	}
	get_page(page);
	return page;
}

/* Flush one dirty page as bounded host-to-local IMAGE transfers. Each DMA
 * packet fits the driver's existing single page. Never hold interrupts off
 * across a full frame, and never reuse the packet before DMA completion.
 */
static void ps2fb_shadow_upload(struct fb_info *info, struct page *page,
				unsigned long offset)
{
	struct ps2fb_par *par = info->par;
	u32 cpp = info->var.bits_per_pixel / 8;
	u32 stride = info->fix.line_length;
	u32 left = min_t(unsigned long, PAGE_SIZE, info->fix.smem_len - offset);
	u32 consumed = 0;
	u32 limit = round_down((par->package.capacity - 6) * 16 / cpp, 8);
	const u8 *pixels;

	flush_dcache_page(page);
	pixels = kmap_local_page(page);
	while (consumed < left) {
		u32 pos = offset + consumed;
		u32 y = pos / stride, x = (pos % stride) / cpp;
		u32 width = min3((left - consumed) / cpp,
				info->var.xres_virtual - x, limit);
		u32 bytes = width * cpp;
		unsigned long flags;
		union package *base, *p;

		if (!width || y >= info->var.yres_virtual)
			break;
		spin_lock_irqsave(&par->lock, flags);
		if (!gif_wait()) {
			spin_unlock_irqrestore(&par->lock, flags);
			pr_err_ratelimited("ps2fb: GS shadow upload timed out\n");
			break;
		}
		base = par->package.buffer;
		p = base;
		GIF_PACKAGE_TAG(p) { .flg = gif_packed_mode,
			.reg0 = gif_reg_ad, .nreg = 1, .nloop = 4 };
		GIF_PACKAGE_AD(p) { .addr = gs_addr_bitbltbuf,
			.data.bitbltbuf = { .dpsm = var_to_psm(&info->var, info),
				.dbw = var_to_fbw(&info->var) } };
		GIF_PACKAGE_AD(p) { .addr = gs_addr_trxpos,
			.data.trxpos = { .dsax = x, .dsay = y } };
		GIF_PACKAGE_AD(p) { .addr = gs_addr_trxreg,
			.data.trxreg = { .rrw = width, .rrh = 1 } };
		GIF_PACKAGE_AD(p) { .addr = gs_addr_trxdir,
			.data.trxdir = { .xdir = gs_trxdir_host_to_local } };
		GIF_PACKAGE_TAG(p) { .flg = gif_image_mode,
			.nloop = DIV_ROUND_UP(bytes, 16), .eop = 1 };
		memset(p, 0, DIV_ROUND_UP(bytes, 16) * 16);
		memcpy(p, pixels + consumed, bytes);
		p += DIV_ROUND_UP(bytes, 16);
		gif_write(&base->gif, p - base);
		spin_unlock_irqrestore(&par->lock, flags);
		consumed += bytes;
		cond_resched();
	}
	kunmap_local(pixels);
}

static void ps2fb_shadow_deferred(struct fb_info *info, struct list_head *pages)
{
	struct fb_deferred_io_pageref *ref;

	list_for_each_entry(ref, pages, list)
		if (ref->offset < info->fix.smem_len)
			ps2fb_shadow_upload(info, ref->page, ref->offset);
}

static void ps2fb_shadow_vm_open(struct vm_area_struct *vma)
{
	struct fb_info *info = vma->vm_file->private_data;
	struct ps2fb_par *par = info->par;

	atomic_inc(&par->shadow_maps);
	if (par->shadow_vm_original->open)
		par->shadow_vm_original->open(vma);
}

static void ps2fb_shadow_vm_close(struct vm_area_struct *vma)
{
	struct fb_info *info = vma->vm_file->private_data;
	struct ps2fb_par *par = info->par;

	if (par->shadow_vm_original->close)
		par->shadow_vm_original->close(vma);
	atomic_dec(&par->shadow_maps);
}

static int ps2fb_shadow_mmap(struct fb_info *info, struct vm_area_struct *vma)
{
	struct ps2fb_par *par = info->par;
	unsigned long length = vma->vm_end - vma->vm_start;
	unsigned long size = PAGE_ALIGN(info->fix.smem_len);
	unsigned long offset;
	int ret;

	if (vma->vm_pgoff > (size >> PAGE_SHIFT))
		return -EINVAL;
	offset = vma->vm_pgoff << PAGE_SHIFT;
	if (length > size - offset || !(vma->vm_flags & VM_SHARED))
		return -EINVAL;
	ret = fb_deferred_io_mmap(info, vma);
	if (ret)
		return ret;
	/* Preserve deferred-IO private data and every original fault callback.
	 * Count VMAs before their first fault, including forked mappings.
	 */
	if (!par->shadow_vm_original) {
		par->shadow_vm_original = vma->vm_ops;
		par->shadow_vm_wrapped = *vma->vm_ops;
		par->shadow_vm_wrapped.open = ps2fb_shadow_vm_open;
		par->shadow_vm_wrapped.close = ps2fb_shadow_vm_close;
	}
	vma->vm_ops = &par->shadow_vm_wrapped;
	atomic_inc(&par->shadow_maps);
	return 0;
}

static void ps2fb_shadow_cleanup(struct fb_info *info)
{
	struct ps2fb_par *par = info->par;
	u32 i;

	fb_deferred_io_cleanup(info);
	for (i = 0; i < PS2FB_SHADOW_PAGES; ++i)
		if (par->shadow_pages[i])
			put_page(par->shadow_pages[i]);
}
