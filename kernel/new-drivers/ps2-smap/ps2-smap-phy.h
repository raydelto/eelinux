/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef PS2_SMAP_PHY_H
#define PS2_SMAP_PHY_H

#include <linux/errno.h>
#include <linux/phy.h>

/* Some ROM/IOP loaders leave the external PHY in their self-test loopback
 * mode. Resetting the EMAC does not reset that PHY, and generic phylib does
 * not supply a soft_reset callback. genphy_config_aneg also retains LOOPBACK.
 *
 * Run before phy_connect_direct(): after this bounded Clause 22 reset,
 * phylib still performs the selected driver's fixups/config_init/resume.
 * phy_start later restores its supported advertisement and autonegotiation.
 * A PHY already in normal mode is only read, preserving the USB handoff.
 */
static inline int smap_phy_prepare(struct phy_device *phy)
{
	const u16 stale = BMCR_LOOPBACK | BMCR_ISOLATE;
	int control, err;

	control = phy_read(phy, MII_BMCR);
	if (control < 0)
		return control;
	if (!(control & stale))
		return 0;

	phydev_info(phy, "clearing loader PHY loopback/isolation (BMCR %#06x)\n",
		    control);
	err = genphy_soft_reset(phy);
	if (err)
		return err;

	/* Clear an isolate strap/default as well; never force speed or duplex. */
	err = phy_clear_bits(phy, MII_BMCR, stale);
	if (err)
		return err;
	control = phy_read(phy, MII_BMCR);
	if (control < 0)
		return control;
	return control & stale ? -EIO : 0;
}

#endif
