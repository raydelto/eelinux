/* SPDX-License-Identifier: GPL-2.0 */
/* Freestanding declarations. Implementations are provided by iopmod/builtin. */
#ifndef PS2_IOP_STRING_H
#define PS2_IOP_STRING_H
#include <stddef.h>
void *memcpy(void *restrict destination, const void *restrict source, size_t size);
void *memset(void *destination, int value, size_t size);
size_t strlen(const char *string);
#endif
