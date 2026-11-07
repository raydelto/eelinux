/* SPDX-License-Identifier: GPL-2.0-only; Linux 7.2.8 genex.S prologue. */
	.macro	BUILD_SKIPOVER_PROLOGUE handler
	FEXPORT(skipover_\handler)
	.set	push
	.set	noat
	MFC0	k0, CP0_EPC
	/* Subtract/add 2 to let the ISA bit propagate through the mask.  */
	PTR_LA	k1, r4k_wait_insn - 2
	ori	k0, r4k_wait_idle_size - 2
	.set	noreorder
	bne	k0, k1, \handler
	 PTR_ADDIU	k0, r4k_wait_exit - r4k_wait_insn + 2
	.set	reorder
	MTC0	k0, CP0_EPC
	.set pop
	.endm
