"""Exercise actual RPC code against a noncoherent, dirty-page DMA model."""
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'scripts'))
from kernel_fault_port import adapt_sif
from kernel_sif_usb_port import adapt_sif as adapt_transport


def body(text, start):
    offset=text.index(start)
    return text[offset:text.index('\n}\n',offset)+3]


class DmaReceive(unittest.TestCase):
    @unittest.skipUnless(shutil.which('cc'),'requires native C compiler')
    def test_dirty_allocator_page_cannot_overwrite_incoming_rpc_reply(self):
        old=adapt_transport((ROOT/'kernel/vendor/frno7/snapshot/drivers/ps2/sif.c').read_text())
        # Replay both versions: the old transfer demonstrably returns stale data.
        for text,expected in ((old,'0xdeadbeef'),(adapt_sif(old),'0xcafebabe')):
            functions=body(text,'static int sif_rpc_dma(')+body(text,'int sif_rpc(')
            code=r'''
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
typedef uint32_t u32;
struct completion { int value; };
struct sif_rpc_client { void *client_buffer; unsigned client_size_max,server,server_buffer; struct completion done; };
struct sif_rpc_call_packet { unsigned rpc_id,send_size,recv_addr,recv_size,recv_mode; struct sif_rpc_client *client; unsigned server; };
static uint32_t cpu_page[1024],ram_page[1024];
static bool dirty=true,launched;
static unsigned flushes;
#define EINVAL 22
#define HZ 100
#define SIF_CMD_RPC_CALL 10
#define DMAC_STAT_MASK 0
#define pr_warn(...) do {} while(0)
static unsigned virt_to_phys(void *p) { assert(p==cpu_page);return 4096; }
static unsigned inl(unsigned reg) { (void)reg;return 0; }
static void reinit_completion(struct completion *p) { p->value=0; }
static void dma_cache_wback_inv(unsigned long p,unsigned n) {
 assert(p==(unsigned long)cpu_page&&n==sizeof(cpu_page)&&!launched);
 if(dirty)memcpy(ram_page,cpu_page,n);
 dirty=false;flushes++;
}
static void dma_wmb(void) { assert(!dirty); }
static void dma_rmb(void) { assert(launched); }
static int sif_cmd_data(unsigned id,void const *p,unsigned n,unsigned addr,void const *src,unsigned size) {
 const struct sif_rpc_call_packet *call=p;
 assert(id==SIF_CMD_RPC_CALL&&n==sizeof(*call)&&addr==16&&!src&&!size);
 assert(call->recv_addr==4096&&call->recv_size==4);
 launched=true;ram_page[0]=0xcafebabe;return 0;
}
static int wait_for_completion_timeout(struct completion *p,unsigned ticks) {
 assert(ticks==500&&launched);
 /* A dirty line evicted after device DMA destroys the incoming result. */
 if(dirty)memcpy(ram_page,cpu_page,sizeof(cpu_page));
 p->value=1;return 1;
}
static void dma_cache_inv(unsigned long p,unsigned n) {
 assert(p==(unsigned long)cpu_page&&n==4&&launched);
 memcpy(cpu_page,ram_page,sizeof(cpu_page));dirty=false;
}
''' + functions + r'''
int main(void) {
 struct sif_rpc_client client={cpu_page,sizeof(cpu_page),8,16,{0}};
 uint32_t reply=0;cpu_page[0]=0xdeadbeef;
 assert(!sif_rpc(&client,1,0,0,&reply,sizeof(reply)));
 assert(reply==EXPECTED);
 assert(flushes==(EXPECTED==0xcafebabe));
 assert(sif_rpc(&client,1,0,0,&reply,sizeof(cpu_page)+1)==-EINVAL);
 return 0;
}
'''
            with tempfile.TemporaryDirectory() as tmp:
                source=Path(tmp)/'dma.c';exe=Path(tmp)/'dma';source.write_text(code)
                subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-Wno-unused-function',
                    '-fsanitize=address,undefined','-DEXPECTED='+expected,str(source),'-o',str(exe)],check=True)
                subprocess.run([str(exe)],check=True)


if __name__=='__main__':unittest.main()
