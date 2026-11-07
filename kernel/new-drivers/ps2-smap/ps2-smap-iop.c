// SPDX-License-Identifier: GPL-2.0-only
/* Native SMAP via IOP. Required on Slim, usable with Fat network adapters.
 * Only process-context work calls SIF RPC; ndo_start_xmit queues bounded SKBs.
 * One dedicated send page and the SIF receive page own every DMA cache line.
 */
#include <linux/delay.h>
#include <linux/etherdevice.h>
#include <linux/ethtool.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/netdevice.h>
#include <linux/phy.h>
#include <linux/platform_device.h>
#include <linux/random.h>
#include <linux/workqueue.h>
#include <asm/mach-ps2/iop-module.h>
#include <asm/mach-ps2/sif.h>
#include "ps2-smap-core.h"
#include "ps2-smap-regs.h"
#include "ps2-smap-rpc.h"
#include "ps2-smap-iop.h"
#include "ps2-smap-phy.h"

struct smap_iop {
	struct net_device *ndev;
	struct sif_rpc_client rpc;
	struct mii_bus *mii;
	struct phy_device *phy;
	struct mutex rpc_lock;
	spinlock_t lock;
	struct delayed_work work;
	struct sk_buff_head pending;
	struct smap_rpc_packet *request;
	struct smap_rpc_packet reply;
	struct rtnl_link_stats64 stats;
	u32 cookie, mode1, rx_mode, hash[4];
	bool running, configure, reset, poisoned;
};

/* Caller holds rpc_lock. A timed-out DMA owns its buffers until reboot.
 * Keep this object and both DMA pages quarantined on timeout; a late RPC
 * completion must never reference a freed client or overwrite reused data.
 */
