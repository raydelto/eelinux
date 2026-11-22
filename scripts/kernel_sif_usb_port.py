"""Serialize SIF DMA, respect DMAC masks, and prepare USB's IOP relay first."""
from pathlib import Path
import difflib
import subprocess
from common import same_patch_content, write_reviewed_patch

PATHS = ('arch/mips/ps2/dmac-irq.c', 'drivers/ps2/sif.c',
         'drivers/usb/host/ohci-ps2.c', 'drivers/ps2/iop-module-request.c',
         'drivers/ps2/iop-irq.c', 'arch/mips/include/asm/mach-ps2/irq.h')

def adapt_dmac(text):
    if text.count('static void dmac_reverse_mask(') != 1:
        raise RuntimeError('Review DMAC toggle-mask source')
    start = text.index('static void dmac_reverse_mask(')
    end = text.index('\n#define DMAC_IRQ_TYPE', start)
    text = text[:start] + '''static DEFINE_RAW_SPINLOCK(dmac_mask_lock);

static void dmac_set_mask(struct irq_data *data, bool enabled)
{
	const unsigned int bit = BIT(data->irq - IRQ_DMAC);
	unsigned long flags;

	/* BUSERR has status/ack bit 15 but no mask bit in D_STAT. */
	if (bit == BIT(15))
		return;
	raw_spin_lock_irqsave(&dmac_mask_lock, flags);
	if (!!(inl(DMAC_STAT_MASK) & (bit << 16)) != enabled) {
		/* Lower bits acknowledge status; upper bits toggle masks. */
		outl(bit << 16, DMAC_STAT_MASK);
		(void)inl(DMAC_STAT_MASK);
	}
	raw_spin_unlock_irqrestore(&dmac_mask_lock, flags);
}

static void dmac_mask(struct irq_data *data)
{
	dmac_set_mask(data, false);
}

static void dmac_unmask(struct irq_data *data)
{
	dmac_set_mask(data, true);
}

static void dmac_mask_ack(struct irq_data *data)
{
	dmac_mask(data);
	outl(BIT(data->irq - IRQ_DMAC), DMAC_STAT_MASK);
}
''' + text[end:]
    before = '\tunsigned int pending = inl(DMAC_STAT_MASK) & 0xffff;'
    if text.count(before) != 1: raise RuntimeError('Review DMAC cascade')
    text = text.replace(before, '\tconst unsigned int status = inl(DMAC_STAT_MASK);\n'
        '\t/* BUSERR is unmaskable; never discard a real bus fault. */\n'
        '\tunsigned int pending = ((status & (status >> 16)) |\n'
        '\t\t(status & BIT(15))) & 0xffff;', 1)
    text = text.replace('#include <linux/module.h>', '#include <linux/module.h>\n#include <linux/spinlock.h>', 1)
    return text.replace('.irq_unmask = dmac_reverse_mask,', '.irq_unmask = dmac_unmask,', 1).replace(
        '.irq_mask = dmac_reverse_mask,', '.irq_mask = dmac_mask,', 1)

