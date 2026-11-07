"""Exercise the real bounded IRX DMA upload and quiet stall reporting C."""
from pathlib import Path
import shutil, subprocess, sys, tempfile, unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'scripts'))
from kernel_sif_usb_port import adapt_sif as transport, adapt_loader as checkpoints
from kernel_fault_port import adapt_sif as receive
from kernel_iop_upload_port import adapt_sif, adapt_loader


def function(text, anchor):
    start=text.index(anchor)
    return text[start:text.index('\n}\n',start)+3]


class IopUpload(unittest.TestCase):
    def source(self):
        return adapt_sif(receive(transport((ROOT/'kernel/vendor/frno7/snapshot/drivers/ps2/sif.c').read_text())))

    def test_cached_iop_stores_removed_and_repeated_adaptation_rejected(self):
        text=checkpoints((ROOT/'kernel/vendor/frno7/snapshot/drivers/ps2/iop-module-request.c').read_text())
        after=adapt_loader(text)
        upload=function(after,'static int iop_module_link_buffer(')
        self.assertNotIn('memcpy(iop_bus_to_virt',upload)
        self.assertNotIn('dma_cache_wback',upload)
        self.assertIn('iop_alloc(ALIGN(nbyte, 16))',upload)
        self.assertIn('sif_write_data(link.addr, buf, nbyte)',upload)
        with self.assertRaises(RuntimeError):adapt_loader(after)
        with self.assertRaises(RuntimeError):adapt_sif(self.source())

    def run_c(self,code):
        with tempfile.TemporaryDirectory() as tmp:
            path=Path(tmp)/'test.c';exe=Path(tmp)/'test'
            path.write_text(code)
            subprocess.run(['cc','-std=gnu11','-Wall','-Wextra','-Werror',
                '-Wno-unused-function','-Wno-unused-parameter','-fsanitize=address,undefined',
                str(path),'-o',str(exe)],check=True,capture_output=True,text=True)
            subprocess.run([str(exe)],check=True,timeout=10,capture_output=True,text=True)

    @unittest.skipUnless(shutil.which('cc'),'requires native C compiler')
    def test_chunk_tail_bounds_and_late_dma_ownership(self):
        code=r'''
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <errno.h>
typedef uint8_t u8; typedef uint32_t iop_addr_t;
#define ALIGN(n,a) (((n)+(a)-1)&~((a)-1))
#define min_t(t,a,b) ((t)(a)<(t)(b)?(t)(a):(t)(b))
#define SIF1_BUFFER_SIZE 4096
static u8 ram[0x200000], payload[12268];
static unsigned calls,busy,completed,logs,sleeps;
#define pr_warn(...) do { logs++; } while(0)
static int sif1_write(void *header,iop_addr_t dst,const void *src,size_t size) {
 assert(!header&&!busy&&size<=4080&&(dst&15)==0);
 memcpy(ram+dst,src,size);
 memset(ram+dst+size,0,ALIGN(size,16)-size);
 calls++;busy=3;return 0;
}
static bool sif1_ready(void) {
 if(busy){busy--;return false;}completed++;return true;
}
static void msleep(unsigned ms) { assert(ms==1);sleeps++; }
''' + function(self.source(),'int sif_write_data(') + r'''
int main(void) {
 memset(ram,0xa5,sizeof(ram));
 for(unsigned i=0;i<sizeof(payload);i++)payload[i]=(i*17)&255;
 assert(!sif_write_data(0x50800,payload,sizeof(payload)));
 assert(calls==4&&completed==4&&logs==1&&sleeps==12&&!busy);
 assert(!memcmp(ram+0x50800,payload,sizeof(payload)));
 for(unsigned i=sizeof(payload);i<ALIGN(sizeof(payload),16);i++)assert(!ram[0x50800+i]);
 assert(ram[0x507ff]==0xa5&&ram[0x50800+ALIGN(sizeof(payload),16)]==0xa5);
 assert(!sif_write_data(0x200000,NULL,0));
 assert(sif_write_data(0x200000,payload,1)==-EINVAL);
 assert(sif_write_data(0x10001,payload,1)==-EINVAL);
 assert(sif_write_data(0x10000,NULL,1)==-EINVAL);
 assert(sif_write_data(0, payload,(size_t)-1)==-EINVAL);
 assert(calls==4);return 0;
}
'''
        self.run_c(code)

    @unittest.skipUnless(shutil.which('cc'),'requires native C compiler')
    def test_stalled_rpc_logs_once_and_only_reports_changed_phases(self):
        code=r'''
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
typedef uint32_t u32;typedef int32_t s32;
struct sif_rpc_client { u32 progress_addr,server; };
static unsigned logs;static u32 memory[16];
#define __iomem
#define CKSEG1ADDR(p) ((uintptr_t)(p))
#define ARRAY_SIZE(a) (sizeof(a)/sizeof(a[0]))
#define DMAC_STAT_MASK 0
#define pr_warn(...) do { logs++; } while(0)
static uintptr_t iop_bus_to_phys(u32 addr) { assert(addr==0x10000);return (uintptr_t)memory; }
static u32 inl(unsigned p) { return 0x200044; }
static u32 readl(const void *p) { return *(const u32*)p; }
''' + function(self.source(),'static void sif_report_stall(') + r'''
int main(void) {
 struct sif_rpc_client client={0x10000,0x1ca68};
 bool reported=false;u32 phase=~0u;
 memory[0]=0x45454952;memory[1]=2;
 for(unsigned i=0;i<100;i++)sif_report_stall(&client,6,false,&reported,&phase);
 assert(logs==2&&reported&&phase==2);
 memory[1]=5;sif_report_stall(&client,6,false,&reported,&phase);
 assert(logs==3&&phase==5);
 memory[1]=100;sif_report_stall(&client,6,false,&reported,&phase);
 assert(logs==4&&phase==100);
 memory[0]=0;memory[1]=99;sif_report_stall(&client,6,false,&reported,&phase);
 assert(logs==4&&phase==100);return 0;
}
'''
        # The log mock must type-check/evaluate format arguments too.
        code=code.replace('#define pr_warn(...) do { logs++; } while(0)',
            '#define pr_warn(...) do { logs++; if (0) printf(__VA_ARGS__); } while(0)')
        code=code.replace('#include <assert.h>','#include <assert.h>\n#include <stdio.h>')
        self.run_c(code)


if __name__=='__main__':unittest.main()
