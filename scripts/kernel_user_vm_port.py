"""Restore missing R5900 exception/TLB guards and retain PID 1 user faults."""
from pathlib import Path
import difflib
import subprocess
from common import same_patch_content, write_reviewed_patch


def replace_once(text, before, after):
    if text.count(before) != 1:
        raise RuntimeError('Review R5900 user VM anchor: ' + before[:80])
    return text.replace(before, after, 1)


def adapt_tlb(text):
    if 'R5900 TLB refill handler space exceeded' in text:
        raise RuntimeError('R5900 TLB guards already installed')
    before = '\tmemset(final_handler, 0, sizeof(final_handler));\n'
    text = replace_once(text, before, before + '''
#ifdef CONFIG_CPU_R5900
	/* Workaround for R5900 exception execution bug (FLX05). */
	uasm_i_nop(&p);
	uasm_i_nop(&p);
#endif
''')
    before = '\tswitch (boot_cpu_type()) {\n\tdefault:\n\t\tif (sizeof(long) == 4) {'
    text = replace_once(text, before, '''	/* The EE refill vector has 32 slots, including all erratum padding. */
	if (boot_cpu_type() == CPU_R5900 && (p - tlb_handler) > 32)
		panic("R5900 TLB refill handler space exceeded");

''' + before)
    before = '\t\tuasm_i_tlbr(&p);\n\n\t\tif (cpu_has_mips_r2_exec_hazard)'
    if text.count(before) != 2:
        raise RuntimeError('Review both modern TLB read hazard paths')
    return text.replace(before, '''		uasm_i_tlbr(&p);
#ifdef CONFIG_CPU_R5900
		/* TLBR must be followed immediately by ERET or SYNC.P on EE. */
		uasm_i_syncp(&p);
		uasm_i_nop(&p);
		uasm_i_nop(&p);
		uasm_i_nop(&p);
		uasm_i_nop(&p);
#endif

		if (cpu_has_mips_r2_exec_hazard)''')


def adapt_vectors(text):
    if 'Do not expose cache-like words after the exception-vector jump' in text:
        raise RuntimeError('R5900 interrupt vector guards already installed')
    before = '\t\tu32 *buf = (u32 *)(ebase + 0x200);\n'
    text = replace_once(text, before, before + '''#ifdef CONFIG_CPU_R5900
		/* Workaround for R5900 exception execution bug (FLX05). */
		uasm_i_nop(&buf);
		uasm_i_nop(&buf);
#endif
''')
    before = '\t\tlocal_flush_icache_range(ebase + 0x200, (unsigned long)buf);'
    return replace_once(text, before, '''#ifdef CONFIG_CPU_R5900
		/* Do not expose cache-like words after the exception-vector jump. */
		uasm_i_nop(&buf);
		uasm_i_nop(&buf);
		uasm_i_nop(&buf);
		uasm_i_nop(&buf);
		uasm_i_nop(&buf);
#endif
''' + before)


USER_FAULT_HELPER = r'''#ifdef CONFIG_CPU_R5900
static void ps2_user_fault_summary(const struct pt_regs *regs,
		unsigned long write, unsigned long address)
{
	/* PID 1 death panics. Keep its user-mode registers below the long logs.
	 * Do not read suspect user memory while reporting a damaged process.
	 */
	pr_emerg("ps2 user fault: task=%s pid=%d %s=%08lx\n",
		 current->comm, task_pid_nr(current), write ? "write" : "read", address);
	pr_emerg("ps2 user fault: epc=%08lx ra=%08lx cause=%08lx\n",
		 regs->cp0_epc, regs->regs[31], regs->cp0_cause);
	pr_emerg("ps2 user fault: s0=%08lx s1=%08lx s2=%08lx s3=%08lx\n",
		 regs->regs[16], regs->regs[17], regs->regs[18], regs->regs[19]);
	pr_emerg("ps2 user fault: s4=%08lx s5=%08lx s6=%08lx s7=%08lx\n",
		 regs->regs[20], regs->regs[21], regs->regs[22], regs->regs[23]);
	pr_emerg("ps2 user fault: v0=%08lx v1=%08lx a0=%08lx a1=%08lx\n",
		 regs->regs[2], regs->regs[3], regs->regs[4], regs->regs[5]);
	pr_emerg("ps2 user fault: a2=%08lx a3=%08lx t0=%08lx t1=%08lx\n",
		 regs->regs[6], regs->regs[7], regs->regs[8], regs->regs[9]);
	pr_emerg("ps2 user fault: sp=%08lx gp=%08lx status=%08lx\n",
		 regs->regs[29], regs->regs[28], regs->cp0_status);
}
#endif

'''


def adapt_fault(text):
    if 'ps2_user_fault_summary' in text:
        raise RuntimeError('R5900 user fault summary already installed')
    text = replace_once(text, 'static void __do_page_fault(',
                        USER_FAULT_HELPER + 'static void __do_page_fault(')
    for signal, code, indent in (('SIGSEGV', 'si_code', '\t\t'), ('SIGBUS', 'BUS_ADRERR', '\t')):
        before = indent + f'force_sig_fault({signal}, {code}, (void __user *)address);'
        text = replace_once(text, before, '''#ifdef CONFIG_CPU_R5900
''' + indent + '''if (task_pid_nr(tsk) == 1)
''' + indent + '''	ps2_user_fault_summary(regs, write, address);
#endif
''' + before)
    return text


def adapt_identity(text):
    return replace_once(text, 'ps2: aligned firmware and SIF startup kernel',
        'ps2: exception and TLB-entry kernel')


def install(source: Path, patch: Path):
    adapters = (('arch/mips/mm/tlbex.c', adapt_tlb),
                ('arch/mips/kernel/traps.c', adapt_vectors),
                ('arch/mips/mm/fault.c', adapt_fault), ('init/main.c', adapt_identity))
    changes = [(name, (source/name).read_text(), fn((source/name).read_text()))
               for name, fn in adapters]
    blob = ''.join(''.join(difflib.unified_diff(before.splitlines(keepends=True),
        after.splitlines(keepends=True), fromfile='a/'+name, tofile='b/'+name))
        for name, before, after in changes).encode()
    if patch.exists() and not same_patch_content(patch.read_bytes(), blob):
        raise RuntimeError('Preserve reviewed R5900 user VM patch')
    patch.parent.mkdir(parents=True, exist_ok=True)
    write_reviewed_patch(patch, blob)
    subprocess.run(['git','apply','--check',str(patch.resolve())],cwd=source,check=True)
    subprocess.run(['git','apply',str(patch.resolve())],cwd=source,check=True)
    return {'TLB_refill_FLX05_padding':True,'TLB_refill_32_slot_limit':True,
            'TLBR_hazards_restored':True,'interrupt_vector_FLX05_padding':True,
            'PID_1_user_fault_summary':True,'physical_userland_retest':'pending'}