static int smap_iop_call(struct smap_iop *s, int op, struct sk_buff *skb,
			 int phy, int reg, u32 value)
{
	struct smap_rpc_packet *p=s->request;
	unsigned long flags;
	int err;
	if (s->poisoned) return -EIO;
	memset(p,0,sizeof(*p)); p->abi=SMAP_RPC_ABI; p->cookie=s->cookie;
	p->phy=phy; p->reg=reg; p->value=value;
	spin_lock_irqsave(&s->lock,flags);
	p->mode1=s->mode1; p->rx_mode=s->rx_mode;
	memcpy(p->hash,s->hash,sizeof(p->hash)); memcpy(p->mac,s->ndev->dev_addr,6);
	spin_unlock_irqrestore(&s->lock,flags);
	if (skb) {
		if (skb->len<ETH_HLEN || skb->len>SMAP_MAX_FRAME) return -EINVAL;
		p->length=skb->len;
		if (skb_copy_bits(skb,0,p->data,skb->len)) return -EINVAL;
	}
	err=sif_rpc_timeout(&s->rpc,op,p,sizeof(*p),&s->reply,sizeof(s->reply),2*HZ);
	if (err == -ETIMEDOUT) {
		s->poisoned=true;
		netdev_err(s->ndev,"IOP transport timed out; DMA buffers retained until reboot\n");
	}
	if (err) return err;
	if (s->reply.abi!=SMAP_RPC_ABI || s->reply.cookie!=s->cookie ||
	    s->reply.length>SMAP_MAX_FRAME ||
	    (s->reply.length && s->reply.length<ETH_HLEN)) return -EPROTO;
	spin_lock_irqsave(&s->lock,flags);
	s->stats.tx_packets=s->reply.tx_packets; s->stats.tx_bytes=s->reply.tx_bytes;
	s->stats.tx_errors=s->reply.tx_errors; s->stats.rx_errors=s->reply.rx_errors;
	spin_unlock_irqrestore(&s->lock,flags);
	return s->reply.status;
}
static int smap_iop_mdio_read(struct mii_bus *bus, int phy, int reg)
{
	struct smap_iop *s=bus->priv; int err;
	mutex_lock(&s->rpc_lock);
	err=smap_iop_call(s,SMAP_RPC_MDIO_READ,NULL,phy,reg,0);
	if (!err) err=s->reply.value & 0xffff;
	mutex_unlock(&s->rpc_lock); return err;
}
static int smap_iop_mdio_write(struct mii_bus *bus,int phy,int reg,u16 value)
{
	struct smap_iop *s=bus->priv; int err;
	mutex_lock(&s->rpc_lock);
	err=smap_iop_call(s,SMAP_RPC_MDIO_WRITE,NULL,phy,reg,value);
	mutex_unlock(&s->rpc_lock); return err;
}
static void smap_iop_adjust(struct net_device *ndev)
{
	struct smap_iop *s=netdev_priv(ndev); unsigned long flags;
	u32 mode=EMAC_MODE1_DEFAULT;
	if (s->phy->speed==SPEED_100) mode|=EMAC_SPEED100;
	if (s->phy->duplex==DUPLEX_FULL) mode|=EMAC_DUPLEX;
	else if (s->phy->speed==SPEED_10) mode &= ~EMAC_IGNORE_SQE;
	spin_lock_irqsave(&s->lock,flags);
	s->mode1=mode; s->configure=true;
	spin_unlock_irqrestore(&s->lock,flags);
	phy_print_status(s->phy);
}
static void smap_iop_work(struct work_struct *work)
{
	struct smap_iop *s=container_of(to_delayed_work(work),struct smap_iop,work);
	unsigned int budget; unsigned long flags; int err=0; bool config,reset;
	if (!READ_ONCE(s->running)) return;
	mutex_lock(&s->rpc_lock);
	spin_lock_irqsave(&s->lock,flags);
	config=s->configure; reset=s->reset; s->configure=s->reset=false;
	spin_unlock_irqrestore(&s->lock,flags);
	if (reset) {
		skb_queue_purge(&s->pending);
		err=smap_iop_call(s,SMAP_RPC_RESET,NULL,0,0,0);
		if (!err) err=smap_iop_call(s,SMAP_RPC_START,NULL,0,0,0);
		if (!err) netif_wake_queue(s->ndev);
	} else if (config) err=smap_iop_call(s,SMAP_RPC_FILTER,NULL,0,0,0);
	for (budget=0; !err && budget<8; budget++) {
		struct sk_buff *tx=skb_peek(&s->pending), *rx;
		int result=smap_iop_call(s,SMAP_RPC_POLL,tx,0,0,0);
		if (result<0 && result!=-EAGAIN) { err=result; break; }
		if (tx && result==(int)tx->len) {
			WARN_ON(skb_dequeue(&s->pending)!=tx); dev_kfree_skb_any(tx);
			netif_trans_update(s->ndev);
			if (skb_queue_len(&s->pending)<16) netif_wake_queue(s->ndev);
		} else if (result>0) { err=-EPROTO; break; }
		if (s->reply.length) {
			rx=netdev_alloc_skb_ip_align(s->ndev,s->reply.length);
			if (rx) {
				skb_put_data(rx,s->reply.data,s->reply.length);
				rx->protocol=eth_type_trans(rx,s->ndev);
				spin_lock_irqsave(&s->lock,flags);
				s->stats.rx_packets++; s->stats.rx_bytes+=s->reply.length;
				spin_unlock_irqrestore(&s->lock,flags);
				netif_rx(rx);
			} else {
				spin_lock_irqsave(&s->lock,flags); s->stats.rx_dropped++;
				spin_unlock_irqrestore(&s->lock,flags);
			}
		}
		if ((!tx && !s->reply.length) || result==-EAGAIN) break;
	}
	mutex_unlock(&s->rpc_lock);
	if (err) {
		netif_stop_queue(s->ndev); netif_carrier_off(s->ndev);
		netdev_err(s->ndev,"IOP packet service failed: %d\n",err);
		return;
	}
	if (READ_ONCE(s->running))
		queue_delayed_work(system_dfl_wq,&s->work,budget==8 ? 0 : max_t(unsigned long,1,HZ/100));
}
static netdev_tx_t smap_iop_xmit(struct sk_buff *skb,struct net_device *ndev)
{
	struct smap_iop *s=netdev_priv(ndev); unsigned long flags;
	if (unlikely(skb->len<ETH_HLEN || skb->len>SMAP_MAX_FRAME)) {
		spin_lock_irqsave(&s->lock,flags); s->stats.tx_dropped++;
		spin_unlock_irqrestore(&s->lock,flags); dev_kfree_skb_any(skb); return NETDEV_TX_OK;
	}
	if (skb_queue_len(&s->pending)>=32) { netif_stop_queue(ndev); return NETDEV_TX_BUSY; }
	skb_queue_tail(&s->pending,skb);
	if (skb_queue_len(&s->pending)>=32) netif_stop_queue(ndev);
	/* ndo_start_xmit is atomic: it never waits for the IOP. */
	mod_delayed_work(system_dfl_wq,&s->work,0);
	return NETDEV_TX_OK;
}
static int smap_iop_open(struct net_device *ndev)
{
	struct smap_iop *s=netdev_priv(ndev); int err;
	mutex_lock(&s->rpc_lock);
	err=smap_iop_call(s,SMAP_RPC_RESET,NULL,0,0,0);
	if (!err) err=smap_iop_call(s,SMAP_RPC_START,NULL,0,0,0);
	mutex_unlock(&s->rpc_lock); if (err) return err;
	WRITE_ONCE(s->running,true); phy_start(s->phy); netif_start_queue(ndev);
	queue_delayed_work(system_dfl_wq,&s->work,0); return 0;
}
static int smap_iop_stop(struct net_device *ndev)
{
	struct smap_iop *s=netdev_priv(ndev);
	netif_stop_queue(ndev); WRITE_ONCE(s->running,false);
	cancel_delayed_work_sync(&s->work); phy_stop(s->phy);
	mutex_lock(&s->rpc_lock); smap_iop_call(s,SMAP_RPC_STOP,NULL,0,0,0);
	mutex_unlock(&s->rpc_lock); skb_queue_purge(&s->pending); return 0;
}
static void smap_iop_timeout(struct net_device *ndev,unsigned int queue)
{
	struct smap_iop *s=netdev_priv(ndev); unsigned long flags;
	(void)queue;
	spin_lock_irqsave(&s->lock,flags); s->reset=true; spin_unlock_irqrestore(&s->lock,flags);
	mod_delayed_work(system_dfl_wq,&s->work,0);
}
static void smap_iop_filter(struct net_device *ndev)
{
	struct smap_iop *s=netdev_priv(ndev); struct netdev_hw_addr *ha;
	u32 hash[4]={0},mode=EMAC_RX_DEFAULT; unsigned long flags;
	if (ndev->flags & IFF_PROMISC) mode|=EMAC_RX_PROMISC;
	if (ndev->flags & IFF_ALLMULTI) mode|=EMAC_RX_ALLMULTI;
	else if (!netdev_mc_empty(ndev)) {
		mode|=EMAC_RX_MULTICAST;
		netdev_for_each_mc_addr(ha,ndev) {
			u32 bit=ether_crc(ETH_ALEN,ha->addr)>>26; hash[bit>>4]|=0x8000u>>(bit&15);
		}
	}
	spin_lock_irqsave(&s->lock,flags); s->rx_mode=mode; memcpy(s->hash,hash,sizeof(hash));
	s->configure=true; spin_unlock_irqrestore(&s->lock,flags);
}
static int smap_iop_mac(struct net_device *ndev,void *addr)
{
	int err=eth_mac_addr(ndev,addr); if (!err) smap_iop_filter(ndev); return err;
}
static void smap_iop_stats(struct net_device *ndev,struct rtnl_link_stats64 *stats)
{
	struct smap_iop *s=netdev_priv(ndev); unsigned long flags;
	spin_lock_irqsave(&s->lock,flags); *stats=s->stats; spin_unlock_irqrestore(&s->lock,flags);
}
static const struct net_device_ops smap_iop_ops={
	.ndo_open=smap_iop_open,.ndo_stop=smap_iop_stop,.ndo_start_xmit=smap_iop_xmit,
	.ndo_tx_timeout=smap_iop_timeout,.ndo_set_rx_mode=smap_iop_filter,
	.ndo_set_mac_address=smap_iop_mac,.ndo_validate_addr=eth_validate_addr,
	.ndo_get_stats64=smap_iop_stats,.ndo_eth_ioctl=phy_do_ioctl_running,
};
static void smap_iop_info(struct net_device *ndev,struct ethtool_drvinfo *info)
{
	(void)ndev; strscpy(info->driver,"ps2-smap",sizeof(info->driver));
	strscpy(info->bus_info,"IOP SMAP / SIF",sizeof(info->bus_info));
}
static const struct ethtool_ops smap_iop_ethtool={
	.get_drvinfo=smap_iop_info,.get_link=ethtool_op_get_link,
	.get_link_ksettings=phy_ethtool_get_link_ksettings,
	.set_link_ksettings=phy_ethtool_set_link_ksettings,.nway_reset=phy_ethtool_nway_reset,
};
int smap_iop_probe(struct platform_device *pdev)
{
	struct net_device *ndev; struct smap_iop *s; int err; u32 hardware;
	BUILD_BUG_ON(sizeof(struct smap_rpc_packet)!=1616);
	err=iop_module_request("dev9",0x0102,NULL);
	if (err<0) { dev_err(&pdev->dev,"IOP SMAP firmware unavailable: %d\n",err); return err; }
	ndev=alloc_etherdev(sizeof(*s)); if (!ndev) return -ENOMEM;
	SET_NETDEV_DEV(ndev,&pdev->dev); s=netdev_priv(ndev); s->ndev=ndev;
	mutex_init(&s->rpc_lock); spin_lock_init(&s->lock); skb_queue_head_init(&s->pending);
	INIT_DELAYED_WORK(&s->work,smap_iop_work);
	s->mode1=EMAC_MODE1_DEFAULT|EMAC_SPEED100; s->rx_mode=EMAC_RX_DEFAULT;
	s->cookie=get_random_u32() ?: 1;
	s->request=(void *)__get_free_page(GFP_KERNEL|GFP_DMA);
	if (!s->request) { err=-ENOMEM; goto fail; }
	err=sif_rpc_bind(&s->rpc,SMAP_RPC_SERVER);
	if (err) {
		if (err==-ENXIO) sif_rpc_unbind(&s->rpc);
		goto fail_send;
	}
	mutex_lock(&s->rpc_lock); err=smap_iop_call(s,SMAP_RPC_INIT,NULL,0,0,0);
	mutex_unlock(&s->rpc_lock); if (err) goto fail_rpc;
	if (!is_valid_ether_addr(s->reply.mac) || !smap_mac_checksum(s->reply.mac,s->reply.checksum)) {
		err=-EADDRNOTAVAIL; goto fail_lease;
	}
	hardware=s->reply.hardware; eth_hw_addr_set(ndev,s->reply.mac);
	s->mii=mdiobus_alloc(); if (!s->mii) { err=-ENOMEM; goto fail_lease; }
	s->mii->name="PlayStation 2 IOP SMAP MDIO"; s->mii->priv=s;
	s->mii->read=smap_iop_mdio_read; s->mii->write=smap_iop_mdio_write;
	s->mii->parent=&pdev->dev; snprintf(s->mii->id,MII_BUS_ID_SIZE,"ps2-smap-iop-%d",pdev->id);
	err=mdiobus_register(s->mii); if (err) goto fail_mii;
	s->phy=phy_find_first(s->mii); if (!s->phy) { err=-ENODEV; goto fail_bus; }
	s->phy->irq=PHY_POLL;
	err=smap_phy_prepare(s->phy); if (err) goto fail_bus;
	err=phy_connect_direct(ndev,s->phy,smap_iop_adjust,PHY_INTERFACE_MODE_MII); if (err) goto fail_bus;
	phy_set_max_speed(s->phy,SPEED_100);
	ndev->netdev_ops=&smap_iop_ops; ndev->ethtool_ops=&smap_iop_ethtool;
	ndev->min_mtu=68; ndev->max_mtu=1500; ndev->tx_queue_len=32; ndev->watchdog_timeo=5*HZ;
	netif_carrier_off(ndev); err=register_netdev(ndev); if (err) goto fail_phy;
	platform_set_drvdata(pdev,ndev);
	netdev_info(ndev,"native SMAP %pM, DEV9 variant %02x, PHY %s, IOP/SIF transport\n",
		ndev->dev_addr,hardware,phydev_name(s->phy)); return 0;
fail_phy: phy_disconnect(s->phy);
fail_bus: mdiobus_unregister(s->mii);
fail_mii: mdiobus_free(s->mii);
fail_lease:
	mutex_lock(&s->rpc_lock); smap_iop_call(s,SMAP_RPC_RELEASE,NULL,0,0,0); mutex_unlock(&s->rpc_lock);
fail_rpc:
	if (s->poisoned) return err; /* DMA quarantine; built-in lifetime ends at reboot. */
	sif_rpc_unbind(&s->rpc);
fail_send: free_page((unsigned long)s->request);
fail: free_netdev(ndev); dev_err(&pdev->dev,"IOP SMAP probe failed: %d\n",err); return err;
}
void smap_iop_remove(struct platform_device *pdev)
{
	struct net_device *ndev=platform_get_drvdata(pdev); struct smap_iop *s=netdev_priv(ndev);
	unregister_netdev(ndev); cancel_delayed_work_sync(&s->work);
	phy_disconnect(s->phy); mdiobus_unregister(s->mii); mdiobus_free(s->mii);
	mutex_lock(&s->rpc_lock); smap_iop_call(s,SMAP_RPC_RELEASE,NULL,0,0,0); mutex_unlock(&s->rpc_lock);
	if (s->poisoned) return;
	sif_rpc_unbind(&s->rpc); free_page((unsigned long)s->request); free_netdev(ndev);
}
