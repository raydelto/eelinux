"""Execute real vector generation and PID 1 reporting; guard source transplants."""
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
from kernel_user_vm_port import adapt_tlb, adapt_vectors, adapt_fault, USER_FAULT_HELPER


class UserVMPort(unittest.TestCase):
    def setUp(self):
        self.vector = (ROOT / 'tests/fixtures/mips-exception-vector.c').read_text()
        self.tlb = (ROOT / 'tests/fixtures/mips-tlb-guards.c').read_text()

    def test_changed_duplicate_and_reapplied_sources_are_rejected(self):
        for original, adapter, anchor in (
            (self.vector, adapt_vectors, 'local_flush_icache_range'),
            (self.tlb, adapt_tlb, 'memset(final_handler'),
        ):
            for invalid in (original + original, original.replace(anchor, 'changed_anchor'),
                            adapter(original)):
                with self.subTest(adapter=adapter.__name__), self.assertRaises(RuntimeError):
                    adapter(invalid)
        with self.assertRaises(RuntimeError):
            adapt_tlb(self.tlb.replace('uasm_i_tlbr(&p);', 'uasm_i_tlbr(&p);', 1)
                      .replace('if (cpu_has_mips_r2_exec_hazard)', 'if (new_hazard)', 1))

    @unittest.skipUnless(shutil.which('cc'), 'requires native C compiler')
    def test_vector_padding_branches_and_other_mips_behavior(self):
        harness = r'''
#include <assert.h>
#include <stdint.h>
#include <string.h>
typedef uint32_t u32;
#define __init
#define GPR_K0 26
#define xchg(p,v) __extension__ ({ unsigned long old=*(p); *(p)=(v); old; })
static u32 memory[512];
static unsigned long ebase, exception_handlers[32], flushed_start, flushed_end;
static int cpu_has_divec=1, flush_count;
static void uasm_i_nop(u32 **p) { *(*p)++=0; }
static void uasm_i_j(u32 **p,unsigned long target) { *(*p)++=0x08000000|((target>>2)&0x03ffffff); }
static void UASM_i_LA(u32 **p,unsigned reg,unsigned long addr) {
    *(*p)++=0x3c000000|(reg<<16)|((addr>>16)&0xffff);
    *(*p)++=0x34000000|(reg<<21)|(reg<<16)|(addr&0xffff);
}
static void uasm_i_jr(u32 **p,unsigned reg) { *(*p)++=(reg<<21)|8; }
static void local_flush_icache_range(unsigned long a,unsigned long b) {
    flushed_start=a; flushed_end=b; flush_count++;
}
''' + adapt_vectors(self.vector) + r'''
int main(void) {
    ebase=(unsigned long)memory;
    u32 *vector=memory+128;
    unsigned long handler=ebase+0x300, previous=0x12345678;
    exception_handlers[0]=previous;
    for (unsigned i=0;i<512;i++) memory[i]=0xbdad0000; /* cache-like trailing words */
    assert(set_except_vector(0,(void *)handler)==(void *)previous);
    assert(exception_handlers[0]==handler && flush_count==1);
#ifdef CONFIG_CPU_R5900
    assert(vector[0]==0 && vector[1]==0);
    assert((vector[2]>>26)==2);
    for(unsigned i=3;i<9;i++) assert(vector[i]==0);
    assert(flushed_end==(unsigned long)(vector+9) && vector[9]==0xbdad0000);
#else
    assert((vector[0]>>26)==2 && vector[1]==0 && vector[2]==0xbdad0000);
    assert(flushed_end==(unsigned long)(vector+2));
#endif
    assert(flushed_start==ebase+0x200 && memory[127]==0xbdad0000);
    memset(vector,0xad,48);
    handler=(ebase^0x10000000)+0x300;
    set_except_vector(0,(void *)handler);
#ifdef CONFIG_CPU_R5900
    assert(vector[0]==0 && vector[1]==0 && (vector[2]>>26)==15);
    assert(vector[4]==((26<<21)|8));
    for(unsigned i=5;i<11;i++) assert(vector[i]==0);
    assert(flushed_end==(unsigned long)(vector+11) && vector[11]==0xadadadad);
#else
    assert((vector[0]>>26)==15 && vector[2]==((26<<21)|8));
    assert(flushed_end==(unsigned long)(vector+4));
#endif
    int before=flush_count;
    set_except_vector(1,(void *)handler); assert(flush_count==before);
    cpu_has_divec=0;
    set_except_vector(0,(void *)handler); assert(flush_count==before);
    return 0;
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            source = Path(tmp) / 'vector.c'; exe = Path(tmp) / 'vector'
            source.write_text(harness)
            for defines in ([], ['-DCONFIG_CPU_R5900']):
                subprocess.run(['cc', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                                '-fsanitize=address,undefined', *defines, str(source), '-o', str(exe)], check=True)
                subprocess.run([str(exe)], check=True)

    @unittest.skipUnless(shutil.which('cc'), 'requires native C compiler')
    def test_fault_report_never_dereferences_bad_user_addresses(self):
        harness = r'''
#include <stdio.h>
#include <stdint.h>
#define CONFIG_CPU_R5900
#define pr_emerg(...) printf(__VA_ARGS__)
struct pt_regs { unsigned long regs[32], cp0_epc, cp0_cause, cp0_status; };
struct task { const char *comm; int pid; };
static struct task init={"init",1};
#define current (&init)
static int task_pid_nr(struct task *p) { return p->pid; }
''' + USER_FAULT_HELPER + r'''
int main(void) {
    struct pt_regs regs={0};
    regs.cp0_epc=0x77ddd614; regs.regs[31]=0x77dde2fc;
    regs.cp0_cause=8; regs.cp0_status=0x10011;
    regs.regs[16]=0; regs.regs[17]=0xdeadbeef;
    regs.regs[29]=1; regs.regs[28]=0x268;
    ps2_user_fault_summary(&regs,0,0x268);
    ps2_user_fault_summary(&regs,1,0xffffffff);
    return 0;
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            source = Path(tmp) / 'fault.c'; exe = Path(tmp) / 'fault'
            source.write_text(harness)
            subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                            '-fsanitize=address,undefined', str(source), '-o', str(exe)], check=True)
            result = subprocess.check_output([str(exe)], text=True)
        self.assertEqual(len(result.splitlines()), 14)
        for value in ('pid=1 read=00000268', 'epc=77ddd614 ra=77dde2fc cause=00000008',
                      's0=00000000 s1=deadbeef', 'sp=00000001 gp=00000268',
                      'pid=1 write=ffffffff'):
            self.assertIn(value, result)

    def test_user_fault_signal_policy_and_other_tasks_are_preserved(self):
        original = '''static void __do_page_fault(void) {
		force_sig_fault(SIGSEGV, si_code, (void __user *)address);
	force_sig_fault(SIGBUS, BUS_ADRERR, (void __user *)address);
}
'''
        result = adapt_fault(original)
        self.assertEqual(result.count('if (task_pid_nr(tsk) == 1)'), 2)
        self.assertEqual(result.count('force_sig_fault('), 2)
        self.assertIn('force_sig_fault(SIGSEGV, si_code, (void __user *)address);', result)
        self.assertIn('force_sig_fault(SIGBUS, BUS_ADRERR, (void __user *)address);', result)
        with self.assertRaises(RuntimeError):
            adapt_fault(result)


if __name__ == '__main__':
    unittest.main()
