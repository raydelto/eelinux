/* SPDX-License-Identifier: GPL-2.0-only
 * Linux 7.2.8 arch/mips/mm/tlbex.c: actual functions before the missing PS2 guards.
 */
static void build_r4000_tlb_refill_handler(void)
{
	u32 *p = tlb_handler;
	struct uasm_label *l = labels;
	struct uasm_reloc *r = relocs;
	u32 *f;
	unsigned int final_len;
	struct mips_huge_tlb_info htlb_info __maybe_unused;
	enum vmalloc64_mode vmalloc_mode __maybe_unused;

	memset(tlb_handler, 0, sizeof(tlb_handler));
	memset(labels, 0, sizeof(labels));
	memset(relocs, 0, sizeof(relocs));
	memset(final_handler, 0, sizeof(final_handler));

	if (IS_ENABLED(CONFIG_64BIT) && (scratch_reg >= 0 || scratchpad_available()) && use_bbit_insns()) {
		htlb_info = build_fast_tlb_refill_handler(&p, &l, &r, GPR_K0, GPR_K1,
							  scratch_reg);
		vmalloc_mode = refill_scratch;
	} else {
		htlb_info.huge_pte = GPR_K0;
		htlb_info.restore_scratch = 0;
		htlb_info.need_reload_pte = true;
		vmalloc_mode = refill_noscratch;
		/*
		 * create the plain linear handler
		 */
		if (bcm1250_m3_war()) {
			unsigned int segbits = 44;

			uasm_i_dmfc0(&p, GPR_K0, C0_BADVADDR);
			uasm_i_dmfc0(&p, GPR_K1, C0_ENTRYHI);
			uasm_i_xor(&p, GPR_K0, GPR_K0, GPR_K1);
			uasm_i_dsrl_safe(&p, GPR_K1, GPR_K0, 62);
			uasm_i_dsrl_safe(&p, GPR_K0, GPR_K0, 12 + 1);
			uasm_i_dsll_safe(&p, GPR_K0, GPR_K0, 64 + 12 + 1 - segbits);
			uasm_i_or(&p, GPR_K0, GPR_K0, GPR_K1);
			uasm_il_bnez(&p, &r, GPR_K0, label_leave);
			/* No need for uasm_i_nop */
		}

#ifdef CONFIG_64BIT
		build_get_pmde64(&p, &l, &r, GPR_K0, GPR_K1); /* get pmd in GPR_K1 */
#else
		build_get_pgde32(&p, GPR_K0, GPR_K1); /* get pgd in GPR_K1 */
#endif

#ifdef CONFIG_MIPS_HUGE_TLB_SUPPORT
		build_is_huge_pte(&p, &r, GPR_K0, GPR_K1, label_tlb_huge_update);
#endif

		build_get_ptep(&p, GPR_K0, GPR_K1);
		build_update_entries(&p, GPR_K0, GPR_K1);
		build_tlb_write_entry(&p, &l, &r, tlb_random);
		uasm_l_leave(&l, p);
		uasm_i_eret(&p); /* return from trap */
	}

#ifdef CONFIG_CPU_R5900
	/* There should be nothing which can be interpreted as cache instruction. */
	uasm_i_nop(&p);
	uasm_i_nop(&p);
	uasm_i_nop(&p);
	uasm_i_nop(&p);
	uasm_i_nop(&p);
#endif

#ifdef CONFIG_MIPS_HUGE_TLB_SUPPORT
	uasm_l_tlb_huge_update(&l, p);
	if (htlb_info.need_reload_pte)
		UASM_i_LW(&p, htlb_info.huge_pte, 0, GPR_K1);
	build_huge_update_entries(&p, htlb_info.huge_pte, GPR_K1);
	build_huge_tlb_write_entry(&p, &l, &r, GPR_K0, tlb_random,
				   htlb_info.restore_scratch);
#endif

#ifdef CONFIG_64BIT
	build_get_pgd_vmalloc64(&p, &l, &r, GPR_K0, GPR_K1, vmalloc_mode);
#endif

	/*
	 * Overflow check: For the 64bit handler, we need at least one
	 * free instruction slot for the wrap-around branch. In worst
	 * case, if the intended insertion point is a delay slot, we
	 * need three, with the second nop'ed and the third being
	 * unused.
	 */
	switch (boot_cpu_type()) {
	default:
		if (sizeof(long) == 4) {
		fallthrough;
	case CPU_LOONGSON2EF:
		/* Loongson2 ebase is different than r4k, we have more space */
			if ((p - tlb_handler) > 64)
				panic("TLB refill handler space exceeded");
			/*
			 * Now fold the handler in the TLB refill handler space.
			 */
			f = final_handler;
			/* Simplest case, just copy the handler. */
			uasm_copy_handler(relocs, labels, tlb_handler, p, f);
			final_len = p - tlb_handler;
			break;
		} else {
			if (((p - tlb_handler) > (MIPS64_REFILL_INSNS * 2) - 1)
			    || (((p - tlb_handler) > (MIPS64_REFILL_INSNS * 2) - 3)
				&& uasm_insn_has_bdelay(relocs,
							tlb_handler + MIPS64_REFILL_INSNS - 3)))
				panic("TLB refill handler space exceeded");
			/*
			 * Now fold the handler in the TLB refill handler space.
			 */
			f = final_handler + MIPS64_REFILL_INSNS;
			if ((p - tlb_handler) <= MIPS64_REFILL_INSNS) {
				/* Just copy the handler. */
				uasm_copy_handler(relocs, labels, tlb_handler, p, f);
				final_len = p - tlb_handler;
			} else {
#ifdef CONFIG_MIPS_HUGE_TLB_SUPPORT
				const enum label_id ls = label_tlb_huge_update;
#else
				const enum label_id ls = label_vmalloc;
#endif
				u32 *split;
				int ov = 0;
				int i;

				for (i = 0; i < ARRAY_SIZE(labels) && labels[i].lab != ls; i++)
					;
				BUG_ON(i == ARRAY_SIZE(labels));
				split = labels[i].addr;

				/*
				 * See if we have overflown one way or the other.
				 */
				if (split > tlb_handler + MIPS64_REFILL_INSNS ||
				    split < p - MIPS64_REFILL_INSNS)
					ov = 1;

				if (ov) {
					/*
					 * Split two instructions before the end.  One
					 * for the branch and one for the instruction
					 * in the delay slot.
					 */
					split = tlb_handler + MIPS64_REFILL_INSNS - 2;

					/*
					 * If the branch would fall in a delay slot,
					 * we must back up an additional instruction
					 * so that it is no longer in a delay slot.
					 */
					if (uasm_insn_has_bdelay(relocs, split - 1))
						split--;
				}
				/* Copy first part of the handler. */
				uasm_copy_handler(relocs, labels, tlb_handler, split, f);
				f += split - tlb_handler;

				if (ov) {
					/* Insert branch. */
					uasm_l_split(&l, final_handler);
					uasm_il_b(&f, &r, label_split);
					if (uasm_insn_has_bdelay(relocs, split))
						uasm_i_nop(&f);
					else {
						uasm_copy_handler(relocs, labels,
								  split, split + 1, f);
						uasm_move_labels(labels, f, f + 1, -1);
						f++;
						split++;
					}
				}

				/* Copy the rest of the handler. */
				uasm_copy_handler(relocs, labels, split, p, final_handler);
				final_len = (f - (final_handler + MIPS64_REFILL_INSNS)) +
					    (p - split);
			}
		}
		break;
	}

	uasm_resolve_relocs(relocs, labels);
	pr_debug("Wrote TLB refill handler (%u instructions).\n",
		 final_len);

	memcpy((void *)ebase, final_handler, 4 * final_len);
	local_flush_icache_range(ebase, ebase + 4 * final_len);
	dump_handler("r4000_tlb_refill", (u32 *)ebase, (u32 *)(ebase + 4 * final_len));
}

