// SPDX-License-Identifier: GPL-2.0
/*
 * DEV9 initialization and bounded EE control transport.
 * Original module: Copyright (C) 2021 Fredrik Noring.
 * PS2 Linux implementation: 2026 PS2 Linux contributors.
 * Hardware protocol references and limitations: kernel/iop-overlay/README.md.
 */
#include "iopmod/bits.h"
#include "iopmod/dev9.h"
#include "iopmod/errno.h"
#include "iopmod/io.h"
#include "iopmod/iop-error.h"
#include "iopmod/interrupt.h"
#include "iopmod/module.h"
#include "iopmod/printk.h"
#include "iopmod/sifcmd.h"
#include "iopmod/sifman.h"
#include "iopmod/spd.h"
#include "iopmod/string.h"
#include "iopmod/thread.h"

#define DEV9_CMD 0x8000001cu
#define REQUEST_TAG 0x44560908u
#define REPLY_TAG 0x44560014u
#define QUEUE_SIZE 8
#define SMAP_CAPABILITY 1
#define PIO_DIR SPD_REG(0x2c)
#define PIO_DATA SPD_REG(0x2e)
#define EE_CLK 0x40
#define EE_CS 0x80
#define EE_IN 0x20
#define EE_OUT 0x10

struct dev9_request_packet { u32 operation, cookie; };
struct dev9_reply { u32 cookie; s32 status; u32 hardware; u8 mac[6]; u16 checksum; };
struct queued_request { struct dev9_request_packet request; int status; };
static struct queued_request queue[QUEUE_SIZE];
static unsigned int queue_head, queue_tail, queue_count;
static int lock_id = -1, wake_id = -1;
static unsigned int dev9_use_count;
static bool initialized, eeprom_valid;
static u32 hardware, lease_cookie, cleanup_cookie;
static u8 mac_address[6];
static u16 mac_checksum;
static int eeprom_error = -ENODEV;
static unsigned int dropped_requests;

static void wait_us(unsigned int usec) { thbase_delay(usec); }
static void dev9_write(unsigned int reg, u16 value) { iowr16(value, DEV9_REG(reg)); }
static u16 dev9_read(unsigned int reg) { return iord16(DEV9_REG(reg)); }

/* Attribute memory is byte-wide on the 16-bit DEV9 bus. Bound every tuple. */
static int identify_pcmcia(void)
{
	unsigned int at = 0;
	while (at + 4 <= 4096) {
		u8 tag = iord8(SPD_REGBASE + at), length;
		if (tag == 0xff) break;
		if (tag == 0) { at += 2; continue; }
		length = iord8(SPD_REGBASE + at + 2);
		if (!length || at + 4 + 2u * length > 4096) return -ENODEV;
		if (tag == 0x20 && length >= 4) {
			u32 id = ((u32)iord8(SPD_REGBASE + at + 4) << 16) |
				((u32)iord8(SPD_REGBASE + at + 6) << 24) |
				iord8(SPD_REGBASE + at + 8) |
				((u32)iord8(SPD_REGBASE + at + 10) << 8);
			return id == 0x00f15300u ? 0 : -ENODEV;
		}
		at += 4 + 2u * length;
	}
	return -ENODEV;
}

