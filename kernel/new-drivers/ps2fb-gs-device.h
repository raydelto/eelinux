/* SPDX-License-Identifier: GPL-2.0 */
/* Included after the framebuffer command engine. The misc device has a real
 * file lifetime; fb_ioctl itself has no file/private_data argument. gate
 * serializes sessions and disconnect. Removal clears info under gate before
 * framebuffer teardown; surviving files then return ENODEV without touching
 * framebuffer memory. No provider pointer is retained outside gate.
 */
#include "ps2fb-gs-arena.h"
#include "ps2fb-gs-surface-packet.h"

struct ps2gs_session;
struct ps2gs_device {
	struct miscdevice misc;
	struct kref references;
	struct mutex gate;
	struct fb_info *info;
	struct ps2gs_session *owner;
};
struct ps2gs_session {
	struct ps2gs_device *device;
	struct ps2gs_arena arena;
	u64 epoch;
	int failed;
};

static void ps2gs_device_free(struct kref *ref)
{
	kfree(container_of(ref, struct ps2gs_device, references));
}

static u32 ps2gs_surface_begin(struct fb_info *info)
{
	struct ps2fb_par *par = info->par;
	u32 i, end = ALIGN(info->var.xres_virtual, 64) *
		ALIGN(info->var.yres_virtual, info->var.bits_per_pixel == 16 ? 64 : 32) *
		(info->var.bits_per_pixel / 8);
	for (i = 0; i <= par->gs_font_blocks; ++i)
		end = max(end, (block_address_for_index(par->cb.block_count + i, info) + 1) * 256);
	return ALIGN(end, PS2GS_PAGE_BYTES);
}

static int ps2gs_device_open(struct inode *inode, struct file *file)
{
	struct ps2gs_device *device = container_of(file->private_data, struct ps2gs_device, misc);
	struct ps2gs_session *session;
	struct ps2gs_caps caps;
	struct fb_info *info;
	struct ps2fb_par *par;
	int ret = 0;

	if (!capable(CAP_SYS_RAWIO))
		return -EPERM;
	session = kzalloc(sizeof(*session), GFP_KERNEL);
	if (!session)
		return -ENOMEM;
	mutex_lock(&device->gate);
	info = device->info;
	if (!info || device->owner) {
		ret = info ? -EBUSY : -ENODEV;
		goto unlock;
	}
	if (!console_trylock()) {
		ret = -EAGAIN;
		goto unlock;
	}
	if (!mutex_trylock(&info->lock)) {
		ret = -EAGAIN;
		goto console;
	}
	par = info->par;
	mutex_lock(&info->mm_lock);
	if (info->var.xoffset || info->var.yoffset) {
		ret = -EOPNOTSUPP;
		goto maps;
	}
	if (atomic_read(&par->shadow_maps)) {
		ret = -EBUSY;
		goto maps;
	}
	flush_delayed_work(&info->deferred_work);
	ps2gs_caps(info, &caps);
	ret = ps2gs_arena_init(&session->arena, ps2gs_surface_begin(info),
		caps.depth_base ? caps.depth_base : GS_MEMORY_SIZE);
	if (ret)
		goto maps;
	session->epoch = par->gs_epoch;
	session->device = device;
	kref_get(&device->references);
	device->owner = session;
	atomic_set(&par->gs_owned, 1);
	file->private_data = session;
maps:
	mutex_unlock(&info->mm_lock);
	mutex_unlock(&info->lock);
console:
	console_unlock();
unlock:
	mutex_unlock(&device->gate);
	if (ret)
		kfree(session);
	return ret;
}

static int ps2gs_device_release(struct inode *inode, struct file *file)
{
	struct ps2gs_session *session = file->private_data;
	struct ps2gs_device *device = session->device;

	mutex_lock(&device->gate);
	if (device->info)
		atomic_set(&((struct ps2fb_par *)device->info->par)->gs_owned, 0);
	device->owner = NULL;
	mutex_unlock(&device->gate);
	kfree(session);
	kref_put(&device->references, ps2gs_device_free);
	return 0;
}

static struct ps2gs_buffer *ps2gs_session_buffer(struct ps2gs_session *session,
		struct fb_info *info, u32 handle, struct ps2gs_buffer *front)
{
	if (handle)
		return ps2gs_arena_lookup(&session->arena, handle);
	*front = (struct ps2gs_buffer) { .width = info->var.xres,
		.height = info->var.yres, .buffer_width = ALIGN(info->var.xres_virtual, 64),
		.bits_per_pixel = info->var.bits_per_pixel };
	return front;
}

