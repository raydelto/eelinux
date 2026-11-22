"""Fix the reviewed PS2 configuration's W=1 compiler and kernel-doc warnings."""
from pathlib import Path
import difflib
import subprocess
from common import same_patch_content, write_reviewed_patch


def once(text, before, after):
    if text.count(before) != 1:
        raise RuntimeError('Review kernel warning anchor: '+before[:100])
    return text.replace(before, after, 1)


def doc(text, function, before, after):
    anchor = text.index(function)
    start = text.rfind('/**', 0, anchor)
    end = text.index('*/', start)+2
    return text[:start]+once(text[start:end], before, after)+text[end:]


def adapt(name, text):
    if name == 'arch/mips/ps2/memory.c':
        return once(text, '#include <asm/mach-ps2/iop.h>',
                    '#include <asm/mach-ps2/iop.h>\n#include <asm/mach-ps2/memory.h>')
    if name == 'arch/mips/ps2/scmd.c':
        return once(text, ' * Tools such as hwclock should therefore set the clock in the UTC timescale.\n\n',
                    ' * Tools such as hwclock should therefore set the clock in the UTC timescale.\n *\n')
    if name == 'arch/mips/ps2/rom-sysfs.c':
        for value in ('rom_file_from_kobj(kobj->parent).extinfo.data', 'rom_file_from_kobj(kobj).data'):
            text = once(text, '\t\tvirt_to_phys('+value+'));',
                        '\t\t(unsigned long)virt_to_phys('+value+'));')
        return text
    if name == 'arch/mips/kernel/traps.c':
        start = text.index('asmlinkage void do_mcheck(')
        end = text.index('\nasmlinkage void do_mt(', start)
        body = once(text[start:end], '\tenum ctx_state prev_state;\n\n\tprev_state = exception_enter();',
                    '\n\texception_enter();')
        return text[:start]+body+text[end:]
    if name == 'kernel/sched/rt.c':
        return once(text, '/* More than 4 hours if BW_SHIFT equals 20. */\nstatic const u64 max_rt_runtime = MAX_BW;',
            '#if defined(CONFIG_RT_GROUP_SCHED) || defined(CONFIG_SYSCTL)\n'
            '/* More than 4 hours if BW_SHIFT equals 20. */\n'
            'static const u64 max_rt_runtime = MAX_BW;\n#endif')
    if name == 'net/ipv4/igmp.c':
        text = once(text, '\tint\tchangerec = 0;',
                    '#ifdef CONFIG_IP_MULTICAST\n\tint\tchangerec = 0;\n#endif')
        return once(text, '\t\tchangerec |= rv > 0;',
                    '#ifdef CONFIG_IP_MULTICAST\n\t\tchangerec |= rv > 0;\n#endif')
    if name == 'ipc/sem.c':
        return once(text, ' * perform_atomic_semop[_slow] - Attempt to perform semaphore',
                    ' * perform_atomic_semop_slow - Attempt to perform semaphore')
    if name == 'fs/inode.c':
        return doc(text, '\nstruct inode *inode_insert5(',
                   ' * @isnew:\tpointer to a bool which will indicate whether I_NEW is set\n', '')
    if name == 'block/blk-map.c':
        return doc(text, '\nstatic struct bio *bio_copy_kern(',
                   ' *\t@op: bio/request operation\n', '')
    if name == 'block/blk-mq-dma.c':
        return doc(text, '\nbool blk_rq_integrity_dma_map_iter_next(',
                   ' * @state:\tDMA IOVA state\n', '')
    if name == 'lib/rhashtable.c':
        text = doc(text, '\nint __rhashtable_init_noprof(',
                   ' * rhashtable_init - initialize a new hash table',
                   ' * __rhashtable_init - initialize a new hash table')
        text = doc(text, '\nint __rhashtable_init_noprof(',
                   ' * @params:\tconfiguration parameters',
                   ' * @params:\tconfiguration parameters\n * @key: lock class key for table locks')
        text = doc(text, '\nint __rhltable_init_noprof(',
                   ' * rhltable_init - initialize a new hash list table',
                   ' * __rhltable_init - initialize a new hash list table')
        return doc(text, '\nint __rhltable_init_noprof(',
                   ' * @params:\tconfiguration parameters',
                   ' * @params:\tconfiguration parameters\n * @key: lock class key for table locks')
    if name == 'fs/iomap/iter.c':
        return doc(text, '\nint iomap_iter_continue(',
                   ' * @srcmap: the source mapping that was just processed',
                   ' * @srcmap: the source mapping that was just processed\n * @ret: result of producing the preceding mapping')
    if name == 'drivers/video/fbdev/ps2fb.c':
        text = once(text, '\nvoid write_cb_environment(', '\nstatic void write_cb_environment(')
        text = once(text, '\nvoid ps2fb_cb_copyarea(', '\nstatic void ps2fb_cb_copyarea(')
        text = once(text, ' * @tile: tile dimensions',
                    ' * @cursor: cursor position, dimensions, visibility and foreground colour\n * @tile: tile dimensions')
        text = once(text, ' * @lock: spin lock to be taken for all structure operations',
            ' * @lock: spin lock to be taken for all structure operations\n'
            ' * @shadow_pages: RAM pages for the deferred framebuffer view\n'
            ' * @shadow_io: deferred I/O state for the shadow view\n'
            ' * @gs_font_blocks: GS blocks occupied by console fonts\n'
            ' * @gs_stats: native GS capability and submission statistics\n'
            ' * @gs_owned: ownership count for native graphics sessions\n'
            ' * @gs_epoch: graphics ownership generation\n'
            ' * @gs_device: native GS command device\n'
            ' * @shadow_maps: active shadow mappings\n'
            ' * @shadow_vm_original: lower mapping operations\n'
            ' * @shadow_vm_wrapped: mapping operations with ownership tracking')
        text = doc(text, '\nstatic u32 var_to_block_count(',
                   ' * @var: screen info object to compute the number of blocks for',
                   ' * @info: framebuffer info object to compute the number of blocks for')
        return doc(text, '\nstatic u32 display_buffer_size(',
                   ' * display_buffer_size - display buffer size for a given video resolution',
                   ' * display_buffer_size - display buffer size for a given video resolution\n'
                   ' * @xres_virtual: virtual width in pixels\n'
                   ' * @yres_virtual: virtual height in pixels\n'
                   ' * @bits_per_pixel: framebuffer pixel depth')
    if name == 'drivers/tty/serial/ps2_uart.c':
        return once(text, '\nvoid __exit ps2_uart_exit(', '\nstatic void __exit ps2_uart_exit(')
    if name == 'drivers/ps2/gs.c':
        for parameter in ('t1248', 'lc', 'rc'):
            text = once(text, ' * @'+parameter+' - ', ' * @'+parameter+': ')
        return doc(text, '\nu32 gs_rfsh_from_synch_gen(',
                   ' * gs_rfsh_from_synch_gen - DRAM refresh for the given synchronization registers',
                   ' * gs_rfsh_from_synch_gen - DRAM refresh for the given synchronization registers\n'
                   ' * @sg: synchronization register values')
    if name == 'drivers/ps2/sif.c':
        text = once(text, '\nstatic bool sif_ready;\n', '\n')
        text = once(text, '/**\n * sif_init - initialise the SIF with a reset',
                    'static bool sif_ready;\n\n/**\n * sif_init - initialise the SIF with a reset')
        start = text.index('bool sif_is_ready(void)')
        end = text.index('static int __init sif_init(void)', start)
        ready = text[start:end]
        text = text[:start]+text[end:]
        return once(text, 'static bool sif_ready;\n', 'static bool sif_ready;\n\n'+ready)
    if name == 'drivers/ps2/iop-module-request.c':
        return once(text, ' * @version_request: requested version', ' * @requested_version: requested version')
    if name == 'arch/mips/include/asm/mach-ps2/iop-module.h':
        return once(text, 'int iop_module_request(const char *name, int version, const char *arg);',
            'int iop_module_request(const char *name, int version, const char *arg);\n'
            'int iop_module_load_rom_arg(const char *filepath, const char *arg);\n'
            'int iop_module_load_rom(const char *filepath);')
    if name == 'drivers/ps2/iop-irq.c':
        for parameter in ('iop', 'map', 'rpc'):
            text = once(text, ' * @u8 '+parameter+':', ' * @'+parameter+':')
        return text
    if name == 'drivers/ps2/iop-module-sysfs.c':
        text = once(text, '\nconst struct iop_module_info *iop_module_from_kobj(',
                    '\nstatic const struct iop_module_info *iop_module_from_kobj(')
        return once(text, 'MODULE_LICENSE("GPL");',
                    'MODULE_DESCRIPTION("PlayStation 2 IOP firmware module information");\nMODULE_LICENSE("GPL");')
    if name == 'drivers/ps2/iop-dev9.c':
        return once(text, 'MODULE_LICENSE("GPL");',
                    'MODULE_DESCRIPTION("PlayStation 2 DEV9 IOP module loader");\nMODULE_LICENSE("GPL");')
    if name == 'drivers/ata/pata_ps2.c':
        return once(text, ' * enum iop_ata_ops - IOP ATA remote operations',
                    ' * enum iop_ata_rops - IOP ATA remote operations')
    raise RuntimeError('Unreviewed warning file: '+name)


