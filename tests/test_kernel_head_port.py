"""Safety boundaries for the optional main-entry synchronization candidate."""
from pathlib import Path
import shutil
import subprocess
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from kernel_head_port import synchronize_kernel_head

MODERN = r'''
.macro setup_unused
.endm
	.macro	setup_c0_status set clr
	.set	push
	mfc0	t0, CP0_STATUS
	or	t0, ST0_KERNEL_CUMASK|\set|0x1f|\clr
	xor	t0, 0x1f|\clr
	mtc0	t0, CP0_STATUS
	.set	noreorder
	sll	zero,3
	.set	pop
	.endm
	MTC0		zero, CP0_CONTEXT	# clear context register
#ifdef CONFIG_64BIT
	MTC0		zero, CP0_XCONTEXT
#endif
	PTR_LA		$28, init_thread_union
unrelated_status_and_stack_code:
	nop
'''


class KernelHeadPort(unittest.TestCase):
    def setUp(self):
        self.vendor = (ROOT / "kernel/vendor/frno7/snapshot/arch/mips/kernel/head.S").read_text()

    def test_changed_reference_or_repeated_application_fails_closed(self):
        for reference in (self.vendor.replace("sync.p", "nop", 1),
                          self.vendor + "/* reference changed */\n"):
            with self.subTest(reference=reference[-30:]), self.assertRaises(RuntimeError):
                synchronize_kernel_head(MODERN, reference)
        with self.assertRaises(RuntimeError):
            synchronize_kernel_head(synchronize_kernel_head(MODERN, self.vendor), self.vendor)

    def test_changed_status_mask_or_context_anchor_fails_closed(self):
        for text in (MODERN + MODERN,
                     MODERN.replace("ST0_KERNEL_CUMASK", "ST0_CU0"),
                     MODERN.replace("mtc0\tt0, CP0_STATUS", "mtc0\tt1, CP0_STATUS"),
                     MODERN.replace("CP0_XCONTEXT", "NEW_CONTEXT")):
            with self.subTest(text=text[:50]), self.assertRaises(RuntimeError):
                synchronize_kernel_head(text, self.vendor)

    @unittest.skipUnless(shutil.which("cc"), "requires native C preprocessor")
    def test_other_cpu_32_and_64_bit_paths_preprocess_byte_exact(self):
        updated = synchronize_kernel_head(MODERN, self.vendor)
        for flags in ([], ["-DCONFIG_64BIT=1"]):
            def preprocess(text):
                return subprocess.check_output(["cc", "-E", "-P", "-x", "assembler-with-cpp", *flags, "-"],
                                               input=text.encode(), timeout=10)
            with self.subTest(flags=flags):
                self.assertEqual(preprocess(MODERN), preprocess(updated))
        actual = subprocess.check_output(["cc", "-E", "-P", "-x", "assembler-with-cpp",
                                          "-DCONFIG_CPU_R5900=1", "-"],
                                         input=updated.encode(), timeout=10).decode()
        self.assertEqual(actual.count("sync.p"), 3)
        self.assertIn("ST0_KERNEL_CUMASK", actual)
        self.assertNotIn("CP0_XCONTEXT", actual)


if __name__ == "__main__":
    unittest.main()