static int power_pcmcia(void)
{
	u16 detect = dev9_read(DEV9_REG_1462), voltage = detect & 0x0c;
	/* 00 is 16-bit PC Card; CardBus and an empty slot are unsupported. */
	if ((detect & 3) || voltage == 4) return -ENODEV;
	/* Preserve a card already initialized by another boot-time user. */
	if (dev9_read(DEV9_REG_POWER)) return 0;
	{
		dev9_write(DEV9_REG_147E, 1);
		dev9_write(DEV9_REG_1460, 0);
		dev9_write(DEV9_REG_1474, 0);
		dev9_write(DEV9_REG_1464, dev9_read(DEV9_REG_1464));
		dev9_write(DEV9_REG_1466, dev9_read(DEV9_REG_1466));
		dev9_write(DEV9_REG_1468, 0x10);
		dev9_write(DEV9_REG_146A, 0x90);
		dev9_write(DEV9_REG_147C, 1);
		dev9_write(DEV9_REG_147A, dev9_read(DEV9_REG_147C));
		dev9_write(DEV9_REG_POWER, 0);
		dev9_write(DEV9_REG_POWER, voltage == 0x0c ? 0x18 : 0x14);
		wait_us(22000);
		if (!(dev9_read(DEV9_REG_1462) & 0x100)) {
			dev9_write(DEV9_REG_POWER, 0);
			return -ENODEV;
		}
		dev9_write(DEV9_REG_POWER, dev9_read(DEV9_REG_POWER) | 2);
		wait_us(500000);
		dev9_write(DEV9_REG_POWER, dev9_read(DEV9_REG_POWER) | 1);
		dev9_write(DEV9_REG_1464, dev9_read(DEV9_REG_1464));
		dev9_write(DEV9_REG_1466, dev9_read(DEV9_REG_1466));
	}
	dev9_write(DEV9_REG_1460, 2);
	iowr32(0x1a00bb, SSBUS_REG_1418);
	if (identify_pcmcia()) return -ENODEV;
	if (dev9_read(DEV9_REG_1474) & 7) return -EBUSY;
	dev9_write(DEV9_REG_1474, 5);
	dev9_write(DEV9_REG_1460, 1);
	iowr8(1, SPD_REG(0x20));
	dev9_write(DEV9_REG_1474, 7);
	iowr32(0xe01a3043, SSBUS_REG_1418);
	wait_us(5000);
	dev9_write(DEV9_REG_POWER, dev9_read(DEV9_REG_POWER) & ~1);
	dev9_write(DEV9_REG_147E, 0);
	return 0;
}

static int power_expansion(void)
{
	if (dev9_read(DEV9_REG_1462) & 1) return -ENODEV;
	if (!(dev9_read(DEV9_REG_POWER) & 4)) {
		dev9_write(DEV9_REG_1466, 1);
		dev9_write(DEV9_REG_1464, 0);
		dev9_write(DEV9_REG_1460, dev9_read(DEV9_REG_1464));
		dev9_write(DEV9_REG_POWER, (dev9_read(DEV9_REG_POWER) & ~1) | 4);
		wait_us(500000);
		dev9_write(DEV9_REG_1460, dev9_read(DEV9_REG_1460) | 1);
		dev9_write(DEV9_REG_POWER, dev9_read(DEV9_REG_POWER) | 1);
		wait_us(500000);
	}
	dev9_write(DEV9_REG_1466, 0);
	return 0;
}

static int power_on(void)
{
	int err;
	u8 revision = iord8(DEV9_REG(DEV9_REG_REV));
	iowr32(0x51011, SSBUS_REG_1420);
	iowr32(0xe01a3043, SSBUS_REG_1418);
	iowr32(0xef1a3043, SSBUS_REG_141c);
	if ((revision & 0xf0) == 0x20) err = power_pcmcia();
	else if ((revision & 0xf0) == 0x30) err = power_expansion();
	else return -ENODEV;
	if (err) return err;
	hardware = revision & 0xf0;
	return 0;
}

static void eeprom_output(u8 base, bool value)
{
	u8 data = base | EE_CS | (value ? EE_IN : 0);
	iowr8(data, PIO_DATA); wait_us(3);
	iowr8(data | EE_CLK, PIO_DATA); wait_us(3);
	iowr8(data, PIO_DATA); wait_us(3);
}

static unsigned int eeprom_input(u8 base)
{
	unsigned int bit;
	iowr8(base | EE_CS, PIO_DATA); wait_us(3);
	iowr8(base | EE_CS | EE_CLK, PIO_DATA); wait_us(3);
	bit = !!(iord8(PIO_DATA) & EE_OUT);
	iowr8(base | EE_CS, PIO_DATA); wait_us(3);
	return bit;
}