def adapt_sif(text):
    if 'sif1_dma_lock' in text or text.count('static int sif1_write_ert_int_0(') != 1:
        raise RuntimeError('Review SIF packet transport')
    text = text.replace('static DEFINE_SPINLOCK(sregs_lock);',
        'static DEFINE_SPINLOCK(sregs_lock);\nstatic DEFINE_SPINLOCK(sif1_dma_lock);', 1)
    text = text.replace('\tdma_addr_t madr;\n', '\tdma_addr_t madr;\n\tunsigned long flags;\n\tint err = 0;\n', 1)
    before = '\tif (!sif1_ready())\n\t\treturn -EBUSY;'
    if text.count(before) != 1: raise RuntimeError('Review SIF DMA readiness')
    text = text.replace(before, '''	/* A command from an IRQ must not overwrite a task's half-built packet.
	 * SIF1 completion is hardware driven and does not require an EE IRQ.
	 */
	spin_lock_irqsave(&sif1_dma_lock, flags);
	if (!sif1_ready()) {
		err = -EBUSY;
		goto out;
	}''', 1)
    before = '\toutl(DMAC_CHCR_SENDN_TIE, DMAC_SIF1_CHCR);\n\n\treturn 0;'
    if text.count(before) != 1: raise RuntimeError('Review SIF DMA launch')
    text = text.replace(before, '\toutl(DMAC_CHCR_SENDN_TIE, DMAC_SIF1_CHCR);\n'
        '\t(void)inl(DMAC_SIF1_CHCR);\n'
        'out:\n\tspin_unlock_irqrestore(&sif1_dma_lock, flags);\n\treturn err;', 1)
    # Retain ownership while a late IOP DMA/callback may still use the client.
    # An unsafe timeout/free would corrupt memory; report the actual stalled RPC.
    text = text.replace('\twait_for_completion(&client->done);',
        '\twhile (!wait_for_completion_timeout(&client->done, 5 * HZ))\n'
        '\t\tpr_warn("SIF: waiting for bind server %#x (DMA status %#x)\\n",\n'
        '\t\t\tserver_id, inl(DMAC_STAT_MASK));', 1)
    text = text.replace('\twait_for_completion(&client->done);',
        '\twhile (!wait_for_completion_timeout(&client->done, 5 * HZ))\n'
        '\t\tpr_warn("SIF: waiting for RPC %u server %#x (DMA status %#x)\\n",\n'
        '\t\t\trpc_id, client->server, inl(DMAC_STAT_MASK));', 1)
    return text

def adapt_ohci(text):
    if 'OHCI: prepare IRQ relay before controller setup' in text:
        raise RuntimeError('USB relay preparation already applied')
    text = text.replace('#include <asm/mach-ps2/iop-heap.h>',
        '#include <asm/mach-ps2/iop-heap.h>\n#include <asm/mach-ps2/irq.h>', 1)
    before = '\tohci_ps2_enable(hcd);\n\tdev_info(hcd->self.controller, "OHCI: reset complete, requesting IRQ relay\\n");'
    if text.count(before) != 1: raise RuntimeError('Review OHCI reset/start ordering')
    text = text.replace(before, '\tdev_info(hcd->self.controller, "OHCI: reset complete, requesting IRQ relay\\n");', 1)
    text = text.replace('\terr = ohci_start_driver(hcd);', '\tohci_ps2_enable(hcd);\n\terr = ohci_start_driver(hcd);\n'
        '\tif (err)\n\t\tohci_ps2_disable(hcd);', 1)
    before = '\terr = iopheap_alloc_dma_buffer(pdev, DMA_BUFFER_SIZE);'
    if text.count(before) != 1: raise RuntimeError('Review OHCI IOP buffer setup')
    text = text.replace(before, '''	/* Keep USB quiet while LOADFILE links the interrupt relay. Do this
	 * before enabling the PS2-specific gate or configuring controller DMA.
	 */
	writel(OHCI_INTR_MIE, hcd->regs + 0x14);
	(void)readl(hcd->regs + 0x14);
	dev_info(dev, "OHCI: prepare IRQ relay before controller setup\\n");
	err = iop_irq_prepare();
	if (err < 0) {
		dev_err(dev, "IOP IRQ relay preparation failed: %d\\n", err);
		goto err_alloc_dma_buffer;
	}
	dev_info(dev, "OHCI: IRQ relay firmware ready\\n");

''' + before, 1)
    return text

