// SPDX-License-Identifier: GPL-2.0
// Broadcom BCM84881 NBASE-T PHY driver, as found on a SFP+ module.
// Copyright (C) 2019 Russell King, Deep Blue Solutions Ltd.
//
// Like the Marvell 88x3310, the Broadcom 84881 changes its host-side
// interface according to the operating speed between 10GBASE-R,
// 2500BASE-X and SGMII (but unlike the 88x3310, without the control
// word).
//
// This driver only supports those aspects of the PHY that I'm able to
// observe and test with the SFP+ module, which is an incomplete subset
// of what this PHY is able to support. For example, I only assume it
// supports a single lane Serdes connection, but it may be that the PHY
// is able to support more than that.
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/phy.h>

enum {
	MDIO_AN_C22 = 0xffe0,
};

/* BCM8489x LED controller (BCM84891L datasheet 2.4.1.58). Each pin has
 * CTL bits in 0xA83B (stride 3: 2-bit CTL + 1-bit OE_N) plus MASK_LOW/
 * MASK_EXT source selects. LED4 is firmware-controlled; always RMW.
 */
#define BCM8489X_LED_CTL		0xa83b
#define BCM8489X_LED_CTL_ON(i)		(0x2 << ((i) * 3))
#define BCM8489X_LED_CTL_MASK(i)	(0x3 << ((i) * 3))

#define BCM8489X_LED_SRC_RX		BIT(1)
#define BCM8489X_LED_SRC_TX		BIT(2)
#define BCM8489X_LED_SRC_1000		BIT(3)	/* high only at 1000 */
#define BCM8489X_LED_SRC_100_1000	BIT(4)	/* high at 100 and 1000 */
#define BCM8489X_LED_SRC_FORCE		BIT(5)	/* always-1 source */
#define BCM8489X_LED_SRC_10G		BIT(7)
#define BCM8489X_LED_SRCX_2500		BIT(2)
#define BCM8489X_LED_SRCX_5000		BIT(3)

#define BCM8489X_MAX_LEDS		2

static const struct {
	u16 mask_low;
	u16 mask_ext;
} bcm8489x_led_regs[BCM8489X_MAX_LEDS] = {
	{ 0xa82c, 0xa8ef },	/* LED1 */
	{ 0xa82f, 0xa8f0 },	/* LED2 */
};

static int bcm84881_wait_init(struct phy_device *phydev)
{
	int val;

	return phy_read_mmd_poll_timeout(phydev, MDIO_MMD_PMAPMD, MDIO_CTRL1,
					 val, !(val & MDIO_CTRL1_RESET),
					 100000, 2000000, false);
}

static void bcm84881_fill_possible_interfaces(struct phy_device *phydev)
{
	unsigned long *possible = phydev->possible_interfaces;

	__set_bit(PHY_INTERFACE_MODE_SGMII, possible);
	__set_bit(PHY_INTERFACE_MODE_2500BASEX, possible);
	__set_bit(PHY_INTERFACE_MODE_10GBASER, possible);
}

static int bcm84881_config_init(struct phy_device *phydev)
{
	bcm84881_fill_possible_interfaces(phydev);

	switch (phydev->interface) {
	case PHY_INTERFACE_MODE_SGMII:
	case PHY_INTERFACE_MODE_2500BASEX:
	case PHY_INTERFACE_MODE_10GBASER:
		break;
	default:
		return -ENODEV;
	}

	return 0;
}

