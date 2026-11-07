// SPDX-License-Identifier: GPL-2.0-only
/*
 * ps2 native PlayStation 2 SMAP Ethernet.
 *
 * Packet data and descriptors are EE-visible device FIFOs, not EE memory
 * DMA rings. No DEV9 DMA channel or cached EE buffer is handed to the IOP.
 * MMIO ordering before descriptor ownership transfer is still essential.
 *
 * Register layout and FIFO sequence derived from the GPL Sony Computer
 * Entertainment (2000-2002)/Juergen Urban (2010-2013) Linux SMAP driver.
 * See kernel/vendor/ethernet/manifest.json for the immutable references.
 */
#include <linux/completion.h>
#include <linux/delay.h>
#include <linux/etherdevice.h>
#include <linux/ethtool.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/netdevice.h>
#include <linux/phy.h>
#include <linux/platform_device.h>
#include <linux/random.h>
#include <linux/unaligned.h>
#include <linux/workqueue.h>
#include <asm/io.h>
#include <asm/mach-ps2/iop-module.h>
#include <asm/mach-ps2/irq.h>
#include <asm/mach-ps2/sif.h>
#include "ps2-smap-core.h"
#include "ps2-smap-regs.h"
#include "ps2-smap-iop.h"
#include "ps2-smap-phy.h"

/* Slim/DECKARD SMAP is not exposed through the EE's legacy DEV9 window.
 * Use IOP hardware ownership on every model by default. The old direct
 * backend remains an explicit development option for original adapters.
 */
static bool iop_transport = true;
module_param(iop_transport, bool, 0444);
MODULE_PARM_DESC(iop_transport, "Use the IOP SMAP transport (required on Slim)");

#define DRV_NAME "ps2-smap"
#define DEV9_CONTROL_CMD 0x8000001cu
#define SMAP_REARM_CMD 0x8000001bu
#define DEV9_REQUEST_OPT 0x44560908u
#define DEV9_REPLY_OPT 0x44560014u
#define SMAP_REARM_OPT 0x534d0104u
#define DEV9_ACQUIRE 1u
#define DEV9_RELEASE 2u
#define REARM_LIMIT 32

struct dev9_request { u32 operation, cookie; };
struct dev9_reply { u32 cookie; s32 status; u32 hardware; u8 mac[6]; u16 checksum; };

struct smap {
	struct net_device *ndev;
	struct napi_struct napi;
	struct mii_bus *mii;
	struct phy_device *phy;
	spinlock_t lock;
	struct mutex state_lock, mdio_lock;
	struct work_struct reset_work;
	struct delayed_work rearm_work;
	struct completion dev9_done;
	struct dev9_reply dev9_info;
	u32 cookie, dev9_op;
	bool dev9_waiting, running, failed, closing, mdio_pending;
	unsigned int irq_count, rearm_attempts, rx_index;
	struct smap_tx_ring tx;
	struct rtnl_link_stats64 stats;
	u32 mode1, rx_mode, hash[4];
	u8 tx_buffer[1536];
};

static const unsigned int smap_irqs[] = {
	IRQ_IOP_SPD_TXDNV, IRQ_IOP_SPD_RXDNV, IRQ_IOP_SPD_TXEND,
	IRQ_IOP_SPD_RXEND, IRQ_IOP_SPD_EMAC3,
};

static u8 smap_read8(unsigned int r) { return inb(SMAP_IO_BASE + r); }
static u16 smap_read16(unsigned int r) { return inw(SMAP_IO_BASE + r); }
static u32 smap_read32(unsigned int r) { return inl(SMAP_IO_BASE + r); }
static void smap_write8(unsigned int r, u8 v) { outb(v, SMAP_IO_BASE + r); }
static void smap_write16(unsigned int r, u16 v) { outw(v, SMAP_IO_BASE + r); }
static void smap_write32(unsigned int r, u32 v) { outl(v, SMAP_IO_BASE + r); }

/* EMAC words are high half first even though the EE is little-endian. */
static u32 emac_read(unsigned int r)
{
	u32 hi = smap_read16(r);
	return (hi << 16) | smap_read16(r + 2);
}

static void emac_write(unsigned int r, u32 v)
{
	smap_write16(r, v >> 16);
	smap_write16(r + 2, v);
	(void)smap_read16(r + 2);
}

static unsigned int bd_reg(bool rx, unsigned int index, unsigned int r)
{
	return (rx ? SMAP_BD_RX : SMAP_BD_TX) + index * 8 + r;
}

