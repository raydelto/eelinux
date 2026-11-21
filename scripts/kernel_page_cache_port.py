"""Preserve the PS2 clean-page writeback contract with modern folio APIs."""
from pathlib import Path
import difflib
import subprocess
from common import same_patch_content, write_reviewed_patch


def replace_once(text, before, after):
    if text.count(before) != 1:
        raise RuntimeError('Review R5900 page-cache anchor: ' + before[:80])
    return text.replace(before, after, 1)


def adapt_cache(text):
    if 'R5900 kernel writes need writeback even without PG_dcache_dirty' in text:
        raise RuntimeError('R5900 page-cache contract already installed')
    # kmap_local_folio takes a byte offset, not a page index. Flush each page
    # in the folio once instead of repeatedly flushing the first page.
    text = replace_once(text, 'kmap_local_folio(folio, i);',
                        'kmap_local_folio(folio, (size_t)i * PAGE_SIZE);')
    anchor = '\t\tfolio_clear_dcache_dirty(folio);\n\t}\n}\n'
    return replace_once(text, anchor, '''\t\tfolio_clear_dcache_dirty(folio);
\t} else if (IS_ENABLED(CONFIG_CPU_R5900) && !cpu_has_ic_fills_f_dc) {
\t\t/* R5900 kernel writes need writeback even without PG_dcache_dirty.
\t\t * Retain the original PS2 port's clean-page path: executable and
\t\t * read-only pages can have been copied through a kernel mapping.
\t\t * Flush the PTE's page; unlike the dirty path this has no folio-wide
\t\t * flag to clear. Other CPU families retain their existing policy.
\t\t */
\t\taddr = (unsigned long)kmap_local_page(pfn_to_page(pfn));
\t\tflush_data_cache_page(addr);
\t\tkunmap_local((void *)addr);
\t}
}
''')


def adapt_interrupt(text):
    anchor = '\t.macro\tBUILD_SKIPOVER_PROLOGUE handler\n'
    start = text.index(anchor) if text.count(anchor) == 1 else -1
    if start < 0:
        raise RuntimeError('Review modern interrupt skipover prologue')
    end = text.index('\t.endm\n', start) + len('\t.endm\n')
    block = text[start:end]
    if 'CONFIG_CPU_R5900' in block:
        raise RuntimeError('R5900 skipover guards already installed')
    block = replace_once(block, '\tMFC0\tk0, CP0_EPC\n',
        '#ifdef CONFIG_CPU_R5900\n\tsync.p\n#endif\n\tMFC0\tk0, CP0_EPC\n')
    block = replace_once(block, '\tMTC0\tk0, CP0_EPC\n',
        '\tMTC0\tk0, CP0_EPC\n#ifdef CONFIG_CPU_R5900\n\tsync.p\n#endif\n')
    return text[:start] + block + text[end:]


def adapt_identity(text):
    return replace_once(text, 'ps2: exception guards and video modes kernel',
                        'ps2: page-cache writeback kernel')


def install(source: Path, patch: Path):
    adapters = (('arch/mips/mm/cache.c', adapt_cache),
                ('arch/mips/kernel/genex.S', adapt_interrupt),
                ('init/main.c', adapt_identity))
    changes = [(name, (source/name).read_text(), fn((source/name).read_text()))
               for name, fn in adapters]
    blob = ''.join(''.join(difflib.unified_diff(before.splitlines(keepends=True),
        after.splitlines(keepends=True), fromfile='a/'+name, tofile='b/'+name))
        for name, before, after in changes).encode()
    if patch.exists() and not same_patch_content(patch.read_bytes(), blob):
        raise RuntimeError('Preserve reviewed R5900 page-cache patch')
    patch.parent.mkdir(parents=True, exist_ok=True)
    write_reviewed_patch(patch, blob)
    subprocess.run(['git','apply','--check',str(patch.resolve())],cwd=source,check=True)
    subprocess.run(['git','apply',str(patch.resolve())],cwd=source,check=True)
    return {'clean_PTE_page_writeback_restored':True,
            'folio_mapping_uses_byte_offsets':True,
            'interrupt_skipover_CP0_guards_restored':True,
            'physical_loader_fix_confirmed':False,
            'reference':'frno7/linux 77174dd4c8958a91aa5b76fb064a60eb68119e70 arch/mips/mm/cache.c and arch/mips/kernel/genex.S'}
