/* SPDX-License-Identifier: GPL-2.0-only */
/* Host model for the ACTUAL driver packet/IRQ functions, inserted by the test. */
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ps2-smap-core.h"
typedef uint32_t u32;
#define BIT(n) (1u << (n))
#include "ps2-smap-regs.h"
#define ETH_HLEN 14
#define NET_IP_ALIGN 2
#define REARM_LIMIT 32
#define SMAP_REARM_CMD 0x8000001bu
#define SMAP_REARM_OPT 0x534d0104u
#define ESHUTDOWN 108
#define IRQ_IOP_SPD_TXDNV 112
#define IRQ_IOP_SPD_EMAC3 116
#define container_of(p, type, member) ((type *)((char *)(p) - offsetof(type, member)))
#define READ_ONCE(x) (x)
#define spin_lock_irqsave(lock, flags) do { (void)(lock); (flags) = 0; } while (0)
#define spin_unlock_irqrestore(lock, flags) do { (void)(lock); (void)(flags); } while (0)
#define wmb() do { barriers++; } while (0)
#define rmb() do { barriers++; } while (0)
#define msecs_to_jiffies(n) (n)
#define netdev_err(...) do { error_logs++; } while (0)
#define system_wq NULL
typedef int netdev_tx_t;
typedef int irqreturn_t;
enum { NETDEV_TX_OK, NETDEV_TX_BUSY, IRQ_HANDLED, IRQ_NONE };
struct work_struct { int unused; };
struct delayed_work { struct work_struct work; };
struct napi_struct { int unused; };
struct net_device { void *priv; int stopped; };
struct sk_buff { unsigned int len; u8 *data, *allocation; int consumed, copy_error; };
struct rtnl_link_stats64 {
    unsigned long tx_errors, tx_fifo_errors, tx_aborted_errors, tx_carrier_errors;
    unsigned long tx_packets, tx_bytes, collisions, tx_dropped;
    unsigned long rx_errors, rx_crc_errors, rx_over_errors, rx_length_errors;
    unsigned long rx_dropped, rx_packets, rx_bytes;
};
struct smap {
    struct net_device *ndev; struct napi_struct napi; int lock;
    struct work_struct reset_work; struct delayed_work rearm_work;
    int running, failed, closing; unsigned int rearm_attempts, rx_index;
    struct smap_tx_ring tx; struct rtnl_link_stats64 stats; u8 tx_buffer[1536];
};
static u8 regs[0x4000], tx_fifo[4096], rx_fifo[16384];
static unsigned int tx_ptr, rx_ptr, tx_words, rx_words, tx_increments, rx_decrements;
static unsigned int barriers, completed_packets, completed_bytes, sent_bytes;
static unsigned int gro_packets, napi_completed, irq_scheduled, rearms, retries, resets;
static unsigned int error_logs, allocation_fail, emac_errors;
static int rearm_error;
static unsigned int expected_rx_pointer, expected_rx_len;

