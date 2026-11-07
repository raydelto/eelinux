/* SPDX-License-Identifier: GPL-2.0 */
static int ps2gs_map_blit_locked(struct ps2gs_session *session,
		struct fb_info *info, void __user *arg)
{
	struct ps2gs_channel_map_blit r;
	struct ps2gs_buffer *source, *destination, *palette, page;
	u32 first, total;
	int ret;
	if (copy_from_user(&r, arg, sizeof(r)))
		return -EFAULT;
	if (r.abi != PS2GS_ABI_VERSION || r.source_channel > 3 || !r.width || !r.height ||
	    r.width > 64 || r.height > 32 || !r.source || !r.destination || !r.palette ||
	    r.source == r.destination || r.source == r.palette || r.destination == r.palette)
		return -EINVAL;
	source = ps2gs_arena_lookup(&session->arena, r.source);
	destination = ps2gs_arena_lookup(&session->arena, r.destination);
	palette = ps2gs_arena_lookup(&session->arena, r.palette);
	if (!source || !destination || !palette)
		return -ENOENT;
	if (ps2gs_depth_role(session, r.source) || ps2gs_depth_role(session, r.destination) ||
	    ps2gs_depth_role(session, r.palette) || source->bits_per_pixel != 32 ||
	    destination->bits_per_pixel != 32 || palette->bits_per_pixel != 32 ||
	    palette->width != 16 || palette->height != 16 || palette->buffer_width != 64)
		return -EOPNOTSUPP;
	if (r.sx >= source->width || r.sy >= source->height || r.dx >= destination->width ||
	    r.dy >= destination->height || r.width > source->width - r.sx ||
	    r.height > source->height - r.sy || r.width > destination->width - r.dx ||
	    r.height > destination->height - r.dy || r.width > 64 - (r.sx & 63) ||
	    r.height > 32 - (r.sy & 31))
		return -EINVAL;
	if (!r.plane_mask)
		return 0;
	page = *source;
	page.base += (r.sy / 32 * (source->buffer_width / 64) + r.sx / 64) * 8192;
	page.width = 64;
	page.height = 32;
	page.buffer_width = 64;
	r.sx &= 63;
	r.sy &= 31;
	total = DIV_ROUND_UP((r.sx & 3) + r.width, 4) * DIV_ROUND_UP((r.sy & 1) + r.height, 2);
	for (first = 0; first < total; first += 48) {
		struct ps2gs_map_blit_operation op = { .lookup = { .texture = &page, .palette = palette,
			.width = r.width, .height = r.height, .mask = r.plane_mask,
			.channel = r.source_channel, .first = first, .count = min_t(u32, total - first, 48) },
			.sx = r.sx, .sy = r.sy, .dx = r.dx, .dy = r.dy };
		ret = ps2gs_surface_execute(info, PS2GS_CHANNEL_MAP_BLIT, NULL, destination, &op, NULL);
		if (ret) {
			session->failed = ret;
			return ret;
		}
	}
	return 0;
}
