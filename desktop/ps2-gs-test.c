/* SPDX-License-Identifier: MIT */
/* Direct GS hardware command test. No Mesa or CPU rasterizer is involved. */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <linux/kd.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>
#include "ps2-gs.h"

_Static_assert(sizeof(struct ps2gs_command) == 64, "command ABI");
_Static_assert(sizeof(struct ps2gs_submit) == 32, "submit ABI");
_Static_assert(sizeof(struct ps2gs_caps) == 80, "caps ABI");

static int call(int fd, unsigned long cmd, void *data)
{
	int ret;
	/* Console/font activity may make the driver's nonblocking lock busy. */
	for (unsigned n = 0; n < 100; ++n) {
		ret = ioctl(fd, cmd, data);
		if (ret == 0 || errno != EAGAIN)
			return ret;
		nanosleep(&(struct timespec){ .tv_nsec = 10000000 }, NULL);
	}
	return ret;
}

static struct ps2gs_command triangle(unsigned w, unsigned h, unsigned z,
				    unsigned color, unsigned inset)
{
	return (struct ps2gs_command) { .op = PS2GS_TRIANGLE,
		.flags = PS2GS_DEPTH_TEST, .v = {
			{ w * 8, (h / 6 + inset) * 16, z, color },
			{ (w / 6 + inset) * 16, (h * 5 / 6 - inset) * 16, z, color },
			{ (w * 5 / 6 - inset) * 16, (h * 5 / 6 - inset) * 16, z, color } } };
}

int main(int argc, char **argv)
{
	struct ps2gs_caps before, after;
	struct ps2gs_command commands[6];
	struct ps2gs_submit submit;
	int fb = -1, tty = -1, old_mode = KD_TEXT, ret = 1;
	unsigned seconds = 4;
	void *map;

	if (argc == 2 && !strcmp(argv[1], "--brief"))
		seconds = 1;
	else if (argc != 1) {
		fprintf(stderr, "Usage: ps2-gs-test [--brief]\n");
		return 2;
	}
	fb = open("/dev/fb0", O_RDWR | O_CLOEXEC);
	if (fb < 0 || call(fb, PS2GS_GET_CAPS, &before) < 0) {
		perror("ps2-gs: query hardware command engine");
		goto out;
	}
	printf("ps2-gs: ABI=%u %ux%u %ubpp features=%x depth=%u bytes\n",
		before.abi, before.width, before.height, before.bits_per_pixel,
		before.features, before.depth_bytes);
	if (before.abi != PS2GS_ABI_VERSION || before.width < 96 || before.height < 96 ||
	    !(before.features & PS2GS_HW_DEPTH16)) {
		fprintf(stderr, "ps2-gs: this mode cannot run the depth test; no fallback\n");
		goto out;
	}
	commands[0] = (struct ps2gs_command) { .op = PS2GS_FILL,
		.width = before.width, .height = before.height,
		.v = { { .rgba = 0x80200b07 } } };
	commands[1] = (struct ps2gs_command) { .op = PS2GS_FILL,
		.width = 32, .height = 16, .v = { { .x = 16*16, .y = 16*16,
			.rgba = 0x80fff000 } } };
	commands[2] = (struct ps2gs_command) { .op = PS2GS_COPY,
		.width = 32, .height = 16, .v = { { .x = 16*16, .y = 16*16 },
			{ .x = 64*16, .y = 16*16 } } };
	commands[3] = (struct ps2gs_command) { .op = PS2GS_CLEAR_DEPTH };
	commands[4] = triangle(before.width, before.height, 40000, 0x80fff000, 20);
	/* The farther red triangle is submitted LAST. GS must retain the nearer
	 * cyan triangle in the overlap, demonstrating hardware depth rejection.
	 */
	commands[5] = triangle(before.width, before.height, 20000, 0x802020e0, 0);
	submit = (struct ps2gs_submit) { .abi = before.abi, .count = 6,
		.width = before.width, .height = before.height,
		.bits_per_pixel = before.bits_per_pixel,
		.commands = (uintptr_t)commands };
	/* An unfaulted mmap must already block GPU ownership. */
	map = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, fb, 0);
	if (map == MAP_FAILED) {
		perror("ps2-gs: map ownership test");
		goto out;
	}
	int mapped_result = call(fb, PS2GS_SUBMIT, &submit), mapped_errno = errno;
	munmap(map, 4096);
	if (mapped_result != -1 || mapped_errno != EBUSY) {
		fprintf(stderr, "ps2-gs: mapped-buffer exclusion FAILED\n");
		goto out;
	}
	/* An invalid last command must reject the WHOLE batch without drawing. */
	commands[5].flags |= 0x80000000U;
	int invalid_result = call(fb, PS2GS_SUBMIT, &submit), invalid_errno = errno;
	commands[5].flags &= ~0x80000000U;
	if (invalid_result != -1 || invalid_errno != EOPNOTSUPP ||
	    call(fb, PS2GS_GET_CAPS, &after) < 0 ||
	    after.completed_batches != before.completed_batches || after.fills != before.fills) {
		fprintf(stderr, "ps2-gs: batch rejection FAILED\n");
		goto out;
	}
	tty = open("/dev/tty0", O_RDWR | O_CLOEXEC);
	if (tty < 0 || ioctl(tty, KDGETMODE, &old_mode) < 0 ||
	    ioctl(tty, KDSETMODE, KD_GRAPHICS) < 0) {
		perror("ps2-gs: acquire graphics console");
		goto out;
	}
	fflush(stdout);
	if (call(fb, PS2GS_SUBMIT, &submit) < 0) {
		perror("ps2-gs: hardware submission");
		goto out;
	}
	nanosleep(&(struct timespec){ .tv_sec = seconds }, NULL);
	if (call(fb, PS2GS_GET_CAPS, &after) < 0) {
		perror("ps2-gs: completion counters");
		goto out;
	}
	if (after.completed_batches != before.completed_batches + 1 ||
	    after.fills != before.fills + 2 || after.copies != before.copies + 1 ||
	    after.triangles != before.triangles + 2 ||
	    after.depth_clears != before.depth_clears + 1 ||
	    after.timeouts != before.timeouts) {
		fprintf(stderr, "ps2-gs: hardware completion counters FAILED\n");
		goto out;
	}
	ret = 0;
out:
	if (tty >= 0) {
		ioctl(tty, KDSETMODE, old_mode);
		close(tty);
	}
	if (fb >= 0)
		close(fb);
	printf("ps2-gs: command/fence test %s; raster image requires visual qualification\n",
		ret ? "FAILED" : "PASSED");
	return ret;
}
