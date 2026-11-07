"""Install the generic PS2 SMAP IOP backend and a bounded SIF RPC API."""
from pathlib import Path
import difflib, hashlib, json, subprocess
from kernel_patch_names import normalize_source

ROOT=Path(__file__).resolve().parents[1]

def adapt_rpc(text):
    if 'EXPORT_SYMBOL_GPL(sif_rpc_timeout)' in text:
        raise RuntimeError('Bounded SIF RPC already installed')
    start=text.index('static int sif_rpc_dma(')
    end=text.index('EXPORT_SYMBOL_GPL(sif_rpc);',start)+len('EXPORT_SYMBOL_GPL(sif_rpc);')
    block=text[start:end]
    old='const void *send, size_t send_size, size_t recv_size)'
    if block.count(old)!=1: raise RuntimeError('Review SIF RPC DMA signature')
    block=block.replace(old,'const void *send, size_t send_size, size_t recv_size, unsigned long timeout)')
    old='sif_rpc_dma(client, rpc_id, send, send_size, recv_size);'
    if block.count(old)!=1: raise RuntimeError('Review legacy SIF RPC wrapper')
    block=block.replace(old,'sif_rpc_dma(client, rpc_id, send, send_size, recv_size, 0);')
    if '\twhile (!wait_for_completion_timeout' in block:
        anchor='\t{\n\t\tbool reported = false;'
        a=block.index(anchor); b=block.index('\n\tdma_rmb();',a)
        wait=block[a:b]
    else:
        wait='\twait_for_completion(&client->done);\n'
        if block.count(wait)!=1: raise RuntimeError('Review SIF RPC completion wait')
    bounded='''\tif (timeout) {
\t\t/* Caller must quarantine client, send and receive pages after timeout.
\t\t * The IOP still owns an in-flight DMA and may complete it later. */
\t\tif (!wait_for_completion_timeout(&client->done, timeout))
\t\t\treturn -ETIMEDOUT;
\t} else {
'''+wait+'''\t}
'''
    block=block.replace(wait,bounded,1)
    block+='''

/* Bounded process-context RPC. On timeout, do not reuse or free client/buffers
 * until the IOP has been reset; the legacy API retains its lifetime contract.
 */
int sif_rpc_timeout(struct sif_rpc_client *client, u32 rpc_id,
\tconst void *send, size_t send_size, void *recv, size_t recv_size,
\tunsigned long timeout)
{
\tint err;
\tif (!timeout) return -EINVAL;
\terr = sif_rpc_dma(client, rpc_id, send, send_size, recv_size, timeout);
\tif (!err) memcpy(recv, client->client_buffer, recv_size);
\treturn err;
}
EXPORT_SYMBOL_GPL(sif_rpc_timeout);
'''
    return text[:start]+block+text[end:]

def adapt_header(text):
    anchor='int sif_rpc(struct sif_rpc_client *client, u32 rpc_id,\n\tconst void *send, size_t send_size, void *recv, size_t recv_size);'
    if text.count(anchor)!=1 or 'int sif_rpc_timeout(' in text:
        raise RuntimeError('Review SIF RPC header')
    return text.replace(anchor,anchor+'''\n
/* A timeout leaves DMA in flight: retain client and both buffers until reset. */
int sif_rpc_timeout(struct sif_rpc_client *client, u32 rpc_id,
\tconst void *send, size_t send_size, void *recv, size_t recv_size,
\tunsigned long timeout);''')

def install(source: Path, patch: Path):
    from kernel_smap_port import FILES
    original=source/'drivers/net/ethernet/ps2/ps2-smap.c'
    expected=json.loads((source/'.eelinux-smap-startup.json').read_text())['driver_sha256']
    if hashlib.sha256(original.read_bytes()).hexdigest()!=expected:
        raise RuntimeError('Native SMAP baseline changed; preserve the tested source')
    normalized=normalize_source(source)
    changes={}
    for name in FILES:
        path='drivers/net/ethernet/ps2/'+name
        changes[path]=(ROOT/'kernel/new-drivers/ps2-smap'/name).read_text()
    for path,fn in (('drivers/ps2/sif.c',adapt_rpc),('arch/mips/include/asm/mach-ps2/sif.h',adapt_header)):
        changes[path]=fn((source/path).read_text())
    p=source/'init/main.c';s=p.read_text();anchor='ps2: SMAP cold-start kernel'
    if s.count(anchor)!=1: raise RuntimeError('Review kernel SMAP boot banner')
    changes['init/main.c']=s.replace(anchor,'ps2: native SMAP IOP transport')
    lines=[]
    for name,after in changes.items():
        p=source/name; before=p.read_text() if p.exists() else ''
        lines+=list(difflib.unified_diff(before.splitlines(keepends=True),after.splitlines(keepends=True),
                    fromfile='a/'+name if p.exists() else '/dev/null',tofile='b/'+name))
    patch.write_text(''.join(lines),encoding='utf-8',newline='\n')
    subprocess.run(['git','apply','--check',str(patch.resolve())],cwd=source,check=True)
    subprocess.run(['git','apply',str(patch.resolve())],cwd=source,check=True)
    report={'normalized_source_files':normalized,'patch_sha256':hashlib.sha256(patch.read_bytes()).hexdigest(),
            'source_sha256':{name:hashlib.sha256((source/name).read_bytes()).hexdigest() for name in changes},
            'default_transport':'IOP SMAP / SIF','physical_network_pass':None}
    (source/'.ps2-smap-iop.json').write_text(json.dumps(report,indent=2)+'\n')
    return report
