"""Run adapted relay entry with preemption, failure and mailbox lifetime mocks."""
from pathlib import Path
import os, shutil, subprocess, sys, tempfile, unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'scripts'))
from iop_dev9 import irqrelay_source


@unittest.skipIf(os.name=='nt' or not shutil.which('cc'),'requires Linux mmap/native compiler')
class RelayStartup(unittest.TestCase):
    def test_entry_never_logs_and_detaches_mailbox_before_return(self):
        text=irqrelay_source((ROOT/'kernel/vendor/iopmod/snapshot/module/irqrelay.c').read_text())
        actual=text[text.index('struct relay_progress'):text.index('/* Fixed-size rearm ABI')]
        actual+=text[text.index('static enum module_init_status irqrelay_init'):text.index('module_init(irqrelay_init);')]
        # Host pointers are wider than IOP pointers; preserve the address value.
        actual=actual.replace('(volatile struct relay_progress *)addr',
                              '(volatile struct relay_progress *)(uintptr_t)addr')
        code=r'''
#define _GNU_SOURCE
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <sys/mman.h>
typedef uint32_t u32;typedef int32_t s32;
enum module_init_status { MODULE_RESIDENT, MODULE_EXIT };
struct iop_thread {u32 attr,option;void(*thread)(void*);u32 stacksize,priority;};
struct sifcmd_rpc_data_queue { int unused; };
struct sifcmd_rpc_server_data { int unused; };
static struct sifcmd_rpc_data_queue rpc_qdata;
static struct sifcmd_rpc_server_data rpc_sdata;
static unsigned char rpc_buffer[16];
static int rpc_stid,create_result=3,start_result;
static unsigned queues,registers,loops,removed,deleted,handlers;
static void (*worker)(void*);
#define THREAD_ATTR_C 0x02000000
#define SIF_SID_IRQ_RELAY 0x80000020
#define SMAP_REARM_CMD 0x8000001b
#define pr_info(...) assert(!"entry must not block in printk")
#define pr_err(...) assert(!"entry must not block in printk")
#define pr_debug(...) assert(!"entry must not block in printk")
static void *irqrelay_service_rpc(int op,void *buffer,size_t size) {return NULL;}
static void smap_rearm(void *header,void *arg) {}
static int thbase_create(const struct iop_thread *p) {
 assert(p->stacksize==4096&&p->priority==0x27&&p->attr==THREAD_ATTR_C);
 worker=p->thread;return create_result;
}
static void sifcmd_set_rpc_queue(struct sifcmd_rpc_data_queue *p,int id) {assert(p==&rpc_qdata&&id==3);queues++;}
static void sifcmd_register_rpc(struct sifcmd_rpc_server_data *s,u32 sid,
 void*(*cb)(int,void*,size_t),void *buf,void *cf,void *cbuf,struct sifcmd_rpc_data_queue *q) {
 assert(s==&rpc_sdata&&sid==SIF_SID_IRQ_RELAY&&cb==irqrelay_service_rpc&&buf==rpc_buffer&&q==&rpc_qdata);
 assert(!cf&&!cbuf);registers++;
}
static void sifcmd_rpc_loop(struct sifcmd_rpc_data_queue *p) {assert(p==&rpc_qdata);loops++;}
static int thbase_start(int id,void *arg) {assert(id==3&&!arg);if(!start_result)worker(arg);return start_result;}
static void sif_request_cmd(u32 cmd,void(*cb)(void*,void*),void *arg) {assert(cmd==SMAP_REARM_CMD&&cb==smap_rearm&&!arg);handlers++;}
static void sifcmd_remove_rpc(void *s,void *q) {assert(s==&rpc_sdata&&q==&rpc_qdata);removed++;}
static void sifcmd_remove_rpc_queue(void *q) {assert(q==&rpc_qdata);removed++;}
static void thbase_delete(int id) {assert(id==3);deleted++;}
''' + actual + r'''
int main(void) {
 void *map=mmap((void*)0x10000,4096,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0);
 assert(map==(void*)0x10000);
 struct relay_progress *p=map;char *args[]={"irqrelay","ps2-progress=00010000"};
 p->magic=0x45454952;
 assert(irqrelay_init(2,args)==MODULE_RESIDENT);
 assert(p->phase==7&&!p->result&&p->thread==3&&!startup_progress);
 assert(queues==1&&registers==1&&loops==1&&handlers==1&&!deleted&&!removed);
 /* A worker scheduled after LOADFILE returns must not touch freed/reused RAM. */
 memset(map,0xa5,4096);worker(NULL);assert(p->magic==0xa5a5a5a5&&p->phase==0xa5a5a5a5);
 memset(map,0,4096);p->magic=0x45454952;create_result=-12;
 assert(irqrelay_init(2,args)==MODULE_EXIT&&p->phase==8&&p->result==-12&&!startup_progress);
 assert(queues==1&&registers==1&&!removed&&!deleted);
 create_result=3;start_result=-5;
 assert(irqrelay_init(2,args)==MODULE_EXIT&&p->phase==8&&p->result==-5&&!startup_progress);
 assert(removed==2&&deleted==1&&handlers==1);
 start_result=0;args[1]="ps2-progress=ffffffff";
 assert(irqrelay_init(2,args)==MODULE_RESIDENT&&!startup_progress);
 assert(irqrelay_init(0,NULL)==MODULE_RESIDENT&&!startup_progress);
 munmap(map,4096);return 0;
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            path=Path(tmp)/'test.c';exe=Path(tmp)/'test'
            path.write_text(code)
            subprocess.run(['cc','-std=gnu11','-Wall','-Wextra','-Werror',
                '-Wno-unused-parameter','-fsanitize=address,undefined',
                str(path),'-o',str(exe)],check=True,capture_output=True,text=True)
            subprocess.run([str(exe)],check=True,timeout=10,capture_output=True,text=True)


if __name__=='__main__':unittest.main()