static int bcm8489x_config_init(struct phy_device *phydev)
{
	bcm84881_fill_possible_interfaces(phydev);
	__set_bit(PHY_INTERFACE_MODE_USXGMII, phydev->possible_interfaces);

	/* The BCM84891 is found both soldered down and attached over
	 * USXGMII, and inside SFP+ copper modules (e.g. various
	 * "OEM SFP-10G-T" RollBall modules), where the host-side
	 * interface is 10GBASE-R with SGMII/2500BASE-X rate switching,
	 * as with the BCM84881. Accept both attachments.
	 */
	switch (phydev->interface) {
	case PHY_INTERFACE_MODE_SGMII:
	case PHY_INTERFACE_MODE_2500BASEX:
	case PHY_INTERFACE_MODE_10GBASER:
	case PHY_INTERFACE_MODE_USXGMII:
		break;
	default:
		return -ENODEV;
	}

	/* MDIO_CTRL1_LPOWER is set at boot on the tested platform. Does not
	 * recur on ifdown/ifup, cable events, or link-partner advertisement
	 * changes; clear it once.
	 */
	return phy_clear_bits_mmd(phydev, MDIO_MMD_PMAPMD, MDIO_CTRL1,
				  MDIO_CTRL1_LPOWER);
}

static int bcm8489x_led_write(struct phy_device *phydev, u8 index,
			      u16 low, u16 ext)
{
	int ret;

	ret = phy_write_mmd(phydev, MDIO_MMD_PMAPMD,
			    bcm8489x_led_regs[index].mask_low, low);
	if (ret)
		return ret;
	ret = phy_write_mmd(phydev, MDIO_MMD_PMAPMD,
			    bcm8489x_led_regs[index].mask_ext, ext);
	if (ret)
		return ret;
	return phy_modify_mmd(phydev, MDIO_MMD_PMAPMD, BCM8489X_LED_CTL,
			      BCM8489X_LED_CTL_MASK(index),
			      (low | ext) ? BCM8489X_LED_CTL_ON(index) : 0);
}

static int bcm8489x_led_brightness_set(struct phy_device *phydev,
				       u8 index, enum led_brightness value)
{
	if (index >= BCM8489X_MAX_LEDS)
		return -EINVAL;

	return bcm8489x_led_write(phydev, index,
				  value ? BCM8489X_LED_SRC_FORCE : 0, 0);
}

static const unsigned long bcm8489x_supported_triggers =
	BIT(TRIGGER_NETDEV_LINK) |
	BIT(TRIGGER_NETDEV_LINK_100) |
	BIT(TRIGGER_NETDEV_LINK_1000) |
	BIT(TRIGGER_NETDEV_LINK_2500) |
	BIT(TRIGGER_NETDEV_LINK_5000) |
	BIT(TRIGGER_NETDEV_LINK_10000) |
	BIT(TRIGGER_NETDEV_RX) |
	BIT(TRIGGER_NETDEV_TX);

static int bcm8489x_led_hw_is_supported(struct phy_device *phydev, u8 index,
					unsigned long rules)
{
	if (index >= BCM8489X_MAX_LEDS)
		return -EINVAL;

	if (rules & ~bcm8489x_supported_triggers)
		return -EOPNOTSUPP;

	/* Source bit 4 lights at both 100 and 1000; "100 only" isn't
	 * representable in hardware. Accept LINK_100 only alongside
	 * LINK_1000 or LINK so the offload is precise.
	 */
	if ((rules & BIT(TRIGGER_NETDEV_LINK_100)) &&
	    !(rules & (BIT(TRIGGER_NETDEV_LINK_1000) |
		       BIT(TRIGGER_NETDEV_LINK))))
		return -EOPNOTSUPP;

	return 0;
}

