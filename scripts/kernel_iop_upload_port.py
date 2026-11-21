"""Send IRX over SIF DMA; retain ownership and report startup progress once."""
from pathlib import Path
import difflib
import subprocess
from common import same_patch_content, write_reviewed_patch


def replace_once(text, before, after):
    if text.count(before) != 1:
        raise RuntimeError('Review IOP upload anchor: ' + before[:100])
    return text.replace(before, after, 1)


def adapt_sif(text):
    anchor = 'static int sif1_write_irq('
    helper = '''/* Use SIF DMA for IOP RAM: EE cache lines must never own IOP heap data.
 * Round only within the caller's allocation, and never read past src.
 */
int sif_write_data(iop_addr_t dst, const void *src, size_t nbytes)
{
	const u8 *p = src;
	bool reported = false;
	int err;

	if ((dst & 15) || nbytes > 0x200000 ||
	    dst > 0x200000 - ALIGN(nbytes, 16))
		return -EINVAL;
	if (!nbytes)
		return 0;
	if (!src)
		return -EINVAL;
	while (nbytes) {
		size_t count = min_t(size_t, nbytes, SIF1_BUFFER_SIZE - 16);
		err = sif1_write(NULL, dst, p, count);
		if (err)
			return err;
		/* Do not release dst or reuse the EE staging page during late DMA. */
		while (!sif1_ready()) {
			if (!reported) {
				pr_warn("SIF: firmware DMA stalled; retaining transfer memory\\n");
				reported = true;
			}
			msleep(1);
		}
		dst += ALIGN(count, 16);
		p += count;
		nbytes -= count;
	}
	return 0;
}
EXPORT_SYMBOL_GPL(sif_write_data);

'''
    text = replace_once(text, anchor, helper + anchor)
    text = replace_once(text,
        '\tmemcpy(&dma_buffer[0], &iop_dma_tag, sizeof(iop_dma_tag));',
        '\tmemset(dma_buffer, 0, dma_nbytes);\n'
        '\tmemcpy(&dma_buffer[0], &iop_dma_tag, sizeof(iop_dma_tag));')
    text = replace_once(text,
        '\tmemcpy(&dma_buffer[sizeof(iop_dma_tag)], header, header_size);',
        '\tif (header_size)\n\t\tmemcpy(&dma_buffer[sizeof(iop_dma_tag)], header, header_size);')
    text = replace_once(text,
        '\tmemcpy(&dma_buffer[sizeof(iop_dma_tag) + header_size], src, nbytes);',
        '\tif (nbytes)\n\t\tmemcpy(&dma_buffer[sizeof(iop_dma_tag) + header_size], src, nbytes);')
    text = replace_once(text, '#include <asm/io.h>',
        '#include <asm/io.h>\n#include <asm/mach-ps2/iop-memory.h>')
    helper = '''static void sif_report_stall(struct sif_rpc_client *client,
	u32 rpc_id, bool bind, bool *reported, u32 *last_phase)
{
	if (!*reported) {
		pr_warn("SIF: IOP reply stalled (%s %u, server %#x, DMA %#x); keeping DMA memory owned\\n",
			bind ? "bind" : "RPC", rpc_id, client->server,
			inl(DMAC_STAT_MASK));
		*reported = true;
	}
	if (client->progress_addr) {
		void __iomem *p = (void __iomem *)CKSEG1ADDR(
			iop_bus_to_phys(client->progress_addr));
		u32 phase = readl(p + 4);
		if (readl(p) == 0x45454952 && phase != *last_phase) {
			static const char * const names[] = {
				"not entered", "entry", "CreateThread", "RPC queue",
				"RPC register", "StartThread", "rearm handler",
				"resident", "failed", "RPC worker"
			};
			pr_warn("IOP relay: phase %u (%s), result %d, thread %u\\n",
				phase, phase < ARRAY_SIZE(names) ? names[phase] : "unknown",
				(s32)readl(p + 8), readl(p + 12));
			*last_phase = phase;
		}
	}
}

'''
    anchor = '/**\n * sif_rpc_bind - request a connection to an IOP RPC server'
    text = replace_once(text, anchor, helper + anchor)
    bind = '''\twhile (!wait_for_completion_timeout(&client->done, 5 * HZ))
\t\tpr_warn("SIF: waiting for bind server %#x (DMA status %#x)\\n",
\t\t\tserver_id, inl(DMAC_STAT_MASK));'''
    text = replace_once(text, bind, '''\t{
\t\tbool reported = false;
\t\tu32 phase = ~0U;
\t\twhile (!wait_for_completion_timeout(&client->done, 5 * HZ))
\t\t\tsif_report_stall(client, server_id, true, &reported, &phase);
\t}''')
    call = '''\twhile (!wait_for_completion_timeout(&client->done, 5 * HZ))
\t\tpr_warn("SIF: waiting for RPC %u server %#x (DMA status %#x)\\n",
\t\t\trpc_id, client->server, inl(DMAC_STAT_MASK));'''
    return replace_once(text, call, '''\t{
\t\tbool reported = false;
\t\tu32 phase = ~0U;
\t\twhile (!wait_for_completion_timeout(&client->done, 5 * HZ))
\t\t\tsif_report_stall(client, rpc_id, false, &reported, &phase);
\t}''')


