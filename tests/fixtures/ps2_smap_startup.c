/* SPDX-License-Identifier: GPL-2.0-only */
/* Cold station-management/FIFO model for the actual SMAP startup functions. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include "ps2-smap-core.h"
typedef uint32_t u32;
#define BIT(n) (1u << (n))
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#include "ps2-smap-regs.h"
struct net_device { u8 dev_addr[6]; };
struct smap {
    struct net_device *ndev; int mdio_lock, lock; bool mdio_pending;
    struct smap_tx_ring tx; unsigned int rx_index;
    u32 mode1, rx_mode, hash[4];
};
struct mii_bus { void *priv; };
static unsigned int held, irq_held, commands, polls, second_reads, error_logs;
static unsigned int fifo_started, fifo_complete, descriptors;
static int fault;
static u32 station, mac_reset;
static bool command_active;
#define mutex_lock(p) do { (void)(p); assert(!held); held++; } while (0)
#define mutex_unlock(p) do { (void)(p); assert(held); held--; } while (0)
#define spin_lock_irqsave(p, flags) do { (void)(p); (flags)=0; irq_held++; } while (0)
#define spin_unlock_irqrestore(p, flags) do { (void)(p); (void)(flags); irq_held--; } while (0)
#define netdev_err(...) do { error_logs++; } while (0)
#define wmb() do {} while (0)
#define read_poll_timeout(op, value, cond, sleep, timeout, before, ...) \
    ({ int result = -ETIMEDOUT; (void)(sleep); (void)(timeout); (void)(before); \
       for (unsigned int n=0; n<200; n++) { (value)=op(__VA_ARGS__); \
           if (cond) { result=0; break; } } result; })
static u32 emac_read(unsigned int reg) {
    assert(held);
    if (reg==EMAC_MDIO) {
        polls++;
        if (!command_active) return station; /* cold hardware starts at zero */
        if (fault==1) return 0; /* submitted operation remains active */
        if (second_reads++==0) return EMAC_MDIO_DONE;
        return EMAC_MDIO_DONE | (fault==2 ? EMAC_MDIO_ERROR : 0x2000u<<16);
    }
    if (reg==EMAC_MODE0) {
        if (fifo_complete==3 && fault!=5) mac_reset=0;
        return mac_reset;
    }
    assert(reg==EMAC_IRQ_STATUS); return 0;
}
static void emac_write(unsigned int reg, u32 value) {
    assert(held);
    if (reg==EMAC_MDIO) {
        assert(irq_held && (value&(EMAC_MDIO_READ|EMAC_MDIO_WRITE)));
        commands++; command_active=true; second_reads=0; station=0;
    } else if (reg==EMAC_MODE0) {
        assert(value==EMAC_RESET); mac_reset=value;
    }
}
static void smap_write8(unsigned int reg, u8 value) {
    assert(held);
    if (reg==SMAP_TX_CTRL || reg==SMAP_RX_CTRL) {
        assert(value==1); fifo_started |= reg==SMAP_TX_CTRL ? 1 : 2;
    } else assert(reg==SMAP_BD_MODE && !value);
}
static u8 smap_read8(unsigned int reg) {
    assert(held && (reg==SMAP_TX_CTRL || reg==SMAP_RX_CTRL));
    unsigned int bit=reg==SMAP_TX_CTRL ? 1 : 2;
    assert(fifo_started&bit);
    if ((fault==3 && bit==1) || (fault==4 && bit==2)) return 1;
    fifo_complete|=bit; return 0;
}
static void smap_write16(unsigned int reg, u16 value) {
    (void)value; assert(held && (reg==SMAP_CLEAR || reg>=SMAP_BD_TX));
    if (reg>=SMAP_BD_TX) descriptors++;
}
/* DRIVER_FUNCTIONS */
static void clear(struct smap *s) {
    held=irq_held=commands=polls=second_reads=error_logs=0;
    fifo_started=fifo_complete=descriptors=0; station=mac_reset=0;
    command_active=false; fault=0; s->mdio_pending=false;
}
int main(void) {
    struct net_device n={{0,4,31,2,10,9}};
    struct smap s={.ndev=&n}; struct mii_bus bus={.priv=&s};
    clear(&s);
    assert(smap_mdio(&bus,1,2,-1)==0x2000 && commands==1);
    assert(!s.mdio_pending && second_reads>=2 && !held && !irq_held);
    assert(smap_mdio(&bus,1,0,0x8000)==0 && commands==2 && !s.mdio_pending);
    clear(&s); fault=1;
    assert(smap_mdio(&bus,1,2,-1)==-ETIMEDOUT && commands==1 && s.mdio_pending);
    assert(smap_mdio(&bus,1,3,-1)==-ETIMEDOUT && commands==1 && s.mdio_pending);
    fault=0; /* the old transaction completes; only now may a new one start */
    assert(smap_mdio(&bus,1,3,-1)==0x2000 && commands==2 && !s.mdio_pending);
    clear(&s); fault=2;
    assert(smap_mdio(&bus,0,2,-1)==-EIO && !s.mdio_pending);
    fault=0; assert(smap_mdio(&bus,1,2,-1)==0x2000 && commands==2);
    clear(&s);
    assert(smap_mdio(&bus,32,2,-1)==-EINVAL && !commands && !held);
    assert(smap_mdio(&bus,1,-1,-1)==-EINVAL && !commands && !held);
    s.mdio_pending=true;
    assert(smap_hw_reset(&s)==0 && fifo_complete==3 && descriptors==512);
    assert(!s.mdio_pending && !held && !error_logs);
    for (int failure=3; failure<=5; failure++) {
        clear(&s); fault=failure; s.mdio_pending=true;
        assert(smap_hw_reset(&s)==-ETIMEDOUT && s.mdio_pending);
        assert(!held && !descriptors && error_logs==1);
    }
    puts("actual SMAP startup: cold MDIO, timeout ownership, PHY errors and FIFO-first reset passed");
    return 0;
}
