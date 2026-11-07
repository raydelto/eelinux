"""Preserve the PS2 OHCI interrupt enable across an unclaimed SIF relay.

The PS2 workaround masks MIE before invoking the generic OHCI handler. Its
IRQ_NONE path does not restore MIE, so a delayed relay can disable all further
controller interrupts. Restore only the incoming enabled state of a running
controller; do not wake a suspended, halted or intentionally masked device.
SPDX-License-Identifier: GPL-2.0-only
"""

_OLD = '''static irqreturn_t ohci_ps2_irq(struct usb_hcd *hcd)
{
	struct ohci_hcd	*ohci = hcd_to_ohci(hcd);
	struct ohci_regs __iomem *regs = ohci->regs;

	/*
	 * OHCI_INTR_MIE needs to be disabled, most likely due to a
	 * hardware bug. Without it, reading a large amount of data
	 * (> 1 GB) from a mass storage device results in a freeze.
	 */
	ohci_writel(ohci, OHCI_INTR_MIE, &regs->intrdisable);

	return ohci_irq(hcd);	/* Call normal IRQ handler. */
}'''

_NEW = '''static irqreturn_t ohci_ps2_irq(struct usb_hcd *hcd)
{
	struct ohci_hcd	*ohci = hcd_to_ohci(hcd);
	struct ohci_regs __iomem *regs = ohci->regs;
	const u32 enabled = ohci_readl(ohci, &regs->intrenable);
	irqreturn_t status;

	/*
	 * OHCI_INTR_MIE needs to be disabled, most likely due to a
	 * hardware bug. Without it, reading a large amount of data
	 * (> 1 GB) from a mass storage device results in a freeze.
	 */
	ohci_writel(ohci, OHCI_INTR_MIE, &regs->intrdisable);

	status = ohci_irq(hcd);	/* Call normal IRQ handler. */
	/* A delayed SIF relay may find its interrupt already acknowledged. */
	if (status == IRQ_NONE && (enabled & OHCI_INTR_MIE) &&
	    ohci->rh_state == OHCI_RH_RUNNING) {
		ohci_writel(ohci, OHCI_INTR_MIE, &regs->intrenable);
		(void)ohci_readl(ohci, &regs->control);	/* Flush the write. */
	}
	return status;
}'''


def modernize_ohci_irq(text: str) -> str:
    """Adapt only the exact reviewed IRQ wrapper, rejecting changed inputs."""
    if text.count(_OLD) != 1 or text.count('static irqreturn_t ohci_ps2_irq(') != 1:
        raise RuntimeError('Unexpected PS2 OHCI IRQ source; review MIE restoration')
    return text.replace(_OLD, _NEW, 1)