static int bcm8489x_led_hw_control_set(struct phy_device *phydev, u8 index,
				       unsigned long rules)
{
	u16 low = 0, ext = 0;

	if (index >= BCM8489X_MAX_LEDS)
		return -EINVAL;

	if (rules & (BIT(TRIGGER_NETDEV_LINK_100) | BIT(TRIGGER_NETDEV_LINK)))
		low |= BCM8489X_LED_SRC_100_1000;
	if (rules & (BIT(TRIGGER_NETDEV_LINK_1000) | BIT(TRIGGER_NETDEV_LINK)))
		low |= BCM8489X_LED_SRC_1000;
	if (rules & (BIT(TRIGGER_NETDEV_LINK_2500) | BIT(TRIGGER_NETDEV_LINK)))
		ext |= BCM8489X_LED_SRCX_2500;
	if (rules & (BIT(TRIGGER_NETDEV_LINK_5000) | BIT(TRIGGER_NETDEV_LINK)))
		ext |= BCM8489X_LED_SRCX_5000;
	if (rules & (BIT(TRIGGER_NETDEV_LINK_10000) | BIT(TRIGGER_NETDEV_LINK)))
		low |= BCM8489X_LED_SRC_10G;
	if (rules & BIT(TRIGGER_NETDEV_RX))
		low |= BCM8489X_LED_SRC_RX;
	if (rules & BIT(TRIGGER_NETDEV_TX))
		low |= BCM8489X_LED_SRC_TX;

	return bcm8489x_led_write(phydev, index, low, ext);
}

static int bcm8489x_led_hw_control_get(struct phy_device *phydev, u8 index,
				       unsigned long *rules)
{
	int low, ext;

	if (index >= BCM8489X_MAX_LEDS)
		return -EINVAL;

	low = phy_read_mmd(phydev, MDIO_MMD_PMAPMD,
			   bcm8489x_led_regs[index].mask_low);
	if (low < 0)
		return low;
	ext = phy_read_mmd(phydev, MDIO_MMD_PMAPMD,
			   bcm8489x_led_regs[index].mask_ext);
	if (ext < 0)
		return ext;

	*rules = 0;
	if (low & BCM8489X_LED_SRC_100_1000)
		*rules |= BIT(TRIGGER_NETDEV_LINK_100);
	if (low & BCM8489X_LED_SRC_1000)
		*rules |= BIT(TRIGGER_NETDEV_LINK_1000);
	if (ext & BCM8489X_LED_SRCX_2500)
		*rules |= BIT(TRIGGER_NETDEV_LINK_2500);
	if (ext & BCM8489X_LED_SRCX_5000)
		*rules |= BIT(TRIGGER_NETDEV_LINK_5000);
	if (low & BCM8489X_LED_SRC_10G)
		*rules |= BIT(TRIGGER_NETDEV_LINK_10000);
	if (low & BCM8489X_LED_SRC_RX)
		*rules |= BIT(TRIGGER_NETDEV_RX);
	if (low & BCM8489X_LED_SRC_TX)
		*rules |= BIT(TRIGGER_NETDEV_TX);

	return 0;
}

static int bcm84881_probe(struct phy_device *phydev)
{
	/* This driver requires PMAPMD and AN blocks */
	const u32 mmd_mask = MDIO_DEVS_PMAPMD | MDIO_DEVS_AN;

	if (!phydev->is_c45 ||
	    (phydev->c45_ids.devices_in_package & mmd_mask) != mmd_mask)
		return -ENODEV;

	return 0;
}

static int bcm84881_get_features(struct phy_device *phydev)
{
	int ret;

	ret = genphy_c45_pma_read_abilities(phydev);
	if (ret)
		return ret;

	/* Although the PHY sets bit 1.11.8, it does not support 10M modes */
	linkmode_clear_bit(ETHTOOL_LINK_MODE_10baseT_Half_BIT,
			   phydev->supported);
	linkmode_clear_bit(ETHTOOL_LINK_MODE_10baseT_Full_BIT,
			   phydev->supported);

	return 0;
}

static const int bcm8489x_features[] = {
	ETHTOOL_LINK_MODE_Autoneg_BIT,
	ETHTOOL_LINK_MODE_100baseT_Half_BIT,
	ETHTOOL_LINK_MODE_100baseT_Full_BIT,
	ETHTOOL_LINK_MODE_1000baseT_Full_BIT,
	ETHTOOL_LINK_MODE_2500baseT_Full_BIT,
	ETHTOOL_LINK_MODE_5000baseT_Full_BIT,
	ETHTOOL_LINK_MODE_10000baseT_Full_BIT,
};

