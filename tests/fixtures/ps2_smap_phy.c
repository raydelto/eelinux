/* Exercise the real handoff helper with an independent PHY transaction model. */
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
typedef uint16_t u16;
#define MII_BMCR 0
#define BMCR_LOOPBACK 0x4000
#define BMCR_ISOLATE 0x0400
#define BMCR_ANENABLE 0x1000

enum fault { NONE, INITIAL_READ, RESET, CLEAR, FINAL_READ, RETAIN_FLAGS };
struct phy_device {
    u16 control;
    enum fault fault;
    int reads, resets, clears, logs;
    unsigned long advertising;
};

static int phy_read(struct phy_device *phy, int reg)
{
    assert(reg == MII_BMCR);
    phy->reads++;
    if (phy->fault == INITIAL_READ ||
        (phy->fault == FINAL_READ && phy->reads == 2))
        return -ENXIO;
    return phy->control;
}

static int genphy_soft_reset(struct phy_device *phy)
{
    phy->resets++;
    if (phy->fault == RESET)
        return -ETIMEDOUT;
    /* A completed Clause 22 reset reloads hardware defaults. Deliberately
     * retain an isolate strap so the subsequent cleanup must handle it. */
    phy->control = BMCR_ANENABLE | BMCR_ISOLATE;
    return 0;
}

static int phy_clear_bits(struct phy_device *phy, int reg, u16 mask)
{
    assert(reg == MII_BMCR);
    assert(mask == (BMCR_LOOPBACK | BMCR_ISOLATE));
    phy->clears++;
    if (phy->fault == CLEAR)
        return -EIO;
    if (phy->fault != RETAIN_FLAGS)
        phy->control &= ~mask;
    return 0;
}
#define phydev_info(phy, ...) ((void)((phy)->logs++))
#include "ps2-smap-phy.h"

static void check_healthy(u16 control)
{
    struct phy_device phy = { .control = control, .advertising = 0xa55a };
    assert(smap_phy_prepare(&phy) == 0);
    assert(phy.control == control && phy.advertising == 0xa55a);
    assert(phy.reads == 1 && !phy.resets && !phy.clears && !phy.logs);
}

static void check_stale(u16 control)
{
    struct phy_device phy = { .control = control, .advertising = 0xa55a };
    assert(smap_phy_prepare(&phy) == 0);
    assert(phy.control == BMCR_ANENABLE && phy.advertising == 0xa55a);
    assert(phy.reads == 2 && phy.resets == 1 && phy.clears == 1 && phy.logs == 1);
}

int main(void)
{
    struct phy_device phy;
    const enum fault faults[] = {INITIAL_READ, RESET, CLEAR, FINAL_READ, RETAIN_FLAGS};
    const int errors[] = {-ENXIO, -ETIMEDOUT, -EIO, -ENXIO, -EIO};
    unsigned int i;
    /* Working USB auto-negotiated and deliberately forced healthy modes. */
    check_healthy(0x3100); check_healthy(0x2100); check_healthy(0x0000);
    check_stale(0x7100); /* Exact physical DVD BMCR from the user. */
    check_stale(0x7500); check_stale(0x0400);
    for (i = 0; i < sizeof(faults) / sizeof(faults[0]); i++) {
        phy = (struct phy_device){.control = 0x7100, .fault = faults[i]};
        assert(smap_phy_prepare(&phy) == errors[i]);
        if (faults[i] == INITIAL_READ)
            assert(!phy.resets && !phy.clears);
        if (faults[i] == RESET)
            assert(phy.resets == 1 && !phy.clears && phy.reads == 1);
        if (faults[i] == CLEAR)
            assert(phy.reads == 1);
    }
    puts("PHY handoff: 3 healthy controls, 3 stale states, 5 failure paths passed");
    return 0;
}