static void smap_dev9_reply(const struct sif_cmd_header *header, void *arg)
{
	struct smap *s = arg;
	const struct dev9_reply *reply = sif_cmd_payload(header);
	if (header->packet_size != sizeof(*header) + sizeof(*reply) ||
	    header->opt != (DEV9_REPLY_OPT | (s->dev9_op << 8)) ||
	    !READ_ONCE(s->dev9_waiting) || reply->cookie != s->cookie)
		return;
	s->dev9_info = *reply;
	complete(&s->dev9_done);
}

static int smap_dev9_control(struct smap *s, u32 op)
{
	struct dev9_request request;
	unsigned long flags;
	int err;
	/* The release identifies the same lease; a new device gets a fresh token. */
	if (op == DEV9_ACQUIRE) {
		s->cookie = get_random_u32();
		if (!s->cookie)
			s->cookie = 1;
	}
	request.operation = op;
	request.cookie = s->cookie;
	s->dev9_op = op;

	reinit_completion(&s->dev9_done);
	WRITE_ONCE(s->dev9_waiting, true);
	local_irq_save(flags);
	err = sif_request_cmd(DEV9_CONTROL_CMD, smap_dev9_reply, s);
	local_irq_restore(flags);
	if (err)
		goto out_wait;
	err = sif_cmd_opt(DEV9_CONTROL_CMD, DEV9_REQUEST_OPT, &request, sizeof(request));
	if (err < 0)
		goto out_callback;
	if (!wait_for_completion_timeout(&s->dev9_done, 10 * HZ)) {
		/* No pointers are transmitted. A late acquire is followed by release. */
		if (op == DEV9_ACQUIRE) {
			request.operation = DEV9_RELEASE;
			if (sif_cmd_opt(DEV9_CONTROL_CMD, DEV9_REQUEST_OPT,
					&request, sizeof(request)) < 0)
				dev_err(&s->ndev->dev, "DEV9 timeout cleanup could not be queued\n");
		}
		err = -ETIMEDOUT;
	} else {
		err = s->dev9_info.status;
	}
out_callback:
	/* Callback execution is in SIF0 IRQ context; remove before freeing s. */
	local_irq_save(flags);
	WRITE_ONCE(s->dev9_waiting, false);
	sif_request_cmd(DEV9_CONTROL_CMD, NULL, NULL);
	local_irq_restore(flags);
	synchronize_irq(IRQ_DMAC_SIF0);
out_wait:
	WRITE_ONCE(s->dev9_waiting, false);
	return err;
}

static void smap_write_mac(struct smap *s)
{
	const u8 *a = s->ndev->dev_addr;
	emac_write(EMAC_MAC_HI, (a[0] << 8) | a[1]);
	emac_write(EMAC_MAC_LO, (a[2] << 24) | (a[3] << 16) | (a[4] << 8) | a[5]);
}

static void smap_write_filter(struct smap *s)
{
	unsigned int i;
	for (i = 0; i < ARRAY_SIZE(s->hash); i++)
		emac_write(EMAC_HASH + i * 4, s->hash[i]);
	emac_write(EMAC_RXMODE, s->rx_mode);
}

static int smap_hw_reset(struct smap *s)
{
	u32 value;
	unsigned int i;
	int err;

	/* Only the IOP changes SPEED's shared Ethernet/ATA interrupt mask. */
	mutex_lock(&s->mdio_lock);
	emac_write(EMAC_IRQ_ENABLE, 0);
	/* Both the SDK and original Linux driver reset the FIFOs before EMAC.
	 * A warm emulator must not be a prerequisite for cold hardware setup.
	 */
	smap_write8(SMAP_TX_CTRL, 1);
	smap_write8(SMAP_RX_CTRL, 1);
	err = read_poll_timeout(smap_read8, value, !(value & 1),
				10, 10000, false, SMAP_TX_CTRL);
	if (err) {
		netdev_err(s->ndev, "TX FIFO reset timed out\n");
		goto out;
	}
	err = read_poll_timeout(smap_read8, value, !(value & 1),
				10, 10000, false, SMAP_RX_CTRL);
	if (err) {
		netdev_err(s->ndev, "RX FIFO reset timed out\n");
		goto out;
	}
	emac_write(EMAC_MODE0, EMAC_RESET);
	err = read_poll_timeout(emac_read, value, !(value & EMAC_RESET),
				10, 10000, false, EMAC_MODE0);
	if (err) {
		netdev_err(s->ndev, "EMAC reset timed out\n");
		goto out;
	}
	/* Reset terminates any previous station-management transaction. */
	s->mdio_pending = false;
	smap_write8(SMAP_BD_MODE, 0);
	smap_write16(SMAP_CLEAR, SMAP_IRQ_MASK);
	emac_write(EMAC_IRQ_STATUS, emac_read(EMAC_IRQ_STATUS));
	smap_ring_init(&s->tx);
	s->rx_index = 0;
	for (i = 0; i < SMAP_BD_COUNT; i++) {
		smap_write16(bd_reg(false, i, SMAP_BD_CTRL), 0);
		smap_write16(bd_reg(false, i, SMAP_BD_RESERVED), 0);
		smap_write16(bd_reg(false, i, SMAP_BD_LENGTH), 0);
		smap_write16(bd_reg(false, i, SMAP_BD_POINTER), 0);
		smap_write16(bd_reg(true, i, SMAP_BD_RESERVED), 0);
		smap_write16(bd_reg(true, i, SMAP_BD_LENGTH), 0);
		smap_write16(bd_reg(true, i, SMAP_BD_POINTER), 0);
		wmb();
		smap_write16(bd_reg(true, i, SMAP_BD_CTRL), SMAP_RX_EMPTY);
	}
	emac_write(EMAC_MODE1, s->mode1);
	smap_write_mac(s);
	smap_write_filter(s);
	emac_write(EMAC_IFG, 4);
	emac_write(EMAC_TXMODE1, (7u << 27) | (15u << 16));
	emac_write(EMAC_TX_THRESHOLD, 12u << 27);
	emac_write(EMAC_RX_WATERMARK, (16u << 23) | (128u << 7));
out:
	mutex_unlock(&s->mdio_lock);
	return err;
}

