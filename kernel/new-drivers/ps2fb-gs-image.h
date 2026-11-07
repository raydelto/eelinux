/* SPDX-License-Identifier: GPL-2.0 */
/* Bounded GIF host-to-local image transfer. Included by ps2fb-gs.h. */
static int ps2gs_validate_upload(const struct ps2gs_upload *u,
				 const struct ps2gs_caps *caps)
{
	u32 bytes_per_pixel = caps->bits_per_pixel / 8;

	if (u->abi != caps->abi || u->reserved || !u->image_width ||
	    !u->image_height || !u->bytes || u->bytes > PS2GS_MAX_UPLOAD_BYTES)
		return -EINVAL;
	if (u->width != caps->width || u->height != caps->height ||
	    u->bits_per_pixel != caps->bits_per_pixel)
		return -ESTALE;
	if ((caps->bits_per_pixel != 16 && caps->bits_per_pixel != 32) ||
	    u->x >= caps->width || u->y >= caps->height ||
	    u->image_width > caps->width - u->x ||
	    u->image_height > caps->height - u->y)
		return -EINVAL;
	/* Divide before multiplying: no overflow even for malicious dimensions. */
	if (u->image_width > PS2GS_MAX_UPLOAD_BYTES / bytes_per_pixel ||
	    u->image_height > PS2GS_MAX_UPLOAD_BYTES /
		(u->image_width * bytes_per_pixel) ||
	    u->bytes != u->image_width * u->image_height * bytes_per_pixel)
		return -EINVAL;
	if (u->pixels >> 32)
		return -EFAULT;
	return 0;
}

static size_t ps2gs_package_upload(union package *base,
		const struct ps2gs_upload *u, const u8 *pixels,
		u32 fbw, enum gs_psm psm)
{
	union package *p = base;
	u32 qwords = DIV_ROUND_UP(u->bytes, 16);

	GIF_PACKAGE_TAG(p) { .flg = gif_packed_mode,
		.reg0 = gif_reg_ad, .nreg = 1, .nloop = 4 };
	GIF_PACKAGE_AD(p) { .addr = gs_addr_bitbltbuf,
		.data.bitbltbuf = { .dbp = 0, .dbw = fbw, .dpsm = psm } };
	GIF_PACKAGE_AD(p) { .addr = gs_addr_trxpos,
		.data.trxpos = { .dsax = u->x, .dsay = u->y } };
	GIF_PACKAGE_AD(p) { .addr = gs_addr_trxreg,
		.data.trxreg = { .rrw = u->image_width, .rrh = u->image_height } };
	GIF_PACKAGE_AD(p) { .addr = gs_addr_trxdir,
		.data.trxdir = { .xdir = gs_trxdir_host_to_local } };
	GIF_PACKAGE_TAG(p) { .flg = gif_image_mode, .nloop = qwords };
	/* The transfer size is exact; padding is outside TRXREG's pixel count. */
	memset(p, 0, qwords * sizeof(*p));
	memcpy(p, pixels, u->bytes);
	p += qwords;
	GIF_PACKAGE_TAG(p) { .flg = gif_packed_mode, .reg0 = gif_reg_ad,
		.nreg = 1, .nloop = 1, .eop = 1 };
	GIF_PACKAGE_AD(p) { .addr = gs_addr_finish };
	return p - base;
}

static int ps2gs_upload_execute(struct fb_info *info,
		const struct ps2gs_upload *u, const u8 *pixels)
{
	struct ps2fb_par *par = info->par;
	unsigned long flags;
	u32 i;
	size_t count;
	int ret = 0;

	/* Four register writes + three tags + FINISH + padded payload. */
	if (DIV_ROUND_UP(u->bytes, 16) + 8 > par->package.capacity)
		return -ENOSPC;
	spin_lock_irqsave(&par->lock, flags);
	if (!gif_wait()) {
		ret = -ETIMEDOUT;
		goto out;
	}
	gs_write_csr((struct gs_csr) { .finish = 1 });
	count = ps2gs_package_upload(par->package.buffer, u, pixels,
		var_to_fbw(&info->var), var_to_psm(&info->var, info));
	gif_write(&par->package.buffer->gif, count);
	if (!gif_wait()) {
		ret = -ETIMEDOUT;
		goto out;
	}
	for (i = 0; i < 1000000; ++i) {
		if (gs_read_csr().finish)
			break;
		cpu_relax();
	}
	if (i == 1000000)
		ret = -ETIMEDOUT;
out:
	if (ret)
		++par->gs_stats.timeouts;
	spin_unlock_irqrestore(&par->lock, flags);
	return ret;
}

static int ps2gs_upload_locked(struct fb_info *info, unsigned long arg,
			       const struct ps2gs_caps *caps)
{
	struct ps2fb_par *par = info->par;
	struct ps2gs_upload upload;
	u8 *pixels;
	int ret;

	if (!capable(CAP_SYS_RAWIO))
		return -EPERM;
	if (copy_from_user(&upload, (void __user *)arg, sizeof(upload)))
		return -EFAULT;
	ret = ps2gs_validate_upload(&upload, caps);
	if (ret)
		return ret;
	pixels = memdup_user(u64_to_user_ptr(upload.pixels), upload.bytes);
	if (IS_ERR(pixels))
		return PTR_ERR(pixels);
	mutex_lock(&info->mm_lock);
	if (atomic_read(&par->shadow_maps)) {
		ret = -EBUSY;
		goto out;
	}
	flush_delayed_work(&info->deferred_work);
	ret = ps2gs_upload_execute(info, &upload, pixels);
out:
	mutex_unlock(&info->mm_lock);
	kfree(pixels);
	return ret;
}
