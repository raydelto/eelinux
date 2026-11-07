"""Actual readback packets/provider with FIFO and privileged-register stubs."""
from pathlib import Path
import shutil,subprocess,tempfile,unittest
from test_ps2_gs_transfer import ROOT,headers as transfer_headers
from kernel_gs_readback_port import adapt_provider
def headers(folder):
    transfer_headers(folder);folder=Path(folder)
    p=folder/'ps2fb-gs-device.h';p.write_text(adapt_provider(p.read_text()))
    source=(ROOT/'kernel/new-drivers/ps2fb-gs-readback-device.h').read_text()
    start=source.index('static void ps2gs_read_fifo(');end=source.index('static int ps2gs_download_execute(',start)
    stub='''
#define U32_MAX UINT32_MAX
static unsigned rx_fdr,rx_direction,rx_reads,rx_stall,rx_busy;
struct gs_busdir {unsigned dir;};
enum {gs_dir_host_to_local,gs_dir_local_to_host};
static u32 inl(unsigned reg) {
    if(reg==PS2GS_VIF1_CHCR)return rx_busy?1U<<8:0;
    assert(reg==PS2GS_VIF1_STAT);return rx_fdr|(rx_direction && !rx_stall?1U<<24:0);
}
static void outl(u32 value,unsigned reg){assert(reg==PS2GS_VIF1_STAT);rx_fdr=value;}
static void gs_write_busdir(struct gs_busdir value){rx_direction=value.dir;}
static void ps2gs_read_fifo(void *out){assert(rx_direction && rx_fdr);memset(out,0x5a,16);++rx_reads;}
'''
    (folder/'ps2fb-gs-readback-device.h').write_text(source[:start]+stub+source[end:])
@unittest.skipUnless(shutil.which('cc'),'native compiler required')
class ReadbackKernel(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.folder=tempfile.TemporaryDirectory();headers(cls.folder.name);cls.binary=Path(cls.folder.name)/'readback'
        subprocess.run(['cc','-std=gnu17','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-unused-parameter',
            '-pthread','-fsanitize=address,undefined','-fno-pie','-no-pie','-I'+cls.folder.name,
            '-I'+str(ROOT/'kernel/new-drivers'),'-I'+str(ROOT/'tests/graphics'),
            str(ROOT/'tests/graphics/ps2-gs-readback.c'),'-o',str(cls.binary)],check=True)
    @classmethod
    def tearDownClass(cls):cls.folder.cleanup()
    def test_native_ct16_ct32_local_to_host_packet_order(self):self.run_case('packets')
    def test_owned_readback_bounds_alignment_and_client_fault(self):self.run_case('validation')
    def test_fifo_timeout_restores_bus_and_never_replays(self):self.run_case('failure')
    def run_case(self,c):subprocess.run([str(self.binary),c],check=True,timeout=30)
if __name__=='__main__':unittest.main()
