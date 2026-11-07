/* SPDX-License-Identifier: GPL-2.0-only */
/* Execute the actual adapted heap/module initialization with failure injection. */
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#define __init
#define __exit
#define ENODEV 19
#define ENOMEM 12
#define WRITE_ONCE(v,x) do { (v)=(x); } while (0)
#define KERN_ERR ""
#define pr_warn(...) do {} while (0)
#define pr_err(...) do {} while (0)
#define printk(...) do {} while (0)
#define SIF_SID_HEAP 0x80000003u
#define SIF_SID_LOAD_MODULE 0x80000006u
#define SIF_CMD_PRINTK 0x80000020u
static int iop_heap_rpc, load_file_rpc_client;
static bool iop_heap_ready, iop_module_ready, sif_ready;
static int failure, heap_calls, module_calls, registered, unregistered, patches;
static void *iop_module_device;
static bool sif_is_ready(void) { return sif_ready; }
static bool iop_heap_is_ready(void) { return iop_heap_ready; }
static int sif_rpc_bind(int *client, unsigned int sid) {
    if (client == &iop_heap_rpc) {
        assert(sid == SIF_SID_HEAP); heap_calls++;
        return failure == 1 ? -5 : 0;
    }
    assert(client == &load_file_rpc_client && sid == SIF_SID_LOAD_MODULE);
    module_calls++; return failure == 4 ? -5 : 0;
}
static void sif_rpc_unbind(int *client) { assert(client == &iop_heap_rpc); }
static int iop_module_patch(void) { patches++; return 0; }
static void *root_device_register(const char *name) {
    assert(!strcmp(name,"iop-module")); registered++;
    return failure == 2 ? NULL : &registered;
}
static void root_device_unregister(void *device) {
    assert(device == &registered); unregistered++;
}
static void cmd_printk(void) {}
static int sif_request_cmd(unsigned int cmd, void (*callback)(void), void *arg) {
    assert(cmd == SIF_CMD_PRINTK && callback == cmd_printk && !arg);
    return failure == 3 ? -5 : 0;
}
static int iop_module_load_rom(const char *name) {
    assert(!strcmp(name,"rom0:ADDDRV")); return failure == 5 ? -5 : 0;
}
/* DRIVER_FUNCTIONS */
static void clear(void) {
    sif_ready=iop_heap_ready=iop_module_ready=false;
    failure=heap_calls=module_calls=registered=unregistered=patches=0;
}
int main(void) {
    clear();
    assert(iop_heap_init() == -ENODEV && heap_calls == 0 && !iop_heap_ready);
    assert(iop_module_init() == -ENODEV && registered == 0 && patches == 0);
    sif_ready=true; failure=1;
    assert(iop_heap_init() == -5 && heap_calls == 1 && !iop_heap_ready);
    assert(iop_module_init() == -ENODEV && module_calls == 0 && !iop_module_ready);
    failure=0;
    assert(iop_heap_init() == 0 && iop_heap_ready);
    for (int f=2; f<=5; f++) {
        failure=f; int old=unregistered;
        assert(iop_module_init() == (f == 2 ? -ENOMEM : -5));
        assert(!iop_module_ready && unregistered == old+(f != 2));
    }
    failure=0; assert(iop_module_init() == 0 && iop_module_ready);
    iop_heap_exit(); assert(!iop_heap_ready);
    puts("actual bridge: failed SIF/bind/device/command/ROM never mark the service ready");
}
