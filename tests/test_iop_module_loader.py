"""Regress the real IRX loader's dependency-cache and cleanup decisions."""
from pathlib import Path
import os
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from kernel_smap_port import module_start_guard


def definition(source: str, name: str) -> str:
    lines = source.splitlines(keepends=True)
    starts = [i for i, line in enumerate(lines)
              if line.startswith("static ") and name + "(" in line]
    if len(starts) != 1:
        raise RuntimeError(f"Expected one actual {name} definition")
    start = starts[0]
    end = next(i for i in range(start, len(lines)) if lines[i] == "}\n")
    return "".join(lines[start:end + 1])


@unittest.skipUnless(os.name != "nt" and shutil.which("cc"), "requires native Linux C compiler")
class ActualIopModuleLoader(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        source = (ROOT / "kernel/vendor/frno7/snapshot/drivers/ps2/iop-module-request.c").read_text()
        adapted = module_start_guard(source)
        actual = "\n".join(definition(adapted, name) for name in
                           ("iop_module_has_exports", "iop_module_link_buffer"))
        harness = (ROOT / "tests/fixtures/iop_module_loader.c").read_text().replace(
            "/* ACTUAL_LOADER_FUNCTIONS */", actual)
        cls.temporary = tempfile.TemporaryDirectory()
        work = Path(cls.temporary.name)
        path = work / "check.c"
        path.write_text(harness)
        cls.executable = work / "check"
        result = subprocess.run(["cc", "-std=c11", "-O1", "-Wall", "-Wextra", "-Werror",
                                 "-Wno-unused-function", "-fsanitize=address,undefined",
                                 "-fno-omit-frame-pointer", str(path), "-o", str(cls.executable)],
                                text=True, capture_output=True)
        if result.returncode:
            raise RuntimeError(result.stderr)

    @classmethod
    def tearDownClass(cls):
        cls.temporary.cleanup()

    def check(self, case):
        result = subprocess.run([str(self.executable), case], text=True,
                                capture_output=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_resident_exports_copy_flush_and_release(self): self.check("resident")
    def test_removable_resident_exports(self): self.check("removable")
    def test_exited_exporter_never_satisfies_dependency_and_retry_succeeds(self): self.check("exit_exporter")
    def test_no_export_rom_initializer_may_exit(self): self.check("exit_no_exports")
    def test_unknown_and_negative_start_results_do_not_poison_cache(self): self.check("unsupported")
    def test_incomplete_rpc_reply_fails_closed(self): self.check("incomplete_reply")
    def test_rpc_load_registration_and_allocation_errors_release_correctly(self): self.check("errors")
    def test_oversized_argument_rejected_before_rpc(self): self.check("argument_overflow")


if __name__ == "__main__":
    unittest.main()