def adapt_irq(text):
    if 'int iop_irq_prepare(void)' in text:
        raise RuntimeError('IOP IRQ preparation already applied')
    start = text.index('\tif (!irq_relay) {', text.index('static int iop_irq_request_resources('))
    end = text.index('\n\tif (data->irq == IRQ_IOP_USB)\n\t\tpr_info("IOP USB: registering interrupt', start)
    text = text[:start] + '\terr = iop_irq_prepare_locked();\n\tif (err < 0)\n\t\tgoto out;\n' + text[end:]
    helper = '''static int iop_irq_prepare_locked(void)
{
	int err;

	if (irq_relay)
		return 0;
	pr_info("IOP USB: loading IRQ relay\\n");
	err = iop_module_request("irqrelay", 0x0100, NULL);
	if (err < 0)
		return err;
	pr_info("IOP USB: binding IRQ relay\\n");
	err = sif_rpc_bind(&iop_irq_rpc, SIF_SID_IRQ_RELAY);
	if (err < 0) {
		pr_err("IOP USB: relay bind failed: %d\\n", err);
		return err;
	}
	irq_relay = true;
	return 0;
}

int iop_irq_prepare(void)
{
	int err;

	mutex_lock(&iop_irq_lock);
	err = iop_irq_prepare_locked();
	mutex_unlock(&iop_irq_lock);
	return err;
}
EXPORT_SYMBOL_GPL(iop_irq_prepare);

'''
    return text.replace('static int iop_irq_request_resources(', helper + 'static int iop_irq_request_resources(', 1)

def adapt_irq_header(text):
    if 'iop_irq_prepare' in text or text.count('#endif') != 1:
        raise RuntimeError('Review PS2 IRQ header')
    return text.replace('#endif', '/* Prepare the IOP relay in sleeping context before enabling USB DMA. */\n'
        'int iop_irq_prepare(void);\n\n#endif', 1)

def adapt_loader(text):
    if 'IOP firmware: %s: read' in text: raise RuntimeError('IOP loader checkpoints already applied')
    pairs = (
        ('\terr = request_firmware(&fw, filepath, iop_module_device);',
         '\tpr_info("IOP firmware: %s: read\\n", name);\n\terr = request_firmware(&fw, filepath, iop_module_device);'),
        ('\terr = iop_module_request_dependencies(ehdr, name);',
         '\tpr_info("IOP firmware: %s: resolve imports\\n", name);\n\terr = iop_module_request_dependencies(ehdr, name);'),
        ('\terr = iop_module_link_buffer(fw->data, fw->size, arg);',
         '\tpr_info("IOP firmware: %s: start on IOP\\n", name);\n'
         '\terr = iop_module_link_buffer(fw->data, fw->size, arg);\n'
         '\tpr_info("IOP firmware: %s: start result %d\\n", name, err);'),
        ('\terr = sif_rpc(&load_file_rpc_client, rpo_mod_buf_load,',
         '\tpr_info("IOP firmware: submit LOADFILE at %#x (%zu bytes)\\n", link.addr, nbyte);\n'
         '\terr = sif_rpc(&load_file_rpc_client, rpo_mod_buf_load,'),
    )
    for before, after in pairs:
        if text.count(before) != 1: raise RuntimeError('Review IOP module loader checkpoint')
        text = text.replace(before, after, 1)
    return text

def install(source: Path, patch: Path) -> dict:
    changes = [(name, (source/name).read_text(), adapter((source/name).read_text()))
        for name, adapter in zip(PATHS, (adapt_dmac, adapt_sif, adapt_ohci, adapt_loader, adapt_irq, adapt_irq_header))]
    blob = ''.join(''.join(difflib.unified_diff(before.splitlines(keepends=True), after.splitlines(keepends=True),
        fromfile='a/'+name, tofile='b/'+name)) for name,before,after in changes).encode()
    if patch.exists() and not same_patch_content(patch.read_bytes(), blob): raise RuntimeError('Preserve reviewed SIF/USB patch')
    patch.parent.mkdir(parents=True,exist_ok=True);write_reviewed_patch(patch, blob)
    subprocess.run(['git','apply','--check',str(patch.resolve())],cwd=source,check=True)
    subprocess.run(['git','apply',str(patch.resolve())],cwd=source,check=True)
    return {'hardware_USB_root_tested': False, 'IOP_RPC_timeout': 'diagnostic wait; DMA/client ownership retained'}
