/* SPDX-License-Identifier: MIT */
#ifndef PS2_TEST_LINE_BIAS_STUB_H
#define PS2_TEST_LINE_BIAS_STUB_H
#undef ARRAY_SIZE
#include "miline.h"
/* Older standalone diagnostics draw only axis-aligned lines, for which bias
 * has no effect. Supply Xorg's key in the unused last slot of their 16-slot
 * screen private arrays. The new diagonal probe initializes the real default
 * bias explicitly in its screen bookkeeping instead. This is test-only. */
DevPrivateKeyRec miZeroLineScreenKeyRec={.offset=15*sizeof(void *),
    .initialized=TRUE,.type=PRIVATE_SCREEN};
#endif
