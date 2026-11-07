"""Execute real INTC mask/cascade functions with toggle-on-write MMIO."""
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
from kernel_intc_port import adapt

class IntcPort(unittest.TestCase):
    def source(self):
        return (ROOT/'kernel/vendor/frno7/snapshot/arch/mips/ps2/intc-irq.c').read_text()

    def test_changed_or_repeated_source_rejected(self):
        for text in (adapt(self.source()), self.source().replace('pending = inl(INTC_STAT);','pending = new_mask;')):
            with self.assertRaises(RuntimeError): adapt(text)

    @unittest.skipUnless(shutil.which('cc'), 'requires native C compiler')
    def test_disabled_gs_is_filtered_and_repeated_masks_never_toggle(self):
        text=adapt(self.source())
        masks=text[text.index('static DEFINE_RAW_SPINLOCK'):text.index('#define INTC_IRQ_TYPE')]
        start=text.index('static irqreturn_t intc_cascade(')
        cascade=text[start:text.index('\n}\n',start)+3]
        code=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#define BIT(n) (1u<<(n))
#define IRQ_INTC 8
#define INTC_MASK 1
#define INTC_STAT 2
#define IRQ_NONE 0
#define IRQ_HANDLED 1
typedef int irqreturn_t;
struct irq_data { unsigned int irq; };
static unsigned int mask,stat,mask_writes,locks,seen,spurious;
#define DEFINE_RAW_SPINLOCK(n) unsigned int n
#define raw_spin_lock_irqsave(lock,flags) do { (void)(lock); (flags)=0; assert(!locks++); } while(0)
#define raw_spin_unlock_irqrestore(lock,flags) do { (void)(lock); (void)(flags); assert(locks--==1); } while(0)
static unsigned int inl(unsigned int reg) { return reg==INTC_MASK ? mask : stat; }
static void outl(unsigned int bit,unsigned int reg) {
 if(reg==INTC_MASK) { assert(locks==1); mask^=bit; mask_writes++; } else stat&=~bit;
}
static unsigned int __fls(unsigned int v) { return 31-__builtin_clz(v); }
static int generic_handle_irq(unsigned int irq) { seen|=BIT(irq-IRQ_INTC); return 0; }
static void spurious_interrupt(void) { spurious++; }
''' + masks + cascade + r'''
int main(void) {
 struct irq_data timer={IRQ_INTC+9}, gs={IRQ_INTC};
 intc_unmask(&timer); intc_unmask(&timer);
 assert(mask==BIT(9) && mask_writes==1 && !locks);
 intc_mask(&timer); intc_mask(&timer);
 assert(mask==0 && mask_writes==2 && !locks);
 intc_unmask(&timer); stat=BIT(9)|BIT(0); seen=0;
 assert(intc_cascade(0,0)==IRQ_HANDLED && seen==BIT(9) && !spurious);
 intc_mask_ack(&gs); assert(stat==BIT(9) && mask==BIT(9) && mask_writes==3);
 intc_mask_ack(&timer); assert(stat==0 && mask==0 && mask_writes==4);
 stat=BIT(0); seen=0;
 assert(intc_cascade(0,0)==IRQ_NONE && seen==0 && stat==BIT(0));
 intc_unmask(&gs); assert(intc_cascade(0,0)==IRQ_HANDLED && seen==BIT(0));
 assert(!locks && !spurious);
 return 0;
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            src=Path(tmp)/'test.c'; src.write_text(code);exe=Path(tmp)/'test'
            subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-Wno-unused-parameter',
                '-fsanitize=address,undefined',str(src),'-o',str(exe)],check=True)
            subprocess.run([str(exe)],check=True)

if __name__=='__main__': unittest.main()
