/* SPDX-License-Identifier: GPL-2.0 */
#ifndef PS2_IOP_SYS_TYPES_H
#define PS2_IOP_SYS_TYPES_H
#include <stddef.h>
/* IOP firmware is an o32 freestanding target; signed pointer-sized result. */
typedef __PTRDIFF_TYPE__ ssize_t;
#endif
