/* SPDX-License-Identifier: GPL-2.0-only */
/* IOP owns SMAP registers/FIFOs. Linux owns IP and PHY policy over SIF.
 * Register protocol: preserved Sony GPL driver and PS2SDK smapregs.h.
 * Polling is intentional: no SPEED interrupt or DEV9 DMA ownership is
 * taken from ATA; the service is serialized and every hardware wait bounded.
 */
#define PS2_SMAP_IOP
#include "iopmod/ps2-smap-core.h"
#include "iopmod/ps2-smap-regs.h"
#include "iopmod/ps2-smap-rpc.h"

static struct sifcmd_rpc_data_queue smap_rpc_queue;
static struct sifcmd_rpc_server_data smap_rpc_server;
static struct smap_rpc_packet smap_rpc_buffer __attribute__((aligned(16)));
static struct smap_rpc_packet smap_rpc_result __attribute__((aligned(16)));
static struct smap_tx_ring smap_ring;
static unsigned int smap_rx_index;
static bool smap_active, smap_initialized, smap_mdio_pending;
static u32 smap_tx_packets, smap_tx_bytes, smap_tx_errors, smap_rx_errors;

static u8 sr8(unsigned int r) { return iord8(SPD_REGBASE + r); }
static u16 sr16(unsigned int r) { return iord16(SPD_REGBASE + r); }
static u32 sr32(unsigned int r) { return iord32(SPD_REGBASE + r); }
static void sw8(unsigned int r, u8 v) { iowr8(v, SPD_REGBASE + r); }
static void sw16(unsigned int r, u16 v) { iowr16(v, SPD_REGBASE + r); }
static void sw32(unsigned int r, u32 v) { iowr32(v, SPD_REGBASE + r); }
static u32 se_read(unsigned int r) { return ((u32)sr16(r) << 16) | sr16(r+2); }
static void se_write(unsigned int r, u32 v)
{
	sw16(r, v >> 16); sw16(r+2, v); (void)sr16(r+2);
}
static unsigned int sb(bool rx, unsigned int i, unsigned int r)
{
	return (rx ? SMAP_BD_RX : SMAP_BD_TX) + i*8 + r;
}
static int smap_wait8(unsigned int reg, u8 mask)
{
	unsigned int i;
	for (i=0; i<100; i++) {
		if (!(sr8(reg) & mask)) return 0;
		wait_us(100);
	}
	return -SMAP_RPC_TIMEOUT;
}
static int smap_station_wait(u32 *value)
{
	unsigned int i;
	for (i=0; i<100; i++) {
		*value = se_read(EMAC_MDIO);
		if (*value & EMAC_MDIO_DONE) {
			*value = se_read(EMAC_MDIO);
			smap_mdio_pending = false;
			return (*value & EMAC_MDIO_ERROR) ? -EIO : 0;
		}
		wait_us(100);
	}
	return -SMAP_RPC_TIMEOUT;
}
static int smap_station(struct smap_rpc_packet *p, bool write)
{
	u32 value, cmd;
	int err;
	if (p->phy>31 || p->reg>31 || (write && p->value>0xffff)) return -EINVAL;
	if (smap_mdio_pending) {
		err = smap_station_wait(&value);
		if (err) return err;
	}
	cmd = (p->phy<<5) | p->reg;
	cmd |= write ? EMAC_MDIO_WRITE | (p->value<<16) : EMAC_MDIO_READ;
	smap_mdio_pending = true;
	se_write(EMAC_MDIO, cmd);
	err = smap_station_wait(&value);
	if (!err) smap_rpc_result.value = value>>16;
	return err;
}
static void smap_configure(const struct smap_rpc_packet *p)
{
	unsigned int i;
	se_write(EMAC_MODE1, p->mode1);
	se_write(EMAC_MAC_HI, ((u32)p->mac[0]<<8) | p->mac[1]);
	se_write(EMAC_MAC_LO, ((u32)p->mac[2]<<24) | ((u32)p->mac[3]<<16) |
		((u32)p->mac[4]<<8) | p->mac[5]);
	for (i=0; i<4; i++) se_write(EMAC_HASH+i*4, p->hash[i]);
	se_write(EMAC_RXMODE, p->rx_mode);
}
static int smap_reset(const struct smap_rpc_packet *p)
{
	unsigned int i;
	int err;
	smap_active = false;
	smap_initialized = false;
	se_write(EMAC_IRQ_ENABLE, 0);
	se_write(EMAC_MODE0, 0);
	sw8(SMAP_BD_MODE, 0);
	sw16(SMAP_CLEAR, SMAP_IRQ_MASK);
	sw8(SMAP_TX_CTRL, 1);
	err = smap_wait8(SMAP_TX_CTRL, 1);
	if (err) { pr_err("smap: IOP TX FIFO reset timed out\n"); return err; }
	sw8(SMAP_RX_CTRL, 1);
	err = smap_wait8(SMAP_RX_CTRL, 1);
	if (err) { pr_err("smap: IOP RX FIFO reset timed out\n"); return err; }
	se_write(EMAC_MODE0, EMAC_RESET);
	for (i=0; i<100; i++) {
		if (!(se_read(EMAC_MODE0) & EMAC_RESET)) break;
		wait_us(100);
	}
	if (i==100) { pr_err("smap: IOP EMAC reset timed out\n"); return -SMAP_RPC_TIMEOUT; }
	smap_mdio_pending = false;
	smap_ring_init(&smap_ring); smap_rx_index = 0;
	for (i=0; i<SMAP_BD_COUNT; i++) {
		sw16(sb(false,i,SMAP_BD_CTRL), 0);
		sw16(sb(false,i,SMAP_BD_RESERVED), 0);
		sw16(sb(false,i,SMAP_BD_LENGTH), 0);
		sw16(sb(false,i,SMAP_BD_POINTER), 0);
		sw16(sb(true,i,SMAP_BD_RESERVED), 0);
		sw16(sb(true,i,SMAP_BD_LENGTH), 0);
		sw16(sb(true,i,SMAP_BD_POINTER), 0);
		mb(); sw16(sb(true,i,SMAP_BD_CTRL), SMAP_RX_EMPTY);
	}
	smap_configure(p);
	se_write(EMAC_IFG, 4);
	se_write(EMAC_TXMODE1, (7u<<27) | (15u<<16));
	se_write(EMAC_TX_THRESHOLD, 12u<<27);
	se_write(EMAC_RX_WATERMARK, (16u<<23) | (128u<<7));
	se_write(EMAC_IRQ_STATUS, se_read(EMAC_IRQ_STATUS));
	smap_initialized = true;
	return 0;
}
static void smap_reclaim(void)
{
	while (smap_ring.used) {
		u16 status = sr16(sb(false,smap_ring.consumer,SMAP_BD_CTRL));
		unsigned int len;
		if (status & SMAP_TX_READY) break;
		len = smap_tx_reclaim(&smap_ring);
		if (status & SMAP_TX_ERRORS) smap_tx_errors++;
		else { smap_tx_packets++; smap_tx_bytes += len; }
	}
}
static int smap_send(const struct smap_rpc_packet *p)
{
	unsigned int i, index = smap_ring.producer;
	if (!p->length) return 0;
	if (p->length<14 || p->length>SMAP_MAX_FRAME) return -EINVAL;
	if (!smap_tx_room(&smap_ring,p->length)) return -EAGAIN;
	sw16(SMAP_TX_PTR, smap_ring.offset);
	/* Never read outside the wire buffer, and never expose DMA padding. */
	for (i=0; i<smap_round(p->length); i+=4) {
		u32 v=0; unsigned int j;
		for (j=0; j<4 && i+j<p->length; j++) v |= (u32)p->data[i+j]<<(j*8);
		sw32(SMAP_TX_DATA,v);
	}
	sw16(sb(false,index,SMAP_BD_LENGTH),p->length);
	sw16(sb(false,index,SMAP_BD_POINTER),SMAP_TX_BASE+smap_ring.offset);
	sw8(SMAP_TX_INC,1);
	mb(); sw16(sb(false,index,SMAP_BD_CTRL),SMAP_TX_READY|SMAP_TX_FCS_PAD);
	smap_tx_commit(&smap_ring,p->length);
	se_write(EMAC_TXMODE0,EMAC_GET_PACKET);
	return p->length;
}
static void smap_receive(void)
{
	unsigned int tries;
	for (tries=0; tries<SMAP_BD_COUNT; tries++) {
		unsigned int i=smap_rx_index, j, len, ptr;
		u16 status=sr16(sb(true,i,SMAP_BD_CTRL));
		if (status & SMAP_RX_EMPTY) return;
		len=sr16(sb(true,i,SMAP_BD_LENGTH)); ptr=sr16(sb(true,i,SMAP_BD_POINTER));
		if (smap_rx_valid(status,len,ptr)) {
			sw16(SMAP_RX_PTR,ptr-SMAP_RX_BASE);
			for (j=0; j<smap_round(len); j+=4) {
				u32 v=sr32(SMAP_RX_DATA); unsigned int k;
				for (k=0; k<4 && j+k<len; k++) smap_rpc_result.data[j+k]=v>>(8*k);
			}
			smap_rpc_result.length=len;
		} else smap_rx_errors++;
		sw8(SMAP_RX_DEC,1);
		sw16(sb(true,i,SMAP_BD_LENGTH),0); sw16(sb(true,i,SMAP_BD_POINTER),0);
		mb(); sw16(sb(true,i,SMAP_BD_CTRL),SMAP_RX_EMPTY);
		smap_rx_index=(i+1)&(SMAP_BD_COUNT-1);
		if (smap_rpc_result.length) return;
	}
}
static void *smap_service(int op, void *buffer, size_t size)
{
	struct smap_rpc_packet *p=buffer;
	int err=-EINVAL;
	memset(&smap_rpc_result,0,sizeof(smap_rpc_result));
	smap_rpc_result.abi=SMAP_RPC_ABI;
	if (size!=sizeof(*p) || p->abi!=SMAP_RPC_ABI || !p->cookie) goto out;
	smap_rpc_result.cookie=p->cookie;
	if (op==SMAP_RPC_INIT) {
		struct dev9_request_packet acquire={1,p->cookie};
		err=process_request(&acquire);
		if (!err) {
			memcpy(smap_rpc_result.mac,mac_address,6);
			smap_rpc_result.checksum=mac_checksum;
			smap_rpc_result.hardware=hardware;
			err=smap_reset(p);
			if (err) { struct dev9_request_packet release={2,p->cookie}; process_request(&release); }
		}
		goto out;
	}
	if (lease_cookie!=p->cookie || (!smap_initialized && op!=SMAP_RPC_RELEASE && op!=SMAP_RPC_RESET)) {
		err=-ENODEV; goto out;
	}
	switch (op) {
	case SMAP_RPC_RESET: err=smap_reset(p); break;
	case SMAP_RPC_START: smap_configure(p); se_write(EMAC_MODE0,EMAC_ENABLE); smap_active=true; err=0; break;
	case SMAP_RPC_STOP: se_write(EMAC_MODE0,0); smap_active=false; err=0; break;
	case SMAP_RPC_MDIO_READ: err=smap_station(p,false); break;
	case SMAP_RPC_MDIO_WRITE: err=smap_station(p,true); break;
	case SMAP_RPC_FILTER: smap_configure(p); err=0; break;
	case SMAP_RPC_POLL:
		if (!smap_active) { err=-SMAP_RPC_NETDOWN; break; }
		smap_reclaim(); err=smap_send(p); smap_receive();
		se_write(EMAC_IRQ_STATUS,se_read(EMAC_IRQ_STATUS)); sw16(SMAP_CLEAR,SMAP_IRQ_MASK);
		break;
	case SMAP_RPC_RELEASE: {
		struct dev9_request_packet release={2,p->cookie};
		se_write(EMAC_MODE0,0); smap_active=smap_initialized=false;
		err=process_request(&release); break;
	}
	}
out:
	smap_rpc_result.status=err;
	smap_rpc_result.tx_packets=smap_tx_packets; smap_rpc_result.tx_bytes=smap_tx_bytes;
	smap_rpc_result.tx_errors=smap_tx_errors; smap_rpc_result.rx_errors=smap_rx_errors;
	return &smap_rpc_result;
}
static void smap_server_thread(void *arg)
{
	(void)arg;
	sifcmd_rpc_loop(&smap_rpc_queue);
}
static int smap_server_start(void)
{
	const struct iop_thread th={.attr=THREAD_ATTR_C,.thread=smap_server_thread,.stacksize=4096,.priority=0x2a};
	int id=thbase_create(&th), err;
	if (id<0) return id;
	sifcmd_set_rpc_queue(&smap_rpc_queue,id);
	sifcmd_register_rpc(&smap_rpc_server,SMAP_RPC_SERVER,smap_service,&smap_rpc_buffer,NULL,NULL,&smap_rpc_queue);
	err=thbase_start(id,NULL);
	if (err<0) {
		sifcmd_remove_rpc(&smap_rpc_server,&smap_rpc_queue);
		sifcmd_remove_rpc_queue(&smap_rpc_queue); thbase_delete(id);
	}
	return err;
}