static int read_eeprom(void)
{
	u8 direction = iord8(PIO_DIR), saved = iord8(PIO_DATA);
	u8 base = saved & ~(EE_CLK | EE_CS | EE_IN);
	u16 words[4] = {0};
	unsigned int i, bit, aggregate = 0;
	int err = 0;
	iowr8((direction | EE_CLK | EE_CS | EE_IN) & ~EE_OUT, PIO_DIR);
	iowr8(base, PIO_DATA); wait_us(3);
	/* Microwire READ: start=1, opcode=10, six address bits=000000. */
	for (bit = 9; bit; bit--) eeprom_output(base, !!(0x180 & (1u << (bit - 1))));
	/* READ exposes a zero dummy bit before the first data clock. Sampling
	 * this phase also matches DEV9's established transaction sequence; do
	 * not treat it as bit 15 of the MAC. Preserve shared PIO on all exits. */
	if (iord8(PIO_DATA) & EE_OUT) {
		err = -EIO;
		goto restore_pio;
	}
	for (i = 0; i < 4; i++)
		for (bit = 0; bit < 16; bit++) words[i] = (words[i] << 1) | eeprom_input(base);
restore_pio:
	iowr8(base, PIO_DATA); wait_us(3);
	iowr8(saved, PIO_DATA);
	iowr8(direction, PIO_DIR);
	if (err) return err;
	if ((u16)(words[0] + words[1] + words[2]) != words[3]) return -EIO;
	for (i = 0; i < 3; i++) {
		mac_address[2*i] = words[i] & 0xff;
		mac_address[2*i+1] = words[i] >> 8;
		aggregate |= words[i];
	}
	if (!aggregate || (mac_address[0] & 1)) return -EINVAL;
	mac_checksum = words[3];
	eeprom_valid = true;
	return 0;
}

/* All physical setup and EEPROM transactions share this process-context lock. */
int dev9_request(void)
{
	int err = thsemap_wait_sema(lock_id);
	if (err < 0) return errno_for_iop_error(err);
	if (!initialized) {
		err = power_on();
		if (!err) {
			/* Cache MAC before allowing ATA to use the shared SPEED PIO. */
			if (iord16(SPD_REG(SPD_REG_REV_3)) & SMAP_CAPABILITY) eeprom_error = read_eeprom();
			initialized = true;
		}
	} else err = 0;
	if (!err) {
		if (dev9_use_count == ~0u) err = -EBUSY;
		else dev9_use_count++;
	}
	thsemap_signal_sema(lock_id);
	return err;
}

int dev9_release(void)
{
	int err = thsemap_wait_sema(lock_id);
	if (err < 0) return errno_for_iop_error(err);
	if (!dev9_use_count) err = -EINVAL;
	else { dev9_use_count--; err = 0; }
	/* Do not power-cycle a shared ATA/SPEED bus on network close. */
	thsemap_signal_sema(lock_id);
	return err;
}

/* One EE netdev owns one cookie lease. Repeated acquisition is idempotent. */
static int process_request(const struct dev9_request_packet *request)
{
	int err;
	if (!request->cookie) return -EINVAL;
	if (request->operation == 2) {
		if (lease_cookie != request->cookie) return 0;
		err = dev9_release();
		if (!err) lease_cookie = 0;
		return err;
	}
	if (request->operation != 1) return -EINVAL;
	if (lease_cookie) return lease_cookie == request->cookie ? 0 : -EBUSY;
	err = dev9_request();
	if (err) return err;
	if (!(iord16(SPD_REG(SPD_REG_REV_3)) & SMAP_CAPABILITY) || !eeprom_valid) {
		dev9_release();
		return eeprom_error;
	}
	lease_cookie = request->cookie;
	return 0;
}

static void request_callback(const struct sif_cmd_header *header, void *arg)
{
	const struct dev9_request_packet *request = sif_cmd_payload(header);
	/* The ROM clears packet_size. opt explicitly identifies this fixed wire ABI. */
	if (header->opt != REQUEST_TAG) return;
	if (queue_count == QUEUE_SIZE) {
		/* A timeout release cannot strand the one permitted EE lease. */
		if (request->operation == 2)
			cleanup_cookie = request->cookie;
		dropped_requests++;
		thsemap_isignal_sema(wake_id);
		return;
	}
	queue[queue_tail].request = *request;
	/* Reserve two slots for timeout cleanup and an EBUSY response. */
	queue[queue_tail].status = request->operation == 1 && queue_count >= QUEUE_SIZE-2 ? -EBUSY : 0;
	queue_tail = (queue_tail + 1) % QUEUE_SIZE;
	queue_count++;
	thsemap_isignal_sema(wake_id);
}

