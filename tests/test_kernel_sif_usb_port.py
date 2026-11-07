"""Exercise actual SIF packet locking and DMAC toggle-on-write masks."""
from pathlib import Path
import shutil,subprocess,sys,tempfile,unittest
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT/'scripts'))
from kernel_sif_usb_port import adapt_dmac,adapt_sif,adapt_irq
from kernel_iop_irq_port import modernize_iop_irq
from kernel_usb_boot_port import adapt_iop_irq
from kernel_fault_port import adapt_sif as receive_sif
from kernel_iop_upload_port import adapt_sif as upload_sif

def function(text,name):
    start=text.index('static ',text.index(name)-40)
    return text[start:text.index('\n}\n',start)+3]

class SifUsbPort(unittest.TestCase):
    def dmac(self):return (ROOT/'kernel/vendor/frno7/snapshot/arch/mips/ps2/dmac-irq.c').read_text()
    def sif(self):return (ROOT/'kernel/vendor/frno7/snapshot/drivers/ps2/sif.c').read_text()

    def test_changed_or_repeated_source_rejected(self):
        for adapt,text in ((adapt_dmac,self.dmac()),(adapt_sif,self.sif())):
            with self.assertRaises(RuntimeError):adapt(adapt(text))

    @unittest.skipUnless(shutil.which('cc'),'requires native C compiler')
    def test_masked_dma_pending_never_dispatches_and_repeated_masks_are_safe(self):
        text=adapt_dmac(self.dmac())
        masks=text[text.index('static DEFINE_RAW_SPINLOCK'):text.index('#define DMAC_IRQ_TYPE')]
        start=text.index('static irqreturn_t dmac_cascade(')
        cascade=text[start:text.index('\n}\n',start)+3]
        code=r'''
#include <assert.h>
#include <stdbool.h>
#define BIT(n) (1u<<(n))
#define IRQ_DMAC 32
#define DMAC_STAT_MASK 0
#define IRQ_NONE 0
#define IRQ_HANDLED 1
typedef int irqreturn_t;
struct irq_data { unsigned int irq; };
static unsigned int mask,stat,writes,locks,seen;
#define DEFINE_RAW_SPINLOCK(n) unsigned int n
#define raw_spin_lock_irqsave(lock,flags) do { (void)(lock);(flags)=0;assert(!locks++); } while(0)
#define raw_spin_unlock_irqrestore(lock,flags) do { (void)(lock);(void)(flags);assert(locks--==1); } while(0)
static unsigned int inl(unsigned int reg) { (void)reg;return stat|(mask<<16); }
static void outl(unsigned int v,unsigned int reg) { (void)reg;mask^=v>>16;stat&=~(v&0xffff);writes++; }
static unsigned int __fls(unsigned int v) { return 31-__builtin_clz(v); }
static int generic_handle_irq(unsigned int irq) { seen|=BIT(irq-IRQ_DMAC);return 0; }
static void spurious_interrupt(void) { assert(0); }
''' + masks + cascade + r'''
int main(void) {
 struct irq_data sif0={IRQ_DMAC+5}, gif={IRQ_DMAC+2}, bus={IRQ_DMAC+15};
 dmac_unmask(&sif0);dmac_unmask(&sif0);assert(mask==BIT(5)&&writes==1);
 stat=BIT(5)|BIT(2);assert(dmac_cascade(0,0)==IRQ_HANDLED&&seen==BIT(5));
 dmac_mask_ack(&gif);assert(mask==BIT(5)&&stat==BIT(5)&&writes==2);
 dmac_mask_ack(&sif0);assert(mask==0&&stat==0&&writes==4);
 dmac_mask(&sif0);assert(writes==4);
 stat=BIT(5);seen=0;assert(dmac_cascade(0,0)==IRQ_NONE&&seen==0);
 dmac_unmask(&sif0);assert(dmac_cascade(0,0)==IRQ_HANDLED&&seen==BIT(5));
 stat=BIT(15);seen=0;dmac_mask(&bus);dmac_unmask(&bus);
 assert(mask==BIT(5)&&dmac_cascade(0,0)==IRQ_HANDLED&&seen==BIT(15));
 dmac_mask_ack(&bus);assert(!stat&&mask==BIT(5));
 assert(!locks);return 0;
}
'''
        self.run_c(code)

    @unittest.skipUnless(shutil.which('cc'),'requires native C compiler')
    def test_real_sif_dma_writer_locks_preparation_launch_and_error_exit(self):
        text=upload_sif(receive_sif(adapt_sif(self.sif())));start=text.index('static int sif1_write_ert_int_0(')
        actual=text[start:text.index('\n}\n',start)+3]
        code=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
