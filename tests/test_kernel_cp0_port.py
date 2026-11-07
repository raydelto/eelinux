"""Exercise the real modern CPU/TLB functions with CP0 access counters."""
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from kernel_cp0_port import adapt_cpu_probe, adapt_tlb


class CP0Port(unittest.TestCase):
    def setUp(self):
        self.cpu = (ROOT / "kernel/vendor/linux-7.2.8/snapshot/arch/mips/kernel/cpu-probe.c").read_text()
        self.tlb = (ROOT / "kernel/vendor/linux-7.2.8/snapshot/arch/mips/mm/tlb-r4k.c").read_text()

    def test_repeated_or_changed_inputs_fail_closed(self):
        for original, adapter in ((self.cpu, adapt_cpu_probe), (self.tlb, adapt_tlb)):
            with self.assertRaises(RuntimeError):
                adapter(adapter(original))
            with self.assertRaises(RuntimeError):
                adapter(original.replace("cpu_has_64bits", "new_cpu_feature"))

    @unittest.skipUnless(shutil.which("cc"), "requires native C compiler")
    def test_ee_never_uses_doubleword_cp0_and_other_mips_preserves_width(self):
        cpu = adapt_cpu_probe(self.cpu)
        tlb = adapt_tlb(self.tlb)
        probe = cpu[cpu.index("static inline void cpu_probe_vmbits"):cpu.index("\nstatic void set_isa")]
        wrappers = tlb[tlb.index("static inline unsigned long long read_c0_entryhi_native"):tlb.index("/* TLB entry state")]
        vpn_line = tlb[tlb.index("\tvpn_mask |="):tlb.index("\n\n\tfor (i = 0; i < tlbsize;", tlb.index("\tvpn_mask |="))]
        harness = r'''
#include <assert.h>
#include <stdint.h>
#define CPU_R5900 1
#define IS_ENABLED(option) (type == CPU_R5900)
struct cpuinfo_mips { int cputype; int vmbits; };
static int cpu_has_64bits, type, reads32, writes32, reads64, writes64;
static uint64_t value;
static int current_cpu_type(void) { return type; }
static uint64_t read_c0_entryhi_64(void) { reads64++; return value; }
static uint32_t read_c0_entryhi(void) { reads32++; return (uint32_t)value; }
static void write_c0_entryhi_64(uint64_t v) { writes64++; value=v; }
static void write_c0_entryhi(uint64_t v) { writes32++; value=(uint32_t)v; }
static void back_to_back_c0_hazard(void) { }
static int fls64(uint64_t v) { return v ? 64-__builtin_clzll(v) : 0; }
'''+probe+wrappers+'''
static uint64_t region(void) { uint64_t vpn_mask=0;
'''+vpn_line+r'''
return vpn_mask; }
int main(void) {
    struct cpuinfo_mips c={CPU_R5900,0};
    type=CPU_R5900; cpu_has_64bits=1;
    cpu_probe_vmbits(&c);
    assert(c.vmbits==31 && reads64==0 && writes64==0);
    write_c0_entryhi_native(UINT64_C(0xabcdef1280000001));
    assert(writes32==1 && writes64==0);
    assert(read_c0_entryhi_native()==UINT64_C(0x80000001));
    assert(reads32==1 && reads64==0 && region()==UINT64_C(0x80000000));
    type=2; c.cputype=2;
    cpu_probe_vmbits(&c);
    assert(reads64==1 && writes64==1 && c.vmbits==62);
    write_c0_entryhi_native(UINT64_C(0xabcdef1280000001));
    assert(read_c0_entryhi_native()==UINT64_C(0xabcdef1280000001));
    assert(region()==(UINT64_C(3)<<62));
    cpu_has_64bits=0; cpu_probe_vmbits(&c);
    assert(c.vmbits==31 && region()==UINT64_C(0x80000000));
    return 0;
}
'''
        with tempfile.TemporaryDirectory() as folder:
            source = Path(folder)/"cp0.c"; exe=Path(folder)/"cp0"
            source.write_text(harness)
            subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-fsanitize=address,undefined",
                            str(source), "-o", str(exe)], check=True)
            subprocess.run([str(exe)], check=True)


if __name__ == "__main__":
    unittest.main()