static int bcm8489x_get_features(struct phy_device *phydev)
{
	/* The PMA/PMD abilities of this family are fixed and known, and
	 * on some modules every MDIO access is expensive (RollBall
	 * MDIO-over-I2C mailbox, tens to hundreds of ms per register),
	 * so do not spend half a dozen reads at attach time discovering
	 * constants. EEE abilities may vary with firmware; keep reading
	 * those.
	 */
	linkmode_set_bit_array(bcm8489x_features,
			       ARRAY_SIZE(bcm8489x_features),
			       phydev->supported);

	return genphy_c45_read_eee_abilities(phydev);
}

static int bcm84881_config_aneg(struct phy_device *phydev)
{
	bool changed = false;
	u32 adv;
	int ret;

	/* Wait for the PHY to finish initialising, otherwise our
	 * advertisement may be overwritten.
	 */
	ret = bcm84881_wait_init(phydev);
	if (ret)
		return ret;

	/* We don't support manual MDI control */
	phydev->mdix_ctrl = ETH_TP_MDI_AUTO;

	/* disabled autoneg doesn't seem to work with this PHY */
	if (phydev->autoneg == AUTONEG_DISABLE)
		return -EINVAL;

	ret = genphy_c45_an_config_aneg(phydev);
	if (ret < 0)
		return ret;
	if (ret > 0)
		changed = true;

	adv = linkmode_adv_to_mii_ctrl1000_t(phydev->advertising);
	ret = phy_modify_mmd_changed(phydev, MDIO_MMD_AN,
				     MDIO_AN_C22 + MII_CTRL1000,
				     ADVERTISE_1000FULL | ADVERTISE_1000HALF,
				     adv);
	if (ret < 0)
		return ret;
	if (ret > 0)
		changed = true;

	return genphy_c45_check_and_restart_aneg(phydev, changed);
}

static int bcm84881_aneg_done(struct phy_device *phydev)
{
	int bmsr, val;

	val = phy_read_mmd(phydev, MDIO_MMD_AN, MDIO_STAT1);
	if (val < 0)
		return val;

	bmsr = phy_read_mmd(phydev, MDIO_MMD_AN, MDIO_AN_C22 + MII_BMSR);
	if (bmsr < 0)
		return bmsr;

	return !!(val & MDIO_AN_STAT1_COMPLETE) &&
	       !!(bmsr & BMSR_ANEGCOMPLETE);
}