static int smap_mdio(struct mii_bus *bus, int phy, int reg, int data)
{
	struct smap *s = bus->priv;
	u32 value, command;
	unsigned long flags;
	int err;
	if (phy < 0 || phy > 31 || reg < 0 || reg > 31)
		return -EINVAL;
	mutex_lock(&s->mdio_lock);
	/* A reset station-management register need not have DONE set. Only wait
	 * for an operation we actually submitted; preserve ownership on timeout.
	 */
	if (s->mdio_pending) {
		err = read_poll_timeout(emac_read, value, value & EMAC_MDIO_DONE,
					10, 2000, false, EMAC_MDIO);
		if (err)
			goto out;
		s->mdio_pending = false;
	}
	command = (phy << 5) | reg;
	command |= data < 0 ? EMAC_MDIO_READ : EMAC_MDIO_WRITE | ((u32)data << 16);
	spin_lock_irqsave(&s->lock, flags);
	s->mdio_pending = true;
	emac_write(EMAC_MDIO, command);
	spin_unlock_irqrestore(&s->lock, flags);
	err = read_poll_timeout(emac_read, value, value & EMAC_MDIO_DONE,
				10, 2000, false, EMAC_MDIO);
	/* The hardware requires a second read after completion for PHY data. */
	if (!err) {
		value = emac_read(EMAC_MDIO);
		s->mdio_pending = false;
		if (value & EMAC_MDIO_ERROR)
			err = -EIO;
	}
out:
	mutex_unlock(&s->mdio_lock);
	return err ? err : data < 0 ? (int)(value >> 16) : 0;
}

static int smap_mdio_read(struct mii_bus *bus, int phy, int reg)
{
	return smap_mdio(bus, phy, reg, -1);
}

static int smap_mdio_write(struct mii_bus *bus, int phy, int reg, u16 data)
{
	return smap_mdio(bus, phy, reg, data);
}

static void smap_adjust_link(struct net_device *ndev)
{
	struct smap *s = netdev_priv(ndev);
	struct phy_device *phy = s->phy;
	unsigned long flags;
	u32 mode;
	spin_lock_irqsave(&s->lock, flags);
	mode = EMAC_MODE1_DEFAULT;
	if (phy->speed == SPEED_100)
		mode |= EMAC_SPEED100;
	if (phy->duplex == DUPLEX_FULL)
		mode |= EMAC_DUPLEX;
	else if (phy->speed == SPEED_10)
		mode &= ~EMAC_IGNORE_SQE;
	s->mode1 = mode;
	if (s->running)
		emac_write(EMAC_MODE1, mode);
	spin_unlock_irqrestore(&s->lock, flags);
	phy_print_status(phy);
}

