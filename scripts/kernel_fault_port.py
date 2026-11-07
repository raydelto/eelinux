"""Prepare PS2 receive buffers for noncoherent DMA and retain useful Oops data."""
from pathlib import Path
import difflib
import subprocess


def adapt_sif(text):
    """Flush previous CPU ownership before allowing IOP DMA to overwrite pages."""
    pairs = (
        ('\treturn 0;\n}\n\nstatic int sif_request_cmds(void)',
         '\t/* Allocator reuse can leave dirty EE cache lines in this page.\n'
         '\t * Publish it to the IOP only after relinquishing CPU ownership.\n'
         '\t */\n'
         '\tdma_cache_wback_inv((unsigned long)sif0_buffer, SIF0_BUFFER_SIZE);\n'
         '\tdma_wmb();\n\treturn 0;\n}\n\nstatic int sif_request_cmds(void)'),
        ('\tif (client->client_buffer == NULL)\n\t\treturn -ENOMEM;\n',
         '\tif (client->client_buffer == NULL)\n\t\treturn -ENOMEM;\n'
         '\tdma_cache_wback_inv((unsigned long)client->client_buffer,\n'
         '\t\tclient->client_size_max);\n\tdma_wmb();\n'),
        ('\treinit_completion(&client->done);\n',
         '\t/* A reused receive page may still be dirty in the EE cache.\n'
         '\t * Preparing it after DMA has finished is too late. The whole\n'
         '\t * dedicated page is ours; no unrelated data shares its lines.\n'
         '\t */\n'
         '\tdma_cache_wback_inv((unsigned long)client->client_buffer,\n'
         '\t\tclient->client_size_max);\n\tdma_wmb();\n'
         '\treinit_completion(&client->done);\n'),
        ('\tif (recv_size > 0)\n\t\tdma_cache_inv(',
         '\tdma_rmb();\n\tif (recv_size > 0)\n\t\tdma_cache_inv('),
        ('\tdma_cache_inv((unsigned long)sif0_buffer, SIF_CMD_PACKET_MAX);',
         '\tdma_rmb();\n\tdma_cache_inv((unsigned long)sif0_buffer, SIF_CMD_PACKET_MAX);'),
    )
    for before, after in pairs:
        if text.count(before) != 1:
            raise RuntimeError('Review SIF DMA ownership anchor: '+before[:80])
        text = text.replace(before, after, 1)
    return text


def adapt_main(text):
    anchor = 'int __init_or_module do_one_initcall(initcall_t fn)'
    before = '\tdo_trace_initcall_start(fn);'
    if text.count(anchor) != 1 or text.count(before) != 1:
        raise RuntimeError('Review initcall diagnostic anchors')
    text = text.replace(anchor, '#ifdef CONFIG_CPU_R5900\n'
        '/* Keep the address, not init-only text, for the final Oops summary. */\n'
        'unsigned long ps2_last_initcall;\n#endif\n\n'+anchor, 1)
    text = text.replace(before, '#ifdef CONFIG_CPU_R5900\n'
        '\tWRITE_ONCE(ps2_last_initcall, (unsigned long)fn);\n#endif\n'+before, 1)
    anchor = 'static void __init do_basic_setup(void)\n{'
    if text.count(anchor) != 1:
        raise RuntimeError('Review boot identity anchor')
    return text.replace(anchor, anchor+'\n#ifdef CONFIG_CPU_R5900\n'
        '\tpr_info("ps2: DMA handoff and fault summary kernel\\n");\n#endif', 1)


def adapt_traps(text):
    before = '\tshow_code((void *)regs->cp0_epc, user_mode(regs));\n\tprintk("\\n");'
    if text.count(before) != 1:
        raise RuntimeError('Review final MIPS Oops summary anchor')
    helper = '''#ifdef CONFIG_CPU_R5900
extern unsigned long ps2_last_initcall;

static void ps2_fault_summary(const struct pt_regs *regs)
{
	/* Print after the long trace, so these lines survive console scrolling.
	 * Never dereference suspect pointers while reporting an exception.
	 */
	pr_emerg("ps2 fault : task=%s pid=%d\\n",
		 current->comm, task_pid_nr(current));
	pr_emerg("ps2 fault: epc=%08lx ra=%08lx\\n",
		 regs->cp0_epc, regs->regs[31]);
	pr_emerg("ps2 fault: bad=%08lx cause=%08lx\\n",
		 regs->cp0_badvaddr, regs->cp0_cause);
	pr_emerg("ps2 fault: a0=%08lx a1=%08lx\\n",
		 regs->regs[4], regs->regs[5]);
	pr_emerg("ps2 fault: a2=%08lx sp=%08lx\\n",
		 regs->regs[6], regs->regs[29]);
	pr_emerg("ps2 fault: EPC %pS\\n", (void *)regs->cp0_epc);
	pr_emerg("ps2 fault: RA %pS\\n", (void *)regs->regs[31]);
	pr_emerg("ps2 fault: last init %pS\\n",
		 (void *)READ_ONCE(ps2_last_initcall));
}
#endif

'''
    anchor = 'void show_registers(struct pt_regs *regs)'
    if text.count(anchor) != 1:
        raise RuntimeError('Review MIPS Oops entry')
    text = text.replace(anchor, helper+anchor, 1)
    return text.replace(before, before+'\n#ifdef CONFIG_CPU_R5900\n'
        '\tps2_fault_summary(regs);\n#endif', 1)


def install(source: Path, patch: Path) -> dict:
    adapters = (('drivers/ps2/sif.c', adapt_sif), ('init/main.c', adapt_main),
                ('arch/mips/kernel/traps.c', adapt_traps))
    changes = [(name, (source/name).read_text(), adapter((source/name).read_text()))
               for name, adapter in adapters]
    blob = ''.join(''.join(difflib.unified_diff(before.splitlines(keepends=True),
        after.splitlines(keepends=True), fromfile='a/'+name, tofile='b/'+name))
        for name, before, after in changes).encode()
    if patch.exists() and patch.read_bytes() != blob:
        raise RuntimeError('Preserve reviewed PS2 DMA/fault patch')
    patch.parent.mkdir(parents=True, exist_ok=True)
    patch.write_bytes(blob)
    subprocess.run(['git', 'apply', '--check', str(patch.resolve())], cwd=source, check=True)
    subprocess.run(['git', 'apply', str(patch.resolve())], cwd=source, check=True)
    return {'physical_panic_cause_confirmed': False,
            'DMA_fix': 'Receive pages flushed before IOP DMA writes, not only invalidated afterwards',
            'fault_summary': 'EPC/RA/BadVA/Cause/copy arguments/task/last initcall at bottom of Oops'}
