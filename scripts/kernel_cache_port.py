"""Focused R5900 cache adapter retaining the Linux 7.2.8 helper infrastructure."""
import re


def modernize_r4kcache(text, baseline, pristine):
    expected = pristine.replace(
        "#define __BUILD_BLAST_CACHE_RANGE(pfx, desc, hitop, prot, extra)",
        "#define __BUILD_BLAST_CACHE_RANGE(fn_pfx, pfx, desc, hitop, prot, extra)")
    expected = expected.replace("\t\tprot##cache_op(hitop, addr);\t\t\t\t\\",
                                "\t\tprot##cache_op##fn_pfx(hitop, addr);\t\t\t\\")
    if text != expected:
        raise RuntimeError("Cache header differs from the verified 7.2.8 partial transplant; review source edits")

    def replace(before, after, count=1):
        nonlocal text
        if text.count(before) != count:
            raise RuntimeError(f"Inspect 7.2.8 cache helper before adapting: {before[:90]}")
        text = text.replace(before, after)

    # The existing fork requires bit31 clear for indexed instruction-cache ops.
    replace("#define INDEX_BASE\tCKSEG0", "#ifdef CONFIG_CPU_R5900\n"
            "/* R5900 instruction-index operations require address bit31 clear. */\n"
            "#define INDEX_BASE\t0\n#else\n#define INDEX_BASE\tCKSEG0\n#endif")

    # Keep the modern operand/address/ISA machinery and make synchronization
    # part of the same noreorder assembly block as the cache instruction.
    anchor = "static inline void flush_icache_line_indexed(unsigned long addr)"
    scalar = r'''#ifdef CONFIG_CPU_R5900
#define r5900_kernel_cache_d(op, base) \
        "sync.l\n" kernel_cache(op, base) "sync.l\n"
#define r5900_kernel_cache_i(op, base) \
        "sync.p\n" kernel_cache(op, base) "sync.p\n"
#define cache_op_d(op, addr) _cache_op(r5900_kernel_cache_d, op, addr)
#define cache_op_i(op, addr) _cache_op(r5900_kernel_cache_i, op, addr)
#else
#define cache_op_d cache_op
#define cache_op_i cache_op
#endif
#define cache_op_s cache_op
#define cache_op_t cache_op

'''
    replace(anchor, scalar + anchor)

    # The old protected helpers did not return faults. Derive R5900 variants from
    # the modern non-EVA helper, preserving its .fixup target and -EFAULT return.
    start = text.index("#define protected_cache_op(op, addr)", text.index("#else\n\n#define protected_cache_op"))
    end = text.index("\n})", start) + len("\n})")
    protected = text[start:end]
    protected = protected.replace("protected_cache_op(op, addr)", "__r5900_protected_cache_op(sync_insn, op, addr)", 1)
    cache_line = '\t"1:\tcache\t%1, (%2)\t\t\\n"\t\t\\\n'
    if cache_line not in protected:
        raise RuntimeError("Modern protected-cache assembly layout changed; preserve its exception fixup")
    protected = protected.replace(cache_line,
                                  '\tsync_insn "\\n"\t\t\t\t\t\\\n' + cache_line +
                                  '\tsync_insn "\\n"\t\t\t\t\t\\\n', 1)
    protected += '''
#define protected_cache_op_d(op, addr) __r5900_protected_cache_op("sync.l", op, addr)
#define protected_cache_op_i(op, addr) __r5900_protected_cache_op("sync.p", op, addr)
'''
    protected = ("#if defined(CONFIG_CPU_R5900) && !defined(CONFIG_EVA)\n" + protected +
                 "#else\n#define protected_cache_op_d protected_cache_op\n"
                 "#define protected_cache_op_i protected_cache_op\n#endif\n"
                 "#define protected_cache_op_s protected_cache_op\n\n")
    anchor = "/*\n * The next two are for badland addresses like signal trampolines.\n */"
    replace(anchor, protected + anchor)

    # Route scalar operations by their original cache type, without changing
    # the existing modern generic helper definition or EVA implementation.
    operations = {
        "i": ["Index_Invalidate_I", "Hit_Invalidate_I", "Hit_Invalidate_I_Loongson2"],
        "d": ["Index_Writeback_Inv_D", "Hit_Writeback_Inv_D", "Hit_Invalidate_D"],
        "s": ["Index_Writeback_Inv_SD", "Hit_Writeback_Inv_SD", "Hit_Invalidate_SD"],
        "t": ["Page_Invalidate_T"],
    }
    for kind, names in operations.items():
        for name in names:
            text = text.replace(f"cache_op({name},", f"cache_op_{kind}({name},")
    # Above also adapts protected_cache_op(...) by its cache-operation suffix.

    # Preserve exact fork sequences for the only R5900 line size (64 bytes):
    # D has sync.l before the batch and after every cache op; I has sync.p
    # before/after the complete batch. Modern generic/node/user helpers remain.
    start = baseline.index("#ifdef CONFIG_CPU_R5900\n#define cache64_unroll32_d")
    end = baseline.index("#else\n#define cache64_unroll32_i", start)
    unroll = baseline[start:end]
    unroll += r'''#define cache_unroll32_d(op, addr, lsize) do { \
        if ((lsize) == 64) { \
                cache64_unroll32_d(addr, op); \
        } else { \
                cache_unroll(32, kernel_cache, op, addr, lsize); \
        } \
} while (0)
#define cache_unroll32_i(op, addr, lsize) do { \
        if ((lsize) == 64) { \
                cache64_unroll32_i(addr, op); \
        } else { \
                cache_unroll(32, kernel_cache, op, addr, lsize); \
        } \
} while (0)
#else
#define cache_unroll32_d(op, addr, lsize) cache_unroll(32, kernel_cache, op, addr, lsize)
#define cache_unroll32_i(op, addr, lsize) cache_unroll(32, kernel_cache, op, addr, lsize)
#endif
#define cache_unroll32_s(op, addr, lsize) cache_unroll(32, kernel_cache, op, addr, lsize)

'''
    anchor = "/* build blast_xxx, blast_xxx_page, blast_xxx_page_indexed */"
    replace(anchor, unroll + anchor)
    replace("#define __BUILD_BLAST_CACHE(pfx, desc, indexop, hitop, lsize, extra)",
            "#define __BUILD_BLAST_CACHE(fn_pfx, pfx, desc, indexop, hitop, lsize, extra)")
    # Limit edits to this generator, preserving modern user/node generators.
    start = text.index("#define __BUILD_BLAST_CACHE(fn_pfx,")
    end = text.index("#define __BUILD_BLAST_USER_CACHE", start)
    block = text[start:end]
    block = block.replace("cache_unroll(32, kernel_cache, indexop,", "cache_unroll32##fn_pfx(indexop,")
    block = block.replace("cache_unroll(32, kernel_cache, hitop,", "cache_unroll32##fn_pfx(hitop,")
    block = re.sub(r"^__BUILD_BLAST_CACHE\(([^,]+), (dcache|icache|scache),",
                   lambda m: f"__BUILD_BLAST_CACHE(_{m[2][0]}, {m[1]}, {m[2]},", block, flags=re.M)
    text = text[:start] + block + text[end:]

    # A six-argument generator was the only cleanly applied old header hunk.
    if "#define __BUILD_BLAST_CACHE_RANGE(fn_pfx, pfx, desc, hitop, prot, extra)" not in text:
        raise RuntimeError("Expected partial six-argument PS2 cache-range generator is absent")
    text = re.sub(r"^__BUILD_BLAST_CACHE_RANGE\(([^,]+), (dcache|icache|scache),",
                  lambda m: f"__BUILD_BLAST_CACHE_RANGE(_{m[2][0]}, {m[1]}, {m[2]},", text, flags=re.M)
    return text
