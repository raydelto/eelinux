/* SPDX-License-Identifier: GPL-2.0-only */
/* Hardware-independent bounds shared by the driver and native C tests. */
#ifndef PS2_PS2_SMAP_CORE_H
#define PS2_PS2_SMAP_CORE_H
#ifdef __KERNEL__
#include <linux/types.h>
#elif !defined(PS2_SMAP_IOP)
#include <stdbool.h>
#include <stdint.h>
typedef uint8_t u8;
typedef uint16_t u16;
#endif

#define SMAP_BD_COUNT 64u
#define SMAP_TX_FIFO_SIZE 4096u
#define SMAP_TX_BASE 0x1000u
#define SMAP_RX_BASE 0x4000u
#define SMAP_RX_FIFO_SIZE 16384u
#define SMAP_MAX_FRAME 1518u

struct smap_tx_ring {
	u16 producer, consumer, used, offset, free_bytes;
	u16 length[SMAP_BD_COUNT];
};

static inline unsigned int smap_round(unsigned int n)
{
	return (n + 3u) & ~3u;
}

static inline void smap_ring_init(struct smap_tx_ring *r)
{
	unsigned int i;
	r->producer = r->consumer = r->used = r->offset = 0;
	r->free_bytes = SMAP_TX_FIFO_SIZE;
	for (i = 0; i < SMAP_BD_COUNT; i++)
		r->length[i] = 0;
}

static inline bool smap_tx_room(const struct smap_tx_ring *r, unsigned int n)
{
	return n >= 14 && n <= SMAP_MAX_FRAME &&
	       r->used < SMAP_BD_COUNT - 1 && r->free_bytes >= smap_round(n);
}

static inline void smap_tx_commit(struct smap_tx_ring *r, unsigned int n)
{
	r->length[r->producer] = n;
	r->free_bytes -= smap_round(n);
	r->offset = (r->offset + smap_round(n)) & (SMAP_TX_FIFO_SIZE - 1);
	r->producer = (r->producer + 1) & (SMAP_BD_COUNT - 1);
	r->used++;
}

static inline unsigned int smap_tx_reclaim(struct smap_tx_ring *r)
{
	unsigned int n = r->length[r->consumer];
	r->free_bytes += smap_round(n);
	r->length[r->consumer] = 0;
	r->consumer = (r->consumer + 1) & (SMAP_BD_COUNT - 1);
	r->used--;
	return n;
}

static inline bool smap_rx_valid(unsigned int status, unsigned int n,
				 unsigned int pointer)
{
	return !(status & 0x3ff) && n >= 14 && n <= SMAP_MAX_FRAME &&
	       pointer >= SMAP_RX_BASE && pointer < SMAP_RX_BASE + SMAP_RX_FIFO_SIZE &&
	       !(pointer & 3);
}

static inline bool smap_mac_checksum(const u8 *mac, u16 checksum)
{
	unsigned int sum = (mac[0] | mac[1] << 8) +
		(mac[2] | mac[3] << 8) + (mac[4] | mac[5] << 8);
	return (u16)sum == checksum;
}
#endif
