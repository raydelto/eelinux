/* SPDX-License-Identifier: GPL-2.0 */
static int ps2gs_execute_draw_batch(struct fb_info *info, u32 operation,
		const struct ps2gs_buffer *depth, const struct ps2gs_buffer *color,
		const void *requests, u32 count, const struct ps2gs_buffer *texture,
		u32 mask, u32 mode)
{
	u32 stride = operation == PS2GS_SOLID_LOGIC ? sizeof(struct ps2gs_solid_logic) :
		operation == PS2GS_RENDER_SUBMIT ? sizeof(struct ps2gs_command) :
		sizeof(struct ps2gs_texture_triangle);
	u32 i;
	for (i = 0; i < count; i += PS2GS_DMA_BATCH_COUNT) {
		struct ps2gs_draw_batch batch = { .operation = operation,
			.count = min_t(u32, count - i, PS2GS_DMA_BATCH_COUNT),
			.requests = (const u8 *)requests + i * stride,
			.texture = texture, .mask = mask, .mode = mode };
		int ret = ps2gs_surface_execute(info, PS2GS_INTERNAL_BATCH, depth, color, &batch, NULL);
		if (ret)
			return ret;
	}
	return 0;
}
static int ps2gs_solid_batch_locked(struct ps2gs_session *session,
		struct fb_info *info, void __user *arg)
{
	struct ps2gs_solid_batch request;
	struct ps2gs_solid_logic *rectangles;
	struct ps2gs_buffer front, *color;
	u32 i;
	int ret;
	if (copy_from_user(&request, arg, sizeof(request)))
		return -EFAULT;
	if (request.abi != PS2GS_ABI_VERSION || !request.count ||
	    request.count > PS2GS_MAX_COMMANDS || request.alu > 15)
		return -EINVAL;
	color = ps2gs_session_buffer(session, info, request.handle, &front);
	if (!color)
		return -ENOENT;
	if (request.width != color->width || request.height != color->height ||
	    request.bits_per_pixel != color->bits_per_pixel)
		return -ESTALE;
	if (request.rectangles >> 32)
		return -EFAULT;
	rectangles = memdup_user(u64_to_user_ptr(request.rectangles), request.count * sizeof(*rectangles));
	if (IS_ERR(rectangles))
		return PTR_ERR(rectangles);
	for (i = 0; i < request.count; ++i) {
		if (rectangles[i].handle != request.handle ||
		    rectangles[i].plane_mask != request.plane_mask || rectangles[i].alu != request.alu) {
			ret = -EINVAL;
			goto out;
		}
		ret = ps2gs_validate_solid_logic(color, rectangles + i);
		if (ret)
			goto out;
	}
	ret = ps2gs_execute_draw_batch(info, PS2GS_SOLID_LOGIC, NULL, color,
		rectangles, request.count, NULL, 0, 0);
	if (ret)
		session->failed = ret;
out:
	kfree(rectangles);
	return ret;
}
