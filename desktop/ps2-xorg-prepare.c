/* SPDX-License-Identifier: MIT */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <linux/input.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>
#include "ps2_keyboard_config.h"

static int bit(const unsigned long *bits, unsigned n)
{
    return (bits[n / (8 * sizeof(*bits))] >> (n % (8 * sizeof(*bits)))) & 1;
}

static int rgb555(const struct fb_var_screeninfo *var)
{
    return var->bits_per_pixel == 16 &&
        !var->red.offset && var->red.length == 5 &&
        var->green.offset == 5 && var->green.length == 5 &&
        var->blue.offset == 10 && var->blue.length == 5;
}

int main(int argc, char **argv)
{
    struct fb_var_screeninfo var;
    struct fb_fix_screeninfo fix;
    char event[64] = "", line[512];
    int fd, i;
    FILE *input, *output;
    struct stat native;
    struct ps2_keyboard_layout keyboard;
    if (argc != 3) return 2;
    if (ps2_keyboard_current(NULL, &keyboard)) { perror("Keyboard layout"); return 1; }
    fd = open("/dev/fb0", O_RDWR);
    if (fd < 0 || ioctl(fd, FBIOGET_VSCREENINFO, &var)) {
        perror("PS2 framebuffer"); return 1;
    }
    /* Retain timing/geometry. Avoid resetting the display and GS when a
     * previous session already selected the required storage format. */
    if (!rgb555(&var)) {
        var.bits_per_pixel = 16;
        var.activate = FB_ACTIVATE_NOW;
        if (ioctl(fd, FBIOPUT_VSCREENINFO, &var) ||
            ioctl(fd, FBIOGET_VSCREENINFO, &var)) {
            perror("PS2 framebuffer RGB555 mode"); close(fd); return 1;
        }
    }
    if (ioctl(fd, FBIOGET_FSCREENINFO, &fix)) {
        perror("PS2 framebuffer RGB555 mode"); close(fd); return 1;
    }
    if (!rgb555(&var) ||
        stat("/dev/ps2-gs", &native) || !S_ISCHR(native.st_mode)) {
        fprintf(stderr, "The native RGB555 GS device is unavailable.\n");
        close(fd); return 1;
    }
    close(fd);
    for (i = 0; i < 64; ++i) {
        unsigned long keys[(KEY_MAX + 8 * sizeof(unsigned long)) / (8 * sizeof(unsigned long))] = {0};
        char path[64];
        snprintf(path, sizeof(path), "/dev/input/event%d", i);
        fd = open(path, O_RDONLY | O_NONBLOCK);
        if (fd < 0) continue;
        if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(keys)), keys) >= 0 &&
            bit(keys, KEY_A) && bit(keys, KEY_Z) && bit(keys, KEY_ENTER) &&
            bit(keys, KEY_LEFTSHIFT)) {
            snprintf(event, sizeof(event), "%s", path);
            close(fd); break;
        }
        close(fd);
    }
    if (!*event) { fprintf(stderr, "Connect a USB keyboard before running startx.\n"); return 1; }
    input = fopen(argv[1], "r");
    if (!input) { perror(argv[1]); return 1; }
    output = fopen(argv[2], "w");
    if (!output) { perror(argv[2]); fclose(input); return 1; }
    while (fgets(line, sizeof(line), input)) {
        if (strstr(line, "Option \"Device\" \"/dev/input/event0\""))
            fprintf(output, "    Option \"Device\" \"%s\"\n", event);
        else if (strstr(line, "Option \"XkbLayout\""))
            fprintf(output, "    Option \"XkbLayout\" \"%s\"\n", keyboard.xkb);
        else if (strstr(line, "Option \"XkbModel\"")) {
            fprintf(output, "    Option \"XkbModel\" \"%s\"\n", keyboard.model);
            fprintf(output, "    Option \"XkbVariant\" \"%s\"\n", keyboard.variant);
        }
        else if (strstr(line, "Option \"XkbVariant\"")) continue;
        else fputs(line, output);
    }
    if (ferror(input) || fclose(output)) { fclose(input); return 1; }
    fclose(input);
    printf("PS2 Xorg: %ux%u RGB555, %s keyboard %s\n", var.xres, var.yres, keyboard.label, event);
    return 0;
}
