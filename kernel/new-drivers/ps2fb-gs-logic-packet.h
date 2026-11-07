/* SPDX-License-Identifier: GPL-2.0 */
static u32 ps2gs_logic_result(u32 alu, u32 source, u32 destination)
{
	/* X11's four-bit truth table: bit index is (!source)*2 + !destination. */
	u32 result = 0;
	if (alu & 1) result |= source & destination;
	if (alu & 2) result |= source & ~destination;
	if (alu & 4) result |= ~source & destination;
	if (alu & 8) result |= ~source & ~destination;
	return result;
}

static u32 ps2gs_logic_rgb(u32 native, u32 bpp)
{
	/* FRAME.FBMSK acts before RGB555 conversion. RGB555 significance starts
	 * at bit 3 in each eight-bit channel; bit 31 is the stored alpha bit.
	 * Use exact bit placement for masks, not scaled color expansion. */
	return bpp == 32 ? native :
		((native & 31) << 3) | (((native >> 5) & 31) << 11) |
		(((native >> 10) & 31) << 19);
}

static int ps2gs_validate_solid_logic(const struct ps2gs_buffer *buffer,
		const struct ps2gs_solid_logic *r)
{
	u32 bits = buffer->bits_per_pixel == 16 ? 0x7fff : 0xffffff;
	if (r->abi != PS2GS_ABI_VERSION || r->reserved || r->alu > 15 ||
	    (r->color & ~bits) || (r->plane_mask & ~bits) ||
	    !ps2gs_buffer_rect(buffer, r->x, r->y, r->width, r->height))
		return -EINVAL;
	return 0;
}

static union package *ps2gs_logic_sprite(union package *p,
		const struct ps2gs_buffer *buffer, const struct ps2gs_solid_logic *r,
		u32 write_bits, u32 color, bool invert)
{
	struct ps2gs_vertex v = { .x = r->x * 16, .y = r->y * 16,
		.rgba = 0x80000000 | color };
	GIF_PACKAGE_AD(p) { .addr = gs_addr_frame_2,
		.data.frame_1 = { .fbp = buffer->base / 8192,
			.fbw = buffer->buffer_width / 64, .psm = ps2gs_buffer_psm(buffer),
			.fbmsk = ~ps2gs_logic_rgb(write_bits, buffer->bits_per_pixel) } };
	GIF_PACKAGE_AD(p) { .addr = gs_addr_prim,
		.data.prim = { .prim = gs_sprite, .abe = invert, .ctxt = gs_context_2 } };
	p = ps2gs_vertex(p, v);
	v.x += r->width * 16; v.y += r->height * 16;
	return ps2gs_vertex(p, v);
}

static size_t ps2gs_package_solid_logic(union package *base,
		const struct ps2gs_buffer *buffer, const struct ps2gs_solid_logic *r)
{
	union package *p = base + 1;
	u32 zero = ps2gs_logic_result(r->alu, r->color, 0);
	u32 one = ps2gs_logic_result(r->alu, r->color, ~0U);
	u32 copy = ~(zero ^ one) & r->plane_mask;
	u32 invert = zero & ~one & r->plane_mask;
	GIF_PACKAGE_AD(p) { .addr = gs_addr_xyoffset_2, .data.xyoffset_1 = { } };
	GIF_PACKAGE_AD(p) { .addr = gs_addr_scissor_2,
		.data.scissor_1 = { .scax1 = buffer->width - 1, .scay1 = buffer->height - 1 } };
	GIF_PACKAGE_AD(p) { .addr = gs_addr_zbuf_2,
		.data.zbuf = { .psm = buffer->bits_per_pixel == 16 ? 2 : 0xa, .zmsk = gs_zbuf_off } };
	GIF_PACKAGE_AD(p) { .addr = gs_addr_test_2,
		.data.test_1 = { .zte = gs_depth_test_on, .ztst = gs_depth_pass } };
	GIF_PACKAGE_AD(p) { .addr = gs_addr_prmodecont, .data.prmodecont = { .ac = 1 } };
	/* (white - destination) * 128/128 + zero: exact RGB complement. */
	GIF_PACKAGE_AD(p) { .addr = gs_addr_alpha_2,
		.data.alpha_1 = { .a = gs_alpha_a_cs, .b = gs_alpha_b_cd,
			.c = gs_alpha_c_fix, .d = gs_alpha_d_0, .fix = 128 } };
	GIF_PACKAGE_AD(p) { .addr = gs_addr_dthe, .data.dthe = { .dthe = gs_dthe_off } };
	GIF_PACKAGE_AD(p) { .addr = gs_addr_pabe };
	GIF_PACKAGE_AD(p) { .addr = gs_addr_fba_2 };
	/* The upstream GIF union has no COLCLAMP member. Encode its single bit. */
	p->gif.doubleword[0] = 1; p->gif.doubleword[1] = gs_addr_colclamp; ++p;
	if (copy) p = ps2gs_logic_sprite(p, buffer, r, copy,
		ps2gs_logic_rgb(zero & copy, buffer->bits_per_pixel), false);
	if (invert) p = ps2gs_logic_sprite(p, buffer, r, invert, 0xffffff, true);
	/* Following context-2 operations cannot inherit a restrictive write mask. */
	GIF_PACKAGE_AD(p) { .addr = gs_addr_frame_2,
		.data.frame_1 = { .fbp = buffer->base / 8192,
			.fbw = buffer->buffer_width / 64, .psm = ps2gs_buffer_psm(buffer) } };
	GIF_PACKAGE_AD(p) { .addr = gs_addr_finish };
	base->gif.tag = (struct gif_tag) { .flg = gif_packed_mode,
		.reg0 = gif_reg_ad, .nreg = 1, .nloop = p - base - 1, .eop = 1 };
	return p - base;
}
