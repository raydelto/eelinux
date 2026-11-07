"""Filter masked EE interrupts and make toggle-on-write mask changes idempotent."""
from __future__ import annotations
import difflib
from pathlib import Path
import subprocess

PATH = 'arch/mips/ps2/intc-irq.c'
def adapt(text: str) -> str:
    if (text.count('static void intc_reverse_mask(') != 1 or
            text.count('for (pending = inl(INTC_STAT);') != 1 or 'intc_set_mask' in text):
        raise RuntimeError('Review the INTC source before changing toggle-mask handling')
    start = text.index('static void intc_reverse_mask(')
    end = text.index('\n#define INTC_IRQ_TYPE', start)
    text = text[:start] + '''static DEFINE_RAW_SPINLOCK(intc_mask_lock);

static void intc_set_mask(struct irq_data *data, bool enabled)
{
	const unsigned int bit = BIT(data->irq - IRQ_INTC);
	unsigned long flags;

	/* INTC_MASK toggles written bits rather than assigning their value. */
	raw_spin_lock_irqsave(&intc_mask_lock, flags);
	if (!!(inl(INTC_MASK) & bit) != enabled) {
		outl(bit, INTC_MASK);
		(void)inl(INTC_MASK);
	}
	raw_spin_unlock_irqrestore(&intc_mask_lock, flags);
}

static void intc_mask(struct irq_data *data)
{
	intc_set_mask(data, false);
}

static void intc_unmask(struct irq_data *data)
{
	intc_set_mask(data, true);
}

static void intc_mask_ack(struct irq_data *data)
{
	intc_mask(data);
	outl(BIT(data->irq - IRQ_INTC), INTC_STAT);
}
''' + text[end:]
    text = text.replace('#include <linux/module.h>', '#include <linux/module.h>\n#include <linux/spinlock.h>', 1)
    text = text.replace('.irq_unmask = intc_reverse_mask,', '.irq_unmask = intc_unmask,', 1)
    text = text.replace('.irq_mask = intc_reverse_mask,', '.irq_mask = intc_mask,', 1)
    return text.replace('for (pending = inl(INTC_STAT);', 'for (pending = inl(INTC_STAT) & inl(INTC_MASK);', 1)

def install(source: Path, patch: Path) -> dict:
    before = (source / PATH).read_text(); after = adapt(before)
    blob = ''.join(difflib.unified_diff(before.splitlines(keepends=True), after.splitlines(keepends=True),
        fromfile='a/' + PATH, tofile='b/' + PATH)).encode()
    if patch.exists() and patch.read_bytes() != blob:
        raise RuntimeError('Preserve the reviewed INTC patch; differing source needs review')
    patch.parent.mkdir(parents=True, exist_ok=True); patch.write_bytes(blob)
    subprocess.run(['git', 'apply', '--check', str(patch.resolve())], cwd=source, check=True)
    subprocess.run(['git', 'apply', str(patch.resolve())], cwd=source, check=True)
    return {'path': PATH, 'physical_interrupt_storm_tested': False}