static int bcm84881_read_status(struct phy_device *phydev)
{
	bool was_resolved;
	unsigned int mode;
	int bmsr, val, stat1;

	/* Whether the previous poll left a fully resolved link whose
	 * negotiated parameters (lp_advertising, speed, duplex, pause,
	 * mdix, interface) are still valid. They can only change through
	 * a renegotiation, which is observable below as a link drop, an
	 * autoneg restart or autoneg-complete deasserting.
	 */
	was_resolved = phydev->link && phydev->autoneg_complete &&
		       phydev->speed != SPEED_UNKNOWN;

	stat1 = -1;

	/* In polling mode with the link previously up and resolved, a
	 * single read of the AN status register is normally sufficient
	 * to confirm nothing changed: the link status bit is latched
	 * low, so a momentary drop or a renegotiation since the last
	 * poll reads as 0 even if the link has already come back, and
	 * autoneg-complete deasserts across a renegotiation. This
	 * mirrors the polling-mode single-read logic in
	 * genphy_c45_read_link().
	 */
	if (phy_polling_mode(phydev) && was_resolved) {
		stat1 = phy_read_mmd(phydev, MDIO_MMD_AN, MDIO_STAT1);
		if (stat1 < 0)
			return stat1;

		if ((stat1 & MDIO_STAT1_LSTATUS) &&
		    (stat1 & MDIO_AN_STAT1_COMPLETE))
			return 0;

		/* Something changed. The latched status has now been
		 * consumed, so the full evaluation below must reuse
		 * this value rather than re-read the register, which
		 * would miss a momentary link drop.
		 */
	}

	val = phy_read_mmd(phydev, MDIO_MMD_AN, MDIO_CTRL1);
	if (val < 0)
		return val;

	if (val & MDIO_AN_CTRL1_RESTART) {
		phydev->link = 0;
		return 0;
	}

	if (stat1 < 0) {
		stat1 = phy_read_mmd(phydev, MDIO_MMD_AN, MDIO_STAT1);
		if (stat1 < 0)
			return stat1;
	}

	bmsr = phy_read_mmd(phydev, MDIO_MMD_AN, MDIO_AN_C22 + MII_BMSR);
	if (bmsr < 0)
		return bmsr;

	phydev->autoneg_complete = !!(stat1 & MDIO_AN_STAT1_COMPLETE) &&
				   !!(bmsr & BMSR_ANEGCOMPLETE);
	phydev->link = !!(stat1 & MDIO_STAT1_LSTATUS) &&
		       !!(bmsr & BMSR_LSTATUS);
	if (phydev->autoneg == AUTONEG_ENABLE && !phydev->autoneg_complete)
		phydev->link = false;

	/* On some modules every MDIO access is expensive (RollBall
	 * MDIO-over-I2C mailbox, tens to hundreds of ms per register).
	 * If the link was already up and resolved on the previous poll
	 * and still is, the negotiated parameters cannot have changed:
	 * skip re-reading them and keep the cached values.
	 */
	if (was_resolved && phydev->link && phydev->autoneg_complete)
		return 0;

	linkmode_zero(phydev->lp_advertising);
	phydev->speed = SPEED_UNKNOWN;
	phydev->duplex = DUPLEX_UNKNOWN;
	phydev->pause = 0;
	phydev->asym_pause = 0;
	phydev->mdix = 0;

	if (!phydev->link)
		return 0;

	if (phydev->autoneg_complete) {
		val = genphy_c45_read_lpa(phydev);
		if (val < 0)
			return val;

		val = phy_read_mmd(phydev, MDIO_MMD_AN,
				   MDIO_AN_C22 + MII_STAT1000);
		if (val < 0)
			return val;

		mii_stat1000_mod_linkmode_lpa_t(phydev->lp_advertising, val);

		if (phydev->autoneg == AUTONEG_ENABLE)
			phy_resolve_aneg_linkmode(phydev);
	}

	if (phydev->autoneg == AUTONEG_DISABLE) {
		/* disabled autoneg doesn't seem to work, so force the link
		 * down.
		 */
		phydev->link = 0;
		return 0;
	}

	/* BCM84891/92 on USXGMII: the host interface mode doesn't change
	 * with copper speed (USXGMII symbol replication; the MAC receives
	 * the negotiated copper speed, not 10G, so no rate adaptation).
	 * Skip 0x4011; phy_resolve_aneg_linkmode() above already set the
	 * speed. Only bcm8489x_config_init() allows USXGMII.
	 */
	if (phydev->interface == PHY_INTERFACE_MODE_USXGMII)
		return genphy_c45_read_mdix(phydev);

	/* Set the host link mode - we set the phy interface mode and
	 * the speed according to this register so that downshift works.
	 * We leave the duplex setting as per the resolution from the
	 * above.
	 */
	val = phy_read_mmd(phydev, MDIO_MMD_VEND1, 0x4011);
	mode = (val & 0x1e) >> 1;
	if (mode == 1 || mode == 2)
		phydev->interface = PHY_INTERFACE_MODE_SGMII;
	else if (mode == 3)
		phydev->interface = PHY_INTERFACE_MODE_10GBASER;
	else if (mode == 4)
		phydev->interface = PHY_INTERFACE_MODE_2500BASEX;
	switch (mode & 7) {
	case 1:
		phydev->speed = SPEED_100;
		break;
	case 2:
		phydev->speed = SPEED_1000;
		break;
	case 3:
		phydev->speed = SPEED_10000;
		break;
	case 4:
		phydev->speed = SPEED_2500;
		break;
	case 5:
		phydev->speed = SPEED_5000;
		break;
	}

	return genphy_c45_read_mdix(phydev);
}