/* Call with s->lock held. Software lengths cannot be corrupted by the MAC. */
static void smap_tx_complete(struct smap *s)
{
	unsigned int packets = 0, bytes = 0;
	while (s->tx.used) {
		unsigned int i = s->tx.consumer;
		u16 status = smap_read16(bd_reg(false, i, SMAP_BD_CTRL));
		unsigned int len;
		if (status & SMAP_TX_READY)
			break;
		rmb();
		len = smap_tx_reclaim(&s->tx);
		packets++;
		bytes += len;
		if (status & SMAP_TX_ERRORS) {
			s->stats.tx_errors++;
			if (status & BIT(1))
				s->stats.tx_fifo_errors++;
			if (status & (BIT(5) | BIT(4)))
				s->stats.tx_aborted_errors++;
			if (status & BIT(7))
				s->stats.tx_carrier_errors++;
		} else {
			s->stats.tx_packets++;
			s->stats.tx_bytes += len;
		}
		if (status & (BIT(3) | BIT(2)))
			s->stats.collisions++;
		smap_write16(bd_reg(false, i, SMAP_BD_CTRL), 0);
	}
	if (packets)
		netdev_completed_queue(s->ndev, packets, bytes);
	if (s->running && !s->failed && smap_tx_room(&s->tx, SMAP_MAX_FRAME))
		netif_wake_queue(s->ndev);
}

static netdev_tx_t smap_xmit(struct sk_buff *skb, struct net_device *ndev)
{
	struct smap *s = netdev_priv(ndev);
	unsigned long flags;
	unsigned int i, index, len = skb->len;
	if (len < ETH_HLEN || len > SMAP_MAX_FRAME ||
	    skb_copy_bits(skb, 0, s->tx_buffer, len)) {
		spin_lock_irqsave(&s->lock, flags);
		s->stats.tx_dropped++;
		spin_unlock_irqrestore(&s->lock, flags);
		dev_kfree_skb_any(skb);
		return NETDEV_TX_OK;
	}
	memset(s->tx_buffer + len, 0, smap_round(len) - len);
	spin_lock_irqsave(&s->lock, flags);
	smap_tx_complete(s);
	if (!s->running || s->failed || !smap_tx_room(&s->tx, len)) {
		netif_stop_queue(ndev);
		spin_unlock_irqrestore(&s->lock, flags);
		return NETDEV_TX_BUSY;
	}
	index = s->tx.producer;
	smap_write16(SMAP_TX_PTR, s->tx.offset);
	for (i = 0; i < smap_round(len); i += 4)
		smap_write32(SMAP_TX_DATA, get_unaligned_le32(s->tx_buffer + i));
	smap_write16(bd_reg(false, index, SMAP_BD_LENGTH), len);
	smap_write16(bd_reg(false, index, SMAP_BD_POINTER), SMAP_TX_BASE + s->tx.offset);
	smap_write8(SMAP_TX_INC, 1);
	/* FIFO payload and metadata must be visible before READY is published. */
	wmb();
	smap_write16(bd_reg(false, index, SMAP_BD_CTRL), SMAP_TX_READY | SMAP_TX_FCS_PAD);
	(void)smap_read16(bd_reg(false, index, SMAP_BD_CTRL));
	smap_tx_commit(&s->tx, len);
	netdev_sent_queue(ndev, len);
	emac_write(EMAC_TXMODE0, EMAC_GET_PACKET);
	netif_trans_update(ndev);
	if (!smap_tx_room(&s->tx, SMAP_MAX_FRAME))
		netif_stop_queue(ndev);
	spin_unlock_irqrestore(&s->lock, flags);
	dev_consume_skb_any(skb);
	return NETDEV_TX_OK;
}

static int smap_rearm(struct smap *s)
{
	const u32 mask = SMAP_IRQ_MASK;
	if (!READ_ONCE(s->running) || READ_ONCE(s->closing))
		return -ESHUTDOWN;
	return sif_cmd_opt(SMAP_REARM_CMD, SMAP_REARM_OPT, &mask, sizeof(mask));
}

static void smap_rearm_work(struct work_struct *work)
{
	struct smap *s = container_of(to_delayed_work(work), struct smap, rearm_work);
	unsigned long flags;
	int err;
	spin_lock_irqsave(&s->lock, flags);
	if (!s->running)
		goto out;
	err = smap_rearm(s);
	if (!err) {
		s->rearm_attempts = 0;
	} else if (++s->rearm_attempts < REARM_LIMIT) {
		mod_delayed_work(system_wq, &s->rearm_work, msecs_to_jiffies(10));
	} else {
		s->failed = true;
		netif_stop_queue(s->ndev);
		netdev_err(s->ndev, "interrupt rearm failed after %u attempts: %d\n",
			   s->rearm_attempts, err);
		schedule_work(&s->reset_work);
	}
out:
	spin_unlock_irqrestore(&s->lock, flags);
}