#include "smap-service.h"

static void dev9_worker(void *arg)
{
	/* SIF DMA rounds its source read up to 16 bytes; transmit length stays 36. */
	static struct {
		struct sif_cmd_header header;
		struct dev9_reply reply;
		u8 dma_padding[12];
	} packet __attribute__((aligned(16)));
	bool reply_transport_failed = false;
	for (;;) {
		struct queued_request queued;
		unsigned int flags, tries;
		thsemap_wait_sema(wake_id);
		for (;;) {
			int dma_id = 0, status;
			irq_save(flags);
			if (!queue_count) {
				if (!cleanup_cookie) { irq_restore(flags); break; }
				queued.request.operation = 2;
				queued.request.cookie = cleanup_cookie;
				queued.status = 0;
				cleanup_cookie = 0;
			} else {
				queued = queue[queue_head];
				queue_head = (queue_head + 1) % QUEUE_SIZE;
				queue_count--;
			}
			irq_restore(flags);
			status = queued.status ? queued.status : process_request(&queued.request);
			if (reply_transport_failed) continue;
			memset(&packet.reply, 0, sizeof(packet.reply));
			packet.reply.cookie = queued.request.cookie;
			packet.reply.status = status;
			packet.reply.hardware = hardware;
			memcpy(packet.reply.mac, mac_address, sizeof(mac_address));
			packet.reply.checksum = mac_checksum;
			packet.header.opt = REPLY_TAG | ((queued.request.operation & 0xff) << 8);
			for (tries = 0; tries < 1000 && !dma_id; tries++) {
				dma_id = sifcmd_send_cmd(DEV9_CMD, &packet,
					sizeof(packet.header) + sizeof(packet.reply), NULL, 0, 0);
				if (!dma_id) wait_us(1000);
			}
			for (tries = 0; dma_id && tries < 1000; tries++) {
				if (sifman_dma_stat(dma_id) == SIF_DMA_STATUS_COMPLETED) break;
				wait_us(1000);
			}
			if (!dma_id || tries == 1000) {
				/* In-flight static packet must never be overwritten after timeout. */
				reply_transport_failed = true;
				pr_err("dev9: control reply DMA timeout; further replies disabled\n");
			}
		}
	}
}

static enum module_init_status dev9_init(int argc, char *argv[])
{
	const struct iop_sema lock = { .initial = 1, .max = 1 };
	const struct iop_sema wake = { .initial = 0, .max = 1 };
	const struct iop_thread worker = {
		.attr = THREAD_ATTR_C, .thread = dev9_worker, .stacksize = 2048, .priority = 0x29,
	};
	int thread_id;
	lock_id = thsemap_create_sema(&lock);
	if (lock_id < 0) return MODULE_EXIT;
	wake_id = thsemap_create_sema(&wake);
	if (wake_id < 0) goto fail_lock;
	thread_id = thbase_create(&worker);
	if (thread_id < 0) goto fail_wake;
	if (thbase_start(thread_id, NULL) < 0) {
		thbase_delete(thread_id);
		goto fail_wake;
	}
	/* Registration precedes LOADFILE completion; hardware work stays in RPC
	 * thread context, never in the ROM loader's module entry context. */
	if (smap_server_start() < 0) {
		/* No EE requests can arrive before this module has finished loading. */
		thbase_terminate(thread_id);
		thbase_delete(thread_id);
		goto fail_wake;
	}
	sif_request_cmd(DEV9_CMD, request_callback, NULL);
	pr_info("dev9: bounded control service ready (hardware setup deferred)\n");
	return MODULE_RESIDENT;
fail_wake:
	thsemap_delete_sema(wake_id);
fail_lock:
	thsemap_delete_sema(lock_id);
	return MODULE_EXIT;
}
module_init(dev9_init);