static u16 smap_read16(unsigned int r) {
    u16 v; assert(r + 2 <= sizeof(regs)); memcpy(&v, regs + r, 2); return v;
}
static void smap_write16(unsigned int r, u16 v) {
    assert(r + 2 <= sizeof(regs));
    if (r == SMAP_TX_PTR) tx_ptr = v;
    if (r == SMAP_RX_PTR) rx_ptr = v;
    if (r >= SMAP_BD_TX && r < SMAP_BD_TX + 512 && (r & 7) == SMAP_BD_CTRL && (v & SMAP_TX_READY)) {
        /* The actual xmit must publish only after data, metadata and frame count. */
        assert(barriers && tx_words && tx_increments);
        assert(smap_read16(r + SMAP_BD_LENGTH) >= 14);
        assert(smap_read16(r + SMAP_BD_POINTER) >= SMAP_TX_BASE);
    }
    if (r >= SMAP_BD_RX && r < SMAP_BD_RX + 512 && (r & 7) == SMAP_BD_CTRL && v == SMAP_RX_EMPTY)
        assert(rx_decrements);
    memcpy(regs + r, &v, 2);
}
static u32 smap_read32(unsigned int r) {
    assert(r == SMAP_RX_DATA); u32 value = 0;
    for (unsigned int i = 0; i < 4; i++) value |= (u32)rx_fifo[(rx_ptr + i) & 0x3fff] << (i * 8);
    rx_ptr = (rx_ptr + 4) & 0x3fff; rx_words++; return value;
}
static void smap_write32(unsigned int r, u32 v) {
    assert(r == SMAP_TX_DATA);
    for (unsigned int i = 0; i < 4; i++) tx_fifo[(tx_ptr + i) & 0xfff] = v >> (i * 8);
    tx_ptr = (tx_ptr + 4) & 0xfff; tx_words++;
}
static void smap_write8(unsigned int r, u8 v) {
    assert(v == 1);
    if (r == SMAP_TX_INC) tx_increments++;
    else { assert(r == SMAP_RX_DEC); rx_decrements++; }
}
static u32 emac_read(unsigned int r) { assert(r == EMAC_IRQ_STATUS); return emac_errors; }
static void emac_write(unsigned int r, u32 v) {
    if (r == EMAC_IRQ_STATUS) emac_errors &= ~v;
    else { assert(r == EMAC_TXMODE0 && v == EMAC_GET_PACKET); }
}
static u32 get_unaligned_le32(const u8 *p) { return (u32)p[0] | (u32)p[1] << 8 | (u32)p[2] << 16 | (u32)p[3] << 24; }
static void put_unaligned_le32(u32 v, u8 *p) { for (unsigned int i = 0; i < 4; i++) p[i] = v >> (i * 8); }
static void *netdev_priv(struct net_device *n) { return n->priv; }
static int skb_copy_bits(struct sk_buff *skb, unsigned int offset, void *dst, unsigned int n) {
    assert(offset + n <= skb->len); if (skb->copy_error) return -1; memcpy(dst, skb->data + offset, n); return 0;
}
static void dev_kfree_skb_any(struct sk_buff *skb) { skb->consumed++; }
static void dev_consume_skb_any(struct sk_buff *skb) { skb->consumed++; }
static void netif_stop_queue(struct net_device *n) { n->stopped = 1; }
static void netif_wake_queue(struct net_device *n) { n->stopped = 0; }
static void netif_trans_update(struct net_device *n) { (void)n; }
static void netdev_sent_queue(struct net_device *n, unsigned int b) { (void)n; sent_bytes += b; }
static void netdev_completed_queue(struct net_device *n, unsigned int p, unsigned int b) {
    (void)n; completed_packets += p; completed_bytes += b;
}
static struct sk_buff *napi_alloc_skb(struct napi_struct *napi, unsigned int n) {
    (void)napi;
    if (allocation_fail) return NULL;
    struct sk_buff *skb = calloc(1, sizeof(*skb)); assert(skb);
    skb->allocation = skb->data = calloc(1, n); assert(skb->data); return skb;
}
static void skb_reserve(struct sk_buff *skb, unsigned int n) { skb->data += n; }
static void skb_put(struct sk_buff *skb, unsigned int n) { skb->len = n; }
static int eth_type_trans(struct sk_buff *skb, struct net_device *n) { (void)skb; (void)n; return 0; }
/* protocol is unused in this model but required by the actual driver. */
#define protocol copy_error
static void napi_gro_receive(struct napi_struct *napi, struct sk_buff *skb) {
    (void)napi; assert(skb->len == expected_rx_len);
    for (unsigned int j = 0; j < skb->len; j++)
        assert(skb->data[j] == rx_fifo[(expected_rx_pointer + j) & 0x3fff]);
    gro_packets++; free(skb->allocation); free(skb);
}
static int napi_complete_done(struct napi_struct *napi, int done) {
    (void)napi; (void)done; napi_completed++; return 1;
}
static void napi_schedule_irqoff(struct napi_struct *napi) { (void)napi; irq_scheduled++; }
static int sif_cmd_opt(u32 command, u32 opt, const void *payload, size_t n) {
    assert(command == SMAP_REARM_CMD && opt == SMAP_REARM_OPT);
    assert(n == 4 && *(const u32 *)payload == SMAP_IRQ_MASK); rearms++; return rearm_error;
}
static struct delayed_work *to_delayed_work(struct work_struct *work) {
    return container_of(work, struct delayed_work, work);
}
static void mod_delayed_work(void *queue, struct delayed_work *work, unsigned int delay) {
    (void)queue; (void)work; assert(delay == 10); retries++;
}
static void schedule_work(struct work_struct *work) { (void)work; resets++; }

