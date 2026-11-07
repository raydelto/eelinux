/* SPDX-License-Identifier: GPL-2.0 */
/* Typed internal descriptors, never user-supplied GS addresses. */
static enum gs_psm ps2gs_buffer_psm(const struct ps2gs_buffer *buffer)
{
	return buffer->bits_per_pixel == 16 ? gs_psm_ct16 : gs_psm_ct32;
}

static size_t ps2gs_package_surface_fill(union package *base,
		const struct ps2gs_buffer *buffer, const struct ps2gs_surface_fill *fill)
{
	union package *p = base + 1;
	struct ps2gs_vertex v = { .x = fill->x * 16, .y = fill->y * 16,
		.rgba = fill->rgba };
	GIF_PACKAGE_AD(p) { .addr = gs_addr_frame_2,
		.data.frame_1 = { .fbp = buffer->base / 8192,
			.fbw = buffer->buffer_width / 64, .psm = ps2gs_buffer_psm(buffer) } };
	GIF_PACKAGE_AD(p) { .addr = gs_addr_xyoffset_2, .data.xyoffset_1 = { } };
	GIF_PACKAGE_AD(p) { .addr = gs_addr_scissor_2,
		.data.scissor_1 = { .scax1 = buffer->width - 1,
			.scay1 = buffer->height - 1 } };
	GIF_PACKAGE_AD(p) { .addr = gs_addr_zbuf_2,
		.data.zbuf = { .psm = buffer->bits_per_pixel == 16 ? 2 : 0xa,
			.zmsk = gs_zbuf_off } };
	GIF_PACKAGE_AD(p) { .addr = gs_addr_test_2,
		.data.test_1 = { .zte = gs_depth_test_on, .ztst = gs_depth_pass } };
	GIF_PACKAGE_AD(p) { .addr = gs_addr_prmodecont, .data.prmodecont = { .ac = 1 } };
	GIF_PACKAGE_AD(p) { .addr = gs_addr_prim,
		.data.prim = { .prim = gs_sprite, .ctxt = gs_context_2 } };
	p = ps2gs_vertex(p, v);
	v.x += fill->width * 16;
	v.y += fill->height * 16;
	p = ps2gs_vertex(p, v);
	GIF_PACKAGE_AD(p) { .addr = gs_addr_finish };
	base->gif.tag = (struct gif_tag) { .flg = gif_packed_mode,
		.reg0 = gif_reg_ad, .nreg = 1, .nloop = p - base - 1, .eop = 1 };
	return p - base;
}

static int ps2gs_validate_surface_copy(const struct ps2gs_buffer *source,
		const struct ps2gs_buffer *destination, const struct ps2gs_surface_copy *c)
{
	if (c->abi != PS2GS_ABI_VERSION || c->reserved ||
	    c->reserved2[0] || c->reserved2[1] ||
	    !ps2gs_buffer_rect(source, c->sx, c->sy, c->width, c->height) ||
	    !ps2gs_buffer_rect(destination, c->dx, c->dy, c->width, c->height))
		return -EINVAL;
	if (source->bits_per_pixel != destination->bits_per_pixel ||
	    c->width % (source->bits_per_pixel == 16 ? 4 : 2))
		return -EOPNOTSUPP;
	return 0;
}

static size_t ps2gs_package_surface_copy(union package *base,
		const struct ps2gs_buffer *source, const struct ps2gs_buffer *destination,
		const struct ps2gs_surface_copy *c)
{
	union package *p = base + 1;
	GIF_PACKAGE_AD(p) { .addr = gs_addr_bitbltbuf,
		.data.bitbltbuf = { .sbp = source->base / 256,
			.sbw = source->buffer_width / 64, .spsm = ps2gs_buffer_psm(source),
			.dbp = destination->base / 256, .dbw = destination->buffer_width / 64,
			.dpsm = ps2gs_buffer_psm(destination) } };
	GIF_PACKAGE_AD(p) { .addr = gs_addr_trxpos,
		.data.trxpos = { .ssax = c->sx, .ssay = c->sy,
			.dsax = c->dx, .dsay = c->dy,
			.dir = source->base != destination->base || c->dy < c->sy ||
				(c->dy == c->sy && c->dx < c->sx) ?
				gs_trxpos_dir_ul_lr : gs_trxpos_dir_lr_ul } };
	GIF_PACKAGE_AD(p) { .addr = gs_addr_trxreg,
		.data.trxreg = { .rrw = c->width, .rrh = c->height } };
	GIF_PACKAGE_AD(p) { .addr = gs_addr_trxdir,
		.data.trxdir = { .xdir = gs_trxdir_local_to_local } };
	GIF_PACKAGE_AD(p) { .addr = gs_addr_finish };
	base->gif.tag = (struct gif_tag) { .flg = gif_packed_mode,
		.reg0 = gif_reg_ad, .nreg = 1, .nloop = p - base - 1, .eop = 1 };
	return p - base;
}
