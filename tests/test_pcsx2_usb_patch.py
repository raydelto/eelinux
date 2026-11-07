"""Compile the actual delayed USB IRQ query/callback from the shipped patch."""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile
import unittest

from tests.test_pcsx2_mmu_patch import PATCH, added_function


class Pcsx2USBInterrupt(unittest.TestCase):
    def test_interface_and_exception_trace(self):
        patch = PATCH.read_text()
        self.assertIn("+\tbool IRQPending();", patch)
        self.assertIn('-\t\t\tConsole.Warning("branch delay!!");', patch)
        self.assertIn('+\t\t\tCPU_LOG("Branch delay exception at %08x", cpuRegs.pc);', patch)

    @unittest.skipUnless(os.name != "nt" and shutil.which("c++"), "requires native C++ compiler")
    def test_delayed_event_uses_current_controller_level(self):
        query = added_function("pcsx2/USB/USB.cpp", "bool USB::IRQPending()")
        callback = added_function("pcsx2/IopIrq.cpp", "void usbInterrupt()")
        harness = r'''
#include <cassert>
#include <cstdint>
using u32 = std::uint32_t;
constexpr u32 OHCI_INTR_MIE = 1u << 31;
struct OHCIState { u32 intr = 0; u32 intr_status = 0; };
static OHCIState* s_qemu_ohci = nullptr;
namespace USB { bool IRQPending(); }
static unsigned delivered;
static void iopIntcIrq(unsigned irq) { assert(irq == 22); ++delivered; }
''' + query + callback + r'''
int main() {
    // A previously scheduled event must be harmless after USB shutdown.
    usbInterrupt();
    assert(delivered == 0);
    OHCIState controller;
    s_qemu_ohci = &controller;
    constexpr u32 WDH = 1u << 1, SF = 1u << 2;
    // Neither MIE alone nor an enabled source without status asserts IRQ.
    controller.intr = OHCI_INTR_MIE | WDH;
    usbInterrupt();
    assert(delivered == 0);
    controller.intr_status = WDH;
    usbInterrupt();
    assert(delivered == 1);
    // Acknowledgment before delayed delivery deasserts the source.
    controller.intr_status = 0;
    usbInterrupt();
    assert(delivered == 1);
    // Pending sources must be individually enabled and master enabled.
    controller.intr_status = WDH;
    controller.intr = OHCI_INTR_MIE | SF;
    usbInterrupt();
    controller.intr = WDH | SF;
    usbInterrupt();
    assert(delivered == 1);
    controller.intr = OHCI_INTR_MIE | SF;
    controller.intr_status = WDH | SF;
    usbInterrupt();
    assert(delivered == 2);
    // Restoring MIE permits a source which remained pending throughout.
    controller.intr = SF;
    usbInterrupt();
    controller.intr |= OHCI_INTR_MIE;
    usbInterrupt();
    assert(delivered == 3);
    // Reset clears source status; shutdown removes the controller entirely.
    controller.intr_status = 0;
    usbInterrupt();
    s_qemu_ohci = nullptr;
    usbInterrupt();
    assert(delivered == 3);
}
'''
        with tempfile.TemporaryDirectory(prefix="eelinux-usb-irq-test-") as directory:
            source = Path(directory) / "usb-irq.cpp"
            executable = Path(directory) / "usb-irq"
            source.write_text(harness)
            subprocess.run(["c++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
                            str(source), "-o", str(executable)], check=True)
            subprocess.run([str(executable)], check=True, timeout=10)


if __name__ == "__main__":
    unittest.main()