def adapt_header(text):
    text = replace_once(text, 'int sif_cmd_data(',
        '/* dst allocation must include ALIGN(nbytes, 16) bytes. */\n'
        'int sif_write_data(iop_addr_t dst, const void *src, size_t nbytes);\n\nint sif_cmd_data(')
    return replace_once(text, '\tiop_addr_t server;\n',
        '\tiop_addr_t server;\n\tiop_addr_t progress_addr; /* Owned IOP startup mailbox, or zero. */\n')


def adapt_loader(text):
    text = replace_once(text, '.addr = iop_alloc(nbyte),', '.addr = iop_alloc(ALIGN(nbyte, 16)),')
    before = '''\t/* Copy the module to IOP memory. */
\tmemcpy(iop_bus_to_virt(link.addr), buf, nbyte);

\t/* Make the module visible to the IOP. */
\tdma_cache_wback((unsigned long)iop_bus_to_virt(link.addr), nbyte);'''
    text = replace_once(text, before, '''\t/* EE cached stores can overwrite live IOP heap/cache-line neighbours.
\t * Transfer IRX bytes with SIF1; LOADFILE runs only after DMA completes.
\t */
\terr = sif_write_data(link.addr, buf, nbyte);
\tif (err)
\t\tgoto err_out;''')
    anchor = '\tpr_info("IOP firmware: submit LOADFILE at %#x (%zu bytes)\\n", link.addr, nbyte);'
    text = replace_once(text, anchor, '''\tif (!strcmp(irx_iopmod(buf)->name, "irqrelay")) {
\t\tconst u32 initial[16] = { 0x45454952 };
\t\tiop_addr_t progress = iop_alloc(sizeof(initial));
\t\tif (!progress) {
\t\t\terr = -ENOMEM;
\t\t\tgoto err_out;
\t\t}
\t\tload_file_rpc_client.progress_addr = progress;
\t\terr = sif_write_data(progress, initial, sizeof(initial));
\t\tif (err)
\t\t\tgoto err_out;
\t\t/* No synchronous IOP logging is needed to observe entry progress. */
\t\tlink.arg_size = snprintf(link.arg, sizeof(link.arg),
\t\t\t"ps2-progress=%08x", progress) + 1;
\t}

''' + anchor)
    # Release the mailbox only after LOADFILE has returned; it is not a timeout.
    text = replace_once(text, '\tiop_free(link.addr);\n\treturn 0;',
        '\tif (load_file_rpc_client.progress_addr) {\n'
        '\t\tiop_free(load_file_rpc_client.progress_addr);\n'
        '\t\tload_file_rpc_client.progress_addr = 0;\n\t}\n'
        '\tiop_free(link.addr);\n\treturn 0;')
    return replace_once(text, 'err_out:\n\tiop_free(link.addr);',
        'err_out:\n\tif (load_file_rpc_client.progress_addr) {\n'
        '\t\tiop_free(load_file_rpc_client.progress_addr);\n'
        '\t\tload_file_rpc_client.progress_addr = 0;\n\t}\n\tiop_free(link.addr);')


def adapt_identity(text):
    return replace_once(text, 'ps2: DMA handoff and fault summary kernel',
        'ps2: aligned firmware and SIF startup kernel')


def install(source: Path, patch: Path):
    adapters = (('drivers/ps2/sif.c', adapt_sif),
                ('arch/mips/include/asm/mach-ps2/sif.h', adapt_header),
                ('drivers/ps2/iop-module-request.c', adapt_loader),
                ('init/main.c', adapt_identity))
    changes = [(name, (source/name).read_text(), fn((source/name).read_text()))
               for name, fn in adapters]
    blob = ''.join(''.join(difflib.unified_diff(before.splitlines(keepends=True),
        after.splitlines(keepends=True), fromfile='a/'+name, tofile='b/'+name))
        for name, before, after in changes).encode()
    if patch.exists() and not same_patch_content(patch.read_bytes(), blob):
        raise RuntimeError('Preserve reviewed IOP upload patch')
    patch.parent.mkdir(parents=True, exist_ok=True)
    write_reviewed_patch(patch, blob)
    subprocess.run(['git', 'apply', '--check', str(patch.resolve())], cwd=source, check=True)
    subprocess.run(['git', 'apply', str(patch.resolve())], cwd=source, check=True)
