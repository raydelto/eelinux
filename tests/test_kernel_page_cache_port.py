"""Exercise cache visibility and all folio pages through the actual MM hook."""
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
from kernel_page_cache_port import adapt_cache, adapt_interrupt


class PageCachePort(unittest.TestCase):
    def setUp(self):
        self.cache = (ROOT / 'tests/fixtures/mips-update-cache.c').read_text()
        self.interrupt = (ROOT / 'tests/fixtures/mips-skipover.S').read_text()

    def test_changed_duplicated_and_reapplied_sources_fail_closed(self):
        for text, adapter, anchor in (
            (self.cache, adapt_cache, 'kmap_local_folio(folio, i);'),
            (self.interrupt, adapt_interrupt, 'BUILD_SKIPOVER_PROLOGUE'),
        ):
            for bad in (text + text, text.replace(anchor, 'changed'), adapter(text)):
                with self.subTest(adapter=adapter.__name__), self.assertRaises(RuntimeError):
                    adapter(bad)

    @unittest.skipUnless(shutil.which('cc'), 'requires native C compiler')
    def test_clean_page_visibility_and_distinct_large_folio_pages(self):
        harness = r'''
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#define PAGE_SHIFT 12
#define PAGE_SIZE 4096UL
#define PAGE_MASK (~(PAGE_SIZE-1))
#define IS_ENABLED(x) (x)
#define unlikely(x) (x)
typedef struct { unsigned long pfn; int noexec; } pte_t;
struct page { unsigned index; };
struct folio { unsigned nr, first; int dirty; };
static struct page pages[8];
static struct folio f;
static unsigned char kernel_copy[8][4096], user_copy[8][4096];
static unsigned mapped, unmapped, flushed, offsets[8], flush_pages[8];
static int cpu_has_ic_fills_f_dc, aliases;
static unsigned long pte_pfn(pte_t pte) { return pte.pfn; }
static int pte_no_exec(pte_t pte) { return pte.noexec; }
static int pfn_valid(unsigned long pfn) { return pfn < 8; }
static struct page *pfn_to_page(unsigned long pfn) { return pages+pfn; }
static struct folio *page_folio(struct page *p) { assert(p->index<8); return &f; }
static unsigned long offset_in_folio(struct folio *folio,unsigned long pa) {
    assert(pa/PAGE_SIZE>=folio->first && pa/PAGE_SIZE<folio->first+folio->nr);
    return pa-folio->first*PAGE_SIZE;
}
static int folio_test_dcache_dirty(struct folio *folio) { return folio->dirty; }
static void folio_clear_dcache_dirty(struct folio *folio) { folio->dirty=0; }
static unsigned folio_nr_pages(struct folio *folio) { return folio->nr; }
static void *kmap_local_page(struct page *p) { mapped++; return kernel_copy[p->index]; }
static void *kmap_local_folio(struct folio *folio,size_t offset) {
    assert(mapped<8 && offset<folio->nr*PAGE_SIZE);
    offsets[mapped]=offset;
    return kmap_local_page(pages+folio->first+offset/PAGE_SIZE)+offset%PAGE_SIZE;
}
static void kunmap_local(void *p) { assert(p); unmapped++; }
static int pages_do_alias(unsigned long a,unsigned long b) { (void)a;(void)b;return aliases; }
static void flush_data_cache_page(unsigned long addr) {
    for(unsigned i=0;i<8;i++) if(addr==(unsigned long)kernel_copy[i]) {
        memcpy(user_copy[i],kernel_copy[i],PAGE_SIZE);
        flush_pages[flushed++]=i; return;
    }
    assert(!"flush must target a page base");
}
static void reset(unsigned first,unsigned nr,int dirty) {
    f=(struct folio){nr,first,dirty};
    mapped=unmapped=flushed=0;cpu_has_ic_fills_f_dc=0;aliases=0;
    for(unsigned i=0;i<8;i++) {
        pages[i].index=i;
        memset(kernel_copy[i],i+1,PAGE_SIZE);memset(user_copy[i],0,PAGE_SIZE);
    }
}
''' + adapt_cache(self.cache) + r'''
int main(void) {
    /* New read-only PTE, kernel writes, dirty flag already clear: the PS2
       must make the bytes visible before the user can load the page. */
    reset(2,4,0); __update_cache(0x77e00000,(pte_t){4,1});
#if CONFIG_CPU_R5900
    assert(flushed==1 && flush_pages[0]==4 && user_copy[4][615]==5);
    assert(mapped==1 && unmapped==1 && f.dirty==0);
    assert(user_copy[2][0]==0 && user_copy[3][0]==0 && user_copy[5][0]==0);
#else
    assert(flushed==0 && mapped==0 && user_copy[4][615]==0);
#endif
    /* Dirty four-page folio, executable tail PTE: each distinct page needs
       writeback. Byte offset mistakes leave pages 2..4 invisible. */
    reset(1,4,1); __update_cache(0x77e03000,(pte_t){4,0});
    assert(flushed==4 && mapped==4 && unmapped==4 && !f.dirty);
    for(unsigned i=0;i<4;i++) {
        assert(offsets[i]==i*PAGE_SIZE && flush_pages[i]==i+1);
        assert(user_copy[i+1][267]==i+2);
    }
    reset(1,4,1);cpu_has_ic_fills_f_dc=1;aliases=0;
    __update_cache(0x77e03000,(pte_t){4,1});
    assert(flushed==0 && mapped==4 && unmapped==4 && !f.dirty);
    reset(1,4,1);aliases=1;
    __update_cache(0x77e03000,(pte_t){4,1});assert(flushed==4 && !f.dirty);
    reset(1,4,0);cpu_has_ic_fills_f_dc=1;
    __update_cache(0x77e03000,(pte_t){4,0});assert(flushed==0 && mapped==0);
    reset(0,1,1);__update_cache(0x77e00000,(pte_t){8,0});
    assert(flushed==0 && mapped==0 && f.dirty==1);
    return 0;
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            source=Path(tmp)/'cache.c'; exe=Path(tmp)/'cache'
            source.write_text(harness)
            for cpu in ('0','1'):
                subprocess.run(['cc','-std=gnu11','-Wall','-Wextra','-Werror',
                    '-fsanitize=address,undefined','-DCONFIG_CPU_R5900='+cpu,
                    str(source),'-o',str(exe)],check=True)
                subprocess.run([str(exe)],check=True)


if __name__ == '__main__':
    unittest.main()
