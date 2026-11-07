/* SPDX-License-Identifier: GPL-2.0-only
 * Extracted from Linux 7.2.8 arch/mips/mm/cache.c; tested with model APIs.
 */
void __update_cache(unsigned long address, pte_t pte)
{
	struct folio *folio;
	unsigned long pfn, addr;
	int exec = !pte_no_exec(pte) && !cpu_has_ic_fills_f_dc;
	unsigned int i;

	pfn = pte_pfn(pte);
	if (unlikely(!pfn_valid(pfn)))
		return;

	folio = page_folio(pfn_to_page(pfn));
	address &= PAGE_MASK;
	address -= offset_in_folio(folio, pfn << PAGE_SHIFT);

	if (folio_test_dcache_dirty(folio)) {
		for (i = 0; i < folio_nr_pages(folio); i++) {
			addr = (unsigned long)kmap_local_folio(folio, i);

			if (exec || pages_do_alias(addr, address))
				flush_data_cache_page(addr);
			kunmap_local((void *)addr);
			address += PAGE_SIZE;
		}
		folio_clear_dcache_dirty(folio);
	}
}

