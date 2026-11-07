/* SPDX-License-Identifier: GPL-2.0-only */
/* Actual bounded DEV9 EE control functions with an independent fake peer. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef uint8_t u8; typedef uint16_t u16; typedef uint32_t u32; typedef int32_t s32;
#define DEV9_CONTROL_CMD 0x8000001cu
#define DEV9_REQUEST_OPT 0x44560908u
#define DEV9_REPLY_OPT 0x44560014u
#define DEV9_ACQUIRE 1u
#define DEV9_RELEASE 2u
#define HZ 100
#define ETIMEDOUT 110
#define IRQ_DMAC_SIF0 21
#define READ_ONCE(v) (v)
#define WRITE_ONCE(v, x) do { (v) = (x); } while (0)
#define local_irq_save(flags) do { (flags) = irq_disabled; irq_disabled = 1; } while (0)
#define local_irq_restore(flags) do { irq_disabled = (flags); } while (0)
#define dev_err(...) do { cleanup_errors++; } while (0)
struct completion { int done; };
struct net_device { int dev; };
struct sif_cmd_header { u32 packet_size:8, data_size:24; u32 data_addr, cmd, opt; };
struct dev9_request { u32 operation, cookie; };
struct dev9_reply { u32 cookie; s32 status; u32 hardware; u8 mac[6]; u16 checksum; };
struct smap {
    struct net_device *ndev; struct completion dev9_done; struct dev9_reply dev9_info;
    u32 cookie, dev9_op; int dev9_waiting;
};
typedef void (*callback)(const struct sif_cmd_header *, void *);
#define sif_cmd_payload(h) ((void *)((h) + 1))
static callback handler;
static void *handler_arg;
static int irq_disabled, cleanup_errors, synchronized, mode, sent, registration_error;
static u32 random_cookie = 0x87654321, lease_cookie;
static unsigned int operations[8];
static void reinit_completion(struct completion *c) { c->done = 0; }
static void complete(struct completion *c) { c->done = 1; }
static unsigned long wait_for_completion_timeout(struct completion *c, unsigned long ticks) {
    assert(ticks == 10 * HZ); return c->done;
}
static u32 get_random_u32(void) { return random_cookie++; }
static int sif_request_cmd(u32 command, callback cb, void *arg) {
    assert(command == DEV9_CONTROL_CMD && irq_disabled);
    if (registration_error && cb) return -22;
    handler = cb; handler_arg = arg; return 0;
}
static void synchronize_irq(unsigned int irq) { assert(irq == IRQ_DMAC_SIF0 && !irq_disabled); synchronized++; }
static int sif_cmd_opt(u32 command, u32 opt, const void *data, unsigned long bytes) {
    const struct dev9_request *request = data;
    struct { struct sif_cmd_header h; struct dev9_reply reply; } packet = {
        .h = { .packet_size = 36, .cmd = DEV9_CONTROL_CMD,
               .opt = DEV9_REPLY_OPT | (request->operation << 8) },
        .reply = { .cookie = request->cookie, .hardware = 0x30,
                   .mac = { 0, 4, 31, 2, 10, 9 }, .checksum = 0x0f29 },
    };
    assert(command == DEV9_CONTROL_CMD && opt == DEV9_REQUEST_OPT && bytes == 8);
    assert(sent < 8); operations[sent++] = request->operation;
    if (request->operation == DEV9_ACQUIRE) lease_cookie = request->cookie;
    else assert(request->cookie == lease_cookie); /* release must preserve lease identity */
    if (mode == 1) return -16;
    if (mode == 2) packet.reply.cookie ^= 1;
    if (mode == 3) packet.h.opt ^= 0x100;
    if (mode == 4) packet.h.packet_size = 35;
    if (mode == 5 && request->operation == DEV9_RELEASE) return -16;
    if (mode == 6) packet.reply.status = -19;
    if (mode == 7) return 0; /* queued request, no reply */
    if (handler) handler(&packet.h, handler_arg);
    return 0;
}
/* DRIVER_FUNCTIONS */
static void init(struct smap *s, struct net_device *ndev) {
    memset(s, 0, sizeof(*s)); s->ndev = ndev; sent = 0; synchronized = 0;
    registration_error = irq_disabled = mode = 0; handler = NULL; handler_arg = NULL;
}
int main(void) {
    struct smap s; struct net_device n;
    init(&s, &n); assert(smap_dev9_control(&s, DEV9_ACQUIRE) == 0);
    assert(s.cookie == 0x87654321 && s.dev9_info.hardware == 0x30);
    assert(!handler && !s.dev9_waiting && synchronized == 1 && sent == 1);
    assert(smap_dev9_control(&s, DEV9_RELEASE) == 0 && s.cookie == 0x87654321);
    assert(operations[0] == DEV9_ACQUIRE && operations[1] == DEV9_RELEASE);
    u32 previous = s.cookie; init(&s, &n);
    assert(smap_dev9_control(&s, DEV9_ACQUIRE) == 0 && s.cookie != previous);
    for (int mismatch = 2; mismatch <= 4; mismatch++) {
        init(&s, &n); mode = mismatch;
        assert(smap_dev9_control(&s, DEV9_ACQUIRE) == -ETIMEDOUT);
        assert(sent == 2 && operations[1] == DEV9_RELEASE && !handler && !s.dev9_waiting);
        assert(synchronized == 1);
    }
    init(&s, &n); mode = 7;
    assert(smap_dev9_control(&s, DEV9_ACQUIRE) == -ETIMEDOUT && sent == 2);
    assert(operations[1] == DEV9_RELEASE && !handler);
    init(&s, &n); mode = 1;
    assert(smap_dev9_control(&s, DEV9_ACQUIRE) == -16 && sent == 1 && !handler);
    init(&s, &n); registration_error = 1;
    assert(smap_dev9_control(&s, DEV9_ACQUIRE) == -22 && !sent && !handler && !s.dev9_waiting);
    init(&s, &n); mode = 6;
    assert(smap_dev9_control(&s, DEV9_ACQUIRE) == -19 && sent == 1);
    init(&s, &n); random_cookie = 0;
    assert(smap_dev9_control(&s, DEV9_ACQUIRE) == 0 && s.cookie == 1);
    /* Delayed old ACQUIRE reply must not complete a RELEASE using the same lease. */
    struct { struct sif_cmd_header h; struct dev9_reply reply; } old = {
        .h = { .packet_size = 36, .opt = DEV9_REPLY_OPT | (DEV9_ACQUIRE << 8) },
        .reply = { .cookie = s.cookie },
    };
    s.dev9_op = DEV9_RELEASE; s.dev9_waiting = 1; s.dev9_done.done = 0;
    smap_dev9_reply(&old.h, &s); assert(!s.dev9_done.done);
    s.dev9_waiting = 0;
    puts("actual DEV9 control: cookies, operation tags, timeouts and callback cleanup passed");
    return 0;
}
