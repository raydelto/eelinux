"""Guarded source transplant and execution of the actual RDHWR dispatch branch."""
from pathlib import Path
import os
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from kernel_tls_port import modernize_traps_header, modernize_unaligned

VENDOR = ROOT / "kernel/vendor/frno7/snapshot/arch/mips"
MODERN = '''/* Modern source sentinel. */
static void emulate_load_store_insn(struct pt_regs *regs, void *addr, unsigned int *pc)
{
	bool user = user_mode(regs);
	__get_inst32(&insn.word, pc, user);
	switch (insn.i_format.opcode) {
	case spec3_op:
		if (insn.dsp_format.func == lx_op) {
			if (user && !access_ok(addr, 4))
				goto sigbus;
			/* Modern DSP handler sentinel. */
		}
		break;
	default:
		goto sigill;
	}
}
/* Modern signal and exception-context sentinel. */
static void unrelated_handler_one(void)
{
	bool user = user_mode(regs);
}
static void unrelated_handler_two(void)
{
	bool user = user_mode(regs);
}
'''


class KernelTLSPort(unittest.TestCase):
    def setUp(self):
        self.vendor = (VENDOR / "kernel/unaligned.c").read_text()
        self.header = (VENDOR / "include/asm/traps.h").read_text()

    def test_modern_access_checks_and_unrelated_code_are_preserved(self):
        updated = modernize_unaligned(MODERN, self.vendor)
        prefix, suffix = MODERN.split("\tcase spec3_op:\n\t\t", 1)
        self.assertTrue(updated.startswith(prefix))
        self.assertTrue(updated.endswith(suffix))
        self.assertEqual(updated.count("simulate_rdhwr("), 1)
        self.assertIn("insn.r_format.rd == MIPS_HWR_ULR", updated)
        self.assertIn("goto sigbus;", updated)

    def test_changed_or_already_adapted_modern_source_is_rejected(self):
        for text in (MODERN + MODERN, MODERN.replace("lx_op", "new_op"),
                     MODERN.replace("bool user", "bool is_user"),
                     MODERN.replace("__get_inst32", "__get_user"),
                     modernize_unaligned(MODERN, self.vendor)):
            with self.subTest(text=text[:30]), self.assertRaises(RuntimeError):
                modernize_unaligned(text, self.vendor)

    def test_modified_vendor_instruction_guards_are_rejected(self):
        for vendor in (self.vendor + self.vendor,
                       self.vendor.replace("insn.r_format.rd == MIPS_HWR_ULR", "true"),
                       self.vendor.replace("insn.r_format.rs == 0", "insn.r_format.rs == 1"),
                       self.vendor.replace("\t\t\tgoto sigbus;\n\t\t} else if (insn.dsp_format.func == lx_op)",
                                           "\t\t\treturn;\n\t\t} else if (insn.dsp_format.func == lx_op)", 1)):
            with self.subTest(vendor=vendor[:30]), self.assertRaises(RuntimeError):
                modernize_unaligned(MODERN, vendor)

    def test_shared_prototype_is_inserted_once_with_source_guards(self):
        prototype = "int simulate_rdhwr(struct pt_regs *regs, int rd, int rt);\n"
        pristine = ("/* SPDX-License-Identifier: GPL-2.0-or-later */\n"
                    "asmlinkage void do_ri(struct pt_regs *regs);\n"
                    "asmlinkage void do_cpu(struct pt_regs *regs);\n")
        updated = modernize_traps_header(pristine, self.header)
        self.assertEqual(updated.replace(prototype, ""), pristine)
        self.assertEqual(updated.count(prototype), 1)
        for text in (updated, pristine + pristine, pristine.replace("void do_ri", "void do_changed_ri")):
            with self.subTest(text=text[:30]), self.assertRaises(RuntimeError):
                modernize_traps_header(text, self.header)

    @unittest.skipUnless(os.name != "nt" and shutil.which("cc"), "requires native Linux C compiler")
    def test_actual_branch_reads_tls_and_rejects_other_sq_encodings(self):
        adapted = modernize_unaligned(MODERN, self.vendor)
        branch = adapted.split("\tcase spec3_op:\n", 1)[1].split(
            "if (insn.dsp_format.func == lx_op) {", 1)[0]
        harness = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
enum { MIPS_HWR_ULR = 29, rdhwr_op = 59 };
#define CONFIG_CPU_R5900 enabled
#define IS_ENABLED(option) (option)
struct pt_regs { uint32_t regs[32], epc; };
struct instruction { struct { unsigned int func, rd, rs, re, rt; } r_format; };
static int enabled, compute_result, simulate_result, computed, simulated, result;
static int compute_return_epc(struct pt_regs *regs) {
    computed++; if (compute_result < 0) return compute_result;
    regs->epc += 4; return 0;
}
static int simulate_rdhwr(struct pt_regs *regs, int rd, int rt) {
    simulated++; assert(rd == MIPS_HWR_ULR);
    if (simulate_result < 0) return simulate_result;
    regs->regs[rt] = 0x00a1b2c3; return 0;
}
static void dispatch(struct pt_regs *regs, uint32_t word) {
    struct instruction insn = {{word & 63, (word >> 11) & 31,
                               (word >> 21) & 31, (word >> 6) & 31,
                               (word >> 16) & 31}};
'''
        harness += branch + "{ result = 3; return; }\nsigbus: result = 1; return;\nsigill: result = 2; }\n"
        harness += r'''
static void reset(void) { computed = simulated = result = compute_result = simulate_result = 0; }
int main(void) {
    enabled = 1;
    for (unsigned int rt = 1; rt < 32; rt++) {
        reset(); struct pt_regs regs = {.epc = 0x00421368};
        dispatch(&regs, 0x7c00e83b | (rt << 16));
        assert(computed == 1 && simulated == 1 && result == 0);
        assert(regs.regs[rt] == 0x00a1b2c3 && regs.epc == 0x0042136c);
    }
    const uint32_t invalid[] = {0x7c03e83a, 0x7c03e07b, 0x7c23e83b, 0x7c03e87b};
    for (unsigned int i = 0; i < sizeof(invalid)/sizeof(invalid[0]); i++) {
        reset(); struct pt_regs regs = {.epc = 0x00421368};
        dispatch(&regs, invalid[i]);
        assert(result == 1 && computed == 0 && simulated == 0 && regs.epc == 0x00421368);
    }
    struct pt_regs regs = {.epc = 0x00421368};
    reset(); compute_result = -1; dispatch(&regs, 0x7c03e83b);
    assert(result == 2 && computed == 1 && simulated == 0);
    reset(); simulate_result = -1; dispatch(&regs, 0x7c03e83b);
    assert(result == 2 && computed == 1 && simulated == 1);
    reset(); enabled = 0; dispatch(&regs, 0x7c03e83b);
    assert(result == 3 && computed == 0 && simulated == 0);
    return 0;
}
'''
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp)
            (path / "check.c").write_text(harness)
            subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", str(path / "check.c"),
                            "-o", str(path / "check")], check=True, capture_output=True, text=True)
            subprocess.run([str(path / "check")], check=True, timeout=5)


if __name__ == "__main__":
    unittest.main()
