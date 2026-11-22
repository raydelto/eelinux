"""Order built-in PS2 OHCI after the SIF bridge and report controller stages."""
from pathlib import Path
import difflib
import subprocess
from common import same_patch_content, write_reviewed_patch

PATHS = ('drivers/ps2/Makefile', 'drivers/usb/host/ohci-ps2.c', 'drivers/ps2/iop-irq.c')
def adapt_makefile(text):
    before = 'obj-m\t\t\t\t+= iop-registers.o'
    if text.count(before) != 1: raise RuntimeError('Review IOP register-provider Kbuild')
    return text.replace(before, 'obj-$(CONFIG_PS2_IOP_BRIDGE) += iop-registers.o', 1)

def adapt_ohci(text):
    if text.count('module_init(ohci_ps2_init);') != 1 or 'ohci_ps2_start_driver' in text:
        raise RuntimeError('Review PS2 OHCI initialization before adapting')
    text = text.replace('static irqreturn_t (*ohci_irq)(struct usb_hcd *hcd);',
        'static irqreturn_t (*ohci_irq)(struct usb_hcd *hcd);\nstatic int (*ohci_start_driver)(struct usb_hcd *hcd);', 1)
    text = text.replace('\tohci_ps2_start_hc(hcd);\n\n\terr = ohci_setup(hcd);',
        '\tdev_info(hcd->self.controller, "OHCI: configure IOP DMA\\n");\n'
        '\tohci_ps2_start_hc(hcd);\n'
        '\tdev_info(hcd->self.controller, "OHCI: initialize schedules\\n");\n\n'
        '\terr = ohci_setup(hcd);', 1)
    text = text.replace('\tohci_ps2_enable(hcd);\n\n\treturn 0;',
        '\tohci_ps2_enable(hcd);\n'
        '\tdev_info(hcd->self.controller, "OHCI: reset complete, requesting IRQ relay\\n");\n\n\treturn 0;', 1)
    anchor = 'static int iopheap_alloc_dma_buffer('
    text = text.replace(anchor, '''static int ohci_ps2_start_driver(struct usb_hcd *hcd)
{
	int err;

	dev_info(hcd->self.controller, "OHCI: IRQ ready, starting controller\\n");
	err = ohci_start_driver(hcd);
	if (!err)
		dev_info(hcd->self.controller, "OHCI: controller running\\n");
	return err;
}

''' + anchor, 1)
    text = text.replace('\tohci_irq = ohci_ps2_hc_driver.irq;',
        '\tohci_start_driver = ohci_ps2_hc_driver.start;\n'
        '\tohci_ps2_hc_driver.start = ohci_ps2_start_driver;\n\n'
        '\tohci_irq = ohci_ps2_hc_driver.irq;', 1)
    return text.replace('module_init(ohci_ps2_init);',
        '#ifdef MODULE\nmodule_init(ohci_ps2_init);\n#else\n'
        '/* All SIF/heap/module/IRQ device initcalls must finish first. */\n'
        'late_initcall_sync(ohci_ps2_init);\n#endif', 1)

def adapt_iop_irq(text):
    if text.count('id = iop_module_request("irqrelay", 0x0100, NULL);') != 1 or 'IOP USB: loading IRQ relay' in text:
        raise RuntimeError('Review IOP IRQ resource callbacks before adding startup diagnostics')
    pairs = (
        ('\t\tid = iop_module_request("irqrelay", 0x0100, NULL);',
         '\t\tif (data->irq == IRQ_IOP_USB)\n\t\t\tpr_info("IOP USB: loading IRQ relay\\n");\n'
         '\t\tid = iop_module_request("irqrelay", 0x0100, NULL);'),
        ('\t\terr = sif_rpc_bind(&iop_irq_rpc, SIF_SID_IRQ_RELAY);',
         '\t\tif (data->irq == IRQ_IOP_USB)\n\t\t\tpr_info("IOP USB: binding IRQ relay\\n");\n'
         '\t\terr = sif_rpc_bind(&iop_irq_rpc, SIF_SID_IRQ_RELAY);'),
        ('\terr = sif_rpc(&iop_irq_rpc, rpo_request_irq,',
         '\tif (data->irq == IRQ_IOP_USB)\n\t\tpr_info("IOP USB: registering interrupt\\n");\n'
         '\terr = sif_rpc(&iop_irq_rpc, rpo_request_irq,'),
        ('\tif (err >= 0)\n\t\terr = status;',
         '\tif (err >= 0)\n\t\terr = status;\n'
         '\tif (data->irq == IRQ_IOP_USB)\n\t\tpr_info("IOP USB: IRQ request result %d\\n", err);'),
    )
    for before, after in pairs:
        if text.count(before) != 1: raise RuntimeError('IOP IRQ startup anchor changed')
        text = text.replace(before, after, 1)
    return text

def install(source: Path, patch: Path) -> dict:
    changes = [(name, (source / name).read_text(), adapter((source / name).read_text()))
        for name, adapter in zip(PATHS, (adapt_makefile, adapt_ohci, adapt_iop_irq))]
    blob = ''.join(''.join(difflib.unified_diff(before.splitlines(keepends=True), after.splitlines(keepends=True),
        fromfile='a/' + name, tofile='b/' + name)) for name, before, after in changes).encode()
    if patch.exists() and not same_patch_content(patch.read_bytes(), blob): raise RuntimeError('Preserve the reviewed USB boot patch')
    patch.parent.mkdir(parents=True, exist_ok=True); write_reviewed_patch(patch, blob)
    subprocess.run(['git', 'apply', '--check', str(patch.resolve())], cwd=source, check=True)
    subprocess.run(['git', 'apply', str(patch.resolve())], cwd=source, check=True)
    return {'built_in_start_order': 'late_initcall_sync after SIF/IOP bridge', 'physical_USB_root_tested': False}
