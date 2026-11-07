"""IRQ relay adaptation guards and host checks of resource callback failures."""
from pathlib import Path
import os
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from kernel_iop_irq_port import modernize_iop_irq


def reviewed_source():
    source = (ROOT / "kernel/vendor/frno7/snapshot/drivers/ps2/iop-irq.c").read_text()
    return source.replace("BUG_ON(in_irq());", "BUG_ON(in_hardirq());")


class IopIRQPort(unittest.TestCase):
    def test_unexpected_source_is_rejected(self):
        source = reviewed_source()
        for changed in (source.replace("0x0100", "0x0200"),
                        source.replace("BUG_ON(in_hardirq());", ""),
                        source + source, modernize_iop_irq(source)):
            with self.subTest(change=changed[:40]):
                with self.assertRaises(RuntimeError):
                    modernize_iop_irq(changed)

    def test_license_and_unrelated_irq_table_are_preserved(self):
        source = reviewed_source()
        updated = modernize_iop_irq(source)
        self.assertEqual(source.split("#include", 1)[0], updated.split("#include", 1)[0])
        self.assertEqual(source.split("static struct {", 1)[1], updated.split("static struct {", 1)[1])

    @unittest.skipUnless(os.name != "nt" and shutil.which("cc"), "requires native Linux C compiler")
    def test_resource_callbacks_unlock_and_propagate_failures(self):
        source = modernize_iop_irq(reviewed_source())
        # Compile the actual transformed callbacks with transport substitutes.
        callbacks = source.split("static struct sif_rpc_client iop_irq_rpc;", 1)[1]
        callbacks = "static struct sif_rpc_client iop_irq_rpc;" + callbacks.split("#define IOP_IRQ_TYPE", 1)[0]
        harness = r'''
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <assert.h>
#include <stdio.h>
typedef int32_t s32;
struct irq_data { unsigned int irq; };
struct sif_rpc_client { int placeholder; };
struct iop_rpc_relay_map { uint8_t iop, map, rpc; };
struct iop_rpc_relay_release { uint8_t iop; };
enum { IOP_IRQ_BASE = 64, SIF_SID_IRQ_RELAY = 7, rpo_request_irq = 1, rpo_release_irq = 2 };
#define DEFINE_MUTEX(name) int name
static int locks, module_result, bind_result, rpc_result, reply, warnings, module_calls, bind_calls;
static void mutex_lock(int *m) { assert(!*m); *m = 1; locks++; }
static void mutex_unlock(int *m) { assert(*m); *m = 0; locks--; }
static int iop_module_request(const char *name, int version, void *args) {
    assert(locks == 1); assert(name && version == 0x100 && !args);
    module_calls++; return module_result;
}
static int sif_rpc_bind(struct sif_rpc_client *client, int id) {
    assert(locks == 1 && client && id == SIF_SID_IRQ_RELAY);
    bind_calls++; return bind_result;
}
static int sif_rpc(struct sif_rpc_client *client, int op, const void *arg,
                   size_t size, void *recv, size_t recv_size) {
    assert(locks == 1 && client && recv_size == sizeof(s32));
    if (op == rpo_request_irq) {
        const struct iop_rpc_relay_map *map = arg;
        assert(size == sizeof(*map) && map->iop == 22 && map->map == 86 && map->rpc);
    } else {
        const struct iop_rpc_relay_release *rel = arg;
        assert(op == rpo_release_irq && size == sizeof(*rel) && rel->iop == 22);
    }
    *(s32 *)recv = reply; return rpc_result;
}
#define pr_err(...) ((void)0)
#define pr_debug(...) ((void)0)
#define pr_warn(...) (warnings++)
'''
        harness += callbacks
        harness += r'''
int main(void) {
    struct irq_data irq = {86};
    module_result = -2;
    assert(iop_irq_request_resources(&irq) == -2 && locks == 0 && !irq_relay && bind_calls == 0);
    module_result = 0; bind_result = -6;
    assert(iop_irq_request_resources(&irq) == -6 && locks == 0 && !irq_relay);
    bind_result = 0; rpc_result = -5;
    assert(iop_irq_request_resources(&irq) == -5 && locks == 0 && irq_relay);
    int modules = module_calls, binds = bind_calls;
    rpc_result = 0; reply = -16;
    assert(iop_irq_request_resources(&irq) == -16 && locks == 0);
    reply = 0;
    assert(iop_irq_request_resources(&irq) == 0 && locks == 0);
    assert(module_calls == modules && bind_calls == binds);
    iop_irq_release_resources(&irq); assert(locks == 0 && warnings == 0);
    rpc_result = -5;
    iop_irq_release_resources(&irq); assert(locks == 0 && warnings == 1);
    rpc_result = 0; reply = -16;
    iop_irq_release_resources(&irq); assert(locks == 0 && warnings == 2);
    return 0;
}
'''
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp)
            (path / "check.c").write_text(harness)
            subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", str(path / "check.c"),
                            "-o", str(path / "check")], check=True, capture_output=True, text=True)
            subprocess.run([str(path / "check")], check=True, timeout=5)


if __name__ == "__main__":
    unittest.main()
