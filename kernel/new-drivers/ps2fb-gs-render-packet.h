/* SPDX-License-Identifier: GPL-2.0 */
static int ps2gs_validate_render(const struct ps2gs_buffer *color,
		const struct ps2gs_buffer *depth, u32 depth_color_bpp,
		const struct ps2gs_command *command)
{
	struct ps2gs_caps caps = { .width = color->width, .height = color->height,
		.bits_per_pixel = color->bits_per_pixel };
	if (depth) {
		if (depth_color_bpp != color->bits_per_pixel ||
		    depth->buffer_width != color->buffer_width ||
		    depth->width != color->width || depth->height != color->height)
			return -EINVAL;
		caps.features = PS2GS_HW_DEPTH16;
	}
	if (command->op == PS2GS_COPY)
		return -EOPNOTSUPP;
	return ps2gs_validate(command, &caps);
}

static size_t ps2gs_package_render(union package *base,
		const struct ps2gs_buffer *color, const struct ps2gs_buffer *depth,
		const struct ps2gs_command *command)
{
	union package *p = base + 1;
	u32 i;
	bool clear = command->op == PS2GS_CLEAR_DEPTH;
	bool zwrite = clear || (command->flags & PS2GS_DEPTH_TEST);
	GIF_PACKAGE_AD(p) { .addr = gs_addr_frame_2,
		.data.frame_1 = { .fbp = color->base / 8192,
			.fbw = color->buffer_width / 64, .psm = ps2gs_buffer_psm(color),
			.fbmsk = clear ? 0xffffffff : 0 } };
	GIF_PACKAGE_AD(p) { .addr = gs_addr_xyoffset_2, .data.xyoffset_1 = { } };
	GIF_PACKAGE_AD(p) { .addr = gs_addr_scissor_2,
		.data.scissor_1 = { .scax1 = color->width - 1, .scay1 = color->height - 1 } };
	GIF_PACKAGE_AD(p) { .addr = gs_addr_zbuf_2,
		.data.zbuf = { .zbp = depth ? depth->base / 8192 : 0,
			.psm = color->bits_per_pixel == 16 ? 2 : 0xa,
			.zmsk = zwrite ? gs_zbuf_on : gs_zbuf_off } };
	GIF_PACKAGE_AD(p) { .addr = gs_addr_test_2,
		.data.test_1 = { .zte = gs_depth_test_on,
			.ztst = !clear && zwrite ? gs_depth_gequal : gs_depth_pass } };
	GIF_PACKAGE_AD(p) { .addr = gs_addr_prmodecont, .data.prmodecont = { .ac = 1 } };
	GIF_PACKAGE_AD(p) { .addr = gs_addr_prim,
		.data.prim = { .prim = command->op == PS2GS_TRIANGLE ? gs_triangle : gs_sprite,
			.ctxt = gs_context_2, .iip = command->op == PS2GS_TRIANGLE ?
				gs_gouraud_shading : gs_flat_shading } };
	if (command->op == PS2GS_TRIANGLE) {
		for (i = 0; i < 3; ++i)
			p = ps2gs_vertex(p, command->v[i]);
	} else {
		struct ps2gs_vertex v = command->v[0];
		p = ps2gs_vertex(p, v);
		v.x += (clear ? color->width : command->width) * 16;
		v.y += (clear ? color->height : command->height) * 16;
		p = ps2gs_vertex(p, v);
	}
	GIF_PACKAGE_AD(p) { .addr = gs_addr_finish };
	base->gif.tag = (struct gif_tag) { .flg = gif_packed_mode,
		.reg0 = gif_reg_ad, .nreg = 1, .nloop = p - base - 1, .eop = 1 };
	return p - base;
}