static int ps2gs_surface_execute(struct fb_info *info, unsigned int operation,
		const struct ps2gs_buffer *source, const struct ps2gs_buffer *destination,
		const void *request, const u8 *pixels)
{
	struct ps2fb_par *par = info->par;
	unsigned long flags;
	size_t count;
	u32 i;
	int ret = 0;

	spin_lock_irqsave(&par->lock, flags);
	/* Wait before touching the shared DMA packet, including after a timeout. */
	if (!gif_wait()) {
		ret = -ETIMEDOUT;
		goto out;
	}
	gs_write_csr((struct gs_csr) { .finish = 1 });
	if (operation == PS2GS_SURFACE_FILL) {
		count = ps2gs_package_surface_fill(par->package.buffer, destination, request);
	} else if (operation == PS2GS_SURFACE_COPY) {
		count = ps2gs_package_surface_copy(par->package.buffer, source, destination, request);
	} else {
		const struct ps2gs_surface_upload *u = request;
		struct ps2gs_upload image = { .x = u->x, .y = u->y,
			.image_width = u->width, .image_height = u->height, .bytes = u->bytes };
		count = ps2gs_package_upload_to(par->package.buffer, &image, pixels,
			destination->base / 256, destination->buffer_width / 64,
			ps2gs_buffer_psm(destination));
	}
	/* Fixed typed packets are <= 200 qwords, in the 256-qword DMA page. */
	if (WARN_ON_ONCE(count > par->package.capacity)) {
		ret = -ENOSPC;
		goto out;
	}
	gif_write(&par->package.buffer->gif, count);
	if (!gif_wait()) {
		ret = -ETIMEDOUT;
		goto out;
	}
	for (i = 0; i < 1000000 && !gs_read_csr().finish; ++i)
		cpu_relax();
	if (i == 1000000)
		ret = -ETIMEDOUT;
	else {
		++par->gs_stats.completed_batches;
		if (operation == PS2GS_SURFACE_FILL)
			++par->gs_stats.fills;
		if (operation == PS2GS_SURFACE_COPY)
			++par->gs_stats.copies;
	}
out:
	if (ret)
		++par->gs_stats.timeouts;
	spin_unlock_irqrestore(&par->lock, flags);
	return ret;
}

static int ps2gs_surface_create_locked(struct ps2gs_session *session,
		struct fb_info *info, void __user *arg)
{
	struct ps2gs_surface_create request;
	struct ps2gs_surface_fill clear = { .abi = PS2GS_ABI_VERSION };
	struct ps2gs_buffer *buffer, padded;
	int ret;

	if (copy_from_user(&request, arg, sizeof(request)))
		return -EFAULT;
	if (request.abi != PS2GS_ABI_VERSION || request.handle || request.reserved ||
	    request.buffer_width || request.allocated_bytes)
		return -EINVAL;
	ret = ps2gs_arena_create(&session->arena, request.width, request.height,
		request.bits_per_pixel, &buffer);
	if (ret)
		return ret;
	/* Clear every reserved pixel, including page padding, before publishing. */
	padded = *buffer;
	padded.width = padded.buffer_width;
	padded.height = padded.bytes / (padded.width * (padded.bits_per_pixel / 8));
	clear.width = padded.width;
	clear.height = padded.height;
	ret = ps2gs_surface_execute(info, PS2GS_SURFACE_FILL, NULL, &padded, &clear, NULL);
	if (ret) {
		session->failed = ret;
		ps2gs_arena_destroy(&session->arena, buffer->handle);
		return ret;
	}
	request.handle = buffer->handle;
	request.buffer_width = buffer->buffer_width;
	request.allocated_bytes = buffer->bytes;
	if (copy_to_user(arg, &request, sizeof(request))) {
		ps2gs_arena_destroy(&session->arena, buffer->handle);
		return -EFAULT;
	}
	return 0;
}

static int ps2gs_surface_ioctl_locked(struct ps2gs_session *session,
		struct fb_info *info, unsigned int cmd, void __user *arg)
{
	struct ps2gs_buffer front1, front2, *source, *destination;
	struct ps2gs_caps caps;
	u8 *pixels;
	int ret;