typedef uint8_t u8;typedef uint32_t dma_addr_t;typedef uint32_t iop_addr_t;
struct sif_cmd_header { uint32_t words[4]; };
struct iop_dma_tag { uint32_t ert,int_0,addr,wc; };
#define ALIGN(v,a) (((v)+(a)-1)&~((a)-1))
#define SIF1_BUFFER_SIZE 4096
#define EINVAL 22
#define EBUSY 16
#define DMAC_SIF1_MADR 0
#define DMAC_SIF1_QWC 1
#define DMAC_SIF1_CHCR 2
#define DMAC_CHCR_SENDN_TIE 0x100
static u8 buffer[4096];static void *sif1_buffer=buffer;
static int sif1_dma_lock,locks,writes,flushes,cache;static bool ready=true;
#define spin_lock_irqsave(lock,flags) do { (void)(lock);(flags)=0;assert(!locks++); } while(0)
#define spin_unlock_irqrestore(lock,flags) do { (void)(lock);(void)(flags);assert(locks--==1); } while(0)
static bool sif1_ready(void) { assert(locks==1);return ready; }
static unsigned nbytes_to_wc(unsigned n) { return n/4; }
static unsigned nbytes_to_qwc(unsigned n) { return n/16; }
static dma_addr_t virt_to_phys(void *p) { assert(p==buffer&&locks==1);return 123; }
static void dma_cache_wback(unsigned long p,unsigned n) { assert(p==(unsigned long)buffer&&n==48&&locks==1);cache++; }
static void outl(unsigned v,unsigned r) { (void)v;(void)r;assert(locks==1);writes++; }
static unsigned inl(unsigned r) { assert(r==DMAC_SIF1_CHCR&&locks==1);flushes++;return 0; }
''' + actual + r'''
int main(void) {
 struct sif_cmd_header h={{1,2,3,4}};char payload[3]={5,6,7};
 memset(buffer,0xa5,sizeof(buffer));
 assert(!sif1_write_ert_int_0(&h,true,true,64,payload,3));
 assert(writes==3&&cache==1&&flushes==1&&!locks);
 assert(!memcmp(buffer+sizeof(struct iop_dma_tag),&h,sizeof(h)));
 assert(!memcmp(buffer+sizeof(struct iop_dma_tag)+sizeof(h),payload,3));
 for(unsigned i=35;i<48;i++)assert(!buffer[i]);
 ready=false;assert(sif1_write_ert_int_0(&h,true,true,64,payload,3)==-EBUSY);
 assert(writes==3&&!locks);
 assert(sif1_write_ert_int_0(&h,true,true,64,payload,4096)==-EINVAL&&!locks);
 assert(!sif1_write_ert_int_0(0,false,false,0,0,0)&&!locks);return 0;
}
'''
        self.run_c(code)

    def run_c(self,code):
        with tempfile.TemporaryDirectory() as tmp:
            src=Path(tmp)/'test.c';exe=Path(tmp)/'test';src.write_text(code)
            subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-Wno-unused-parameter',
                '-fsanitize=address,undefined',str(src),'-o',str(exe)],check=True)
            subprocess.run([str(exe)],check=True)

    @unittest.skipUnless(shutil.which('cc'),'requires native C compiler')
    def test_preparing_relay_twice_does_not_load_or_bind_it_twice(self):
        text=(ROOT/'kernel/vendor/frno7/snapshot/drivers/ps2/iop-irq.c').read_text()
        text=text.replace('BUG_ON(in_irq());','BUG_ON(in_hardirq());')
        text=adapt_irq(adapt_iop_irq(modernize_iop_irq(text)))
        start=text.index('static int iop_irq_prepare_locked(')
        end=text.index('EXPORT_SYMBOL_GPL(iop_irq_prepare);',start)
        actual=text[start:end]
        code=r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
struct sif_rpc_client { int unused; };
static struct sif_rpc_client iop_irq_rpc;
static bool irq_relay;
static int iop_irq_lock,locks,loads,binds,load_error,bind_error;
#define SIF_SID_IRQ_RELAY 0x80000001
#define pr_info(...) do {} while(0)
#define pr_err(...) do {} while(0)
static void mutex_lock(int *p) { (void)p;assert(!locks++); }
static void mutex_unlock(int *p) { (void)p;assert(locks--==1); }
static int iop_module_request(const char *n,int version,void *arg) {
 assert(locks==1&&!strcmp(n,"irqrelay")&&version==0x0100&&!arg);loads++;return load_error;
}
static int sif_rpc_bind(struct sif_rpc_client *p,unsigned sid) {
 assert(locks==1&&p==&iop_irq_rpc&&sid==SIF_SID_IRQ_RELAY);binds++;return bind_error;
}
''' + actual + r'''
int main(void) {
 load_error=-5;assert(iop_irq_prepare()==-5&&!irq_relay&&loads==1&&binds==0&&!locks);
 load_error=0;bind_error=-6;assert(iop_irq_prepare()==-6&&!irq_relay&&loads==2&&binds==1&&!locks);
 bind_error=0;assert(!iop_irq_prepare()&&irq_relay&&loads==3&&binds==2&&!locks);
 assert(!iop_irq_prepare()&&irq_relay&&loads==3&&binds==2&&!locks);return 0;
}
'''
        self.run_c(code)

if __name__=='__main__':unittest.main()