PATHS = ('arch/mips/ps2/memory.c', 'arch/mips/ps2/scmd.c', 'arch/mips/ps2/rom-sysfs.c', 'arch/mips/kernel/traps.c',
         'kernel/sched/rt.c', 'net/ipv4/igmp.c', 'ipc/sem.c', 'fs/inode.c',
         'block/blk-map.c', 'block/blk-mq-dma.c', 'lib/rhashtable.c', 'fs/iomap/iter.c',
         'drivers/video/fbdev/ps2fb.c', 'drivers/tty/serial/ps2_uart.c', 'drivers/ps2/gs.c',
         'drivers/ps2/sif.c', 'drivers/ps2/iop-module-request.c',
         'arch/mips/include/asm/mach-ps2/iop-module.h', 'drivers/ps2/iop-irq.c',
         'drivers/ps2/iop-module-sysfs.c', 'drivers/ps2/iop-dev9.c', 'drivers/ata/pata_ps2.c')


def install(source: Path, patch: Path):
    changes = [(name, (source/name).read_text(), adapt(name, (source/name).read_text())) for name in PATHS]
    header = 'arch/mips/include/asm/mach-ps2/memory.h'
    assert not (source/header).exists()
    changes.append((header, '', '// SPDX-License-Identifier: GPL-2.0\n'
        '#ifndef __ASM_MACH_PS2_MEMORY_H\n#define __ASM_MACH_PS2_MEMORY_H\n\n'
        '#include <linux/types.h>\n\n'
        'int valid_phys_addr_range(phys_addr_t addr, size_t size);\n'
        'int valid_mmap_phys_addr_range(unsigned long pfn, size_t size);\n\n#endif\n'))
    blob = ''.join(''.join(difflib.unified_diff(before.splitlines(keepends=True), after.splitlines(keepends=True),
        fromfile='a/'+name if before else '/dev/null', tofile='b/'+name)) for name, before, after in changes).encode()
    if patch.exists() and not same_patch_content(patch.read_bytes(), blob):
        raise RuntimeError('Preserve reviewed warning patch')
    patch.parent.mkdir(parents=True, exist_ok=True); write_reviewed_patch(patch, blob)
    subprocess.run(['git', 'apply', '--check', str(patch.resolve())], cwd=source, check=True)
    subprocess.run(['git', 'apply', str(patch.resolve())], cwd=source, check=True)


def export_applied(baseline: Path, source: Path, patch: Path):
    changes = []
    for name in (*PATHS, 'arch/mips/include/asm/mach-ps2/memory.h'):
        before = (baseline/name).read_text() if (baseline/name).exists() else ''
        after = (source/name).read_text()
        changes.append((name, before, after))
    blob = ''.join(''.join(difflib.unified_diff(before.splitlines(keepends=True), after.splitlines(keepends=True),
        fromfile='a/'+name if before else '/dev/null', tofile='b/'+name)) for name, before, after in changes).encode()
    patch.write_bytes(blob)
    subprocess.run(['git', 'apply', '--check', str(patch.resolve())], cwd=baseline, check=True)