static void build_r4000_tlb_load_handler(void)
{
	u32 *p = (u32 *)msk_isa16_mode((ulong)handle_tlbl);
	struct uasm_label *l = labels;
	struct uasm_reloc *r = relocs;
	struct work_registers wr;

	memset(p, 0, handle_tlbl_end - (char *)p);
	memset(labels, 0, sizeof(labels));
	memset(relocs, 0, sizeof(relocs));

	if (bcm1250_m3_war()) {
		unsigned int segbits = 44;

		uasm_i_dmfc0(&p, GPR_K0, C0_BADVADDR);
		uasm_i_dmfc0(&p, GPR_K1, C0_ENTRYHI);
		uasm_i_xor(&p, GPR_K0, GPR_K0, GPR_K1);
		uasm_i_dsrl_safe(&p, GPR_K1, GPR_K0, 62);
		uasm_i_dsrl_safe(&p, GPR_K0, GPR_K0, 12 + 1);
		uasm_i_dsll_safe(&p, GPR_K0, GPR_K0, 64 + 12 + 1 - segbits);
		uasm_i_or(&p, GPR_K0, GPR_K0, GPR_K1);
		uasm_il_bnez(&p, &r, GPR_K0, label_leave);
		/* No need for uasm_i_nop */
	}

	wr = build_r4000_tlbchange_handler_head(&p, &l, &r);
	build_pte_present(&p, &r, wr.r1, wr.r2, wr.r3, label_nopage_tlbl);
	if (m4kc_tlbp_war())
		build_tlb_probe_entry(&p);

	if (cpu_has_rixi && !cpu_has_rixiex) {
		/*
		 * If the page is not _PAGE_VALID, RI or XI could not
		 * have triggered it.  Skip the expensive test..
		 */
		if (use_bbit_insns()) {
			uasm_il_bbit0(&p, &r, wr.r1, ilog2(_PAGE_VALID),
				      label_tlbl_goaround1);
		} else {
			uasm_i_andi(&p, wr.r3, wr.r1, _PAGE_VALID);
			uasm_il_beqz(&p, &r, wr.r3, label_tlbl_goaround1);
		}
		uasm_i_nop(&p);

		/*
		 * Warn if something may race with us & replace the TLB entry
		 * before we read it here. Everything with such races should
		 * also have dedicated RiXi exception handlers, so this
		 * shouldn't be hit.
		 */
		WARN(cpu_has_tlbex_tlbp_race(), "Unhandled race in RiXi path");

		uasm_i_tlbr(&p);

		if (cpu_has_mips_r2_exec_hazard)
			uasm_i_ehb(&p);

		/* Examine  entrylo 0 or 1 based on ptr. */
		if (use_bbit_insns()) {
			uasm_i_bbit0(&p, wr.r2, ilog2(sizeof(pte_t)), 8);
		} else {
			uasm_i_andi(&p, wr.r3, wr.r2, sizeof(pte_t));
			uasm_i_beqz(&p, wr.r3, 8);
		}
		/* load it in the delay slot*/
		UASM_i_MFC0(&p, wr.r3, C0_ENTRYLO0);
		/* load it if ptr is odd */
		UASM_i_MFC0(&p, wr.r3, C0_ENTRYLO1);
		/*
		 * If the entryLo (now in wr.r3) is valid (bit 1), RI or
		 * XI must have triggered it.
		 */
		if (use_bbit_insns()) {
			uasm_il_bbit1(&p, &r, wr.r3, 1, label_nopage_tlbl);
			uasm_i_nop(&p);
			uasm_l_tlbl_goaround1(&l, p);
		} else {
			uasm_i_andi(&p, wr.r3, wr.r3, 2);
			uasm_il_bnez(&p, &r, wr.r3, label_nopage_tlbl);
			uasm_i_nop(&p);
		}
		uasm_l_tlbl_goaround1(&l, p);
	}
	build_make_valid(&p, &r, wr.r1, wr.r2, wr.r3);
	build_r4000_tlbchange_handler_tail(&p, &l, &r, wr.r1, wr.r2);

#ifdef CONFIG_MIPS_HUGE_TLB_SUPPORT
	/*
	 * This is the entry point when build_r4000_tlbchange_handler_head
	 * spots a huge page.
	 */
	uasm_l_tlb_huge_update(&l, p);
	iPTE_LW(&p, wr.r1, wr.r2);
	build_pte_present(&p, &r, wr.r1, wr.r2, wr.r3, label_nopage_tlbl);
	build_tlb_probe_entry(&p);

	if (cpu_has_rixi && !cpu_has_rixiex) {
		/*
		 * If the page is not _PAGE_VALID, RI or XI could not
		 * have triggered it.  Skip the expensive test..
		 */
		if (use_bbit_insns()) {
			uasm_il_bbit0(&p, &r, wr.r1, ilog2(_PAGE_VALID),
				      label_tlbl_goaround2);
		} else {
			uasm_i_andi(&p, wr.r3, wr.r1, _PAGE_VALID);
			uasm_il_beqz(&p, &r, wr.r3, label_tlbl_goaround2);
		}
		uasm_i_nop(&p);

		/*
		 * Warn if something may race with us & replace the TLB entry
		 * before we read it here. Everything with such races should
		 * also have dedicated RiXi exception handlers, so this
		 * shouldn't be hit.
		 */
		WARN(cpu_has_tlbex_tlbp_race(), "Unhandled race in RiXi path");

		uasm_i_tlbr(&p);

		if (cpu_has_mips_r2_exec_hazard)
			uasm_i_ehb(&p);

		/* Examine  entrylo 0 or 1 based on ptr. */
		if (use_bbit_insns()) {
			uasm_i_bbit0(&p, wr.r2, ilog2(sizeof(pte_t)), 8);
		} else {
			uasm_i_andi(&p, wr.r3, wr.r2, sizeof(pte_t));
			uasm_i_beqz(&p, wr.r3, 8);
		}
		/* load it in the delay slot*/
		UASM_i_MFC0(&p, wr.r3, C0_ENTRYLO0);
		/* load it if ptr is odd */
		UASM_i_MFC0(&p, wr.r3, C0_ENTRYLO1);
		/*
		 * If the entryLo (now in wr.r3) is valid (bit 1), RI or
		 * XI must have triggered it.
		 */
		if (use_bbit_insns()) {
			uasm_il_bbit0(&p, &r, wr.r3, 1, label_tlbl_goaround2);
		} else {
			uasm_i_andi(&p, wr.r3, wr.r3, 2);
			uasm_il_beqz(&p, &r, wr.r3, label_tlbl_goaround2);
		}
		if (PM_DEFAULT_MASK == 0)
			uasm_i_nop(&p);
		/*
		 * We clobbered C0_PAGEMASK, restore it.  On the other branch
		 * it is restored in build_huge_tlb_write_entry.
		 */
		build_restore_pagemask(&p, &r, wr.r3, label_nopage_tlbl, 0);

		uasm_l_tlbl_goaround2(&l, p);
	}
	uasm_i_ori(&p, wr.r1, wr.r1, (_PAGE_ACCESSED | _PAGE_VALID));
	build_huge_handler_tail(&p, &r, &l, wr.r1, wr.r2, 1);
#endif

	uasm_l_nopage_tlbl(&l, p);
	if (IS_ENABLED(CONFIG_CPU_LOONGSON3_WORKAROUNDS))
		uasm_i_sync(&p, 0);
	build_restore_work_registers(&p);
#ifdef CONFIG_CPU_MICROMIPS
	if ((unsigned long)tlb_do_page_fault_0 & 1) {
		uasm_i_lui(&p, GPR_K0, uasm_rel_hi((long)tlb_do_page_fault_0));
		uasm_i_addiu(&p, GPR_K0, GPR_K0, uasm_rel_lo((long)tlb_do_page_fault_0));
		uasm_i_jr(&p, GPR_K0);
	} else
#endif
	uasm_i_j(&p, (unsigned long)tlb_do_page_fault_0 & 0x0fffffff);
	uasm_i_nop(&p);

	if (p >= (u32 *)handle_tlbl_end)
		panic("TLB load handler fastpath space exceeded");

	uasm_resolve_relocs(relocs, labels);
	pr_debug("Wrote TLB load handler fastpath (%u instructions).\n",
		 (unsigned int)(p - (u32 *)handle_tlbl));

	dump_handler("r4000_tlb_load", handle_tlbl, handle_tlbl_end);
}
