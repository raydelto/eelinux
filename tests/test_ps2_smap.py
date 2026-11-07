"""Exercise the actual native SMAP packet functions against independent MMIO."""
from pathlib import Path
import os
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
DRIVER = ROOT / "kernel/new-drivers/ps2-smap"
sys.path.insert(0, str(ROOT / "scripts"))
from kernel_smap_port import BRIDGE_FILES, bridge_changes, driver_changes


def function(source, name):
    """Extract an entire definition, rejecting ambiguity rather than editing it."""
    lines = source.splitlines(keepends=True)
    starts = [i for i, line in enumerate(lines) if name + "(" in line and line.startswith("static ")]
    if len(starts) != 1:
        raise ValueError(f"Expected one definition of {name}")
    start = starts[0]
    end = next(i for i in range(start, len(lines)) if lines[i] == "}\n")
    return "".join(lines[start:end + 1])


class NativeSmap(unittest.TestCase):
    @unittest.skipUnless(os.name != "nt" and shutil.which("cc"), "requires native Linux C compiler")
    def test_actual_phy_loader_handoff_and_failures(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary)
            (path / "linux").mkdir()
            (path / "linux/phy.h").write_text("/* PHY API is modelled by the fixture. */\n")
            result = subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                                     "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
                                     "-I", str(path), "-I", str(DRIVER),
                                     str(ROOT / "tests/fixtures/ps2_smap_phy.c"),
                                     "-o", str(path / "check")], text=True, capture_output=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            result = subprocess.run([str(path / "check")], text=True, capture_output=True, timeout=10)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("5 failure paths passed", result.stdout)

    def test_phy_cleanup_precedes_selected_driver_initialization_and_unwinds(self):
        for name, connect, failure in (
                ("ps2-smap-iop.c", "phy_connect_direct(ndev,s->phy", "goto fail_bus"),
                ("ps2-smap.c", "phy_connect_direct(ndev, s->phy", "goto err_bus")):
            source = (DRIVER / name).read_text()
            prepare = source.index("smap_phy_prepare(s->phy)")
            attach = source.index(connect)
            self.assertLess(prepare, attach)
            self.assertIn(failure, source[prepare:attach])
            self.assertLess(attach, source.index("phy_set_max_speed"))
        helper = (DRIVER / "ps2-smap-phy.h").read_text()
        self.assertIn("genphy_soft_reset(phy)", helper)
        for forced in ("SPEED_10", "SPEED_100", "DUPLEX_FULL", "MII_ADVERTISE"):
            self.assertNotIn(forced, helper)

    @unittest.skipUnless(os.name != "nt" and shutil.which("cc"), "requires native Linux C compiler")
    def test_actual_iop_smap_on_independent_bus(self):
        service = (ROOT / "kernel/iop-overlay/module/smap-service.h").read_text()
        for name in ("ps2-smap-core.h", "ps2-smap-regs.h", "ps2-smap-rpc.h"):
            service = service.replace('"iopmod/' + name + '"', '"' + name + '"')
        harness = (ROOT / "tests/fixtures/ps2_smap_iop.c").read_text().replace("/* IOP_SERVICE */", service)
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary)
            (path / "check.c").write_text(harness)
            subprocess.run(["cc", "-std=gnu11", "-Wall", "-Wextra", "-Werror",
                            "-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-I", str(DRIVER),
                            str(path / "check.c"), "-o", str(path / "check")], check=True, capture_output=True, text=True)
            result = subprocess.run([str(path / "check")], capture_output=True, text=True, timeout=10)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("IOP SMAP:", result.stdout)

    @unittest.skipUnless(os.name != "nt" and shutil.which("cc"), "requires native Linux C compiler")
    def test_actual_cold_mdio_and_reset_failure_paths(self):
        source = (DRIVER / "ps2-smap.c").read_text()
        actual = "\n".join(function(source, name) for name in
                           ("bd_reg", "smap_write_mac", "smap_write_filter", "smap_hw_reset", "smap_mdio"))
        harness = (ROOT / "tests/fixtures/ps2_smap_startup.c").read_text().replace(
            "/* DRIVER_FUNCTIONS */", actual)
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary)
            (path / "check.c").write_text(harness)
            result = subprocess.run(["cc", "-std=gnu11", "-Wall", "-Wextra", "-Werror",
                                     "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
                                     "-I", str(DRIVER), str(path / "check.c"), "-o", str(path / "check")],
                                    text=True, capture_output=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            result = subprocess.run([str(path / "check")], text=True, capture_output=True, timeout=10)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("cold MDIO", result.stdout)

    @unittest.skipUnless(os.name != "nt" and shutil.which("cc"), "requires native Linux C compiler")
    def test_actual_builtin_bridge_failure_paths(self):
        vendor = ROOT / "kernel/vendor/frno7/snapshot"
        before = {path: (vendor / path).read_text() for path in BRIDGE_FILES}
        updated = bridge_changes(before)
        actual = "\n".join(function(updated[path], name) for path, name in
                           (("drivers/ps2/iop-heap.c", "iop_heap_init"),
                            ("drivers/ps2/iop-heap.c", "iop_heap_exit"),
                            ("drivers/ps2/iop-module-request.c", "iop_module_init")))
        harness = (ROOT / "tests/fixtures/ps2_smap_bridge.c").read_text().replace(
            "/* DRIVER_FUNCTIONS */", actual)
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary)
            (path / "check.c").write_text(harness)
            result = subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                                     "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
                                     str(path / "check.c"), "-o", str(path / "check")],
                                    text=True, capture_output=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            result = subprocess.run([str(path / "check")], text=True,
                                    capture_output=True, timeout=10)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_builtin_bridge_registration_and_failure_guards(self):
        vendor = ROOT / "kernel/vendor/frno7/snapshot"
        before = {path: (vendor / path).read_text() for path in BRIDGE_FILES}
        updated = bridge_changes(before)
        make = updated["drivers/ps2/Makefile"]
        positions = [make.index(" += " + name + ".o") for name in
                     ("iop-memory", "sif", "iop-heap", "iop-module", "iop-irq")]
        self.assertEqual(positions, sorted(positions))
        self.assertIn("obj-m\t\t\t\t+= iop-dev9.o", make)
        self.assertIn("if (!sif_is_ready())", updated["drivers/ps2/iop-heap.c"])
        self.assertIn("if (!iop_heap_is_ready())", updated["drivers/ps2/iop-module-request.c"])
        self.assertIn("if (!READ_ONCE(iop_module_ready))", updated["drivers/ps2/iop-module-request.c"])
        for changed in (updated, {**before, "drivers/ps2/Makefile": make},
                        {**before, "drivers/ps2/iop-heap.c": before["drivers/ps2/iop-heap.c"].replace(
                            "return sif_rpc_bind(&iop_heap_rpc, SIF_SID_HEAP);", "return 0;")}):
            with self.assertRaises(RuntimeError):
                bridge_changes(changed)

    @unittest.skipUnless(os.name != "nt" and shutil.which("cc"), "requires native Linux C compiler")
    def test_actual_bounded_control_and_late_replies(self):
        source = (DRIVER / "ps2-smap.c").read_text()
        actual = "\n".join(function(source, name) for name in
                           ("smap_dev9_reply", "smap_dev9_control"))
        harness = (ROOT / "tests/fixtures/ps2_smap_control.c").read_text().replace(
            "/* DRIVER_FUNCTIONS */", actual)
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary)
            (path / "check.c").write_text(harness)
            result = subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                                     "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
                                     str(path / "check.c"), "-o", str(path / "check")],
                                    text=True, capture_output=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            result = subprocess.run([str(path / "check")], text=True,
                                    capture_output=True, timeout=10)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_guarded_source_registration(self):
        before = 'menu "Ethernet"\nsource "drivers/net/ethernet/other/Kconfig"\nendif # ETHERNET\n'
        makefile = 'obj-$(CONFIG_OTHER_NET) += other/\n'
        changes = driver_changes(before, makefile)
        self.assertEqual(changes["drivers/net/ethernet/Kconfig"], before.replace(
            'endif # ETHERNET\n', 'source "drivers/net/ethernet/ps2/Kconfig"\n\nendif # ETHERNET\n'))
        self.assertTrue(changes["drivers/net/ethernet/Makefile"].startswith(makefile))
        self.assertEqual(len(changes), 11)
        for name in ("ps2-smap.c", "ps2-smap-core.h", "ps2-smap-regs.h",
                     "ps2-smap-iop.c", "ps2-smap-phy.h"):
            self.assertEqual(changes["drivers/net/ethernet/ps2/" + name], (DRIVER / name).read_text())
        for config, mk, installed in ((before, makefile, True),
                                      (before + before, makefile, False),
                                      (before.replace("endif # ETHERNET", "endif"), makefile, False),
                                      (changes["drivers/net/ethernet/Kconfig"], makefile, False),
                                      (before, changes["drivers/net/ethernet/Makefile"], False)):
            with self.subTest(config=config):
                with self.assertRaises(RuntimeError):
                    driver_changes(config, mk, installed)

    @unittest.skipUnless(os.name != "nt" and shutil.which("cc"), "requires native Linux C compiler")
    def test_actual_packet_and_irq_handlers(self):
        source = (DRIVER / "ps2-smap.c").read_text()
        names = ("bd_reg", "smap_tx_complete", "smap_xmit", "smap_rearm",
                 "smap_rearm_work", "smap_poll", "smap_interrupt")
        actual = "\n".join(function(source, name) for name in names)
        harness = (ROOT / "tests/fixtures/ps2_smap_mmio.c").read_text().replace(
            "/* DRIVER_FUNCTIONS */", actual)
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary)
            (path / "check.c").write_text(harness)
            result = subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                                     "-Wno-unused-function", "-fsanitize=address,undefined",
                                     "-fno-omit-frame-pointer", "-I", str(DRIVER),
                                     str(path / "check.c"), "-o", str(path / "check")],
                                    text=True, capture_output=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            result = subprocess.run([str(path / "check")], text=True,
                                    capture_output=True, timeout=10)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("handlers:", result.stdout)


if __name__ == "__main__":
    unittest.main()