static int smap_poll(struct napi_struct *napi, int budget)
{
	struct smap *s = container_of(napi, struct smap, napi);
	unsigned long flags;
	int done = 0;
	u32 errors;
	spin_lock_irqsave(&s->lock, flags);
	smap_tx_complete(s);
	errors = emac_read(EMAC_IRQ_STATUS);
	emac_write(EMAC_IRQ_STATUS, errors);
	if (errors & EMAC_IRQ_DEAD) {
		s->failed = true;
		netif_stop_queue(s->ndev);
		schedule_work(&s->reset_work);
	}
	spin_unlock_irqrestore(&s->lock, flags);
	if (!budget)
		return 0;
	while (done < budget) {
		unsigned int index = s->rx_index;
		u16 status = smap_read16(bd_reg(true, index, SMAP_BD_CTRL));
		u16 len, pointer;
		struct sk_buff *skb = NULL;
		unsigned int i;
		if (status & SMAP_RX_EMPTY)
			break;
		rmb();
		len = smap_read16(bd_reg(true, index, SMAP_BD_LENGTH));
		pointer = smap_read16(bd_reg(true, index, SMAP_BD_POINTER));
		if (smap_rx_valid(status, len, pointer)) {
			skb = napi_alloc_skb(napi, smap_round(len) + NET_IP_ALIGN);
			if (skb) {
				skb_reserve(skb, NET_IP_ALIGN);
				skb_put(skb, len);
				smap_write16(SMAP_RX_PTR, (pointer - SMAP_RX_BASE) & 0x3ffc);
				for (i = 0; i < smap_round(len); i += 4)
					put_unaligned_le32(smap_read32(SMAP_RX_DATA), skb->data + i);
			}
		}
		/* Return every descriptor, including invalid frames and allocation failure. */
		smap_write8(SMAP_RX_DEC, 1);
		wmb();
		smap_write16(bd_reg(true, index, SMAP_BD_CTRL), SMAP_RX_EMPTY);
		(void)smap_read16(bd_reg(true, index, SMAP_BD_CTRL));
		s->rx_index = (index + 1) & (SMAP_BD_COUNT - 1);
		spin_lock_irqsave(&s->lock, flags);
		if (!smap_rx_valid(status, len, pointer)) {
			s->stats.rx_errors++;
			if (status & BIT(3))
				s->stats.rx_crc_errors++;
			if (status & BIT(9))
				s->stats.rx_over_errors++;
			if (len < ETH_HLEN || len > SMAP_MAX_FRAME)
				s->stats.rx_length_errors++;
		} else if (!skb) {
			s->stats.rx_dropped++;
		} else {
			s->stats.rx_packets++;
			s->stats.rx_bytes += len;
		}
		spin_unlock_irqrestore(&s->lock, flags);
		if (skb) {
			skb->protocol = eth_type_trans(skb, s->ndev);
			napi_gro_receive(napi, skb);
		}
		done++;
	}
	if (done < budget && napi_complete_done(napi, done)) {
		spin_lock_irqsave(&s->lock, flags);
		if (s->running && !s->failed && smap_rearm(s) < 0)
			mod_delayed_work(system_wq, &s->rearm_work, msecs_to_jiffies(10));
		spin_unlock_irqrestore(&s->lock, flags);
	}
	return done;
}

static irqreturn_t smap_interrupt(int irq, void *arg)
{
	struct smap *s = arg;
	if (irq < IRQ_IOP_SPD_TXDNV || irq > IRQ_IOP_SPD_EMAC3)
		return IRQ_NONE;
	/* IOP has already masked and acknowledged the group before this relay. */
	if (READ_ONCE(s->running))
		napi_schedule_irqoff(&s->napi);
	return IRQ_HANDLED;
}

static void smap_free_irqs(struct smap *s)
{
	while (s->irq_count)
		free_irq(smap_irqs[--s->irq_count], s);
}

static int smap_open(struct net_device *ndev)
{
	struct smap *s = netdev_priv(ndev);
	unsigned long flags;
	int err;
	mutex_lock(&s->state_lock);
	err = smap_hw_reset(s);
	if (err)
		goto out;
	napi_enable(&s->napi);
	spin_lock_irqsave(&s->lock, flags);
	s->running = true;
	s->failed = false;
	s->closing = false;
	s->rearm_attempts = 0;
	spin_unlock_irqrestore(&s->lock, flags);
	for (s->irq_count = 0; s->irq_count < ARRAY_SIZE(smap_irqs); s->irq_count++) {
		err = request_irq(smap_irqs[s->irq_count], smap_interrupt, 0, ndev->name, s);
		if (err)
			goto err_irq;
	}
	emac_write(EMAC_IRQ_ENABLE, EMAC_IRQ_ALL);
	emac_write(EMAC_MODE0, EMAC_ENABLE);
	err = smap_rearm(s);
	if (err < 0)
		goto err_irq;
	netdev_reset_queue(ndev);
	phy_start(s->phy);
	netif_start_queue(ndev);
	mutex_unlock(&s->state_lock);
	return 0;
err_irq:
	WRITE_ONCE(s->running, false);
	emac_write(EMAC_IRQ_ENABLE, 0);
	emac_write(EMAC_MODE0, 0);
	smap_free_irqs(s);
	napi_disable(&s->napi);
out:
	mutex_unlock(&s->state_lock);
	return err;
}

