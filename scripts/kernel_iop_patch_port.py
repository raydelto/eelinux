"""Keep live IOP memory out of the EE cache while patching LOADFILE."""
from pathlib import Path
import difflib
import subprocess


def replace_once(text, before, after):
    if text.count(before) != 1:
        raise RuntimeError('Review IOP patch anchor: ' + before[:100])
    return text.replace(before, after, 1)


def adapt_memory(text):
    text = replace_once(text, '#include <linux/dma-mapping.h>',
                        '#include <asm/addrspace.h>')
    return replace_once(text,
        '\treturn phys_to_virt(iop_bus_to_phys(baddr));',
        '\t/* The IOP owns this RAM. Cached EE aliases can retain stale data\n'
        '\t * and write back neighbours modified by the running IOP.\n'
        '\t */\n'
        '\treturn (void *)CKSEG1ADDR(iop_bus_to_phys(baddr));')


def adapt_patch(text):
    text = replace_once(text, '#include <asm/io.h>\t/* FIXME: For dma_cache_wback */',
                        '#include <asm/io.h>')
    text = replace_once(text,
        '\t\tconst u32 *code = iop_bus_to_virt(',
        '\t\tconst void __iomem *code = iop_bus_to_virt(')
    text = replace_once(text, '\t\tif (match[i].code != *code)',
                        '\t\tif (match[i].code != readl(code))')
    text = replace_once(text,
        '\tconst struct i_format *insn = iop_bus_to_virt(\n'
        '\t\tmodule->text_start + address);\n\n\treturn insn->simmediate;',
        '\tconst void __iomem *insn = iop_bus_to_virt(\n'
        '\t\tmodule->text_start + address);\n\n\treturn (s16)readl(insn);')
    text = replace_once(text,
        '\tstruct u_format *sltiu = iop_bus_to_virt(',
        '\tvoid __iomem *sltiu = iop_bus_to_virt(')
    text = replace_once(text,
        '\tu32 *patch_entry = iop_bus_to_virt(jump_table + 6 * sizeof(u32));\n\n'
        '\t*patch_entry = patch_address;\t/* Assign 7:th jump table entry. */\n'
        '\tsltiu->uimmediate++;\t/* Increase jump table from 6 to 7 entries. */',
        '\tvoid __iomem *patch_entry =\n'
        '\t\tiop_bus_to_virt(jump_table + 6 * sizeof(u32));\n'
        '\tu32 instruction = readl(sltiu);\n\n'
        '\t/* Publish the code and seventh entry before enabling RPC 6.\n'
        '\t * Word stores never write back a neighbouring IOP cache line.\n'
        '\t */\n'
        '\twritel(patch_address, patch_entry);\n\twmb();\n'
        '\twritel((instruction & 0xffff0000) |\n'
        '\t\t((instruction + 1) & 0xffff), sltiu);\n'
        '\t(void)readl(sltiu);')
    text = replace_once(text,
        '\tconst u32 result_address = patch_address + 24 * sizeof(u32);',
        '\tconst u32 result_address = patch_address + 24 * sizeof(u32);\n'
        '\tvoid __iomem *destination = iop_bus_to_virt(patch_address);\n'
        '\tsize_t i;')
    text = replace_once(text,
        '\tmemcpy(iop_bus_to_virt(patch_address), &patch, sizeof(patch));',
        '\tfor (i = 0; i < ARRAY_SIZE(patch.word); i++)\n'
        '\t\twritel(patch.word[i], destination + i * sizeof(u32));\n'
        '\twmb();\n'
        '\t(void)readl(destination + sizeof(patch) - sizeof(u32));')
    return replace_once(text,
        '\t/* FIXME: Write back or use KSEG1? */\n'
        '\tdma_cache_wback((unsigned long)iop_bus_to_virt(0), 2*1024*1024);',
        '\t/* All writes used the uncached IOP window; no EE cache flush. */')


def adapt_loader(text):
    if 'IOP firmware: ROM ADDDRV:' in text:
        raise RuntimeError('ROM loader checkpoints already applied')
    return replace_once(text,
        '\terr = iop_module_load_rom("rom0:ADDDRV");',
        '\tpr_info("IOP firmware: ROM ADDDRV: start on IOP\\n");\n'
        '\terr = iop_module_load_rom("rom0:ADDDRV");\n'
        '\tpr_info("IOP firmware: ROM ADDDRV: start result %d\\n", err);')


def install(source: Path, patch: Path):
    adapters = (('drivers/ps2/iop-memory.c', adapt_memory),
                ('drivers/ps2/iop-module-patch.c', adapt_patch),
                ('drivers/ps2/iop-module-request.c', adapt_loader))
    changes = [(name, (source/name).read_text(), fn((source/name).read_text()))
               for name, fn in adapters]
    blob = ''.join(''.join(difflib.unified_diff(before.splitlines(keepends=True),
        after.splitlines(keepends=True), fromfile='a/'+name, tofile='b/'+name))
        for name, before, after in changes).encode()
    if patch.exists() and patch.read_bytes() != blob:
        raise RuntimeError('Preserve reviewed uncached IOP patch')
    patch.parent.mkdir(parents=True, exist_ok=True)
    patch.write_bytes(blob)
    subprocess.run(['git', 'apply', '--check', str(patch.resolve())], cwd=source, check=True)
    subprocess.run(['git', 'apply', str(patch.resolve())], cwd=source, check=True)
    return {'IOP_mapping': 'uncached KSEG1', 'LOADFILE_patch': 'ordered 32-bit I/O stores',
            'physical_stall_cause_confirmed': False}