/* DRIVER_FUNCTIONS */

static void init(struct smap *s, struct net_device *n) {
    memset(s, 0, sizeof(*s)); memset(n, 0, sizeof(*n)); memset(regs, 0, sizeof(regs));
    s->ndev = n; n->priv = s; s->running = 1; smap_ring_init(&s->tx);
    for (unsigned int i = 0; i < 64; i++) {
        u16 empty = SMAP_RX_EMPTY; memcpy(regs + SMAP_BD_RX + i * 8, &empty, 2);
    }
}
static void complete_one(struct smap *s, u16 status) {
    unsigned int r = SMAP_BD_TX + s->tx.consumer * 8;
    memcpy(regs + r, &status, 2);
    /* Deliberately corrupt hardware length. Reclaim must use software accounting. */
    u16 bogus = 65535; memcpy(regs + r + SMAP_BD_LENGTH, &bogus, 2);
    smap_tx_complete(s);
}
static void tx_checks(void) {
    struct smap s; struct net_device n; u8 payload[1518]; init(&s, &n);
    for (unsigned int j = 0; j < sizeof(payload); j++) payload[j] = j * 13 + 7;
    for (unsigned int k = 0; k < 1000; k++) {
        unsigned int len = 14 + (k * 67 % 1505), before = s.tx.offset;
        struct sk_buff skb = { .len = len, .data = payload };
        assert(smap_xmit(&skb, &n) == NETDEV_TX_OK && skb.consumed == 1);
        assert(s.tx.used == 1 && s.tx.free_bytes == 4096 - smap_round(len));
        for (unsigned int j = 0; j < len; j++) assert(tx_fifo[(before + j) & 4095] == payload[j]);
        for (unsigned int j = len; j < smap_round(len); j++) assert(tx_fifo[(before + j) & 4095] == 0);
        smap_tx_complete(&s); assert(s.tx.used == 1); /* READY is still owned by MAC. */
        complete_one(&s, k & 1 ? BIT(2) : 0);
        assert(s.tx.used == 0 && s.tx.free_bytes == 4096);
    }
    assert(s.stats.tx_packets == 1000 && s.stats.collisions == 500);
    for (unsigned int k = 0; k < 63; k++) {
        struct sk_buff skb = { .len = 14, .data = payload };
        assert(smap_xmit(&skb, &n) == NETDEV_TX_OK && skb.consumed == 1);
    }
    struct sk_buff busy = { .len = 14, .data = payload };
    assert(smap_xmit(&busy, &n) == NETDEV_TX_BUSY && !busy.consumed && s.tx.used == 63);
    while (s.tx.used) complete_one(&s, SMAP_TX_ERRORS);
    assert(s.tx.free_bytes == 4096 && s.stats.tx_errors == 63);
    struct sk_buff large = { .len = 1518, .data = payload };
    assert(smap_xmit(&large, &n) == NETDEV_TX_OK);
    assert(smap_xmit(&large, &n) == NETDEV_TX_OK);
    large.consumed = 0;
    assert(smap_xmit(&large, &n) == NETDEV_TX_BUSY && !large.consumed);
    assert(s.tx.used == 2 && s.tx.free_bytes == 1056);
    struct sk_buff bad = { .len = 1519, .data = payload };
    assert(smap_xmit(&bad, &n) == NETDEV_TX_OK && bad.consumed == 1);
    bad.len = 13; bad.consumed = 0;
    assert(smap_xmit(&bad, &n) == NETDEV_TX_OK && bad.consumed == 1);
    bad.len = 64; bad.copy_error = 1; bad.consumed = 0;
    assert(smap_xmit(&bad, &n) == NETDEV_TX_OK && bad.consumed == 1);
    assert(s.stats.tx_dropped == 3 && s.tx.used == 2);
}
static void rx_descriptor(struct smap *s, u16 status, u16 len, u16 pointer) {
    unsigned int r = SMAP_BD_RX + s->rx_index * 8;
    memcpy(regs + r, &status, 2); memcpy(regs + r + 4, &len, 2); memcpy(regs + r + 6, &pointer, 2);
    expected_rx_pointer = (pointer - SMAP_RX_BASE) & 0x3fff; expected_rx_len = len;
}
static void rx_checks(void) {
    struct smap s; struct net_device n; init(&s, &n);
    for (unsigned int i = 0; i < sizeof(rx_fifo); i++) rx_fifo[i] = i * 17 + 1;
    for (unsigned int i = 0; i < 128; i++) {
        u16 len = 14 + (i * 53 % 1505), ptr = 0x4000 + ((i * 148) & 0x3ffc);
        rx_descriptor(&s, 0, len, ptr);
        assert(smap_poll(&s.napi, 1) == 1);
        assert(smap_read16(SMAP_BD_RX + ((s.rx_index + 63) & 63) * 8) == SMAP_RX_EMPTY);
    }
    assert(gro_packets == 128 && s.stats.rx_packets == 128 && s.rx_index == 0);
    unsigned int reads = rx_words;
    const u16 statuses[] = { BIT(3), 0, 0, 0, 0, 0 };
    const u16 lengths[] = { 64, 13, 1519, 65535, 64, 64 };
    const u16 pointers[] = { 0x4000, 0x4000, 0x4000, 0x4000, 0x8000, 0x4001 };
    for (unsigned int i = 0; i < 6; i++) {
        rx_descriptor(&s, statuses[i], lengths[i], pointers[i]);
        assert(smap_poll(&s.napi, 8) == 1);
    }
    assert(s.stats.rx_errors == 6 && s.stats.rx_crc_errors == 1 && rx_words == reads);
    allocation_fail = 1; rx_descriptor(&s, 0, 1518, 0x7ffc);
    assert(smap_poll(&s.napi, 8) == 1 && s.stats.rx_dropped == 1 && rx_words == reads);
    allocation_fail = 0; rx_descriptor(&s, 0, 1518, 0x7ffc);
    unsigned int completes = napi_completed;
    assert(smap_poll(&s.napi, 0) == 0 && rx_words == reads && napi_completed == completes);
    assert(smap_poll(&s.napi, 8) == 1 && s.stats.rx_packets == 129); /* wraps RX FIFO */
}
static void irq_checks(void) {
    struct smap s; struct net_device n; init(&s, &n);
    unsigned int before = irq_scheduled;
    assert(smap_interrupt(115, &s) == IRQ_HANDLED && irq_scheduled == before + 1);
    s.running = 0;
    assert(smap_interrupt(115, &s) == IRQ_HANDLED && irq_scheduled == before + 1);
    assert(smap_interrupt(77, &s) == IRQ_NONE && irq_scheduled == before + 1);
    assert(smap_rearm(&s) == -ESHUTDOWN);
    s.running = 1; s.closing = 1;
    assert(smap_rearm(&s) == -ESHUTDOWN);
    s.closing = 0;
    s.running = 1; rearm_error = -16;
    unsigned int start = rearms;
    for (unsigned int i = 0; i < REARM_LIMIT; i++) smap_rearm_work(&s.rearm_work.work);
    assert(rearms - start == REARM_LIMIT && s.failed && n.stopped && resets == 1);
    assert(s.rearm_attempts == REARM_LIMIT && error_logs == 1);
    s.running = 0; smap_rearm_work(&s.rearm_work.work);
    assert(rearms - start == REARM_LIMIT);
    init(&s, &n); s.rearm_attempts = 7; rearm_error = 0;
    smap_rearm_work(&s.rearm_work.work); assert(s.rearm_attempts == 0 && !s.failed);
    emac_errors = EMAC_IRQ_DEAD; smap_poll(&s.napi, 8);
    assert(s.failed && n.stopped && !emac_errors && resets == 2);
}
int main(void) {
    tx_checks(); rx_checks(); irq_checks();
    assert(smap_mac_checksum((u8[]){0, 4, 31, 2, 10, 9}, 0x0f29));
    assert(!smap_mac_checksum((u8[]){0, 4, 31, 2, 10, 9}, 0x0f28));
    puts("actual SMAP packet/IRQ handlers: TX/RX wrap, bounds, ownership, budgets and recovery passed");
    return 0;
}
