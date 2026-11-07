/* SPDX-License-Identifier: GPL-2.0-only
 * Linux 7.2.8 arch/mips/kernel/traps.c: actual functions before the missing PS2 guards.
 */
void __init *set_except_vector(int n, void *addr)
{
	unsigned long handler = (unsigned long) addr;
	unsigned long old_handler;

#ifdef CONFIG_CPU_MICROMIPS
	/*
	 * Only the TLB handlers are cache aligned with an even
	 * address. All other handlers are on an odd address and
	 * require no modification. Otherwise, MIPS32 mode will
	 * be entered when handling any TLB exceptions. That
	 * would be bad...since we must stay in microMIPS mode.
	 */
	if (!(handler & 0x1))
		handler |= 1;
#endif
	old_handler = xchg(&exception_handlers[n], handler);

	if (n == 0 && cpu_has_divec) {
#ifdef CONFIG_CPU_MICROMIPS
		unsigned long jump_mask = ~((1 << 27) - 1);
#else
		unsigned long jump_mask = ~((1 << 28) - 1);
#endif
		u32 *buf = (u32 *)(ebase + 0x200);
		if ((handler & jump_mask) == ((ebase + 0x200) & jump_mask)) {
			uasm_i_j(&buf, handler & ~jump_mask);
			uasm_i_nop(&buf);
		} else {
			UASM_i_LA(&buf, GPR_K0, handler);
			uasm_i_jr(&buf, GPR_K0);
			uasm_i_nop(&buf);
		}
		local_flush_icache_range(ebase + 0x200, (unsigned long)buf);
	}
	return (void *)old_handler;
}
