/* SPDX-License-Identifier: GPL-2.0 */
static int ps2gs_validate_download(const struct ps2gs_buffer *buffer,
		const struct ps2gs_surface_download *r)
{
	u32 cpp = buffer->bits_per_pixel / 8;
	if (r->abi != PS2GS_ABI_VERSION || r->reserved ||
	    !ps2gs_buffer_rect(buffer, r->x, r->y, r->width, r->height) ||
	    r->width % (buffer->bits_per_pixel == 16 ? 4 : 2) ||
	    !r->bytes || r->bytes > PS2GS_MAX_READBACK_BYTES || r->bytes % 128 ||
	    r->width * r->height * cpp != r->bytes)
		return -EINVAL;
	return 0;
}
static size_t ps2gs_package_download(union package *base,
		const struct ps2gs_buffer *buffer, const struct ps2gs_surface_download *r)
{
	union package *p = base + 1;
	GIF_PACKAGE_AD(p) { .addr = gs_addr_bitbltbuf,
		.data.bitbltbuf = { .sbp = buffer->base / 256,
			.sbw = buffer->buffer_width / 64, .spsm = ps2gs_buffer_psm(buffer) } };
	GIF_PACKAGE_AD(p) { .addr = gs_addr_trxpos, .data.trxpos = { .ssax = r->x, .ssay = r->y } };
	GIF_PACKAGE_AD(p) { .addr = gs_addr_trxreg, .data.trxreg = { .rrw = r->width, .rrh = r->height } };
	/* FINISH precedes TRXDIR: the receive path must not wait for a FINISH
	 * command queued behind the local-to-host stream it is about to drain. */
	GIF_PACKAGE_AD(p) { .addr = gs_addr_finish };
	GIF_PACKAGE_AD(p) { .addr = gs_addr_trxdir, .data.trxdir = { .xdir = gs_trxdir_local_to_host } };
	base->gif.tag = (struct gif_tag) { .flg = gif_packed_mode,
		.reg0 = gif_reg_ad, .nreg = 1, .nloop = p - base - 1, .eop = 1 };
	return p - base;
}
