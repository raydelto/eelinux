"""Replay LOADFILE patching while the IOP changes a shared EE cache line."""
from pathlib import Path
import os
import shlex
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'scripts'))
from kernel_iop_patch_port import adapt_memory, adapt_patch, adapt_loader


def function(text, anchor):
    start = text.index(anchor)
    return text[start:text.index('\n}\n', start)+3]


class IopPatch(unittest.TestCase):
    def test_adapters_reject_unreviewed_or_repeated_sources(self):
        vendor = ROOT/'kernel/vendor/frno7/snapshot/drivers/ps2'
        for filename, adapter in (('iop-memory.c', adapt_memory),
                                  ('iop-module-patch.c', adapt_patch),
                                  ('iop-module-request.c', adapt_loader)):
            text = (vendor/filename).read_text()
            updated = adapter(text)
            with self.assertRaises(RuntimeError):
                adapter(updated)
            with self.assertRaises(RuntimeError):
                adapter('changed source')

    @unittest.skipUnless(shutil.which('cc'), 'requires native C compiler')
    def test_live_iop_neighbour_survives_patch_and_rpc_publication(self):
        vendor = ROOT/'kernel/vendor/frno7/snapshot/drivers/ps2'
        memory = (vendor/'iop-memory.c').read_text()
        patch = (vendor/'iop-module-patch.c').read_text()
        for fixed in (False, True):
            current_memory = adapt_memory(memory) if fixed else memory
            current_patch = adapt_patch(patch) if fixed else patch
            code = r'''
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
typedef uint32_t u32; typedef int16_t s16;
typedef uint32_t iop_addr_t;
#define __init
#define __iomem
#define ARRAY_SIZE(a) (sizeof(a)/sizeof(a[0]))
#define BUILD_BUG_ON(e) _Static_assert(!(e), #e)
struct iop_module_info { u32 text_start, entry; };
struct i_format { int simmediate:16; unsigned rt:5,rs:5,op:6; };
struct u_format { unsigned uimmediate:16,rt:5,rs:5,op:6; };
struct j_format { unsigned target:26,op:6; };
union mips_instruction {
 u32 word; struct i_format i_format;
 struct u_format u_format; struct j_format j_format;
};
static uint32_t ram[0x4000/4], cache[0x4000/4], snapshot[0x4000/4];
static unsigned barriers, code_words, published;
static uintptr_t iop_bus_to_phys(u32 p) { assert(p<sizeof(ram));return p; }
static void *phys_to_virt(uintptr_t p) { return (uint8_t*)cache+p; }
#define CKSEG1ADDR(p) ((uintptr_t)((uint8_t*)ram+(p)))
static u32 readl(const void *p) { return *(const u32*)p; }
static void wmb(void) { barriers++; }
static void writel(u32 v, void *p) {
 unsigned offset=(const uint8_t*)p-(const uint8_t*)ram;
 assert(offset<sizeof(ram)&&(offset&3)==0);
 if(offset>=0x2000&&offset<0x2080)code_words++;
 if(offset==0x1818){assert(code_words==32&&barriers>=1);published=1;}
 if(offset==0x14c8){assert(published&&barriers>=2);}
 *(u32*)p=v;
}
static void old_writeback(void) {
 /* Only dirty EE lines are written back, including unmodified neighbours. */
 for(unsigned offset=0;offset<sizeof(ram);offset+=64)
  if(memcmp((uint8_t*)cache+offset,(uint8_t*)snapshot+offset,64))
   memcpy((uint8_t*)ram+offset,(uint8_t*)cache+offset,64);
}
''' + function(current_memory, 'void *iop_bus_to_virt(')
            for anchor in ('static s16 __init iop_simm_insn(',
                           'static void __init iop_patch_jump_table(',
                           'static void __init iop_patch_apply('):
                code += function(current_patch, anchor)
            code += r'''
int main(void) {
 struct iop_module_info loadfile={0x1000,0x2000}, modload={0x3000,0};
 ram[0x14c8/4]=0x2c820006;
 ram[0x14e0/4]=0x3c010000;ram[0x14e8/4]=0x8c221800;
 ram[0x14fc/4]=0x11223344;
 memcpy(cache,ram,sizeof(ram));memcpy(snapshot,cache,sizeof(cache));
 /* IOP progress occurs after the EE has read the old cache line. */
 ram[0x14fc/4]=0x55667788;
 iop_patch_apply(&loadfile,&modload,loadfile.entry);
 if(!FIXED)old_writeback();
 assert(ram[0x14c8/4]==0x2c820007&&ram[0x1818/4]==loadfile.entry);
 assert(ram[0x2000/4]==0x27bdffd8);
 assert(ram[(0x2000+5*4)/4]==(0x0c000000|((0x3000+0x248)>>2)));
 assert(ram[(0x2000+15*4)/4]==(0x0c000000|((0x3000+0x358)>>2)));
 assert(ram[0x14fc/4]==(FIXED?0x55667788:0x11223344));
 assert(!FIXED||(code_words==32&&published&&barriers==2));
 return 0;
}
'''
            with tempfile.TemporaryDirectory() as tmp:
                source = Path(tmp)/'patch.c'; exe = Path(tmp)/'patch'
                source.write_text(code)
                compiler = os.environ.get('PS2_IOP_PATCH_CC', 'cc')
                flags = shlex.split(os.environ.get('PS2_IOP_PATCH_FLAGS', '-fsanitize=address,undefined'))
                runner = shlex.split(os.environ.get('PS2_IOP_PATCH_RUNNER', ''))
                subprocess.run([compiler, '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                    '-Wno-unused-function', *flags,
                    '-DFIXED='+str(int(fixed)), str(source), '-o', str(exe)], check=True)
                subprocess.run([*runner, str(exe)], check=True, timeout=10)


if __name__ == '__main__':
    unittest.main()
