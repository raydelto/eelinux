/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef PS2_SMAP_IOP_H
#define PS2_SMAP_IOP_H
struct platform_device;
int smap_iop_probe(struct platform_device *pdev);
void smap_iop_remove(struct platform_device *pdev);
#endif