/* The Broadcom BCM84881 in the Methode DM7052 is unable to provide a SGMII
 * or 802.3z control word, so inband will not work.
 */
static unsigned int bcm84881_inband_caps(struct phy_device *phydev,
					 phy_interface_t interface)
{
	return LINK_INBAND_DISABLE;
}

static int bcm84881_config_inband(struct phy_device *phydev,
				  unsigned int modes)
{
	/* This PHY does not generate inband signalling in any mode (see
	 * bcm84881_inband_caps()); inband is permanently disabled in
	 * hardware. A request to disable inband therefore requires no
	 * action, but must succeed: phylink treats any error from
	 * phy_config_inband() (including -EOPNOTSUPP from a missing
	 * config_inband method) as a major configuration failure which
	 * forces and holds the link down.
	 */
	if (modes == LINK_INBAND_DISABLE)
		return 0;

	return -EINVAL;
}

static struct phy_driver bcm84881_drivers[] = {
	{
		.phy_id		= 0xae025150,
		.phy_id_mask	= 0xfffffff0,
		.name		= "Broadcom BCM84881",
		.inband_caps	= bcm84881_inband_caps,
		.config_inband	= bcm84881_config_inband,
		.config_init	= bcm84881_config_init,
		.probe		= bcm84881_probe,
		.get_features	= bcm84881_get_features,
		.config_aneg	= bcm84881_config_aneg,
		.aneg_done	= bcm84881_aneg_done,
		.read_status	= bcm84881_read_status,
	}, {
		PHY_ID_MATCH_MODEL(0x35905080),
		.name		= "Broadcom BCM84891",
		.inband_caps	= bcm84881_inband_caps,
		.config_inband	= bcm84881_config_inband,
		.config_init	= bcm8489x_config_init,
		.probe		= bcm84881_probe,
		.get_features	= bcm8489x_get_features,
		.config_aneg	= bcm84881_config_aneg,
		.aneg_done	= bcm84881_aneg_done,
		.read_status	= bcm84881_read_status,
		.led_brightness_set = bcm8489x_led_brightness_set,
		.led_hw_is_supported = bcm8489x_led_hw_is_supported,
		.led_hw_control_set = bcm8489x_led_hw_control_set,
		.led_hw_control_get = bcm8489x_led_hw_control_get,
	}, {
		PHY_ID_MATCH_MODEL(0x359050a0),
		.name		= "Broadcom BCM84892",
		.inband_caps	= bcm84881_inband_caps,
		.config_inband	= bcm84881_config_inband,
		.config_init	= bcm8489x_config_init,
		.probe		= bcm84881_probe,
		.get_features	= bcm8489x_get_features,
		.config_aneg	= bcm84881_config_aneg,
		.aneg_done	= bcm84881_aneg_done,
		.read_status	= bcm84881_read_status,
		.led_brightness_set = bcm8489x_led_brightness_set,
		.led_hw_is_supported = bcm8489x_led_hw_is_supported,
		.led_hw_control_set = bcm8489x_led_hw_control_set,
		.led_hw_control_get = bcm8489x_led_hw_control_get,
	},
};

module_phy_driver(bcm84881_drivers);

/* FIXME: module auto-loading for Clause 45 PHYs seems non-functional */
static const struct mdio_device_id __maybe_unused bcm84881_tbl[] = {
	{ 0xae025150, 0xfffffff0 },
	{ PHY_ID_MATCH_MODEL(0x35905080) },
	{ PHY_ID_MATCH_MODEL(0x359050a0) },
	{ },
};
MODULE_AUTHOR("Russell King");
MODULE_DESCRIPTION("Broadcom BCM84881/BCM84891/BCM84892 PHY driver");
MODULE_DEVICE_TABLE(mdio, bcm84881_tbl);
MODULE_LICENSE("GPL");