static int smap_stop(struct net_device *ndev)
{
	struct smap *s = netdev_priv(ndev);
	unsigned long flags;
	spin_lock_irqsave(&s->lock, flags);
	s->running = false;
	s->closing = true;
	spin_unlock_irqrestore(&s->lock, flags);
	netif_tx_disable(ndev);
	phy_stop(s->phy);
	cancel_delayed_work_sync(&s->rearm_work);
	cancel_work_sync(&s->reset_work);
	mutex_lock(&s->state_lock);
	WRITE_ONCE(s->running, false);
	emac_write(EMAC_IRQ_ENABLE, 0);
	emac_write(EMAC_MODE0, 0);
	smap_free_irqs(s);
	napi_disable(&s->napi);
	spin_lock_irqsave(&s->lock, flags);
	s->stats.tx_dropped += s->tx.used;
	smap_ring_init(&s->tx);
	spin_unlock_irqrestore(&s->lock, flags);
	netdev_reset_queue(ndev);
	mutex_unlock(&s->state_lock);
	return 0;
}

static void smap_reset_work(struct work_struct *work)
{
	struct smap *s = container_of(work, struct smap, reset_work);
	unsigned long flags;
	unsigned int i;
	int err;
	mutex_lock(&s->state_lock);
	if (!s->running || s->closing)
		goto out;
	spin_lock_irqsave(&s->lock, flags);
	s->running = false;
	spin_unlock_irqrestore(&s->lock, flags);
	netif_tx_disable(s->ndev);
	for (i = 0; i < s->irq_count; i++)
		synchronize_irq(smap_irqs[i]);
	napi_disable(&s->napi);
	spin_lock_irqsave(&s->lock, flags);
	s->stats.tx_dropped += s->tx.used;
	spin_unlock_irqrestore(&s->lock, flags);
	err = smap_hw_reset(s);
	netdev_reset_queue(s->ndev);
	napi_enable(&s->napi);
	spin_lock_irqsave(&s->lock, flags);
	s->running = !s->closing;
	s->failed = !!err;
	if (!err && !s->closing) {
		emac_write(EMAC_IRQ_ENABLE, EMAC_IRQ_ALL);
		emac_write(EMAC_MODE0, EMAC_ENABLE);
		err = smap_rearm(s);
		s->failed = err < 0;
	}
	spin_unlock_irqrestore(&s->lock, flags);
	if (err) {
		netif_carrier_off(s->ndev);
		netdev_err(s->ndev, "recovery failed: %d; bring interface down/up to retry\n", err);
	} else if (!s->closing) {
		netif_wake_queue(s->ndev);
	}
out:
	mutex_unlock(&s->state_lock);
}

static void smap_timeout(struct net_device *ndev, unsigned int queue)
{
	struct smap *s = netdev_priv(ndev);
	unsigned long flags;
	spin_lock_irqsave(&s->lock, flags);
	s->stats.tx_errors++;
	s->failed = true;
	spin_unlock_irqrestore(&s->lock, flags);
	netif_stop_queue(ndev);
	schedule_work(&s->reset_work);
}

static void smap_rx_mode(struct net_device *ndev)
{
	struct smap *s = netdev_priv(ndev);
	struct netdev_hw_addr *ha;
	u32 hash[4] = { 0 }, mode = EMAC_RX_DEFAULT;
	unsigned long flags;
	if (ndev->flags & IFF_PROMISC)
		mode |= EMAC_RX_PROMISC;
	if (ndev->flags & IFF_ALLMULTI) {
		mode |= EMAC_RX_ALLMULTI;
	} else if (!netdev_mc_empty(ndev)) {
		mode |= EMAC_RX_MULTICAST;
		netdev_for_each_mc_addr(ha, ndev) {
			u32 bit = ether_crc(ETH_ALEN, ha->addr) >> 26;
			hash[bit >> 4] |= 0x8000u >> (bit & 15);
		}
	}
	spin_lock_irqsave(&s->lock, flags);
	s->rx_mode = mode;
	memcpy(s->hash, hash, sizeof(hash));
	if (s->running)
		smap_write_filter(s);
	spin_unlock_irqrestore(&s->lock, flags);
}

