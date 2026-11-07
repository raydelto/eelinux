/* SPDX-License-Identifier: MIT */
#ifndef PS2_KEYBOARD_CONFIG_H
#define PS2_KEYBOARD_CONFIG_H
#include <stddef.h>
#define PS2_KEYBOARD_LAYOUT_LIMIT 64
#ifndef PS2_KEYBOARD_CATALOG
#define PS2_KEYBOARD_CATALOG "/usr/share/eelinux/keyboard-layouts.tsv"
#endif
#ifndef PS2_KEYBOARD_SETTINGS
#define PS2_KEYBOARD_SETTINGS "/etc/default/keyboard"
#endif
struct ps2_keyboard_layout {
    char id[32], xkb[32], variant[32], model[32], map[64], label[128];
};
int ps2_keyboard_layouts(struct ps2_keyboard_layout *, size_t);
int ps2_keyboard_current(const char *, struct ps2_keyboard_layout *);
#endif