	if (cmd == PS2GS_GET_CAPS) {
		ps2gs_caps(info, &caps);
		/* This owned interface currently exposes color storage/transfers. The
		 * legacy framebuffer triangle/depth engine is a separate ABI endpoint;
		 * do not claim those operations on a file that cannot submit them. */
		caps.features = PS2GS_HW_FILL | PS2GS_HW_COPY | PS2GS_HW_UPLOAD |
			PS2GS_HW_SURFACES;
		return copy_to_user(arg, &caps, sizeof(caps)) ? -EFAULT : 0;
	}
	if (session->epoch != ((struct ps2fb_par *)info->par)->gs_epoch)
		return -ESTALE;
	if (session->failed)
		return session->failed;
	if (!vc_cons[fg_console].d || vc_cons[fg_console].d->vc_mode != KD_GRAPHICS)
		return -EBUSY;
	switch (cmd) {
	case PS2GS_SURFACE_CREATE:
		return ps2gs_surface_create_locked(session, info, arg);
	case PS2GS_SURFACE_DESTROY: {
		struct ps2gs_surface_destroy destroy;
		if (copy_from_user(&destroy, arg, sizeof(destroy)))
			return -EFAULT;
		if (destroy.abi != PS2GS_ABI_VERSION || destroy.reserved[0] || destroy.reserved[1])
			return -EINVAL;
		return ps2gs_arena_destroy(&session->arena, destroy.handle);
	}
	case PS2GS_SURFACE_FILL: {
		struct ps2gs_surface_fill fill;
		if (copy_from_user(&fill, arg, sizeof(fill)))
			return -EFAULT;
		destination = ps2gs_session_buffer(session, info, fill.handle, &front1);
		if (!destination)
			return -ENOENT;
		if (fill.abi != PS2GS_ABI_VERSION || fill.reserved ||
		    !ps2gs_buffer_rect(destination, fill.x, fill.y, fill.width, fill.height))
			return -EINVAL;
		ret = ps2gs_surface_execute(info, cmd, NULL, destination, &fill, NULL);
		break;
	}
	case PS2GS_SURFACE_COPY: {
		struct ps2gs_surface_copy copy;
		if (copy_from_user(&copy, arg, sizeof(copy)))
			return -EFAULT;
		source = ps2gs_session_buffer(session, info, copy.source, &front1);
		destination = ps2gs_session_buffer(session, info, copy.destination, &front2);
		if (!source || !destination)
			return -ENOENT;
		ret = ps2gs_validate_surface_copy(source, destination, &copy);
		if (ret)
			return ret;
		ret = ps2gs_surface_execute(info, cmd, source, destination, &copy, NULL);
		break;
	}
	case PS2GS_SURFACE_UPLOAD: {
		struct ps2gs_surface_upload upload;
		struct ps2gs_upload bounded;
		if (copy_from_user(&upload, arg, sizeof(upload)))
			return -EFAULT;
		destination = ps2gs_session_buffer(session, info, upload.handle, &front1);
		if (!destination)
			return -ENOENT;
		caps = (struct ps2gs_caps) { .abi = PS2GS_ABI_VERSION,
			.width = destination->width, .height = destination->height,
			.bits_per_pixel = destination->bits_per_pixel };
		bounded = (struct ps2gs_upload) { .abi = upload.abi,
			.width = caps.width, .height = caps.height, .bits_per_pixel = caps.bits_per_pixel,
			.x = upload.x, .y = upload.y, .image_width = upload.width,
			.image_height = upload.height, .bytes = upload.bytes,
			.reserved = upload.reserved, .pixels = upload.pixels };
		ret = ps2gs_validate_upload(&bounded, &caps);
		if (ret)
			return ret;
		pixels = memdup_user(u64_to_user_ptr(upload.pixels), upload.bytes);
		if (IS_ERR(pixels))
			return PTR_ERR(pixels);
		ret = ps2gs_surface_execute(info, cmd, NULL, destination, &upload, pixels);
		kfree(pixels);
		break;
	}
	default:
		return -ENOTTY;
	}
	if (ret)
		session->failed = ret;
	return ret;
}

static long ps2gs_device_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct ps2gs_session *session = file->private_data;
	struct ps2gs_device *device = session->device;
	struct fb_info *info;
	int ret;

	if (!capable(CAP_SYS_RAWIO))
		return -EPERM;
	mutex_lock(&device->gate);
	info = device->info;
	if (!info) {
		ret = -ENODEV;
		goto unlock;
	}
	if (!console_trylock()) {
		ret = -EAGAIN;
		goto unlock;
	}
	/* Never block against fbdev's opposite lock order. EAGAIN is pre-effect. */
	if (!mutex_trylock(&info->lock)) {
		ret = -EAGAIN;
		goto console;
	}
	mutex_lock(&info->mm_lock);
	ret = ps2gs_surface_ioctl_locked(session, info, cmd, (void __user *)arg);
	mutex_unlock(&info->mm_lock);
	mutex_unlock(&info->lock);
console:
	console_unlock();
unlock:
	mutex_unlock(&device->gate);
	return ret;
}

static const struct file_operations ps2gs_device_operations = {
	.owner = THIS_MODULE, .open = ps2gs_device_open, .release = ps2gs_device_release,
	.unlocked_ioctl = ps2gs_device_ioctl, .compat_ioctl = ps2gs_device_ioctl,
	.llseek = noop_llseek,
};

static int ps2gs_device_register(struct fb_info *info)
{
	struct ps2fb_par *par = info->par;
	struct ps2gs_device *device = kzalloc(sizeof(*device), GFP_KERNEL);
	int ret;
	if (!device)
		return -ENOMEM;
	kref_init(&device->references);
	mutex_init(&device->gate);
	device->info = info;
	device->misc = (struct miscdevice) { .minor = MISC_DYNAMIC_MINOR,
		.name = "ps2-gs", .fops = &ps2gs_device_operations, .mode = 0600 };
	ret = misc_register(&device->misc);
	if (ret) {
		kref_put(&device->references, ps2gs_device_free);
		return ret;
	}
	par->gs_device = device;
	return 0;
}

static void ps2gs_device_unregister(struct fb_info *info)
{
	struct ps2fb_par *par = info->par;
	struct ps2gs_device *device = par->gs_device;
	if (!device)
		return;
	/* misc_deregister synchronizes open before the provider reference drops. */
	misc_deregister(&device->misc);
	mutex_lock(&device->gate);
	device->info = NULL;
	atomic_set(&par->gs_owned, 0);
	par->gs_device = NULL;
	mutex_unlock(&device->gate);
	kref_put(&device->references, ps2gs_device_free);
}