static int smap_mac(struct net_device *ndev, void *address)
{
	struct smap *s = netdev_priv(ndev);
	int err = eth_mac_addr(ndev, address);
	unsigned long flags;
	if (err)
		return err;
	spin_lock_irqsave(&s->lock, flags);
	smap_write_mac(s);
	spin_unlock_irqrestore(&s->lock, flags);
	return 0;
}

static void smap_stats(struct net_device *ndev, struct rtnl_link_stats64 *stats)
{
	struct smap *s = netdev_priv(ndev);
	unsigned long flags;
	spin_lock_irqsave(&s->lock, flags);
	*stats = s->stats;
	spin_unlock_irqrestore(&s->lock, flags);
}

static const struct net_device_ops smap_netdev_ops = {
	.ndo_open = smap_open,
	.ndo_stop = smap_stop,
	.ndo_start_xmit = smap_xmit,
	.ndo_tx_timeout = smap_timeout,
	.ndo_set_rx_mode = smap_rx_mode,
	.ndo_set_mac_address = smap_mac,
	.ndo_validate_addr = eth_validate_addr,
	.ndo_get_stats64 = smap_stats,
	.ndo_eth_ioctl = phy_do_ioctl_running,
};

static void smap_drvinfo(struct net_device *ndev, struct ethtool_drvinfo *info)
{
	strscpy(info->driver, DRV_NAME, sizeof(info->driver));
	strscpy(info->bus_info, "EE DEV9 PIO / IOP IRQ relay", sizeof(info->bus_info));
}

static const struct ethtool_ops smap_ethtool_ops = {
	.get_drvinfo = smap_drvinfo,
	.get_link = ethtool_op_get_link,
	.get_link_ksettings = phy_ethtool_get_link_ksettings,
	.set_link_ksettings = phy_ethtool_set_link_ksettings,
	.nway_reset = phy_ethtool_nway_reset,
};

static int smap_probe(struct platform_device *pdev)
{
	struct net_device *ndev;
	struct smap *s;
	int err;
	if (iop_transport)
		return smap_iop_probe(pdev);
	BUILD_BUG_ON(sizeof(struct dev9_reply) != 20);
	/* The existing firmware loader is synchronous; no DEV9 power work in its init. */
	err = iop_module_request("dev9", 0x0101, NULL);
	if (err < 0) {
		dev_err(&pdev->dev, "DEV9 firmware unavailable: %d\n", err);
		return err;
	}
	ndev = alloc_etherdev(sizeof(*s));
	if (!ndev)
		return -ENOMEM;
	SET_NETDEV_DEV(ndev, &pdev->dev);
	s = netdev_priv(ndev);
	s->ndev = ndev;
	spin_lock_init(&s->lock);
	mutex_init(&s->state_lock);
	mutex_init(&s->mdio_lock);
	init_completion(&s->dev9_done);
	INIT_WORK(&s->reset_work, smap_reset_work);
	INIT_DELAYED_WORK(&s->rearm_work, smap_rearm_work);
	s->mode1 = EMAC_MODE1_DEFAULT;
	s->rx_mode = EMAC_RX_DEFAULT;
	err = smap_dev9_control(s, DEV9_ACQUIRE);
	if (err) {
		dev_err(&pdev->dev, "DEV9 factory-MAC acquisition failed: %d\n", err);
		goto err_free;
	}
	if (smap_read16(SMAP_CAPS) == 0xffff ||
	    !(smap_read16(SMAP_CAPS) & SMAP_CAP_ETH) ||
	    (s->dev9_info.hardware != 0x20 && s->dev9_info.hardware != 0x30)) {
		err = -ENODEV;
		dev_err(&pdev->dev, "SMAP capability/DEV9 variant unavailable\n");
		goto err_lease;
	}
	if (!is_valid_ether_addr(s->dev9_info.mac) ||
	    !smap_mac_checksum(s->dev9_info.mac, s->dev9_info.checksum)) {
		dev_err(&pdev->dev, "invalid factory EEPROM MAC/checksum\n");
		err = -EADDRNOTAVAIL;
		goto err_lease;
	}
	eth_hw_addr_set(ndev, s->dev9_info.mac);
	/* Do not claim SPEED's shared ATA registers or change its DMA configuration. */
	if (!request_mem_region(SMAP_IO_BASE + 0x100, 0x3300, DRV_NAME)) {
		err = -EBUSY;
		goto err_lease;
	}
	err = smap_hw_reset(s);
	if (err) {
		dev_err(&pdev->dev, "SMAP hardware reset failed: %d\n", err);
		goto err_region;
	}
	s->mii = mdiobus_alloc();
	if (!s->mii) {
		err = -ENOMEM;
		goto err_region;
	}
	s->mii->name = "PlayStation 2 SMAP MDIO";
	s->mii->read = smap_mdio_read;
	s->mii->write = smap_mdio_write;
	s->mii->priv = s;
	s->mii->parent = &pdev->dev;
	snprintf(s->mii->id, MII_BUS_ID_SIZE, "ps2-smap-%d", pdev->id);
	err = mdiobus_register(s->mii);
	if (err) {
		dev_err(&pdev->dev, "MDIO discovery failed: %d\n", err);
		goto err_mii;
	}
	s->phy = phy_find_first(s->mii);
	if (!s->phy) {
		err = -ENODEV;
		dev_err(&pdev->dev, "no PHY responded on SMAP MDIO bus\n");
		goto err_bus;
	}
	s->phy->irq = PHY_POLL;
	err = smap_phy_prepare(s->phy);
	if (err) {
		dev_err(&pdev->dev, "PHY handoff cleanup failed: %d\n", err);
		goto err_bus;
	}
	err = phy_connect_direct(ndev, s->phy, smap_adjust_link, PHY_INTERFACE_MODE_MII);
	if (err) {
		dev_err(&pdev->dev, "PHY attachment failed: %d\n", err);
		goto err_bus;
	}
	phy_set_max_speed(s->phy, SPEED_100);
	ndev->netdev_ops = &smap_netdev_ops;
	ndev->ethtool_ops = &smap_ethtool_ops;
	ndev->watchdog_timeo = 5 * HZ;
	ndev->min_mtu = 68;
	ndev->max_mtu = 1500;
	ndev->tx_queue_len = 32;
	netif_carrier_off(ndev);
	netif_napi_add(ndev, &s->napi, smap_poll);
	err = register_netdev(ndev);
	if (err)
		goto err_napi;
	platform_set_drvdata(pdev, ndev);
	netdev_info(ndev, "native SMAP %pM, DEV9 variant %02x, PHY %s, PIO\n",
		    ndev->dev_addr, s->dev9_info.hardware, phydev_name(s->phy));
	return 0;
err_napi:
	netif_napi_del(&s->napi);
	phy_disconnect(s->phy);
err_bus:
	mdiobus_unregister(s->mii);
err_mii:
	mdiobus_free(s->mii);
err_region:
	emac_write(EMAC_IRQ_ENABLE, 0);
	emac_write(EMAC_MODE0, 0);
	release_mem_region(SMAP_IO_BASE + 0x100, 0x3300);
err_lease:
	if (smap_dev9_control(s, DEV9_RELEASE))
		dev_warn(&pdev->dev, "DEV9 lease cleanup failed\n");
err_free:
	free_netdev(ndev);
	return err;
}

