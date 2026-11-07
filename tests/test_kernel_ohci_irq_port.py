"""Check the actual PS2 OHCI wrapper's spurious-relay and controller-state paths."""
from pathlib import Path
import os
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
from kernel_ohci_irq_port import modernize_ohci_irq


def reviewed_source():
    return (ROOT / 'kernel/vendor/frno7/snapshot/drivers/usb/host/ohci-ps2.c').read_text()


class OhciIRQPort(unittest.TestCase):
    def test_changed_or_repeated_wrapper_is_rejected(self):
        source = reviewed_source()
        for changed in (source.replace('return ohci_irq(hcd);', 'return IRQ_NONE;'),
                        source + source, modernize_ohci_irq(source)):
            with self.subTest(change=changed[:40]):
                with self.assertRaises(RuntimeError):
                    modernize_ohci_irq(changed)

    def test_other_driver_code_and_license_are_preserved(self):
        source = reviewed_source()
        updated = modernize_ohci_irq(source)
        self.assertEqual(source.split('static irqreturn_t ohci_ps2_irq(', 1)[0],
                         updated.split('static irqreturn_t ohci_ps2_irq(', 1)[0])
        self.assertEqual(source.split('static int iopheap_alloc_dma_buffer(', 1)[1],
                         updated.split('static int iopheap_alloc_dma_buffer(', 1)[1])

    @unittest.skipUnless(os.name != 'nt' and shutil.which('cc'), 'requires native Linux C compiler')
    def test_actual_wrapper_restores_only_running_enabled_spurious_irq(self):
        source = modernize_ohci_irq(reviewed_source())
        wrapper = 'static irqreturn_t ohci_ps2_irq(' + source.split(
            'static irqreturn_t ohci_ps2_irq(', 1)[1].split(
            '\nstatic int iopheap_alloc_dma_buffer(', 1)[0]
        harness = r'''
#include <stdint.h>
#include <assert.h>
typedef uint32_t u32;
typedef int irqreturn_t;
enum { IRQ_NONE, IRQ_HANDLED };
enum { OHCI_RH_HALTED, OHCI_RH_SUSPENDED, OHCI_RH_RUNNING };
#define OHCI_INTR_MIE (UINT32_C(1) << 31)
#define __iomem
struct ohci_regs { u32 intrenable, intrdisable, control; };
struct ohci_hcd { struct ohci_regs *regs; int rh_state; };
struct usb_hcd { struct ohci_hcd *ohci; };
static int flushed, disabled, enabled, callback_calls, callback_result;
static int callback_state, callback_restores;
static struct ohci_hcd *hcd_to_ohci(struct usb_hcd *hcd) { return hcd->ohci; }
static u32 ohci_readl(struct ohci_hcd *ohci, u32 *reg) {
    if (reg == &ohci->regs->control) flushed++;
    return *reg;
}
static void ohci_writel(struct ohci_hcd *ohci, u32 value, u32 *reg) {
    assert(value == OHCI_INTR_MIE);
    if (reg == &ohci->regs->intrdisable) {
        disabled++; ohci->regs->intrenable &= ~value;
    } else {
        assert(reg == &ohci->regs->intrenable);
        enabled++; ohci->regs->intrenable |= value;
    }
}
static irqreturn_t generic_irq(struct usb_hcd *hcd) {
    assert(!(hcd->ohci->regs->intrenable & OHCI_INTR_MIE));
    assert(disabled == 1 && flushed == 0);
    callback_calls++;
    hcd->ohci->rh_state = callback_state;
    if (callback_restores)
        ohci_writel(hcd->ohci, OHCI_INTR_MIE, &hcd->ohci->regs->intrenable);
    return callback_result;
}
static irqreturn_t (*ohci_irq)(struct usb_hcd *) = generic_irq;
'''
        harness += wrapper
        harness += r'''
static void check(u32 incoming, int state, int result, int generic_restore,
                  u32 expected, int expected_enables, int expected_flushes) {
    struct ohci_regs regs = { .intrenable = incoming };
    struct ohci_hcd ohci = { &regs, OHCI_RH_RUNNING };
    struct usb_hcd hcd = { &ohci };
    disabled = enabled = flushed = callback_calls = 0;
    callback_state = state; callback_result = result; callback_restores = generic_restore;
    assert(ohci_ps2_irq(&hcd) == result);
    assert(disabled == 1 && callback_calls == 1);
    assert(regs.intrenable == expected);
    assert(enabled == expected_enables && flushed == expected_flushes);
}
int main(void) {
    const u32 bits = 0x5a, mie = OHCI_INTR_MIE;
    /* A delayed relay with no pending IRQ must preserve live interrupts. */
    check(bits | mie, OHCI_RH_RUNNING, IRQ_NONE, 0, bits | mie, 1, 1);
    /* Intentionally masked controllers remain masked. */
    check(bits, OHCI_RH_RUNNING, IRQ_NONE, 0, bits, 0, 0);
    check(bits | mie, OHCI_RH_HALTED, IRQ_NONE, 0, bits, 0, 0);
    check(bits | mie, OHCI_RH_SUSPENDED, IRQ_NONE, 0, bits, 0, 0);
    /* Generic handled IRQ/recovery paths retain their own decisions. */
    check(bits | mie, OHCI_RH_RUNNING, IRQ_HANDLED, 1, bits | mie, 1, 0);
    check(bits | mie, OHCI_RH_HALTED, IRQ_HANDLED, 0, bits, 0, 0);
    return 0;
}
'''
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp)
            (path / 'check.c').write_text(harness)
            subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', str(path / 'check.c'),
                            '-o', str(path / 'check')], check=True, capture_output=True, text=True)
            subprocess.run([str(path / 'check')], check=True, timeout=5)


if __name__ == '__main__':
    unittest.main()
