/* SPDX-License-Identifier: GPL-2.0 */
/* Included inside the owned provider, after its synchronous executor. */
static u32 ps2gs_depth_role(struct ps2gs_session *session, u32 handle)
{
	struct ps2gs_buffer *buffer = ps2gs_arena_lookup(&session->arena, handle);
	return buffer ? session->depth_color_bpp[buffer - session->arena.buffers] : 0;
}

static int ps2gs_destroy_owned(struct ps2gs_session *session, u32 handle)
{
	struct ps2gs_buffer *buffer = ps2gs_arena_lookup(&session->arena, handle);
	if (!buffer)
		return -ENOENT;
	session->depth_color_bpp[buffer - session->arena.buffers] = 0;
	return ps2gs_arena_destroy(&session->arena, handle);
}

static int ps2gs_depth_create_locked(struct ps2gs_session *session,
		struct fb_info *info, void __user *arg)
{
	struct ps2gs_depth_create request;
	struct ps2gs_buffer *depth, padded, dummy;
	struct ps2gs_command clear = { .op = PS2GS_CLEAR_DEPTH };
	int ret;
	if (copy_from_user(&request, arg, sizeof(request)))
		return -EFAULT;
	if (request.abi != PS2GS_ABI_VERSION || request.reserved || request.handle ||
	    request.buffer_width || request.allocated_bytes ||
	    (request.color_bits_per_pixel != 16 && request.color_bits_per_pixel != 32))
		return -EINVAL;
	ret = ps2gs_arena_create(&session->arena, request.width, request.height, 16, &depth);
	if (ret)
		return ret;
	/* Zero the full Z-swizzled allocation using depth writes; mask every
	 * color bit. FRAME is a dummy at zero, never written by this operation. */
	padded = *depth;
	padded.width = padded.buffer_width;
	padded.height = padded.bytes / (padded.width * 2);
	dummy = padded;
	dummy.base = 0;
	dummy.bits_per_pixel = request.color_bits_per_pixel;
	ret = ps2gs_surface_execute(info, PS2GS_RENDER_SUBMIT, &padded, &dummy, &clear, NULL);
	if (ret) {
		session->failed = ret;
		ps2gs_arena_destroy(&session->arena, depth->handle);
		return ret;
	}
	session->depth_color_bpp[depth - session->arena.buffers] = request.color_bits_per_pixel;
	request.handle = depth->handle;
	request.buffer_width = depth->buffer_width;
	request.allocated_bytes = depth->bytes;
	if (copy_to_user(arg, &request, sizeof(request))) {
		ps2gs_destroy_owned(session, depth->handle);
		return -EFAULT;
	}
	return 0;
}

static int ps2gs_render_locked(struct ps2gs_session *session,
		struct fb_info *info, void __user *arg)
{
	struct ps2gs_render_submit request;
	struct ps2gs_buffer front, *color, *depth = NULL;
	struct ps2gs_command *commands;
	u32 i, role = 0;
	int ret;
	if (copy_from_user(&request, arg, sizeof(request)))
		return -EFAULT;
	if (request.abi != PS2GS_ABI_VERSION || request.reserved || !request.count ||
	    request.count > PS2GS_MAX_COMMANDS)
		return -EINVAL;
	color = ps2gs_session_buffer(session, info, request.target, &front);
	if (!color)
		return -ENOENT;
	if (request.width != color->width || request.height != color->height ||
	    request.bits_per_pixel != color->bits_per_pixel)
		return -ESTALE;
	if (request.depth) {
		depth = ps2gs_arena_lookup(&session->arena, request.depth);
		role = ps2gs_depth_role(session, request.depth);
		if (!depth || !role)
			return -ENOENT;
	}
	if (request.commands >> 32)
		return -EFAULT;
	commands = memdup_user(u64_to_user_ptr(request.commands), request.count * sizeof(*commands));
	if (IS_ERR(commands))
		return PTR_ERR(commands);
	/* All handles, dimensions and commands are snapshotted under the provider
	 * gate; reject the whole request before the first drawing side effect. */
	for (i = 0; i < request.count; ++i) {
		ret = ps2gs_validate_render(color, depth, role, &commands[i]);
		if (ret)
			goto out;
	}
	for (i = 0; i < request.count; ++i) {
		ret = ps2gs_surface_execute(info, PS2GS_RENDER_SUBMIT, depth, color, &commands[i], NULL);
		if (ret) {
			session->failed = ret;
			goto out;
		}
	}
out:
	kfree(commands);
	return ret;
}