static void smap_remove(struct platform_device *pdev)
{
	if (iop_transport) {
		smap_iop_remove(pdev);
		return;
	}
	struct net_device *ndev = platform_get_drvdata(pdev);
	struct smap *s = netdev_priv(ndev);
	unregister_netdev(ndev);
	cancel_delayed_work_sync(&s->rearm_work);
	cancel_work_sync(&s->reset_work);
	netif_napi_del(&s->napi);
	phy_disconnect(s->phy);
	mdiobus_unregister(s->mii);
	mdiobus_free(s->mii);
	emac_write(EMAC_IRQ_ENABLE, 0);
	emac_write(EMAC_MODE0, 0);
	release_mem_region(SMAP_IO_BASE + 0x100, 0x3300);
	if (smap_dev9_control(s, DEV9_RELEASE))
		dev_warn(&pdev->dev, "DEV9 release failed\n");
	free_netdev(ndev);
}

static struct platform_driver smap_driver = {
	.remove = smap_remove,
	.driver = { .name = DRV_NAME },
};
static struct platform_device *smap_device;

static int __init smap_init(void)
{
	int err;
	smap_device = platform_device_register_simple(DRV_NAME, -1, NULL, 0);
	if (IS_ERR(smap_device))
		return PTR_ERR(smap_device);
	/* A missing/failed hardware probe must fail modprobe, not report success. */
	err = platform_driver_probe(&smap_driver, smap_probe);
	if (err) {
		platform_device_unregister(smap_device);
		return err;
	}
	return 0;
}

static void __exit smap_exit(void)
{
	platform_device_unregister(smap_device);
	platform_driver_unregister(&smap_driver);
}
#ifdef MODULE
module_init(smap_init);
#else
/* The SIF/heap/module/IRQ device initcalls and embedded firmware come first. */
late_initcall(smap_init);
#endif
module_exit(smap_exit);
MODULE_SOFTDEP("pre: sif iop-module iop-irq");
MODULE_DESCRIPTION("ps2 native PlayStation 2 SMAP Ethernet with IOP transport");
MODULE_LICENSE("GPL");
