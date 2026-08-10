// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Driver for MaxLinear MxL862xx switch family
 *
 * Copyright (C) 2024 MaxLinear Inc.
 * Copyright (C) 2025 John Crispin <john@phrozen.org>
 * Copyright (C) 2025 Daniel Golle <daniel@makrotopia.org>
 */

#include <linux/bitfield.h>
#include <linux/delay.h>
#include <linux/etherdevice.h>
#include <linux/icmpv6.h>
#include <linux/if_bridge.h>
#include <linux/if_vlan.h>
#include <linux/mii.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/of_mdio.h>
#include <linux/phy.h>
#include <linux/phylink.h>
#include <linux/dsa/8021q.h>
#include <net/dsa.h>
#include <linux/stddef.h>
#include <linux/gpio/consumer.h>
#include <linux/of_net.h>

#include "mxl862xx.h"
#include "mxl862xx-api.h"
#include "mxl862xx-cmd.h"
#include "mxl862xx-fw.h"
#include "mxl862xx-host.h"
#include "mxl862xx-phylink.h"

/* Polling interval for RMON counter accumulation. At 2.5 Gbps with
 * minimum-size (64-byte) frames, a 32-bit packet counter wraps in ~880s.
 * 2s gives a comfortable margin.
 */
#define MXL862XX_STATS_POLL_INTERVAL	(2 * HZ)

struct mxl862xx_mib_desc {
	unsigned int size;
	unsigned int offset;
	const char *name;
};

#define MIB_DESC(_size, _name, _element)					\
{									\
	.size = _size,							\
	.name = _name,							\
	.offset = offsetof(struct mxl862xx_rmon_port_cnt, _element)	\
}

/* Hardware-specific counters not covered by any standardized stats callback. */
static const struct mxl862xx_mib_desc mxl862xx_mib[] = {
	MIB_DESC(1, "TxAcmDroppedPkts", tx_acm_dropped_pkts),
	MIB_DESC(1, "RxFilteredPkts", rx_filtered_pkts),
	MIB_DESC(1, "RxExtendedVlanDiscardPkts", rx_extended_vlan_discard_pkts),
	MIB_DESC(1, "MtuExceedDiscardPkts", mtu_exceed_discard_pkts),
	MIB_DESC(2, "RxBadBytes", rx_bad_bytes),
};

static const struct ethtool_rmon_hist_range mxl862xx_rmon_ranges[] = {
	{ 0, 64 },
	{ 65, 127 },
	{ 128, 255 },
	{ 256, 511 },
	{ 512, 1023 },
	{ 1024, 10240 },
	{}
};

#define MXL862XX_SDMA_PCTRLP(p)		(0xbc0 + ((p) * 0x6))
#define MXL862XX_SDMA_PCTRL_EN		BIT(0)

#define MXL862XX_FDMA_PCTRLP(p)		(0xa80 + ((p) * 0x6))
#define MXL862XX_FDMA_PCTRL_EN		BIT(0)

#define MXL862XX_READY_TIMEOUT_MS	10000
#define MXL862XX_READY_POLL_MS		100

#define MXL862XX_PHY_READY_TIMEOUT_MS	5000
#define MXL862XX_PHY_READY_POLL_MS	20

#define MXL862XX_TCM_INST_SEL		0xe00
#define MXL862XX_TCM_CBS		0xe12
#define MXL862XX_TCM_EBS		0xe13

static const int mxl862xx_flood_meters[] = {
	MXL862XX_BRIDGE_PORT_EGRESS_METER_UNKNOWN_UC,
	MXL862XX_BRIDGE_PORT_EGRESS_METER_UNKNOWN_MC_IP,
	MXL862XX_BRIDGE_PORT_EGRESS_METER_UNKNOWN_MC_NON_IP,
	MXL862XX_BRIDGE_PORT_EGRESS_METER_BROADCAST,
};

enum mxl862xx_evlan_action {
	EVLAN_ACCEPT,			/* pass-through, no tag removal */
	EVLAN_STRIP_IF_UNTAGGED,	/* remove 1 tag if entry's untagged flag set */
	EVLAN_PVID_OR_DISCARD,		/* insert PVID tag or discard if no PVID */
	EVLAN_STRIP1_AND_PVID_OR_DISCARD,/* strip 1 tag + insert PVID, or discard */
	EVLAN_INSERT_OUTER,		/* insert outer tag with mgmt_vid */
	EVLAN_STRIP1,			/* strip 1 tag unconditionally */
	EVLAN_REASSIGN,			/* reassign bridge port (keep tags) */
};

struct mxl862xx_evlan_rule_desc {
	u8 outer_type;		/* enum mxl862xx_extended_vlan_filter_type */
	u8 inner_type;		/* enum mxl862xx_extended_vlan_filter_type */
	u8 outer_tpid;		/* enum mxl862xx_extended_vlan_filter_tpid */
	u8 inner_tpid;		/* enum mxl862xx_extended_vlan_filter_tpid */
	bool match_vid;		/* true: match on VID from the vid parameter */
	u8 action;		/* enum mxl862xx_evlan_action */
	u16 bridge_port_id;	/* for EVLAN_REASSIGN */
};

/* Shorthand constants for readability */
#define FT_NORMAL	MXL862XX_EXTENDEDVLAN_FILTER_TYPE_NORMAL
#define FT_NO_FILTER	MXL862XX_EXTENDEDVLAN_FILTER_TYPE_NO_FILTER
#define FT_DEFAULT	MXL862XX_EXTENDEDVLAN_FILTER_TYPE_DEFAULT
#define FT_NO_TAG	MXL862XX_EXTENDEDVLAN_FILTER_TYPE_NO_TAG
#define TP_NONE		MXL862XX_EXTENDEDVLAN_FILTER_TPID_NO_FILTER
#define TP_8021Q	MXL862XX_EXTENDEDVLAN_FILTER_TPID_8021Q

/*
 * VLAN-aware ingress: 7 final catchall rules.
 *
 * VLAN Filter handles VID membership for tagged frames, so the
 * Extended VLAN ingress block only needs to handle:
 * - Priority-tagged (VID=0): strip + insert PVID
 * - Untagged: insert PVID or discard
 * - Standard 802.1Q VID>0: pass through (VF handles membership)
 * - Non-8021Q TPID (0x88A8 etc.): treat as untagged
 *
 * Rule ordering is critical: the EVLAN engine scans entries in
 * ascending index order and stops at the first match.
 *
 * The 802.1Q ACCEPT rules (indices 3--4) must appear BEFORE the
 * NO_FILTER catchalls (indices 5--6). NO_FILTER matches any tag
 * regardless of TPID, so without the ACCEPT guard, it would also
 * catch standard 802.1Q VID>0 frames and corrupt them. With the
 * guard, 802.1Q VID>0 frames match the ACCEPT rules first and
 * pass through untouched; only non-8021Q TPID frames pass through
 * to the NO_FILTER catchalls.
 */
static const struct mxl862xx_evlan_rule_desc ingress_aware_final[] = {
	/* 802.1p / priority-tagged (VID 0): strip + PVID */
	{ FT_NORMAL,    FT_NORMAL, TP_8021Q, TP_8021Q, true,  EVLAN_STRIP1_AND_PVID_OR_DISCARD },
	{ FT_NORMAL,    FT_NO_TAG, TP_8021Q, TP_NONE,  true,  EVLAN_STRIP1_AND_PVID_OR_DISCARD },
	/* Untagged: PVID insertion or discard */
	{ FT_NO_TAG,    FT_NO_TAG, TP_NONE,  TP_NONE,  false, EVLAN_PVID_OR_DISCARD },
	/* 802.1Q VID>0: accept - VF handles membership.
	 * match_vid=false means any VID; VID=0 is already caught above.
	 */
	{ FT_NORMAL,    FT_NORMAL, TP_8021Q, TP_8021Q, false, EVLAN_ACCEPT },
	{ FT_NORMAL,    FT_NO_TAG, TP_8021Q, TP_NONE,  false, EVLAN_ACCEPT },
	/* Non-8021Q TPID (0x88A8 etc.): treat as untagged - strip + PVID */
	{ FT_NO_FILTER, FT_NO_FILTER, TP_NONE, TP_NONE, false, EVLAN_STRIP1_AND_PVID_OR_DISCARD },
	{ FT_NO_FILTER, FT_NO_TAG,    TP_NONE, TP_NONE, false, EVLAN_STRIP1_AND_PVID_OR_DISCARD },
};

/*
 * VID-specific accept rules (VLAN-aware, standard tag, 2 per VID).
 * Outer tag carries the VLAN; inner may or may not be present.
 */
static const struct mxl862xx_evlan_rule_desc vid_accept_standard[] = {
	{ FT_NORMAL, FT_NORMAL, TP_8021Q, TP_8021Q, true, EVLAN_STRIP_IF_UNTAGGED },
	{ FT_NORMAL, FT_NO_TAG, TP_8021Q, TP_NONE,  true, EVLAN_STRIP_IF_UNTAGGED },
};

/*
 * Egress tag-stripping rules for VLAN-unaware mode (2 per untagged VID).
 * The HW sees the MxL tag as outer; the real VLAN tag, if any, is inner.
 */
static const struct mxl862xx_evlan_rule_desc vid_accept_egress_unaware[] = {
	{ FT_NO_FILTER, FT_NORMAL, TP_NONE, TP_8021Q, true,  EVLAN_STRIP_IF_UNTAGGED },
	{ FT_NO_FILTER, FT_NO_TAG, TP_NONE, TP_NONE,  false, EVLAN_STRIP_IF_UNTAGGED },
};

/*
 * tag_8021q: virtual bridge port egress rules.
 *
 * Inserts the management VID as an outer 802.1Q tag on all frames
 * exiting toward the CPU via a virtual bridge port. Covers every
 * possible frame type (untagged, single-tagged, double-tagged).
 *
 * 802.1Q ACCEPT rules must precede NO_FILTER catchalls to prevent
 * NO_FILTER from matching standard 802.1Q frames first.
 */
static const struct mxl862xx_evlan_rule_desc cpu_egress_tag_8021q[] = {
	/* 802.1Q outer + inner present */
	{ FT_NORMAL,    FT_NORMAL,    TP_8021Q, TP_8021Q, false, EVLAN_INSERT_OUTER },
	/* 802.1Q outer, no inner */
	{ FT_NORMAL,    FT_NO_TAG,    TP_8021Q, TP_NONE,  false, EVLAN_INSERT_OUTER },
	/* Non-8021Q outer + inner present */
	{ FT_NO_FILTER, FT_NO_FILTER, TP_NONE,  TP_NONE,  false, EVLAN_INSERT_OUTER },
	/* Non-8021Q outer only */
	{ FT_NO_FILTER, FT_NO_TAG,    TP_NONE,  TP_NONE,  false, EVLAN_INSERT_OUTER },
	/* Untagged */
	{ FT_NO_TAG,    FT_NO_TAG,    TP_NONE,  TP_NONE,  false, EVLAN_INSERT_OUTER },
};

/*
 * tag_8021q: CPU port ingress reassignment rules.
 *
 * Each user port with a management VID gets these rules on the CPU port's
 * ingress EVLAN block. They match the management VID as outer 802.1Q tag
 * and reassign the frame to the user port's virtual bridge port.
 *
 * NO_FILTER is used for the inner position so that frames with any inner
 * TPID (including non-802.1Q TPIDs like 802.1ad 0x88A8) are routed
 * correctly. The management VID tag is kept and stripped later by the
 * user port's egress EVLAN catchall rules.
 *
 * The bridge_port_id is overridden per-port at programming time.
 */
static const struct mxl862xx_evlan_rule_desc cpu_ingress_reassign[] = {
	/* Mgmt VID outer + any inner tag present */
	{ FT_NORMAL,    FT_NO_FILTER, TP_8021Q, TP_NONE,  true, EVLAN_REASSIGN },
	/* Mgmt VID outer, no inner */
	{ FT_NORMAL,    FT_NO_TAG,    TP_8021Q, TP_NONE,  true, EVLAN_REASSIGN },
};

/* User port egress catchall rules for tag_8021q mode.
 * Strip the outer management VID tag from CPU->user frames that were
 * not matched by any per-VID egress rule. Appended to the user port
 * egress EVLAN block when tag_8021q is active.
 */
static const struct mxl862xx_evlan_rule_desc tag_8021q_egress_strip[] = {
	/* Any outer tag + inner present: strip outer (mgmt VID) */
	{ FT_NO_FILTER, FT_NO_FILTER, TP_NONE,  TP_NONE,  false, EVLAN_STRIP1 },
	/* Any outer tag, no inner: strip it */
	{ FT_NO_FILTER, FT_NO_TAG,    TP_NONE,  TP_NONE,  false, EVLAN_STRIP1 },
};

static enum dsa_tag_protocol mxl862xx_get_tag_protocol(struct dsa_switch *ds,
						       int port,
						       enum dsa_tag_protocol m)
{
	struct mxl862xx_priv *priv = ds->priv;

	return priv->tag_proto;
}

/* PHY access via firmware relay */
static int mxl862xx_phy_read_mmd(struct mxl862xx_priv *priv, int addr,
				 int devadd, int regnum)
{
	struct mdio_relay_data param = {
		.phy = addr,
		.mmd = devadd,
		.reg = cpu_to_le16(regnum),
	};
	int ret;

	ret = MXL862XX_API_READ(priv, INT_GPHY_READ, param);
	if (ret)
		return ret;

	return le16_to_cpu(param.data);
}

static int mxl862xx_phy_write_mmd(struct mxl862xx_priv *priv, int addr,
				  int devadd, int regnum, u16 data)
{
	struct mdio_relay_data param = {
		.phy = addr,
		.mmd = devadd,
		.reg = cpu_to_le16(regnum),
		.data = cpu_to_le16(data),
	};

	return MXL862XX_API_WRITE(priv, INT_GPHY_WRITE, param);
}

/* Quiet variant for polling: relay errors are expected while a GPHY is
 * still booting its own firmware.
 */
static int mxl862xx_phy_read_quiet(struct mxl862xx_priv *priv, int addr,
				   int regnum)
{
	struct mdio_relay_data param = {
		.phy = addr,
		.reg = cpu_to_le16(regnum),
	};
	int ret;

	ret = MXL862XX_API_READ_QUIET(priv, INT_GPHY_READ, param);
	if (ret)
		return ret;

	return le16_to_cpu(param.data);
}

static int mxl862xx_phy_read_mii_bus(struct mii_bus *bus, int addr, int regnum)
{
	return mxl862xx_phy_read_mmd(bus->priv, addr, 0, regnum);
}

static int mxl862xx_phy_write_mii_bus(struct mii_bus *bus, int addr,
				      int regnum, u16 val)
{
	return mxl862xx_phy_write_mmd(bus->priv, addr, 0, regnum, val);
}

static int mxl862xx_phy_read_c45_mii_bus(struct mii_bus *bus, int addr,
					 int devadd, int regnum)
{
	return mxl862xx_phy_read_mmd(bus->priv, addr, devadd, regnum);
}

static int mxl862xx_phy_write_c45_mii_bus(struct mii_bus *bus, int addr,
					  int devadd, int regnum, u16 val)
{
	return mxl862xx_phy_write_mmd(bus->priv, addr, devadd, regnum, val);
}

/**
 * mxl862xx_init_fw_caps - Derive the firmware capability mask
 * @priv: driver private data
 *
 * Translates the cached firmware version into the capability bits the
 * rest of the driver tests with mxl862xx_fw_has().  Called once from
 * mxl862xx_wait_ready() after the version has been read.
 *
 * Keeping the version comparisons in one place means a firmware that
 * gains or moves a feature only needs this function updated, rather
 * than every call site that depends on the feature.
 */
static void mxl862xx_init_fw_caps(struct mxl862xx_priv *priv)
{
	u32 caps = 0;

	if (MXL862XX_FW_VER_MIN(priv, 1, 0, 83))
		caps |= MXL862XX_CAP_PCE_LOGIC_IDX;

	if (MXL862XX_FW_VER_MIN(priv, 1, 0, 80)) {
		caps |= MXL862XX_CAP_XPCS_API | MXL862XX_CAP_SERDES_STATS;

		/* Firmware 1.0.84 reshaped the XPCS PCS commands: the
		 * PCS_CONFIG and PCS_GET_STATE payloads changed layout
		 * and size, PCS_ENABLE and AN_DISABLE were removed (the
		 * bringup became implicit in PCS_CONFIG) and FORCE_SPEED
		 * became PCS_LINK_UP.  The Zephyr -ENOTSUP (-134) that
		 * XPCS_PCS_ENABLE draws on the BananaPi 1.0.85 build is
		 * the v2 firmware refusing a removed v1 command, not the
		 * API being absent: 0x1a03 and 0x1a06 are missing from
		 * the 1.0.85 dispatch table while the v2 payload sizes
		 * match it exactly.  Select the matching ops generation.
		 */
		if (MXL862XX_FW_VER_MIN(priv, 1, 0, 84))
			caps |= MXL862XX_CAP_XPCS_V2;
	} else {
		caps |= MXL862XX_CAP_FW_GLOBAL_RULES;
	}

	priv->fw_caps = caps;
}

static int mxl862xx_wait_ready(struct dsa_switch *ds)
{
	struct mxl862xx_sys_fw_image_version ver = {};
	unsigned long start = jiffies, timeout;
	struct mxl862xx_priv *priv = ds->priv;
	struct mxl862xx_cfg cfg = {};
	int ret;

	timeout = start + msecs_to_jiffies(MXL862XX_READY_TIMEOUT_MS);
	msleep(2000); /* it always takes at least 2 seconds */
	do {
		ret = MXL862XX_API_READ_QUIET(priv, SYS_MISC_FW_VERSION, ver);
		if (ret || !ver.iv_major)
			goto not_ready_yet;

		/* being able to perform CFGGET indicates that
		 * the firmware is ready
		 */
		ret = MXL862XX_API_READ_QUIET(priv,
					      MXL862XX_COMMON_CFGGET,
					      cfg);
		if (ret)
			goto not_ready_yet;

		dev_info(ds->dev, "switch ready after %ums, firmware %u.%u.%u (build %u)\n",
			 jiffies_to_msecs(jiffies - start),
			 ver.iv_major, ver.iv_minor,
			 le16_to_cpu(ver.iv_revision),
			 le32_to_cpu(ver.iv_build_num));

		priv->fw_version.major = ver.iv_major;
		priv->fw_version.minor = ver.iv_minor;
		priv->fw_version.revision = le16_to_cpu(ver.iv_revision);

		mxl862xx_init_fw_caps(priv);

		return 0;

not_ready_yet:
		msleep(MXL862XX_READY_POLL_MS);
	} while (time_before(jiffies, timeout));

	dev_err(ds->dev, "switch not responding after reset\n");
	return -ETIMEDOUT;
}

static int mxl862xx_setup_mdio(struct dsa_switch *ds)
{
	struct mxl862xx_priv *priv = ds->priv;
	struct device *dev = ds->dev;
	struct device_node *child;
	struct device_node *mdio_np;
	struct mii_bus *bus;
	int ret;

	bus = devm_mdiobus_alloc(dev);
	if (!bus)
		return -ENOMEM;

	bus->priv = priv;
	bus->name = KBUILD_MODNAME "-mii";
	snprintf(bus->id, MII_BUS_ID_SIZE, "%s-mii", dev_name(dev));
	bus->read_c45 = mxl862xx_phy_read_c45_mii_bus;
	bus->write_c45 = mxl862xx_phy_write_c45_mii_bus;
	bus->read = mxl862xx_phy_read_mii_bus;
	bus->write = mxl862xx_phy_write_mii_bus;
	bus->parent = dev;
	bus->phy_mask = ~ds->phys_mii_mask;

	mdio_np = of_get_child_by_name(dev->of_node, "mdio");
	if (!mdio_np)
		return -ENODEV;

	/* The switch reports ready once its management firmware answers
	 * commands, but each internal GPHY keeps booting its own
	 * firmware for a window beyond that and the relay answers even
	 * ID-register reads with an error until it is done.
	 * of_mdiobus_register()'s one-shot scan permanently drops any
	 * address that fails.  Wait for every DT-declared PHY to
	 * identify itself before registering the bus.  The timeout is
	 * deliberately generous: the window is firmware-dependent, and
	 * the GPHYs boot in parallel so normally only the first address
	 * polled waits at all.
	 *
	 * ds->phys_mii_mask cannot locate the PHYs here: it carries
	 * user port indices (lan1 = port 1) while the GPHYs answer at
	 * MDIO addresses 0-3.
	 */
	for_each_available_child_of_node(mdio_np, child) {
		unsigned int waited;
		u32 addr;
		int id = 0;

		if (of_property_read_u32(child, "reg", &addr) || addr > 31)
			continue;

		for (waited = 0; waited <= MXL862XX_PHY_READY_TIMEOUT_MS;
		     waited += MXL862XX_PHY_READY_POLL_MS) {
			id = mxl862xx_phy_read_quiet(priv, addr, MII_PHYSID1);
			if (id > 0 && id != 0xffff)
				break;
			msleep(MXL862XX_PHY_READY_POLL_MS);
		}
		if (id <= 0 || id == 0xffff)
			dev_warn(dev,
				 "internal PHY %u not responding; its port will be missing\n",
				 addr);
		else if (waited)
			dev_info(dev, "internal PHY %u ready after %ums\n",
				 addr, waited);
	}

	ret = devm_of_mdiobus_register(dev, bus, mdio_np);
	of_node_put(mdio_np);

	return ret;
}

static int mxl862xx_bridge_config_fwd(struct dsa_switch *ds, u16 bridge_id,
				      bool ucast_flood, bool mcast_flood,
				      bool bcast_flood)
{
	struct mxl862xx_bridge_config bridge_config = {};
	struct mxl862xx_priv *priv = ds->priv;
	int ret;

	bridge_config.mask = cpu_to_le32(MXL862XX_BRIDGE_CONFIG_MASK_FORWARDING_MODE);
	bridge_config.bridge_id = cpu_to_le16(bridge_id);

	bridge_config.forward_unknown_unicast = cpu_to_le32(ucast_flood ?
		MXL862XX_BRIDGE_FORWARD_FLOOD : MXL862XX_BRIDGE_FORWARD_DISCARD);

	bridge_config.forward_unknown_multicast_ip = cpu_to_le32(mcast_flood ?
		MXL862XX_BRIDGE_FORWARD_FLOOD : MXL862XX_BRIDGE_FORWARD_DISCARD);
	bridge_config.forward_unknown_multicast_non_ip =
		bridge_config.forward_unknown_multicast_ip;

	bridge_config.forward_broadcast = cpu_to_le32(bcast_flood ?
		MXL862XX_BRIDGE_FORWARD_FLOOD : MXL862XX_BRIDGE_FORWARD_DISCARD);

	ret = MXL862XX_API_WRITE(priv, MXL862XX_BRIDGE_CONFIGSET, bridge_config);
	if (ret)
		dev_err(ds->dev, "failed to configure bridge %u forwarding: %d\n",
			bridge_id, ret);

	return ret;
}

/* Allocate a single zero-rate meter shared by all ports and flood types.
 * All flood-blocking egress sub-meters point to this one meter so that any
 * packet hitting this meter is unconditionally dropped.
 *
 * The firmware API requires CBS >= 64 (its bs2ls encoder clamps smaller
 * values), so the meter is initially configured with CBS=EBS=64.
 * A zero-rate bucket starts full at CBS bytes, which would let one packet
 * through before the bucket empties. To eliminate this one-packet leak we
 * override CBS and EBS to zero via direct register writes after the API call;
 * the hardware accepts CBS=0 and immediately flags the bucket as exceeded,
 * so no traffic can ever pass.
 */
static int mxl862xx_setup_drop_meter(struct dsa_switch *ds)
{
	struct mxl862xx_qos_meter_cfg meter = {};
	struct mxl862xx_priv *priv = ds->priv;
	struct mxl862xx_register_mod reg;
	int ret;

	/* meter_id=0 means auto-alloc */
	ret = MXL862XX_API_READ(priv, MXL862XX_QOS_METERALLOC, meter);
	if (ret)
		return ret;

	meter.enable = true;
	meter.cbs = cpu_to_le32(64);
	meter.ebs = cpu_to_le32(64);
	snprintf(meter.meter_name, sizeof(meter.meter_name), "drop");

	ret = MXL862XX_API_WRITE(priv, MXL862XX_QOS_METERCFGSET, meter);
	if (ret)
		return ret;

	priv->drop_meter = le16_to_cpu(meter.meter_id);

	/* Select the meter instance for subsequent TCM register access. */
	reg.addr = cpu_to_le16(MXL862XX_TCM_INST_SEL);
	reg.data = cpu_to_le16(priv->drop_meter);
	reg.mask = cpu_to_le16(0xffff);
	ret = MXL862XX_API_WRITE(priv, MXL862XX_COMMON_REGISTERMOD, reg);
	if (ret)
		return ret;

	/* Zero CBS so the committed bucket starts empty (exceeded). */
	reg.addr = cpu_to_le16(MXL862XX_TCM_CBS);
	reg.data = 0;
	ret = MXL862XX_API_WRITE(priv, MXL862XX_COMMON_REGISTERMOD, reg);
	if (ret)
		return ret;

	/* Zero EBS so the excess bucket starts empty (exceeded). */
	reg.addr = cpu_to_le16(MXL862XX_TCM_EBS);
	return MXL862XX_API_WRITE(priv, MXL862XX_COMMON_REGISTERMOD, reg);
}

/* Disable firmware global PCE rules that trap various protocols to the
 * on-die microcontroller (port 0) via PORTMAP_CPU. Under DSA, these
 * frames must either reach the host CPU via per-port rules (link-local)
 * or through the normal bridge forwarding path (ARP broadcast), so the
 * global firmware rules are not needed. With the microcontroller port
 * disabled they would silently drop matching traffic.
 *
 * Global rules have lower indices than CTP rules, hence higher priority
 * in the PCE pipeline -- they must be explicitly disabled or they will
 * shadow the per-CTP traps.
 *
 * Indices from gsw_flow_index.h:
 *   1 -- BPDU (STP/RSTP, dst 01:80:c2:00:00:00)
 *   3 -- LLDP         (EtherType 0x88cc)
 *   4 -- OAM/LACP     (EtherType 0x8809)
 *   6 -- System MAC   (dst 02:e0:92:00:00:01, vendor management MAC)
 *   7 -- ARP Request  (broadcast + EtherType 0x0806 + TPA 192.0.2.1)
 */
static int mxl862xx_disable_fw_global_rules(struct dsa_switch *ds)
{
	static const u16 indices[] = { 1, 3, 4, 6, 7 };
	struct mxl862xx_pce_rule rule;
	int i, ret;

	for (i = 0; i < ARRAY_SIZE(indices); i++) {
		memset(&rule, 0, sizeof(rule));
		rule.pattern.index = cpu_to_le16(indices[i]);
		/* pattern.enable == 0 -> rule is disabled */

		ret = MXL862XX_API_WRITE(ds->priv,
					 MXL862XX_TFLOW_PCERULEWRITE, rule);
		if (ret)
			return ret;
	}

	return 0;
}

/* Per-CTP offsets for protocol trap rules. Each port's CTP flow-table
 * block is pre-allocated by the firmware during init (44 entries per
 * port on a 10-port SKU, of which offset 0 is reserved for flow-control
 * marking). Offsets 1-4 are used for link-local and multicast snooping
 * traps; all others remain free.
 */
#define MXL862XX_LINK_LOCAL_CTP_OFFSET		1
#define MXL862XX_IGMP_CTP_OFFSET		2
#define MXL862XX_MLDV1_CTP_OFFSET		3
#define MXL862XX_MLDV2_CTP_OFFSET		4

/**
 * mxl862xx_cpu_bridge_port_id - Get the bridge port ID for CPU-side forwarding
 * @ds: DSA switch
 * @port: user port number
 *
 * In tag_8021q mode, returns the virtual bridge port ID so that frames
 * destined for the CPU pass through the virtual bridge port's egress
 * EVLAN (which inserts the management VID). In native SpTag mode,
 * returns the physical CPU port index.
 */
static int mxl862xx_cpu_bridge_port_id(struct dsa_switch *ds, int port)
{
	struct mxl862xx_priv *priv = ds->priv;
	struct mxl862xx_port *p = &priv->ports[port];

	if (priv->tag_proto == DSA_TAG_PROTO_MXL862_8021Q && p->bridge_port_cpu)
		return p->bridge_port_cpu;

	return dsa_to_port(ds, port)->cpu_dp->index;
}

/**
 * mxl862xx_tag_8021q_disable_cpu_egress - Disable virtual bridge port egress EVLAN
 * @ds: DSA switch
 * @port: user port whose virtual bridge port egress EVLAN to disable
 */
static void mxl862xx_tag_8021q_disable_cpu_egress(struct dsa_switch *ds,
						   int port)
{
	struct mxl862xx_priv *priv = ds->priv;
	struct mxl862xx_port *p = &priv->ports[port];
	struct mxl862xx_bridge_port_config bp_cfg = {};

	if (!p->bridge_port_cpu || !p->cpu_egress_evlan.allocated)
		return;

	/* Disable egress EVLAN on the virtual bridge port */
	bp_cfg.bridge_port_id = cpu_to_le16(p->bridge_port_cpu);
	bp_cfg.mask = cpu_to_le32(MXL862XX_BRIDGE_PORT_CONFIG_MASK_EGRESS_VLAN);
	bp_cfg.egress_extended_vlan_enable = 0;
	MXL862XX_API_WRITE(priv, MXL862XX_BRIDGEPORT_CONFIGSET, bp_cfg);

	p->cpu_egress_evlan.in_use = false;
}

/**
 * mxl862xx_set_cpu_ctp_ingress_evlan - Assign ingress EVLAN to the CPU
 *                                      port's CTP
 * @ds: DSA switch
 * @cpu: CPU port index
 *
 * Both the reference and legacy drivers assign the CPU port's ingress
 * EVLAN at the CTP level (via CTP_PORTCONFIGSET) rather than the
 * bridge port level (BRIDGEPORT_CONFIGSET). The GSWIP ingress
 * pipeline evaluates Bridge Port EVLAN first, then CTP EVLAN; the
 * bridge port reassignment treatment used by tag_8021q only works
 * reliably from the CTP level.
 */
static int mxl862xx_set_cpu_ctp_ingress_evlan(struct dsa_switch *ds, int cpu)
{
	struct mxl862xx_priv *priv = ds->priv;
	struct mxl862xx_evlan_block *blk = &priv->ports[cpu].ingress_evlan;
	struct mxl862xx_ctp_port_config ctp = {};

	ctp.logical_port_id = cpu;
	ctp.mask = cpu_to_le32(MXL862XX_CTP_PORT_CONFIG_MASK_INGRESS_VLAN);
	ctp.ingress_extended_vlan_enable = blk->in_use;
	ctp.ingress_extended_vlan_block_id = cpu_to_le16(blk->block_id);

	return MXL862XX_API_WRITE(priv, MXL862XX_CTP_PORTCONFIGSET, ctp);
}

/* Fill the action fields of a PCE rule that traps ingress frames to
 * the CPU port. Used by both the link-local trap and the multicast
 * snooping traps. The caller must already have set the rule header
 * (logicalportid, subifidgroup, region) and the pattern fields.
 *
 * PORTMAP_ALTERNATIVE redirects the frame to the CPU port but does
 * not by itself bypass downstream flood gates. In SpTag mode the
 * ingress port's private FID may have forward_unknown_multicast=false,
 * which silently drops IGMP/MLD before they reach the CPU. In
 * tag_8021q mode the VBP egress sub-meters can have the same effect.
 * Setting bFidEnable to cpu_trap_fid (a dedicated bridge with all
 * flood modes enabled) overrides the FID used by the bridge engine,
 * so the frame is never classified as blocked unknown MC regardless
 * of the ingress port's standalone flood policy.
 *
 * In tag_8021q mode the VBP egress EVLAN block is also attached so
 * that the management VID is inserted before the frame reaches the
 * CPU. Cross-state is enabled so trapped frames bypass STP port
 * state.
 */
static void mxl862xx_fill_cpu_trap_action(struct dsa_switch *ds, int port,
					   struct mxl862xx_pce_rule *rule)
{
	struct mxl862xx_priv *priv = ds->priv;
	struct mxl862xx_port *p = &priv->ports[port];
	int cpu_port = dsa_to_port(ds, port)->cpu_dp->index;

	rule->action.port_map_action =
		cpu_to_le32(MXL862XX_PCE_ACTION_PORTMAP_ALTERNATIVE);
	mxl862xx_fw_portmap_set_bit(rule->action.forward_port_map, cpu_port);

	if (priv->tag_proto == DSA_TAG_PROTO_MXL862_8021Q &&
	    p->cpu_egress_evlan.in_use) {
		rule->action.extended_vlan_enable = 1;
		rule->action.extended_vlan_block_id =
			cpu_to_le16(p->cpu_egress_evlan.block_id);
	}

	rule->action.cross_state_action =
		cpu_to_le32(MXL862XX_PCE_ACTION_CROSS_STATE_CROSS);

	rule->action.fid_enable = 1;
	rule->action.fid = priv->cpu_trap_fid;
}

/* Install one of the per-CTP protocol trap rules.
 *
 * Two firmware interfaces exist for this, taking the same 466-byte
 * struct mxl862xx_pce_rule.  With %MXL862XX_CAP_PCE_LOGIC_IDX the
 * rule index is a logical index within the region selected by
 * @region and @logicalportid, and the firmware grows the underlying
 * block on demand.  Without it, the index is a direct offset into a
 * block the firmware pre-allocated for the CTP at init, and a write
 * past the end of that block is refused.
 *
 * The older interface is what makes firmware 1.0.85 refuse some of
 * these writes with -1022: the pre-allocated block is smaller than it
 * was on 1.0.70, so the higher trap offsets fall outside it, and the
 * refusal pattern follows the block size rather than the port type
 * (port 0 accepts offsets 1-4, port 1 accepts only 1-3).
 *
 * The rules installed here only add link-local trapping and IGMP/MLD
 * snooping and the switch forwards normally without them, so a
 * rejection is still logged rather than propagated: failing the write
 * would abort mxl862xx_refresh_cpu_targets() and leave the board with
 * no user ports at all.
 */
static int mxl862xx_pce_trap_write(struct mxl862xx_priv *priv,
				   struct mxl862xx_pce_rule *rule)
{
	u16 cmd = mxl862xx_fw_has(priv, MXL862XX_CAP_PCE_LOGIC_IDX) ?
		  MXL862XX_TFLOW_PCERULELOGICWRITE :
		  MXL862XX_TFLOW_PCERULEWRITE;
	int ret;

	ret = mxl862xx_api_wrap(priv, cmd, rule, sizeof(*rule), false, true);
	if (ret)
		dev_warn(&priv->mdiodev->dev,
			 "PCE trap rule rejected (port %u index %u): %pe\n",
			 rule->logicalportid,
			 le16_to_cpu(rule->pattern.index), ERR_PTR(ret));

	return 0;
}

/* Install a PCE rule that traps IEEE 802.1D link-local frames
 * (01:80:c2:00:00:0x) to the CPU port for a single user port,
 * preventing the hardware bridge from flooding them to other ports.
 * The firmware does not install this rule by default because its own
 * STP module is not used when DSA manages STP.
 *
 * The rule is written into the port's per-CTP flow table at index 1.
 * Setting region=CTP makes the firmware resolve the index against that
 * port's block rather than the global table, so no dynamic allocation
 * via PCERULEALLOC is needed; see mxl862xx_pce_trap_write() for how the
 * index is interpreted on either firmware interface.
 */
static int mxl862xx_setup_link_local_trap(struct dsa_switch *ds, int port)
{
	struct mxl862xx_priv *priv = ds->priv;
	struct mxl862xx_pce_rule rule = {};

	rule.logicalportid = port;
	rule.region = cpu_to_le32(MXL862XX_PCE_RULE_CTP);

	rule.pattern.index = cpu_to_le16(MXL862XX_LINK_LOCAL_CTP_OFFSET);
	rule.pattern.enable = 1;
	rule.pattern.mac_dst_enable = 1;
	memcpy(rule.pattern.mac_dst, eth_reserved_addr_base, ETH_ALEN);
	rule.pattern.mac_dst_mask = cpu_to_le16(0x0001);

	mxl862xx_fill_cpu_trap_action(ds, port, &rule);

	return mxl862xx_pce_trap_write(priv, &rule);
}

/* Install PCE rules that trap IGMP and MLD frames to the CPU port for
 * a single user port. PORTMAP_ALTERNATIVE overrides the bridge
 * forwarding portmap to the CPU port. bFidEnable points the bridge
 * engine at cpu_trap_fid (all flood modes enabled) so the frames are
 * never classified as blocked unknown MC regardless of the ingress
 * port's standalone flood policy.
 *
 * Three rules are installed per port:
 *   offset 2 -- IPv4 IGMP (IP protocol 2, all versions)
 *   offset 3 -- ICMPv6 types 130-132 (MLDv1 query, report, done)
 *   offset 4 -- ICMPv6 type 143 (MLDv2 Listener Report)
 *
 * The MLDv1 rule uses range mode on the first two bytes after the IP
 * header (ICMPv6 type + code): lower bound 0x8200 (type 130, code 0)
 * to upper bound 0x84ff (type 132, code 255). The MLDv2 rule uses
 * nibble mask 0x3 to match type 143 with any code byte.
 */
static int mxl862xx_setup_snooping_traps(struct dsa_switch *ds, int port)
{
	struct mxl862xx_priv *priv = ds->priv;
	struct mxl862xx_pce_rule rule = {};
	int ret;

	rule.logicalportid = port;
	rule.region = cpu_to_le32(MXL862XX_PCE_RULE_CTP);
	mxl862xx_fill_cpu_trap_action(ds, port, &rule);

	/* IGMP: IPv4 protocol 2, all versions */
	rule.pattern.index = cpu_to_le16(MXL862XX_IGMP_CTP_OFFSET);
	rule.pattern.enable = 1;
	rule.pattern.protocol = IPPROTO_IGMP;
	rule.pattern.protocol_enable = 1;

	ret = mxl862xx_pce_trap_write(priv, &rule);
	if (ret)
		return ret;

	/* MLDv1: ICMPv6 types 130 (query), 131 (report), 132 (done).
	 * Range mode covers all three types with any code value.
	 */
	memset(&rule.pattern, 0, sizeof(rule.pattern));
	rule.pattern.index = cpu_to_le16(MXL862XX_MLDV1_CTP_OFFSET);
	rule.pattern.enable = 1;
	rule.pattern.protocol = IPPROTO_ICMPV6;
	rule.pattern.protocol_enable = 1;
	rule.pattern.app_data_msb =
		cpu_to_le16((u16)ICMPV6_MGM_QUERY << 8);
	rule.pattern.app_mask_range_msb =
		cpu_to_le16(((u16)ICMPV6_MGM_REDUCTION << 8) | 0xff);
	rule.pattern.app_data_msb_enable = 1;
	rule.pattern.app_mask_range_msb_select = 1; /* range mode */

	ret = mxl862xx_pce_trap_write(priv, &rule);
	if (ret)
		return ret;

	/* MLDv2: ICMPv6 type 143 (Listener Report v2), any code byte.
	 * Nibble mask 0x3 masks nibbles 0-1 (lower byte = code field).
	 */
	memset(&rule.pattern, 0, sizeof(rule.pattern));
	rule.pattern.index = cpu_to_le16(MXL862XX_MLDV2_CTP_OFFSET);
	rule.pattern.enable = 1;
	rule.pattern.protocol = IPPROTO_ICMPV6;
	rule.pattern.protocol_enable = 1;
	rule.pattern.app_data_msb = cpu_to_le16((u16)ICMPV6_MLD2_REPORT << 8);
	rule.pattern.app_mask_range_msb = cpu_to_le16(0x0003);
	rule.pattern.app_data_msb_enable = 1;
	/* app_mask_range_msb_select = 0: nibble mask mode (default) */

	return mxl862xx_pce_trap_write(priv, &rule);
}

static bool mxl862xx_is_lag_master(const struct mxl862xx_priv *priv, int port)
{
	struct dsa_lag *lag = priv->ports[port].lag;
	int i;

	if (!lag)
		return true;

	for (i = 0; i < port; i++) {
		if (priv->ports[i].lag == lag)
			return false;
	}

	return true;
}

/**
 * mxl862xx_lag_bridge_port - Get the effective bridge port ID for a port
 * @priv: driver private data
 * @port: port index
 *
 * If @port is a member of a LAG, returns the LAG's dedicated firmware
 * bridge port ID. Otherwise returns @port itself.
 */
static u16 mxl862xx_lag_bridge_port(const struct mxl862xx_priv *priv, int port)
{
	struct dsa_lag *lag = priv->ports[port].lag;

	if (lag && priv->lag_bridge_ports[lag->id])
		return priv->lag_bridge_ports[lag->id];

	return port;
}

static int __mxl862xx_set_bridge_port(struct dsa_switch *ds, int port,
				      u16 bp_id)
{
	struct mxl862xx_bridge_port_config br_port_cfg = {};
	struct dsa_port *dp = dsa_to_port(ds, port);
	struct mxl862xx_priv *priv = ds->priv;
	struct mxl862xx_port *p = &priv->ports[port];
	struct dsa_port *member_dp;
	u16 bridge_id;
	u16 vf_scan;
	bool enable;
	int i, idx;

	if (dsa_port_is_unused(dp))
		return 0;

	if (dsa_port_is_cpu(dp)) {
		/* In tag_8021q mode the CPU TX path uses per-user-port virtual
		 * bridge ports; leave the physical CPU bridge port map empty to
		 * prevent FID 0 flooding back to user ports.
		 */
		if (priv->tag_proto != DSA_TAG_PROTO_MXL862_8021Q) {
			dsa_switch_for_each_user_port(member_dp, ds) {
				if (member_dp->cpu_dp->index != port)
					continue;
				mxl862xx_fw_portmap_set_bit(br_port_cfg.bridge_port_map,
							    member_dp->index);
			}
		}
	} else if (dp->bridge) {
		dsa_switch_for_each_bridge_member(member_dp, ds,
						  dp->bridge->dev) {
			if (member_dp->index == port)
				continue;
			if (!mxl862xx_is_lag_master(priv, member_dp->index))
				continue;
			if (p->isolated && priv->ports[member_dp->index].isolated)
				continue;
			mxl862xx_fw_portmap_set_bit(
				br_port_cfg.bridge_port_map,
				mxl862xx_lag_bridge_port(priv,
							 member_dp->index));
		}
		mxl862xx_fw_portmap_set_bit(br_port_cfg.bridge_port_map,
					    mxl862xx_cpu_bridge_port_id(ds, port));
		if (p->hairpin)
			mxl862xx_fw_portmap_set_bit(br_port_cfg.bridge_port_map,
						    mxl862xx_lag_bridge_port(priv,
									     port));
	} else {
		mxl862xx_fw_portmap_set_bit(br_port_cfg.bridge_port_map,
					    mxl862xx_cpu_bridge_port_id(ds, port));
		p->flood_block = 0;
		p->learning = false;
	}

	bridge_id = dp->bridge ? priv->bridges[dp->bridge->num] : p->fid;

	br_port_cfg.bridge_port_id = cpu_to_le16(bp_id);
	br_port_cfg.bridge_id = cpu_to_le16(bridge_id);
	br_port_cfg.mask = cpu_to_le32(MXL862XX_BRIDGE_PORT_CONFIG_MASK_BRIDGE_ID |
				       MXL862XX_BRIDGE_PORT_CONFIG_MASK_BRIDGE_PORT_MAP |
				       MXL862XX_BRIDGE_PORT_CONFIG_MASK_MC_SRC_MAC_LEARNING |
				       MXL862XX_BRIDGE_PORT_CONFIG_MASK_EGRESS_SUB_METER |
				       MXL862XX_BRIDGE_PORT_CONFIG_MASK_INGRESS_VLAN |
				       MXL862XX_BRIDGE_PORT_CONFIG_MASK_EGRESS_VLAN |
				       MXL862XX_BRIDGE_PORT_CONFIG_MASK_INGRESS_VLAN_FILTER |
				       MXL862XX_BRIDGE_PORT_CONFIG_MASK_EGRESS_VLAN_FILTER1 |
				       MXL862XX_BRIDGE_PORT_CONFIG_MASK_VLAN_BASED_MAC_LEARNING);
	br_port_cfg.src_mac_learning_disable = !p->learning;

	/* Extended VLAN block assignments.
	 * Ingress: block_size is sent as-is (all entries are finals).
	 * Egress: n_active narrows the scan window to only the
	 * entries actually written by evlan_program_egress.
	 */
	br_port_cfg.ingress_extended_vlan_enable = p->ingress_evlan.in_use;
	br_port_cfg.ingress_extended_vlan_block_id =
		cpu_to_le16(p->ingress_evlan.block_id);
	br_port_cfg.ingress_extended_vlan_block_size =
		cpu_to_le16(p->ingress_evlan.block_size);
	br_port_cfg.egress_extended_vlan_enable = p->egress_evlan.in_use;
	br_port_cfg.egress_extended_vlan_block_id =
		cpu_to_le16(p->egress_evlan.block_id);
	br_port_cfg.egress_extended_vlan_block_size =
		cpu_to_le16(p->egress_evlan.n_active);

	/* VLAN Filter block assignments (per-port).
	 * The block_size sent to the firmware narrows the HW scan
	 * window to [block_id, block_id + active_count), relying on
	 * discard_unmatched_tagged for frames outside that range.
	 * When active_count=0, send 1 to scan only the DISCARD
	 * sentinel at index 0 (block_size=0 would disable narrowing
	 * and scan the entire allocated block).
	 *
	 * The bridge check ensures VF is disabled when the port
	 * leaves the bridge, without needing to prematurely clear
	 * vlan_filtering (which the DSA framework handles later via
	 * port_vlan_filtering).
	 */
	if (p->vf.allocated && p->vlan_filtering &&
	    dsa_port_bridge_dev_get(dp)) {
		vf_scan = max_t(u16, p->vf.active_count, 1);
		br_port_cfg.ingress_vlan_filter_enable = 1;
		br_port_cfg.ingress_vlan_filter_block_id =
			cpu_to_le16(p->vf.block_id);
		br_port_cfg.ingress_vlan_filter_block_size =
			cpu_to_le16(vf_scan);

		br_port_cfg.egress_vlan_filter1enable = 1;
		br_port_cfg.egress_vlan_filter1block_id =
			cpu_to_le16(p->vf.block_id);
		br_port_cfg.egress_vlan_filter1block_size =
			cpu_to_le16(vf_scan);
	} else {
		br_port_cfg.ingress_vlan_filter_enable = 0;
		br_port_cfg.egress_vlan_filter1enable = 0;
	}

	/* IVL when VLAN-aware: include VID in FDB lookup keys so that
	 * learned entries are per-VID. In VLAN-unaware mode, SVL is
	 * used (VID excluded from key).
	 */
	br_port_cfg.vlan_src_mac_vid_enable = p->vlan_filtering;
	br_port_cfg.vlan_dst_mac_vid_enable = p->vlan_filtering;

	for (i = 0; i < ARRAY_SIZE(mxl862xx_flood_meters); i++) {
		idx = mxl862xx_flood_meters[i];
		enable = !!(p->flood_block & BIT(idx));

		br_port_cfg.egress_traffic_sub_meter_id[idx] =
			enable ? cpu_to_le16(priv->drop_meter) : 0;
		br_port_cfg.egress_sub_metering_enable[idx] = enable;
	}

	return MXL862XX_API_WRITE(priv, MXL862XX_BRIDGEPORT_CONFIGSET,
				  br_port_cfg);
}

static int mxl862xx_set_bridge_port(struct dsa_switch *ds, int port)
{
	struct mxl862xx_priv *priv = ds->priv;
	struct mxl862xx_port *p = &priv->ports[port];
	u16 lag_bp;
	int ret;

	ret = __mxl862xx_set_bridge_port(ds, port, port);
	if (ret)
		return ret;

	/* If this port is a LAG master, also push its config to the
	 * LAG's dedicated bridge port (which is the actual target of
	 * all member CTP redirections).
	 */
	if (p->lag && mxl862xx_is_lag_master(priv, port)) {
		lag_bp = priv->lag_bridge_ports[p->lag->id];
		if (lag_bp)
			ret = __mxl862xx_set_bridge_port(ds, port, lag_bp);
	}

	return ret;
}

static int mxl862xx_sync_bridge_members(struct dsa_switch *ds,
					const struct dsa_bridge *bridge)
{
	struct mxl862xx_priv *priv = ds->priv;
	struct mxl862xx_port *p;
	struct dsa_port *dp;
	u16 lag_bp;
	int err, ret = 0;

	dsa_switch_for_each_bridge_member(dp, ds, bridge->dev) {
		err = mxl862xx_set_bridge_port(ds, dp->index);
		if (err)
			ret = err;
	}

	/* Push updated portmaps to LAG bridge ports. Each LAG master's
	 * portmap (which excludes itself) is used for the LAG bridge
	 * port -- this naturally avoids self-forwarding.
	 */
	dsa_switch_for_each_bridge_member(dp, ds, bridge->dev) {
		p = &priv->ports[dp->index];

		if (!p->lag || !mxl862xx_is_lag_master(priv, dp->index))
			continue;

		lag_bp = priv->lag_bridge_ports[p->lag->id];
		if (!lag_bp)
			continue;

		err = __mxl862xx_set_bridge_port(ds, dp->index, lag_bp);
		if (err)
			ret = err;
	}

	return ret;
}

static int mxl862xx_evlan_block_alloc(struct mxl862xx_priv *priv,
				      struct mxl862xx_evlan_block *blk)
{
	struct mxl862xx_extendedvlan_alloc param = {};
	int ret;

	param.number_of_entries = cpu_to_le16(blk->block_size);

	ret = MXL862XX_API_READ(priv, MXL862XX_EXTENDEDVLAN_ALLOC, param);
	if (ret)
		return ret;

	blk->block_id = le16_to_cpu(param.extended_vlan_block_id);
	blk->allocated = true;

	return 0;
}

static int mxl862xx_vf_block_alloc(struct mxl862xx_priv *priv,
				   u16 size, u16 *block_id)
{
	struct mxl862xx_vlanfilter_alloc param = {};
	int ret;

	param.number_of_entries = cpu_to_le16(size);
	param.discard_untagged = 0;
	param.discard_unmatched_tagged = 1;

	ret = MXL862XX_API_READ(priv, MXL862XX_VLANFILTER_ALLOC, param);
	if (ret)
		return ret;

	*block_id = le16_to_cpu(param.vlan_filter_block_id);
	return 0;
}

static int mxl862xx_vf_entry_discard(struct mxl862xx_priv *priv,
				     u16 block_id, u16 index)
{
	struct mxl862xx_vlanfilter_config cfg = {};

	cfg.vlan_filter_block_id = cpu_to_le16(block_id);
	cfg.entry_index = cpu_to_le16(index);
	cfg.vlan_filter_mask = cpu_to_le32(MXL862XX_VLAN_FILTER_TCI_MASK_VID);
	cfg.val = cpu_to_le32(0);
	cfg.discard_matched = 1;

	return MXL862XX_API_WRITE(priv, MXL862XX_VLANFILTER_SET, cfg);
}

static int mxl862xx_vf_alloc(struct mxl862xx_priv *priv,
			     struct mxl862xx_vf_block *vf)
{
	int ret;

	ret = mxl862xx_vf_block_alloc(priv, vf->block_size, &vf->block_id);
	if (ret)
		return ret;

	vf->allocated = true;
	vf->active_count = 0;

	/* Sentinel: block VID-0 when scan window covers only index 0 */
	return mxl862xx_vf_entry_discard(priv, vf->block_id, 0);
}

static int mxl862xx_allocate_bridge(struct mxl862xx_priv *priv)
{
	struct mxl862xx_bridge_alloc br_alloc = {};
	int ret;

	ret = MXL862XX_API_READ(priv, MXL862XX_BRIDGE_ALLOC, br_alloc);
	if (ret)
		return ret;

	return le16_to_cpu(br_alloc.bridge_id);
}

static void mxl862xx_free_bridge(struct dsa_switch *ds,
				 const struct dsa_bridge *bridge)
{
	struct mxl862xx_priv *priv = ds->priv;
	u16 fw_id = priv->bridges[bridge->num];
	struct mxl862xx_bridge_alloc br_alloc = {
		.bridge_id = cpu_to_le16(fw_id),
	};
	int ret;

	ret = MXL862XX_API_WRITE(priv, MXL862XX_BRIDGE_FREE, br_alloc);
	if (ret) {
		dev_err(ds->dev, "failed to free fw bridge %u: %pe\n",
			fw_id, ERR_PTR(ret));
		return;
	}

	priv->bridges[bridge->num] = 0;
}

static int mxl862xx_setup(struct dsa_switch *ds)
{
	struct mxl862xx_priv *priv = ds->priv;
	int n_user_ports = 0, n_cpu_ports = 0, max_vlans;
	int cpu_egress_rules, cpu_ingress_per_port;
	int ingress_finals, vid_rules;
	int egress_catchalls, evlan_reserved;
	struct dsa_port *dp;
	int ret, i, port;

	ret = mxl862xx_reset(priv);
	if (ret)
		return ret;

	ret = mxl862xx_wait_ready(ds);
	if (ret)
		return ret;

	for (i = 0; i < 8; i++)
		mxl862xx_setup_pcs(priv, &priv->serdes_ports[i], i + 9);

	/* Calculate Extended VLAN and VLAN Filter block sizes.
	 * With VLAN Filter handling VID membership checks:
	 *   Ingress: only final catchall rules (PVID insertion, 802.1Q
	 *            accept, non-8021Q TPID handling, discard).
	 *            Block sized to exactly fit the finals -- no per-VID
	 *            ingress EVLAN rules are needed. (7 entries.)
	 *   Egress:  2 rules per VID that needs tag stripping (untagged VIDs).
	 *            No egress final catchalls -- VLAN Filter does the discard.
	 *
	 * tag_8021q mode reserves additional resources from the global
	 * pools for management VID handling:
	 *   EVLAN: 5 egress rules per user port (on virtual bridge ports)
	 *          + dynamically-sized CPU ingress EVLAN (2 per user port,
	 *            budgeted here to guarantee space).
	 *   VF:    CPU port needs its own VF block for management VIDs.
	 *
	 * Total EVLAN budget:
	 *   n_user_ports * (ingress + egress + cpu_egress + cpu_ingress_share)
	 *   <= 1024.
	 * Ingress blocks are small (7 entries), so almost all capacity
	 * goes to egress VID rules.
	 * Total VF budget:
	 *   (n_user_ports + n_cpu_ports) * vf_block_size <= 1024.
	 */
	dsa_switch_for_each_user_port(dp, ds)
		n_user_ports++;
	dsa_switch_for_each_cpu_port(dp, ds)
		n_cpu_ports++;

	if (n_user_ports) {
		ingress_finals = ARRAY_SIZE(ingress_aware_final);
		vid_rules = ARRAY_SIZE(vid_accept_standard);
		cpu_egress_rules = ARRAY_SIZE(cpu_egress_tag_8021q);
		cpu_ingress_per_port = ARRAY_SIZE(cpu_ingress_reassign);
		egress_catchalls = ARRAY_SIZE(tag_8021q_egress_strip);

		/* Ingress block: fixed at finals count (7 entries) */
		priv->evlan_ingress_size = ingress_finals;

		/* CPU port ingress EVLAN: reassign rules per user port */
		priv->cpu_evlan_ingress_size =
			cpu_ingress_per_port * n_user_ports;

		/* Reserve EVLAN entries for tag_8021q:
		 *  - virtual bridge port egress blocks (cpu_egress_rules each)
		 *  - CPU port ingress EVLAN (cpu_ingress_per_port each)
		 *  - user port egress catchalls for mgmt VID stripping
		 */
		evlan_reserved = n_user_ports * (ingress_finals +
						 cpu_egress_rules +
						 cpu_ingress_per_port +
						 egress_catchalls);

		/* Egress block: remaining budget divided equally among
		 * user ports. Each untagged VID needs vid_rules (2)
		 * EVLAN entries for tag stripping. Tagged-only VIDs
		 * need no EVLAN rules at all. The block also includes
		 * space for the tag_8021q egress catchall rules.
		 */
		max_vlans = (MXL862XX_TOTAL_EVLAN_ENTRIES - evlan_reserved) /
			    (n_user_ports * vid_rules);
		priv->evlan_egress_size = vid_rules * max_vlans +
					  egress_catchalls;

		/* VLAN Filter block: one per user port plus one per CPU
		 * port (used in tag_8021q mode for management VIDs).
		 * The 1024-entry table is divided equally among all
		 * consumers.
		 */
		priv->vf_block_size = MXL862XX_TOTAL_VF_ENTRIES /
				      (n_user_ports + n_cpu_ports);
	}

	priv->mirror_dest = -1;

	ret = mxl862xx_setup_drop_meter(ds);
	if (ret)
		return ret;

	if (mxl862xx_fw_has(priv, MXL862XX_CAP_FW_GLOBAL_RULES)) {
		ret = mxl862xx_disable_fw_global_rules(ds);
		if (ret)
			return ret;
	}

	/* Pre-allocate firmware resources for all ports. The DSA core
	 * calls change_tag_protocol() between setup() and port_setup(),
	 * and in tag_8021q mode that triggers dsa_tag_8021q_register()
	 * which fires tag_8021q_vlan_add callbacks that need EVLAN and
	 * VF blocks. complete_tag_8021q_setup() also needs per-port
	 * FIDs allocated before port_setup() runs.
	 *
	 * Per-port configuration (SpTag, CTP, portmaps, link-local
	 * traps) is deferred to port_setup().
	 */
	dsa_switch_for_each_cpu_port(dp, ds) {
		port = dp->index;

		priv->ports[port].vf.block_size = priv->vf_block_size;
		INIT_LIST_HEAD(&priv->ports[port].vf.vids);
		priv->ports[port].ingress_evlan.block_size =
			priv->cpu_evlan_ingress_size;
		ret = mxl862xx_evlan_block_alloc(priv,
						 &priv->ports[port].ingress_evlan);
		if (ret)
			return ret;

		ret = mxl862xx_vf_alloc(priv, &priv->ports[port].vf);
		if (ret)
			return ret;
	}

	dsa_switch_for_each_user_port(dp, ds) {
		port = dp->index;

		priv->ports[port].ingress_evlan.block_size =
			priv->evlan_ingress_size;
		ret = mxl862xx_evlan_block_alloc(priv,
						 &priv->ports[port].ingress_evlan);
		if (ret)
			return ret;

		priv->ports[port].egress_evlan.block_size =
			priv->evlan_egress_size;
		ret = mxl862xx_evlan_block_alloc(priv,
						 &priv->ports[port].egress_evlan);
		if (ret)
			return ret;

		priv->ports[port].vf.block_size = priv->vf_block_size;
		INIT_LIST_HEAD(&priv->ports[port].vf.vids);
		ret = mxl862xx_vf_alloc(priv, &priv->ports[port].vf);
		if (ret)
			return ret;

		priv->ports[port].cpu_egress_evlan.block_size =
			ARRAY_SIZE(cpu_egress_tag_8021q);
		ret = mxl862xx_evlan_block_alloc(priv,
						 &priv->ports[port].cpu_egress_evlan);
		if (ret)
			return ret;

		ret = mxl862xx_allocate_bridge(priv);
		if (ret < 0)
			return ret;
		priv->ports[port].fid = ret;

		/* Initialize flood forwarding for the private FID.
		 * change_tag_protocol() runs between setup() and port_setup();
		 * ports must be in a clean standalone state before that window.
		 */
		ret = mxl862xx_bridge_config_fwd(ds, priv->ports[port].fid,
						 false, false, true);
		if (ret)
			return ret;
	}


	/* Allocate a dedicated PCE snooping FID with all flood modes enabled.
	 * Per-port PCE trap rules (link-local, IGMP, MLD) set bFidEnable to
	 * this FID so that the bridge engine uses it for its flood-permission
	 * check instead of the ingress port's private FID (which has
	 * mc_flood=false to restrict unknown MC from reaching the CPU in the
	 * normal path). The hardware PCE FID action field is 6 bits wide, so
	 * the allocated ID must be in range 0..63.
	 */
	ret = mxl862xx_allocate_bridge(priv);
	if (ret < 0)
		return ret;

	if (WARN_ON_ONCE(ret > 0x3F))
		return -ERANGE;

	priv->cpu_trap_fid = ret;

	ret = mxl862xx_bridge_config_fwd(ds, priv->cpu_trap_fid,
					 true, true, true);
	if (ret)
		return ret;
	schedule_delayed_work(&priv->stats_work,
			      MXL862XX_STATS_POLL_INTERVAL);

	return mxl862xx_setup_mdio(ds);
}

static int mxl862xx_port_state(struct dsa_switch *ds, int port, bool enable)
{
	struct mxl862xx_register_mod sdma = {
		.addr = cpu_to_le16(MXL862XX_SDMA_PCTRLP(port)),
		.data = cpu_to_le16(enable ? MXL862XX_SDMA_PCTRL_EN : 0),
		.mask = cpu_to_le16(MXL862XX_SDMA_PCTRL_EN),
	};
	struct mxl862xx_register_mod fdma = {
		.addr = cpu_to_le16(MXL862XX_FDMA_PCTRLP(port)),
		.data = cpu_to_le16(enable ? MXL862XX_FDMA_PCTRL_EN : 0),
		.mask = cpu_to_le16(MXL862XX_FDMA_PCTRL_EN),
	};
	int ret;

	ret = MXL862XX_API_WRITE(ds->priv, MXL862XX_COMMON_REGISTERMOD, sdma);
	if (ret)
		return ret;

	return MXL862XX_API_WRITE(ds->priv, MXL862XX_COMMON_REGISTERMOD, fdma);
}

static int mxl862xx_port_enable(struct dsa_switch *ds, int port,
				struct phy_device *phydev)
{
	return mxl862xx_port_state(ds, port, true);
}

static void mxl862xx_port_disable(struct dsa_switch *ds, int port)
{
	if (mxl862xx_port_state(ds, port, false))
		dev_err(ds->dev, "failed to disable port %d\n", port);
}

static void mxl862xx_port_fast_age(struct dsa_switch *ds, int port)
{
	struct mxl862xx_mac_table_clear param = {
		.type = MXL862XX_MAC_CLEAR_PHY_PORT,
		.port_id = port,
	};

	if (MXL862XX_API_WRITE(ds->priv, MXL862XX_MAC_TABLECLEARCOND, param))
		dev_err(ds->dev, "failed to clear fdb on port %d\n", port);
}

static int mxl862xx_configure_ctp_port(struct dsa_switch *ds, int port,
				       u16 first_ctp_port_id,
				       u16 number_of_ctp_ports)
{
	struct mxl862xx_ctp_port_assignment ctp_assign = {
		.logical_port_id = port,
		.first_ctp_port_id = cpu_to_le16(first_ctp_port_id),
		.number_of_ctp_port = cpu_to_le16(number_of_ctp_ports),
		.mode = cpu_to_le32(MXL862XX_LOGICAL_PORT_ETHERNET),
	};

	return MXL862XX_API_WRITE(ds->priv, MXL862XX_CTP_PORTASSIGNMENTSET,
				  ctp_assign);
}

static int mxl862xx_configure_sp_tag_proto(struct dsa_switch *ds, int port,
					   bool enable)
{
	struct mxl862xx_ss_sp_tag tag = {
		.pid = port,
		.mask = MXL862XX_SS_SP_TAG_MASK_RX | MXL862XX_SS_SP_TAG_MASK_TX,
		.rx = enable ? MXL862XX_SS_SP_TAG_RX_TAG_NO_INSERT :
			       MXL862XX_SS_SP_TAG_RX_NO_TAG_INSERT,
		.tx = enable ? MXL862XX_SS_SP_TAG_TX_TAG_NO_REMOVE :
			       MXL862XX_SS_SP_TAG_TX_TAG_REMOVE,
	};

	return MXL862XX_API_WRITE(ds->priv, MXL862XX_SS_SPTAG_SET, tag);
}

/**
 * mxl862xx_set_cpu_vbp - Push CPU-side virtual bridge port config to firmware
 * @ds: DSA switch
 * @port: user port index whose VBP to configure
 *
 * Each user port in tag_8021q mode has a virtual bridge port (VBP) that
 * sits on the CPU RX path. The VBP lives in the user port's permanent
 * per-port FID so host FDB/MDB entries in that FID can target it directly.
 * Per-port host flood control is implemented via egress sub-meters on
 * the VBP.
 *
 * This is intentionally separate from mxl862xx_set_bridge_port() because
 * the VBP and the physical bridge port are independent firmware entities:
 * host flood changes (deferred from atomic context) only need the VBP
 * update, and VLAN/STP changes only need the physical bridge port update.
 */
static int mxl862xx_set_cpu_vbp(struct dsa_switch *ds, int port)
{
	struct mxl862xx_bridge_port_config vbp_cfg = {};
	struct mxl862xx_priv *priv = ds->priv;
	struct mxl862xx_port *p = &priv->ports[port];
	bool enable;
	int i, idx;

	if (!p->bridge_port_cpu)
		return 0;

	vbp_cfg.bridge_port_id = cpu_to_le16(p->bridge_port_cpu);
	vbp_cfg.mask = cpu_to_le32(
		MXL862XX_BRIDGE_PORT_CONFIG_MASK_BRIDGE_ID |
		MXL862XX_BRIDGE_PORT_CONFIG_MASK_EGRESS_SUB_METER);
	vbp_cfg.bridge_id = cpu_to_le16(p->fid);

	for (i = 0; i < ARRAY_SIZE(mxl862xx_flood_meters); i++) {
		idx = mxl862xx_flood_meters[i];
		enable = !!(p->host_flood_block & BIT(idx));

		vbp_cfg.egress_traffic_sub_meter_id[idx] =
			enable ? cpu_to_le16(priv->drop_meter) : 0;
		vbp_cfg.egress_sub_metering_enable[idx] = enable;
	}

	return MXL862XX_API_WRITE(priv, MXL862XX_BRIDGEPORT_CONFIGSET,
				  vbp_cfg);
}

/**
 * mxl862xx_evlan_write_rule - Write a single Extended VLAN rule to hardware
 * @priv: driver private data
 * @block_id: HW Extended VLAN block ID
 * @entry_index: entry index within the block
 * @desc: rule descriptor (filter type + action)
 * @vid: VLAN ID for VID-specific rules (ignored when !desc->match_vid)
 * @untagged: strip tag on egress for EVLAN_STRIP_IF_UNTAGGED action
 * @pvid: port VLAN ID for PVID insertion rules (0 = no PVID)
 * @mgmt_vid: tag_8021q management VID for outer tag insertion (0 = unused)
 *
 * Translates a compact rule descriptor into a full firmware
 * mxl862xx_extendedvlan_config struct and writes it via the API.
 */
static int mxl862xx_evlan_write_rule(struct mxl862xx_priv *priv,
				     u16 block_id, u16 entry_index,
				     const struct mxl862xx_evlan_rule_desc *desc,
				     u16 vid, bool untagged, u16 pvid,
				     u16 mgmt_vid)
{
	struct mxl862xx_extendedvlan_config cfg = {};
	struct mxl862xx_extendedvlan_filter_vlan *fv;

	cfg.extended_vlan_block_id = cpu_to_le16(block_id);
	cfg.entry_index = cpu_to_le16(entry_index);

	/* Populate filter */
	cfg.filter.outer_vlan.type = cpu_to_le32(desc->outer_type);
	cfg.filter.inner_vlan.type = cpu_to_le32(desc->inner_type);
	cfg.filter.outer_vlan.tpid = cpu_to_le32(desc->outer_tpid);
	cfg.filter.inner_vlan.tpid = cpu_to_le32(desc->inner_tpid);

	if (desc->match_vid) {
		/* For egress unaware: outer=NO_FILTER, match on inner tag */
		if (desc->outer_type == FT_NO_FILTER)
			fv = &cfg.filter.inner_vlan;
		else
			fv = &cfg.filter.outer_vlan;

		fv->vid_enable = 1;
		fv->vid_val = cpu_to_le32(vid);
	}

	/* Populate treatment based on action */
	switch (desc->action) {
	case EVLAN_ACCEPT:
		cfg.treatment.remove_tag =
			cpu_to_le32(MXL862XX_EXTENDEDVLAN_TREATMENT_NOT_REMOVE_TAG);
		break;

	case EVLAN_STRIP_IF_UNTAGGED:
		cfg.treatment.remove_tag = cpu_to_le32(untagged ?
			MXL862XX_EXTENDEDVLAN_TREATMENT_REMOVE_1_TAG :
			MXL862XX_EXTENDEDVLAN_TREATMENT_NOT_REMOVE_TAG);
		break;

	case EVLAN_PVID_OR_DISCARD:
		if (pvid) {
			cfg.treatment.remove_tag =
				cpu_to_le32(MXL862XX_EXTENDEDVLAN_TREATMENT_NOT_REMOVE_TAG);
			cfg.treatment.add_outer_vlan = 1;
			cfg.treatment.outer_vlan.vid_mode =
				cpu_to_le32(MXL862XX_EXTENDEDVLAN_TREATMENT_VID_VAL);
			cfg.treatment.outer_vlan.vid_val = cpu_to_le32(pvid);
			cfg.treatment.outer_vlan.tpid =
				cpu_to_le32(MXL862XX_EXTENDEDVLAN_TREATMENT_8021Q);
		} else {
			cfg.treatment.remove_tag =
				cpu_to_le32(MXL862XX_EXTENDEDVLAN_TREATMENT_DISCARD_UPSTREAM);
		}
		break;

	case EVLAN_STRIP1_AND_PVID_OR_DISCARD:
		if (pvid) {
			cfg.treatment.remove_tag =
				cpu_to_le32(MXL862XX_EXTENDEDVLAN_TREATMENT_REMOVE_1_TAG);
			cfg.treatment.add_outer_vlan = 1;
			cfg.treatment.outer_vlan.vid_mode =
				cpu_to_le32(MXL862XX_EXTENDEDVLAN_TREATMENT_VID_VAL);
			cfg.treatment.outer_vlan.vid_val = cpu_to_le32(pvid);
			cfg.treatment.outer_vlan.tpid =
				cpu_to_le32(MXL862XX_EXTENDEDVLAN_TREATMENT_8021Q);
		} else {
			cfg.treatment.remove_tag =
				cpu_to_le32(MXL862XX_EXTENDEDVLAN_TREATMENT_DISCARD_UPSTREAM);
		}
		break;

	case EVLAN_INSERT_OUTER:
		cfg.treatment.remove_tag =
			cpu_to_le32(MXL862XX_EXTENDEDVLAN_TREATMENT_NOT_REMOVE_TAG);
		cfg.treatment.add_outer_vlan = 1;
		cfg.treatment.outer_vlan.vid_mode =
			cpu_to_le32(MXL862XX_EXTENDEDVLAN_TREATMENT_VID_VAL);
		cfg.treatment.outer_vlan.vid_val = cpu_to_le32(mgmt_vid);
		cfg.treatment.outer_vlan.tpid =
			cpu_to_le32(MXL862XX_EXTENDEDVLAN_TREATMENT_8021Q);
		break;

	case EVLAN_STRIP1:
		cfg.treatment.remove_tag =
			cpu_to_le32(MXL862XX_EXTENDEDVLAN_TREATMENT_REMOVE_1_TAG);
		break;

	case EVLAN_REASSIGN:
		cfg.treatment.remove_tag =
			cpu_to_le32(MXL862XX_EXTENDEDVLAN_TREATMENT_NOT_REMOVE_TAG);
		cfg.treatment.reassign_bridge_port = 1;
		cfg.treatment.new_bridge_port_id =
			cpu_to_le16(desc->bridge_port_id);
		break;

	}

	return MXL862XX_API_WRITE(priv, MXL862XX_EXTENDEDVLAN_SET, cfg);
}

static int mxl862xx_evlan_deactivate_entry(struct mxl862xx_priv *priv,
					   u16 block_id, u16 entry_index)
{
	struct mxl862xx_extendedvlan_config cfg = {};

	cfg.extended_vlan_block_id = cpu_to_le16(block_id);
	cfg.entry_index = cpu_to_le16(entry_index);

	/* Use an unreachable filter (DEFAULT+DEFAULT) with DISCARD treatment.
	 * A zeroed entry would have NORMAL+NORMAL filter which matches
	 * real double-tagged traffic and passes it through.
	 */
	cfg.filter.outer_vlan.type =
		cpu_to_le32(MXL862XX_EXTENDEDVLAN_FILTER_TYPE_DEFAULT);
	cfg.filter.inner_vlan.type =
		cpu_to_le32(MXL862XX_EXTENDEDVLAN_FILTER_TYPE_DEFAULT);
	cfg.treatment.remove_tag =
		cpu_to_le32(MXL862XX_EXTENDEDVLAN_TREATMENT_DISCARD_UPSTREAM);

	return MXL862XX_API_WRITE(priv, MXL862XX_EXTENDEDVLAN_SET, cfg);
}

static int mxl862xx_evlan_write_final_rules(struct mxl862xx_priv *priv,
					    struct mxl862xx_evlan_block *blk,
					    const struct mxl862xx_evlan_rule_desc *rules,
					    int n_rules, u16 pvid)
{
	u16 start_idx = blk->block_size - n_rules;
	int i, ret;

	for (i = 0; i < n_rules; i++) {
		ret = mxl862xx_evlan_write_rule(priv, blk->block_id,
						start_idx + i, &rules[i],
						0, false, pvid, 0);
		if (ret)
			return ret;
	}

	return 0;
}

static int mxl862xx_vf_entry_set(struct mxl862xx_priv *priv,
				 u16 block_id, u16 index, u16 vid)
{
	struct mxl862xx_vlanfilter_config cfg = {};

	cfg.vlan_filter_block_id = cpu_to_le16(block_id);
	cfg.entry_index = cpu_to_le16(index);
	cfg.vlan_filter_mask = cpu_to_le32(MXL862XX_VLAN_FILTER_TCI_MASK_VID);
	cfg.val = cpu_to_le32(vid);
	cfg.discard_matched = 0;

	return MXL862XX_API_WRITE(priv, MXL862XX_VLANFILTER_SET, cfg);
}

static struct mxl862xx_vf_vid *mxl862xx_vf_find_vid(struct mxl862xx_vf_block *vf,
						    u16 vid)
{
	struct mxl862xx_vf_vid *ve;

	list_for_each_entry(ve, &vf->vids, list)
		if (ve->vid == vid)
			return ve;

	return NULL;
}

static int mxl862xx_vf_add_vid(struct mxl862xx_priv *priv,
			       struct mxl862xx_vf_block *vf,
			       u16 vid, bool untagged)
{
	struct mxl862xx_vf_vid *ve;
	int ret;

	ve = mxl862xx_vf_find_vid(vf, vid);
	if (ve) {
		ve->untagged = untagged;
		return 0;
	}

	if (vf->active_count >= vf->block_size)
		return -ENOSPC;

	ve = kzalloc_obj(*ve);
	if (!ve)
		return -ENOMEM;

	ve->vid = vid;
	ve->index = vf->active_count;
	ve->untagged = untagged;

	ret = mxl862xx_vf_entry_set(priv, vf->block_id, ve->index, vid);
	if (ret) {
		kfree(ve);
		return ret;
	}

	list_add_tail(&ve->list, &vf->vids);
	vf->active_count++;

	return 0;
}

static int mxl862xx_vf_del_vid(struct mxl862xx_priv *priv,
			       struct mxl862xx_vf_block *vf, u16 vid)
{
	struct mxl862xx_vf_vid *ve, *last_ve;
	u16 gap, last;
	int ret;

	ve = mxl862xx_vf_find_vid(vf, vid);
	if (!ve)
		return 0;

	if (!vf->allocated) {
		/* Software-only state -- just remove the tracking entry */
		list_del(&ve->list);
		kfree(ve);
		vf->active_count--;
		return 0;
	}

	gap = ve->index;
	last = vf->active_count - 1;

	if (vf->active_count == 1) {
		/* Last VID -- restore DISCARD sentinel at index 0 */
		ret = mxl862xx_vf_entry_discard(priv, vf->block_id, 0);
		if (ret)
			return ret;
	} else if (gap < last) {
		/* Swap: move the last ALLOW entry into the gap */
		list_for_each_entry(last_ve, &vf->vids, list)
			if (last_ve->index == last)
				break;

		if (WARN_ON(list_entry_is_head(last_ve, &vf->vids, list)))
			return -EINVAL;

		ret = mxl862xx_vf_entry_set(priv, vf->block_id,
					    gap, last_ve->vid);
		if (ret)
			return ret;

		last_ve->index = gap;
	}

	list_del(&ve->list);
	kfree(ve);
	vf->active_count--;

	return 0;
}

/**
 * mxl862xx_vf_clear_vids - Remove all VID entries without freeing the HW block
 * @priv: driver private data
 * @vf: VLAN Filter block
 *
 * Frees the software VID list and resets active_count, but keeps the
 * HW block allocated to avoid firmware table fragmentation.
 */
static void mxl862xx_vf_clear_vids(struct mxl862xx_priv *priv,
				   struct mxl862xx_vf_block *vf)
{
	struct mxl862xx_vf_vid *ve, *tmp;

	list_for_each_entry_safe(ve, tmp, &vf->vids, list) {
		list_del(&ve->list);
		kfree(ve);
	}

	vf->active_count = 0;
}

/**
 * mxl862xx_evlan_program_ingress - Write the fixed ingress catchall rules
 * @priv: driver private data
 * @port: port number
 *
 * In VLAN-aware mode the ingress EVLAN block handles PVID insertion for
 * untagged/priority-tagged frames, passes through standard 802.1Q
 * tagged frames for VF membership checking, and treats non-8021Q TPID
 * frames as untagged. The block is sized to exactly fit the 7 catchall
 * rules and is rewritten whenever PVID changes.
 *
 * In VLAN-unaware mode the firmware passes frames through unchanged when
 * no ingress block is assigned, so nothing is programmed.
 */
static int mxl862xx_evlan_program_ingress(struct mxl862xx_priv *priv, int port)
{
	struct mxl862xx_port *p = &priv->ports[port];
	struct mxl862xx_evlan_block *blk = &p->ingress_evlan;

	if (!p->vlan_filtering)
		return 0;

	blk->in_use = true;
	blk->n_active = blk->block_size;

	return mxl862xx_evlan_write_final_rules(priv, blk,
						ingress_aware_final,
						ARRAY_SIZE(ingress_aware_final),
						p->pvid);
}

static int mxl862xx_evlan_program_egress(struct mxl862xx_priv *priv, int port)
{
	struct mxl862xx_port *p = &priv->ports[port];
	struct mxl862xx_evlan_block *blk = &p->egress_evlan;
	const struct mxl862xx_evlan_rule_desc *vid_rules;
	struct mxl862xx_vf_vid *vfv;
	u16 old_active = blk->n_active;
	int n_vid, n_catch, ret;
	u16 idx = 0, i;

	if (p->vlan_filtering) {
		vid_rules = vid_accept_standard;
		n_vid = ARRAY_SIZE(vid_accept_standard);
	} else {
		vid_rules = vid_accept_egress_unaware;
		n_vid = ARRAY_SIZE(vid_accept_egress_unaware);
	}

	list_for_each_entry(vfv, &p->vf.vids, list) {
		if (!vfv->untagged)
			continue;

		/* Skip the tag_8021q management VID -- it must NOT get
		 * per-VID egress rules. The management VID arrives as
		 * the outer tag on CPU->user frames and is stripped by
		 * the catchall rules appended below. A per-VID rule
		 * here would match first (NO_FILTER outer) and prevent
		 * the catchall from stripping the tag.
		 */
		if (p->tag_8021q_vid && vfv->vid == p->tag_8021q_vid)
			continue;

		if (idx + n_vid > blk->block_size)
			return -ENOSPC;

		ret = mxl862xx_evlan_write_rule(priv, blk->block_id,
						idx++, &vid_rules[0],
						vfv->vid, vfv->untagged,
						p->pvid, 0);
		if (ret)
			return ret;

		if (n_vid > 1) {
			ret = mxl862xx_evlan_write_rule(priv, blk->block_id,
							idx++, &vid_rules[1],
							vfv->vid,
							vfv->untagged,
							p->pvid, 0);
			if (ret)
				return ret;
		}
	}

	/* In tag_8021q mode, append catchall rules that strip the outer
	 * management VID tag from CPU->user frames. The management VID
	 * is kept through the forwarding pipeline (CPU ingress EVLAN
	 * only reassigns the bridge port, without stripping) and must
	 * be removed here before the frame exits the user port.
	 */
	if (priv->tag_proto == DSA_TAG_PROTO_MXL862_8021Q) {
		n_catch = ARRAY_SIZE(tag_8021q_egress_strip);

		if (idx + n_catch > blk->block_size)
			return -ENOSPC;

		for (i = 0; i < n_catch; i++) {
			ret = mxl862xx_evlan_write_rule(priv, blk->block_id,
							idx++,
							&tag_8021q_egress_strip[i],
							0, false, 0, 0);
			if (ret)
				return ret;
		}
	}

	/* Deactivate stale entries that are no longer needed.
	 * This closes the brief window between writing the new rules
	 * and set_bridge_port narrowing the scan window.
	 */
	for (i = idx; i < old_active; i++) {
		ret = mxl862xx_evlan_deactivate_entry(priv,
						      blk->block_id, i);
		if (ret)
			return ret;
	}

	blk->n_active = idx;
	blk->in_use = idx > 0;

	return 0;
}

static int mxl862xx_port_vlan_filtering(struct dsa_switch *ds, int port,
					bool vlan_filtering,
					struct netlink_ext_ack *extack)
{
	struct mxl862xx_priv *priv = ds->priv;
	struct mxl862xx_port *p = &priv->ports[port];
	bool old_vlan_filtering = p->vlan_filtering;
	bool old_in_use = p->ingress_evlan.in_use;
	bool changed = (p->vlan_filtering != vlan_filtering);
	int ret;

	p->vlan_filtering = vlan_filtering;

	if (changed) {
		/* When leaving VLAN-aware mode, disable the ingress
		 * EVLAN engine. The block stays allocated to avoid
		 * firmware EVLAN table fragmentation.
		 */
		if (!vlan_filtering) {
			p->ingress_evlan.in_use = false;
			ret = mxl862xx_set_bridge_port(ds, port);
			if (ret)
				return ret;
		}

		ret = mxl862xx_evlan_program_ingress(priv, port);
		if (ret)
			goto err_restore;

		ret = mxl862xx_evlan_program_egress(priv, port);
		if (ret)
			goto err_restore;
	}

	return mxl862xx_set_bridge_port(ds, port);

	/* No HW rollback -- restoring SW state is sufficient for a correct retry. */
err_restore:
	p->vlan_filtering = old_vlan_filtering;
	p->ingress_evlan.in_use = old_in_use;
	return ret;
}

static int mxl862xx_port_vlan_add(struct dsa_switch *ds, int port,
				  const struct switchdev_obj_port_vlan *vlan,
				  struct netlink_ext_ack *extack)
{
	struct mxl862xx_priv *priv = ds->priv;
	struct mxl862xx_port *p = &priv->ports[port];
	bool untagged = !!(vlan->flags & BRIDGE_VLAN_INFO_UNTAGGED);
	u16 vid = vlan->vid;
	u16 old_pvid = p->pvid;
	bool pvid_changed = false;
	int ret;

	/* CPU port is VLAN-transparent: the SP tag handles port
	 * identification and the host-side DSA tagger manages VLAN
	 * delivery. Egress EVLAN catchalls are set up once in
	 * setup_cpu_bridge; no per-VID VF/EVLAN programming needed.
	 */
	if (dsa_is_cpu_port(ds, port))
		return 0;

	/* Update PVID tracking */
	if (vlan->flags & BRIDGE_VLAN_INFO_PVID) {
		if (p->pvid != vid) {
			p->pvid = vid;
			pvid_changed = true;
		}
	} else if (p->pvid == vid) {
		p->pvid = 0;
		pvid_changed = true;
	}

	/* Add/update VID in this port's VLAN Filter block.
	 * VF must be updated before programming egress EVLAN because
	 * evlan_program_egress walks the VF VID list.
	 */
	ret = mxl862xx_vf_add_vid(priv, &p->vf, vid, untagged);
	if (ret)
		goto err_pvid;

	/* Reprogram ingress finals if PVID changed */
	if (pvid_changed) {
		ret = mxl862xx_evlan_program_ingress(priv, port);
		if (ret)
			goto err_rollback;
	}

	/* Reprogram egress tag-stripping rules (walks VF VID list) */
	ret = mxl862xx_evlan_program_egress(priv, port);
	if (ret)
		goto err_rollback;

	/* Apply VLAN block IDs and MAC learning flags to bridge port */
	ret = mxl862xx_set_bridge_port(ds, port);
	if (ret)
		goto err_rollback;

	return 0;

err_rollback:
	/* Best-effort: undo VF add and restore consistent hardware state.
	 * A retry of port_vlan_add will converge since vf_add_vid is
	 * idempotent.
	 */
	p->pvid = old_pvid;
	mxl862xx_vf_del_vid(priv, &p->vf, vid);
	mxl862xx_evlan_program_ingress(priv, port);
	mxl862xx_evlan_program_egress(priv, port);
	mxl862xx_set_bridge_port(ds, port);
	return ret;
err_pvid:
	p->pvid = old_pvid;
	return ret;
}

static int mxl862xx_port_vlan_del(struct dsa_switch *ds, int port,
				  const struct switchdev_obj_port_vlan *vlan)
{
	struct mxl862xx_priv *priv = ds->priv;
	struct mxl862xx_port *p = &priv->ports[port];
	struct mxl862xx_vf_vid *ve;
	bool pvid_changed = false;
	u16 vid = vlan->vid;
	bool old_untagged;
	u16 old_pvid;
	int ret;

	if (dsa_is_cpu_port(ds, port))
		return 0;

	ve = mxl862xx_vf_find_vid(&p->vf, vid);
	if (!ve)
		return 0;
	old_untagged = ve->untagged;
	old_pvid = p->pvid;

	/* Clear PVID if we're deleting it */
	if (p->pvid == vid) {
		p->pvid = 0;
		pvid_changed = true;
	}

	/* Remove VID from this port's VLAN Filter block.
	 * Must happen before egress reprogram so the VID is no
	 * longer in the list that evlan_program_egress walks.
	 */
	ret = mxl862xx_vf_del_vid(priv, &p->vf, vid);
	if (ret)
		goto err_pvid;

	/* Reprogram egress tag-stripping rules (VID is now gone) */
	ret = mxl862xx_evlan_program_egress(priv, port);
	if (ret)
		goto err_rollback;

	/* If PVID changed, reprogram ingress finals */
	if (pvid_changed) {
		ret = mxl862xx_evlan_program_ingress(priv, port);
		if (ret)
			goto err_rollback;
	}

	ret = mxl862xx_set_bridge_port(ds, port);
	if (ret)
		goto err_rollback;

	return 0;

err_rollback:
	/* Best-effort: re-add the VID and restore consistent hardware
	 * state. A retry of port_vlan_del will converge.
	 */
	p->pvid = old_pvid;
	mxl862xx_vf_add_vid(priv, &p->vf, vid, old_untagged);
	mxl862xx_evlan_program_egress(priv, port);
	mxl862xx_evlan_program_ingress(priv, port);
	mxl862xx_set_bridge_port(ds, port);
	return ret;
err_pvid:
	p->pvid = old_pvid;
	return ret;
}

static int mxl862xx_setup_cpu_bridge(struct dsa_switch *ds, int port)
{
	struct mxl862xx_priv *priv = ds->priv;
	struct mxl862xx_port *p = &priv->ports[port];

	p->fid = MXL862XX_DEFAULT_BRIDGE;
	p->learning = true;

	/* EVLAN is left disabled on CPU ports -- frames pass through
	 * without EVLAN processing. Only the portmap and bridge
	 * assignment need to be configured.
	 */

	return mxl862xx_set_bridge_port(ds, port);
}

static int mxl862xx_port_mirror_add(struct dsa_switch *ds, int port,
				    struct dsa_mall_mirror_tc_entry *mirror,
				    bool ingress,
				    struct netlink_ext_ack *extack)
{
	struct mxl862xx_priv *priv = ds->priv;
	struct mxl862xx_port *p = &priv->ports[port];
	struct mxl862xx_monitor_port_cfg mon = {
		.port_id = mirror->to_local_port,
	};
	struct mxl862xx_ctp_port_config ctp = {
		.logical_port_id = port,
		.mask = cpu_to_le32(
			MXL862XX_CTP_PORT_CONFIG_MASK_LOOPBACK_AND_MIRROR),
		.ingress_mirror_enable = p->ingress_mirror,
		.egress_mirror_enable = p->egress_mirror,
	};
	int ret;

	/* The hardware has a single global monitor port. Reject if an
	 * existing mirror session targets a different destination.
	 */
	if (priv->mirror_dest >= 0 &&
	    priv->mirror_dest != mirror->to_local_port) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Only one mirror destination port is supported");
		return -EBUSY;
	}

	if (ingress)
		ctp.ingress_mirror_enable = 1;
	else
		ctp.egress_mirror_enable = 1;

	ret = MXL862XX_API_WRITE(priv, MXL862XX_CTP_PORTCONFIGSET, ctp);
	if (ret) {
		dev_err(ds->dev, "mirror: CTP write failed for port %d: %pe\n",
			port, ERR_PTR(ret));
		return ret;
	}

	ret = MXL862XX_API_WRITE(priv, MXL862XX_COMMON_MONITORPORTCFGSET, mon);
	if (ret) {
		dev_err(ds->dev,
			"mirror: failed to set monitor port %d: %pe\n",
			mirror->to_local_port, ERR_PTR(ret));
		/* Roll back CTP change */
		ctp.ingress_mirror_enable = p->ingress_mirror;
		ctp.egress_mirror_enable = p->egress_mirror;
		MXL862XX_API_WRITE(priv, MXL862XX_CTP_PORTCONFIGSET, ctp);
		return ret;
	}

	if (ingress)
		p->ingress_mirror = true;
	else
		p->egress_mirror = true;

	priv->mirror_dest = mirror->to_local_port;

	return 0;
}

static void mxl862xx_port_mirror_del(struct dsa_switch *ds, int port,
				     struct dsa_mall_mirror_tc_entry *mirror)
{
	struct mxl862xx_priv *priv = ds->priv;
	struct mxl862xx_port *p = &priv->ports[port];
	struct mxl862xx_ctp_port_config ctp = {
		.logical_port_id = port,
		.mask = cpu_to_le32(
			MXL862XX_CTP_PORT_CONFIG_MASK_LOOPBACK_AND_MIRROR),
		.ingress_mirror_enable = p->ingress_mirror,
		.egress_mirror_enable = p->egress_mirror,
	};
	struct mxl862xx_monitor_port_cfg mon = {};
	bool active = false;
	int i, ret;

	if (mirror->ingress)
		ctp.ingress_mirror_enable = 0;
	else
		ctp.egress_mirror_enable = 0;

	ret = MXL862XX_API_WRITE(priv, MXL862XX_CTP_PORTCONFIGSET, ctp);
	if (ret)
		dev_err(ds->dev, "mirror: CTP write failed for port %d: %pe\n",
			port, ERR_PTR(ret));

	if (mirror->ingress)
		p->ingress_mirror = false;
	else
		p->egress_mirror = false;

	/* If no ports have any mirrors active, clear the monitor port */
	for (i = 0; i < ds->num_ports; i++) {
		if (priv->ports[i].ingress_mirror ||
		    priv->ports[i].egress_mirror) {
			active = true;
			break;
		}
	}

	if (active)
		return;

	ret = MXL862XX_API_WRITE(priv, MXL862XX_COMMON_MONITORPORTCFGSET, mon);
	if (ret)
		dev_err(ds->dev, "mirror: failed to clear monitor port: %pe\n",
			ERR_PTR(ret));

	priv->mirror_dest = -1;
}

/**
 * mxl862xx_mac_portmap_add - Set port bits in a MAC table entry's portmap
 * @priv: driver private data
 * @addr: MAC address
 * @fid: firmware FID
 * @vid: VLAN ID
 * @add_map: firmware-format portmap of bits to set
 *
 * Queries the existing MAC table entry by {addr, fid, vid}. If found,
 * the existing portmap is preserved and @add_map bits are OR'd in.
 * The entry is then written back as a static portmap-mode entry.
 */
static int mxl862xx_mac_portmap_add(struct mxl862xx_priv *priv,
				    const unsigned char *addr,
				    u16 fid, u16 vid,
				    const __le16 *add_map)
{
	struct mxl862xx_mac_table_query qparam = {};
	struct mxl862xx_mac_table_add aparam = {};
	int i, ret;

	ether_addr_copy(qparam.mac, addr);
	qparam.fid = cpu_to_le16(fid);
	qparam.tci = cpu_to_le16(FIELD_PREP(MXL862XX_TCI_VLAN_ID, vid));

	ret = MXL862XX_API_READ(priv, MXL862XX_MAC_TABLEENTRYQUERY, qparam);
	if (ret)
		return ret;

	ether_addr_copy(aparam.mac, addr);
	aparam.fid = cpu_to_le16(fid);
	aparam.tci = cpu_to_le16(FIELD_PREP(MXL862XX_TCI_VLAN_ID, vid));
	aparam.static_entry = true;
	aparam.port_id = cpu_to_le32(MXL862XX_PORTMAP_FLAG);

	if (qparam.found)
		memcpy(aparam.port_map, qparam.port_map,
		       sizeof(aparam.port_map));

	for (i = 0; i < ARRAY_SIZE(aparam.port_map); i++)
		aparam.port_map[i] |= add_map[i];

	return MXL862XX_API_WRITE(priv, MXL862XX_MAC_TABLEENTRYADD, aparam);
}

/**
 * mxl862xx_mac_portmap_del - Clear port bits from a MAC table entry's portmap
 * @priv: driver private data
 * @addr: MAC address
 * @fid: firmware FID
 * @vid: VLAN ID
 * @del_map: firmware-format portmap of bits to clear
 *
 * Queries the existing MAC table entry. If not found, returns 0.
 * Clears all @del_map bits from the portmap. If the portmap becomes
 * empty, the entry is removed entirely; otherwise it is updated.
 */
static int mxl862xx_mac_portmap_del(struct mxl862xx_priv *priv,
				    const unsigned char *addr,
				    u16 fid, u16 vid,
				    const __le16 *del_map)
{
	struct mxl862xx_mac_table_remove rparam = {};
	struct mxl862xx_mac_table_query qparam = {};
	struct mxl862xx_mac_table_add aparam = {};
	int i, ret;

	ether_addr_copy(qparam.mac, addr);
	qparam.fid = cpu_to_le16(fid);
	qparam.tci = cpu_to_le16(FIELD_PREP(MXL862XX_TCI_VLAN_ID, vid));

	ret = MXL862XX_API_READ(priv, MXL862XX_MAC_TABLEENTRYQUERY, qparam);
	if (ret)
		return ret;

	if (!qparam.found)
		return 0;

	for (i = 0; i < ARRAY_SIZE(qparam.port_map); i++)
		qparam.port_map[i] &= ~del_map[i];

	if (mxl862xx_fw_portmap_is_empty(qparam.port_map)) {
		ether_addr_copy(rparam.mac, addr);
		rparam.fid = cpu_to_le16(fid);
		rparam.tci = cpu_to_le16(FIELD_PREP(MXL862XX_TCI_VLAN_ID,
						     vid));
		return MXL862XX_API_WRITE(priv, MXL862XX_MAC_TABLEENTRYREMOVE,
					  rparam);
	}

	ether_addr_copy(aparam.mac, addr);
	aparam.fid = cpu_to_le16(fid);
	aparam.tci = cpu_to_le16(FIELD_PREP(MXL862XX_TCI_VLAN_ID, vid));
	aparam.static_entry = true;
	aparam.port_id = cpu_to_le32(MXL862XX_PORTMAP_FLAG);
	memcpy(aparam.port_map, qparam.port_map, sizeof(aparam.port_map));

	return MXL862XX_API_WRITE(priv, MXL862XX_MAC_TABLEENTRYADD, aparam);
}

/**
 * mxl862xx_mac_add_host_bridge - Install a host FDB/MDB entry with VBP portmap
 * @ds: DSA switch
 * @addr: MAC address
 * @vid: VLAN ID
 * @bridge: bridge whose members' VBPs to include
 *
 * In tag_8021q mode, host FDB/MDB entries in a shared bridge FID must use
 * portmap mode targeting ALL bridge members' virtual bridge ports (VBPs).
 * The firmware ANDs the entry's portmap with each ingress port's
 * bridge_port_map, which contains only that port's own VBP. This
 * selects the correct VBP per ingress port, ensuring frames exit
 * through the right egress EVLAN (which inserts the per-port management
 * VID that identifies the source port to DSA on the CPU side).
 */
static int mxl862xx_mac_add_host_bridge(struct dsa_switch *ds,
					const unsigned char *addr, u16 vid,
					const struct dsa_bridge *bridge)
{
	__le16 add_map[MXL862XX_FW_PORTMAP_WORDS] = {};
	struct mxl862xx_priv *priv = ds->priv;
	u16 fid = priv->bridges[bridge->num];
	struct dsa_port *member_dp;

	dsa_switch_for_each_bridge_member(member_dp, ds, bridge->dev)
		mxl862xx_fw_portmap_set_bit(add_map,
					    priv->ports[member_dp->index].bridge_port_cpu);

	return mxl862xx_mac_portmap_add(priv, addr, fid, vid, add_map);
}

/**
 * mxl862xx_mac_del_host_bridge - Remove VBP bits from a host FDB/MDB entry
 * @ds: DSA switch
 * @addr: MAC address
 * @vid: VLAN ID
 * @bridge: bridge whose members' VBPs to clear
 *
 * Clears all bridge member VBP bits from the portmap. If the portmap
 * becomes empty (no user-port bits remain), removes the entry entirely.
 */
static int mxl862xx_mac_del_host_bridge(struct dsa_switch *ds,
					const unsigned char *addr, u16 vid,
					const struct dsa_bridge *bridge)
{
	__le16 del_map[MXL862XX_FW_PORTMAP_WORDS] = {};
	struct mxl862xx_priv *priv = ds->priv;
	u16 fid = priv->bridges[bridge->num];
	struct dsa_port *member_dp;

	dsa_switch_for_each_bridge_member(member_dp, ds, bridge->dev)
		mxl862xx_fw_portmap_set_bit(del_map,
					    priv->ports[member_dp->index].bridge_port_cpu);

	return mxl862xx_mac_portmap_del(priv, addr, fid, vid, del_map);
}

static int mxl862xx_host_mac_resync_cb(struct dsa_switch *ds,
				       const unsigned char *addr, u16 vid,
				       const struct dsa_db *db, void *ctx)
{
	return mxl862xx_mac_add_host_bridge(ds, addr, vid, &db->bridge);
}

static int mxl862xx_host_mac_drop_vbp_cb(struct dsa_switch *ds,
					 const unsigned char *addr, u16 vid,
					 const struct dsa_db *db, void *ctx)
{
	__le16 del_map[MXL862XX_FW_PORTMAP_WORDS] = {};
	struct mxl862xx_priv *priv = ds->priv;
	int leaving = *(const int *)ctx;
	u16 fid = priv->bridges[db->bridge.num];
	u16 vbp = priv->ports[leaving].bridge_port_cpu;

	if (!fid || !vbp)
		return 0;

	mxl862xx_fw_portmap_set_bit(del_map, vbp);

	return mxl862xx_mac_portmap_del(priv, addr, fid, vid, del_map);
}

/**
 * mxl862xx_lag_master_port - Find the LAG master (lowest-numbered member)
 * @ds: DSA switch
 * @lag: LAG to search
 *
 * The master's bridge port hosts the P-mapper and receives all ingress
 * traffic via CTP redirection from other members.
 *
 * Return: port index of the master, or -ENOENT if no members.
 */
static int mxl862xx_lag_master_port(struct dsa_switch *ds,
				    const struct dsa_lag *lag)
{
	struct dsa_port *dp;
	int master = -ENOENT;

	dsa_lag_foreach_port(dp, ds->dst, lag) {
		if (dp->ds != ds)
			continue;
		if (master < 0 || dp->index < master)
			master = dp->index;
	}

	return master;
}

/**
 * mxl862xx_lag_hash_bits - Translate Linux hash mode to firmware hash bitmask
 * @info: bonding upper info (tx_type + hash_type)
 *
 * Return: 6-bit hash field bitmask (MXL862XX_TRUNK_HASH_*), or negative
 *         errno if the mode is unsupported.
 */
static int mxl862xx_lag_hash_bits(const struct netdev_lag_upper_info *info)
{
	if (info->tx_type != NETDEV_LAG_TX_TYPE_HASH)
		return 0;

	switch (info->hash_type) {
	case NETDEV_LAG_HASH_L2:
		return MXL862XX_TRUNK_HASH_SA | MXL862XX_TRUNK_HASH_DA;
	case NETDEV_LAG_HASH_L34:
		return MXL862XX_TRUNK_HASH_SIP | MXL862XX_TRUNK_HASH_DIP |
		       MXL862XX_TRUNK_HASH_SPORT | MXL862XX_TRUNK_HASH_DPORT;
	case NETDEV_LAG_HASH_L23:
	case NETDEV_LAG_HASH_E23:
		return MXL862XX_TRUNK_HASH_SA | MXL862XX_TRUNK_HASH_DA |
		       MXL862XX_TRUNK_HASH_SIP | MXL862XX_TRUNK_HASH_DIP;
	case NETDEV_LAG_HASH_E34:
		return MXL862XX_TRUNK_HASH_SA | MXL862XX_TRUNK_HASH_DA |
		       MXL862XX_TRUNK_HASH_SIP | MXL862XX_TRUNK_HASH_DIP |
		       MXL862XX_TRUNK_HASH_SPORT | MXL862XX_TRUNK_HASH_DPORT;
	default:
		return -EOPNOTSUPP;
	}
}

/**
 * mxl862xx_lag_set_hash - Push trunk hash configuration to firmware
 * @priv: driver private data
 * @hash_bits: 6-bit hash field bitmask (MXL862XX_TRUNK_HASH_*)
 *
 * Only issues a firmware command when @hash_bits differs from the
 * currently active configuration.
 */
static int mxl862xx_lag_set_hash(struct mxl862xx_priv *priv, u8 hash_bits)
{
	struct mxl862xx_trunking_cfg cfg = {};

	if (priv->trunk_hash == hash_bits)
		return 0;

	cfg.mac_src  = !!(hash_bits & MXL862XX_TRUNK_HASH_SA);
	cfg.mac_dst  = !!(hash_bits & MXL862XX_TRUNK_HASH_DA);
	cfg.ip_src   = !!(hash_bits & MXL862XX_TRUNK_HASH_SIP);
	cfg.ip_dst   = !!(hash_bits & MXL862XX_TRUNK_HASH_DIP);
	cfg.src_port = !!(hash_bits & MXL862XX_TRUNK_HASH_SPORT);
	cfg.dst_port = !!(hash_bits & MXL862XX_TRUNK_HASH_DPORT);

	priv->trunk_hash = hash_bits;

	return MXL862XX_API_WRITE(priv, MXL862XX_TRUNKING_CFGSET, cfg);
}

/**
 * mxl862xx_lag_recompute_hash - Recompute global hash from all active LAGs
 * @ds: DSA switch
 *
 * Scans all ports and ORs together the stored hash requirements of every
 * active LAG member. Used after a LAG is destroyed to potentially narrow
 * the global hash configuration.
 *
 * Return: union of all active LAGs' hash field bitmasks.
 */
static u8 mxl862xx_lag_recompute_hash(struct dsa_switch *ds)
{
	struct mxl862xx_priv *priv = ds->priv;
	u8 hash = 0;
	int port;

	for (port = 0; port < ds->num_ports; port++) {
		if (priv->ports[port].lag)
			hash |= priv->ports[port].lag_hash_bits;
	}

	return hash;
}

/**
 * mxl862xx_lag_build_pmapper - Fill P-mapper with round-robin LAG distribution
 * @ds: DSA switch
 * @lag: LAG group
 * @pm: P-mapper struct to fill (entries 9..72)
 *
 * Only ports with lag_tx_enabled are included. Falls back to the
 * master port if no members are active.
 */
static void mxl862xx_lag_build_pmapper(struct dsa_switch *ds,
				       const struct dsa_lag *lag,
				       struct mxl862xx_pmapper *pm)
{
	struct mxl862xx_priv *priv = ds->priv;
	int active_ports[MXL862XX_MAX_PORTS];
	int n_active = 0, master, port;
	struct dsa_port *dp;
	int i;

	dsa_lag_foreach_port(dp, ds->dst, lag) {
		if (dp->ds != ds)
			continue;
		if (priv->ports[dp->index].lag_tx_enabled)
			active_ports[n_active++] = dp->index;
	}

	/* Fallback: if no members are active, use the master port */
	if (!n_active) {
		master = mxl862xx_lag_master_port(ds, lag);

		if (master >= 0) {
			active_ports[0] = master;
			n_active = 1;
		}
	}

	if (!n_active)
		return;

	for (i = 0; i < MXL862XX_PMAPPER_LAG_COUNT; i++) {
		port = active_ports[i % n_active];

		pm->dest_sub_if_id_group[MXL862XX_PMAPPER_LAG_FIRST + i] =
			(port << 4) & 0xff;
	}
}

/**
 * mxl862xx_lag_redirect_ctp - Redirect a port's CTP to the LAG master
 * @priv: driver private data
 * @port: port whose CTP to redirect
 * @master_port: LAG master port index
 */
static int mxl862xx_lag_redirect_ctp(struct mxl862xx_priv *priv,
				     int port, int master_port)
{
	struct mxl862xx_ctp_port_config ctp = {};

	ctp.logical_port_id = port;
	ctp.mask = cpu_to_le32(MXL862XX_CTP_PORT_CONFIG_MASK_BRIDGE_PORT_ID);
	ctp.bridge_port_id = cpu_to_le16(master_port);

	return MXL862XX_API_WRITE(priv, MXL862XX_CTP_PORTCONFIGSET, ctp);
}

/**
 * mxl862xx_lag_restore_ctp - Restore a port's CTP to point to itself
 * @priv: driver private data
 * @port: port whose CTP to restore
 */
static int mxl862xx_lag_restore_ctp(struct mxl862xx_priv *priv, int port)
{
	return mxl862xx_lag_redirect_ctp(priv, port, port);
}

/**
 * mxl862xx_lag_disable_pmapper - Disable P-mapper on a bridge port
 * @ds: DSA switch
 * @bp_id: firmware bridge port ID to reconfigure
 */
static int mxl862xx_lag_disable_pmapper(struct dsa_switch *ds, u16 bp_id)
{
	struct mxl862xx_bridge_port_config bp_cfg = {};
	struct mxl862xx_priv *priv = ds->priv;

	bp_cfg.bridge_port_id = cpu_to_le16(bp_id);
	bp_cfg.mask = cpu_to_le32(
		MXL862XX_BRIDGE_PORT_CONFIG_MASK_EGRESS_CTP_MAPPING);
	bp_cfg.dest_logical_port_id = bp_id;
	bp_cfg.pmapper_enable = 0;

	return MXL862XX_API_WRITE(priv, MXL862XX_BRIDGEPORT_CONFIGSET, bp_cfg);
}

/**
 * mxl862xx_lag_sync - Synchronize LAG hardware state for a LAG group
 * @ds: DSA switch
 * @lag: LAG group to synchronize
 *
 * Finds the master (lowest-numbered member), redirects all member CTPs
 * to the LAG's dedicated firmware bridge port, configures the P-mapper
 * for hash distribution, and pushes the master's full bridge port
 * configuration (EVLAN, VF, portmap, learning) to the LAG bridge port.
 */
static int mxl862xx_lag_sync(struct dsa_switch *ds, const struct dsa_lag *lag)
{
	struct mxl862xx_bridge_port_config bp_cfg = {};
	struct mxl862xx_priv *priv = ds->priv;
	struct mxl862xx_pmapper pm = {};
	struct dsa_port *dp;
	int master, ret;
	u16 lag_bp;

	lag_bp = priv->lag_bridge_ports[lag->id];
	if (!lag_bp)
		return -ENOENT;

	master = mxl862xx_lag_master_port(ds, lag);
	if (master < 0)
		return master;

	/* Redirect all member CTPs to the LAG bridge port */
	dsa_lag_foreach_port(dp, ds->dst, lag) {
		if (dp->ds != ds)
			continue;
		ret = mxl862xx_lag_redirect_ctp(priv, dp->index, lag_bp);
		if (ret)
			return ret;
	}

	/* Push the master's full config to the LAG bridge port so it
	 * inherits the current bridge_id, EVLAN/VF blocks, portmap,
	 * learning and flood settings.
	 */
	ret = __mxl862xx_set_bridge_port(ds, master, lag_bp);
	if (ret)
		return ret;

	/* Build P-mapper with active members */
	mxl862xx_lag_build_pmapper(ds, lag, &pm);

	/* Enable P-mapper in LAG mode on the LAG bridge port */
	bp_cfg.bridge_port_id = cpu_to_le16(lag_bp);
	bp_cfg.mask = cpu_to_le32(
		MXL862XX_BRIDGE_PORT_CONFIG_MASK_EGRESS_CTP_MAPPING);
	bp_cfg.dest_logical_port_id = master;
	bp_cfg.pmapper_enable = 1;
	bp_cfg.pmapper_mapping_mode =
		cpu_to_le32(MXL862XX_PMAPPER_MAPPING_LAG);
	bp_cfg.pmapper = pm;

	return MXL862XX_API_WRITE(priv, MXL862XX_BRIDGEPORT_CONFIGSET, bp_cfg);
}

static int mxl862xx_port_lag_join(struct dsa_switch *ds, int port,
				  const struct dsa_lag lag,
				  struct netdev_lag_upper_info *info,
				  struct netlink_ext_ack *extack)
{
	struct mxl862xx_bridge_port_alloc bp_alloc = {};
	struct mxl862xx_priv *priv = ds->priv;
	struct dsa_port *dp = dsa_to_port(ds, port);
	int hash_bits;
	u8 new_hash;
	int ret;

	if (dsa_is_cpu_port(ds, port)) {
		NL_SET_ERR_MSG_MOD(extack, "CPU port LAG not supported");
		return -EOPNOTSUPP;
	}

	if (info->tx_type != NETDEV_LAG_TX_TYPE_HASH &&
	    info->tx_type != NETDEV_LAG_TX_TYPE_ACTIVEBACKUP) {
		NL_SET_ERR_MSG_MOD(extack, "Only hash and active-backup LAG modes supported");
		return -EOPNOTSUPP;
	}

	hash_bits = mxl862xx_lag_hash_bits(info);
	if (hash_bits < 0) {
		NL_SET_ERR_MSG_MOD(extack, "Unsupported LAG hash mode");
		return hash_bits;
	}

	/* Allocate a dedicated firmware bridge port for this LAG on
	 * first member join. This bridge port is stable for the
	 * LAG's lifetime -- all CTP redirections, FDB and MDB entries
	 * target it, so no migration is needed on membership changes.
	 */
	if (!priv->lag_bridge_ports[lag.id]) {
		ret = MXL862XX_API_READ(priv, MXL862XX_BRIDGEPORT_ALLOC,
					bp_alloc);
		if (ret) {
			NL_SET_ERR_MSG_MOD(extack,
					   "Failed to allocate LAG bridge port");
			return ret;
		}
		priv->lag_bridge_ports[lag.id] =
			le16_to_cpu(bp_alloc.bridge_port_id);
	}

	priv->ports[port].lag = dp->lag;
	priv->ports[port].lag_tx_enabled = dp->lag_tx_enabled;
	priv->ports[port].lag_hash_bits = hash_bits;

	/* Widen global hash to include this LAG's requirements */
	new_hash = priv->trunk_hash | hash_bits;
	ret = mxl862xx_lag_set_hash(priv, new_hash);
	if (ret)
		goto err_undo;

	ret = mxl862xx_lag_sync(ds, dp->lag);
	if (ret)
		goto err_undo;

	return 0;

err_undo:
	priv->ports[port].lag = NULL;
	priv->ports[port].lag_tx_enabled = false;
	priv->ports[port].lag_hash_bits = 0;
	return ret;
}

static int mxl862xx_port_lag_leave(struct dsa_switch *ds, int port,
				   const struct dsa_lag lag)
{
	struct mxl862xx_bridge_port_alloc bp_alloc = {};
	struct mxl862xx_priv *priv = ds->priv;
	u8 new_hash;
	int ret;

	/* Restore this port's CTP to point to itself */
	ret = mxl862xx_lag_restore_ctp(priv, port);
	if (ret)
		dev_err(ds->dev, "failed to restore CTP for port %d: %pe\n",
			port, ERR_PTR(ret));

	priv->ports[port].lag = NULL;
	priv->ports[port].lag_tx_enabled = false;
	priv->ports[port].lag_hash_bits = 0;

	/* If other members remain, re-sync the LAG */
	if (mxl862xx_lag_master_port(ds, &lag) >= 0) {
		ret = mxl862xx_lag_sync(ds, &lag);
		if (ret)
			dev_err(ds->dev,
				"failed to re-sync LAG after port %d left: %pe\n",
				port, ERR_PTR(ret));
	} else if (priv->lag_bridge_ports[lag.id]) {
		/* Last member left -- disable P-mapper and free the
		 * LAG's dedicated bridge port.
		 */
		ret = mxl862xx_lag_disable_pmapper(ds,
						   priv->lag_bridge_ports[lag.id]);
		if (ret)
			dev_err(ds->dev,
				"failed to disable P-mapper on LAG bridge port %u: %pe\n",
				priv->lag_bridge_ports[lag.id], ERR_PTR(ret));

		bp_alloc.bridge_port_id =
			cpu_to_le16(priv->lag_bridge_ports[lag.id]);
		ret = MXL862XX_API_WRITE(priv, MXL862XX_BRIDGEPORT_FREE,
					 bp_alloc);
		if (ret)
			dev_err(ds->dev,
				"failed to free LAG bridge port %u: %pe\n",
				priv->lag_bridge_ports[lag.id], ERR_PTR(ret));

		priv->lag_bridge_ports[lag.id] = 0;
	}

	/* Recompute global hash from remaining LAGs */
	new_hash = mxl862xx_lag_recompute_hash(ds);
	ret = mxl862xx_lag_set_hash(priv, new_hash);
	if (ret)
		dev_err(ds->dev, "failed to update trunk hash: %pe\n",
			ERR_PTR(ret));

	return 0;
}

static int mxl862xx_port_lag_change(struct dsa_switch *ds, int port)
{
	struct mxl862xx_priv *priv = ds->priv;
	struct dsa_port *dp = dsa_to_port(ds, port);

	if (!priv->ports[port].lag)
		return 0;

	priv->ports[port].lag_tx_enabled = dp->lag_tx_enabled;

	return mxl862xx_lag_sync(ds, priv->ports[port].lag);
}

static int mxl862xx_port_bridge_join(struct dsa_switch *ds, int port,
				     const struct dsa_bridge bridge,
				     bool *tx_fwd_offload,
				     struct netlink_ext_ack *extack)
{
	struct mxl862xx_priv *priv = ds->priv;
	int ret;

	if (!priv->bridges[bridge.num]) {
		ret = mxl862xx_allocate_bridge(priv);
		if (ret < 0)
			return ret;

		priv->bridges[bridge.num] = ret;

		/* Free bridge here on error, DSA rollback won't. */
		ret = mxl862xx_sync_bridge_members(ds, &bridge);
		if (ret) {
			mxl862xx_free_bridge(ds, &bridge);
			return ret;
		}

		return 0;
	}

	ret = mxl862xx_sync_bridge_members(ds, &bridge);
	if (ret)
		return ret;

	/* DSA refcount-bumps existing bridge-scoped host entries on each
	 * member join, so ->port_fdb_add is not re-invoked and the new
	 * member's VBP would be missing from each entry's portmap.
	 */
	if (priv->tag_proto == DSA_TAG_PROTO_MXL862_8021Q) {
		ret = dsa_switch_for_each_bridge_host_fdb(ds, &bridge,
							  mxl862xx_host_mac_resync_cb,
							  NULL);
		if (ret)
			return ret;

		ret = dsa_switch_for_each_bridge_host_mdb(ds, &bridge,
							  mxl862xx_host_mac_resync_cb,
							  NULL);
		if (ret)
			return ret;
	}

	/* If this port is in a LAG, re-sync the LAG bridge port so it
	 * picks up the new bridge_id (switching from standalone FID to
	 * the shared bridge FID).
	 */
	if (priv->ports[port].lag)
		ret = mxl862xx_lag_sync(ds, priv->ports[port].lag);

	return ret;
}

static void mxl862xx_port_bridge_leave(struct dsa_switch *ds, int port,
				       const struct dsa_bridge bridge)
{
	struct mxl862xx_priv *priv = ds->priv;
	struct mxl862xx_port *p = &priv->ports[port];
	int err;

	/* Counterpart of the replay in port_bridge_join: DSA keeps the
	 * shared entry alive until the last member leaves, so the leaving
	 * member's bit must be pruned from each entry's portmap here.
	 */
	if (priv->tag_proto == DSA_TAG_PROTO_MXL862_8021Q) {
		dsa_switch_for_each_bridge_host_fdb(ds, &bridge,
						    mxl862xx_host_mac_drop_vbp_cb,
						    &port);
		dsa_switch_for_each_bridge_host_mdb(ds, &bridge,
						    mxl862xx_host_mac_drop_vbp_cb,
						    &port);
	}

	err = mxl862xx_sync_bridge_members(ds, &bridge);
	if (err)
		dev_err(ds->dev,
			"failed to sync bridge members after port %d left: %pe\n",
			port, ERR_PTR(err));

	/* Revert leaving port, omitted by the sync above, to its
	 * single-port bridge state. Egress EVLAN is reprogrammed below
	 * -- in tag_8021q mode it gets management VID strip catchalls,
	 * in SpTag mode it is cleared.
	 *
	 * Do NOT clear the VF VID list here. Bridge VLANs are already
	 * removed by port_vlan_del during the switchdev replay in
	 * dsa_port_pre_bridge_leave. The remaining VIDs (e.g. the
	 * tag_8021q management VID) must survive bridge leave.
	 */
	p->pvid = 0;
	p->ingress_evlan.in_use = false;

	err = mxl862xx_evlan_program_egress(priv, port);
	if (err)
		dev_err(ds->dev,
			"failed to restore egress EVLAN on port %d: %pe\n",
			port, ERR_PTR(err));

	/* Push the complete standalone port state to firmware. The
	 * firmware compares old vs new EVLAN/VF enable flags and adjusts
	 * block refcounts accordingly, so a single call suffices.
	 */
	err = mxl862xx_set_bridge_port(ds, port);
	if (err)
		dev_err(ds->dev,
			"failed to update bridge port %d state: %pe\n", port,
			ERR_PTR(err));

	err = mxl862xx_set_cpu_vbp(ds, port);
	if (err)
		dev_err(ds->dev,
			"failed to update CPU VBP for port %d: %pe\n", port,
			ERR_PTR(err));

	/* If this port is in a LAG, re-sync the LAG bridge port so it
	 * reverts to the standalone FID.
	 */
	if (p->lag) {
		err = mxl862xx_lag_sync(ds, p->lag);
		if (err)
			dev_err(ds->dev,
				"failed to re-sync LAG after port %d left bridge: %pe\n",
				port, ERR_PTR(err));
	}

	if (!dsa_bridge_ports(ds, bridge.dev))
		mxl862xx_free_bridge(ds, &bridge);
}

static int mxl862xx_setup_virtual_bridge_port(struct dsa_switch *ds, int port)
{
	struct mxl862xx_bridge_port_alloc bp_alloc = {};
	struct mxl862xx_bridge_port_config bp_cfg = {};
	struct mxl862xx_priv *priv = ds->priv;
	struct dsa_port *cpu_dp;
	int ret;

	cpu_dp = dsa_to_port(ds, port)->cpu_dp;

	ret = MXL862XX_API_READ(priv, MXL862XX_BRIDGEPORT_ALLOC, bp_alloc);
	if (ret) {
		dev_err(ds->dev,
			"failed to allocate virtual bridge port for port %d: %pe\n",
			port, ERR_PTR(ret));
		return ret;
	}

	priv->ports[port].bridge_port_cpu = le16_to_cpu(bp_alloc.bridge_port_id);

	bp_cfg.bridge_port_id = bp_alloc.bridge_port_id;
	bp_cfg.mask = cpu_to_le32(
		MXL862XX_BRIDGE_PORT_CONFIG_MASK_BRIDGE_ID |
		MXL862XX_BRIDGE_PORT_CONFIG_MASK_BRIDGE_PORT_MAP |
		MXL862XX_BRIDGE_PORT_CONFIG_MASK_MC_SRC_MAC_LEARNING |
		MXL862XX_BRIDGE_PORT_CONFIG_MASK_EGRESS_CTP_MAPPING);
	bp_cfg.bridge_id = cpu_to_le16(priv->ports[port].fid);
	bp_cfg.src_mac_learning_disable = 1;
	bp_cfg.dest_logical_port_id = cpu_dp->index;
	mxl862xx_fw_portmap_set_bit(bp_cfg.bridge_port_map, port);

	ret = MXL862XX_API_WRITE(priv, MXL862XX_BRIDGEPORT_CONFIGSET, bp_cfg);
	if (ret)
		dev_err(ds->dev,
			"failed to configure virtual bridge port %u for port %d: %pe\n",
			priv->ports[port].bridge_port_cpu, port, ERR_PTR(ret));

	return ret;
}

static void mxl862xx_free_virtual_bridge_port(struct dsa_switch *ds, int port)
{
	struct mxl862xx_bridge_port_alloc bp_alloc = {};
	struct mxl862xx_priv *priv = ds->priv;
	int ret;

	if (!priv->ports[port].bridge_port_cpu)
		return;

	mxl862xx_tag_8021q_disable_cpu_egress(ds, port);

	bp_alloc.bridge_port_id = cpu_to_le16(priv->ports[port].bridge_port_cpu);
	ret = MXL862XX_API_WRITE(priv, MXL862XX_BRIDGEPORT_FREE, bp_alloc);
	if (ret)
		dev_err(ds->dev,
			"failed to free virtual bridge port %u for port %d: %pe\n",
			priv->ports[port].bridge_port_cpu, port, ERR_PTR(ret));
	else
		priv->ports[port].bridge_port_cpu = 0;
}

static int mxl862xx_setup_tag_8021q(struct dsa_switch *ds)
{
	struct dsa_port *dp;
	int ret;

	dsa_switch_for_each_user_port(dp, ds) {
		ret = mxl862xx_setup_virtual_bridge_port(ds, dp->index);
		if (ret)
			return ret;
	}

	return 0;
}

static void mxl862xx_teardown_tag_8021q(struct dsa_switch *ds)
{
	struct mxl862xx_priv *priv = ds->priv;
	struct dsa_port *dp;
	int cpu;

	dsa_switch_for_each_user_port(dp, ds) {
		mxl862xx_free_virtual_bridge_port(ds, dp->index);
		priv->ports[dp->index].tag_8021q_vid = 0;
	}

	/* Disable CPU port EVLAN engine and clear VF VID entries.
	 * The HW blocks stay allocated (freed in port_teardown).
	 */
	dsa_switch_for_each_cpu_port(dp, ds) {
		cpu = dp->index;

		priv->ports[cpu].ingress_evlan.in_use = false;
		mxl862xx_set_cpu_ctp_ingress_evlan(ds, cpu);
		mxl862xx_vf_clear_vids(priv, &priv->ports[cpu].vf);
	}

}

/**
 * mxl862xx_tag_8021q_program_cpu_egress - Program virtual bridge port egress EVLAN
 * @ds: DSA switch
 * @port: user port whose virtual bridge port needs programming
 *
 * Programs the egress EVLAN block on the virtual bridge port associated
 * with @port. The block is pre-allocated in port_setup. The rules insert the
 * port's tag_8021q management VID as an outer 802.1Q tag on all
 * frames exiting toward the CPU through this virtual bridge port.
 */
static int mxl862xx_tag_8021q_program_cpu_egress(struct dsa_switch *ds,
						  int port)
{
	struct mxl862xx_priv *priv = ds->priv;
	struct mxl862xx_port *p = &priv->ports[port];
	struct mxl862xx_evlan_block *blk = &p->cpu_egress_evlan;
	struct mxl862xx_bridge_port_config bp_cfg = {};
	int n_rules = ARRAY_SIZE(cpu_egress_tag_8021q);
	int i, ret;

	if (!p->bridge_port_cpu || !p->tag_8021q_vid)
		return 0;

	for (i = 0; i < n_rules; i++) {
		ret = mxl862xx_evlan_write_rule(priv, blk->block_id,
						i, &cpu_egress_tag_8021q[i],
						0, false, 0,
						p->tag_8021q_vid);
		if (ret)
			return ret;
	}

	blk->n_active = n_rules;
	blk->in_use = true;

	/* Enable egress EVLAN on the virtual bridge port */
	bp_cfg.bridge_port_id = cpu_to_le16(p->bridge_port_cpu);
	bp_cfg.mask = cpu_to_le32(MXL862XX_BRIDGE_PORT_CONFIG_MASK_EGRESS_VLAN);
	bp_cfg.egress_extended_vlan_enable = 1;
	bp_cfg.egress_extended_vlan_block_id = cpu_to_le16(blk->block_id);
	bp_cfg.egress_extended_vlan_block_size = cpu_to_le16(n_rules);

	return MXL862XX_API_WRITE(priv, MXL862XX_BRIDGEPORT_CONFIGSET, bp_cfg);
}

/**
 * mxl862xx_tag_8021q_cpu_vlan_program - Reprogram CPU port ingress EVLAN
 * @ds: DSA switch
 *
 * Rebuilds the CPU port ingress EVLAN block with reassign rules for
 * every tag_8021q VID currently in use. Called whenever a tag_8021q
 * VID is added or removed.
 *
 * Each user port with a non-zero tag_8021q_vid gets 2 rules:
 *   - outer VID match + inner present: reassign to virtual bridge port
 *   - outer VID match + no inner:      reassign to virtual bridge port
 *
 * The EVLAN block is assigned to the CPU port's CTP (not its bridge
 * port) via CTP_PORTCONFIGSET, matching the reference and legacy
 * driver architecture.
 */
static int mxl862xx_tag_8021q_cpu_vlan_program(struct dsa_switch *ds)
{
	struct mxl862xx_evlan_rule_desc rule;
	struct mxl862xx_priv *priv = ds->priv;
	struct mxl862xx_evlan_block *blk;
	struct dsa_port *cpu_dp, *dp;
	struct mxl862xx_port *p;
	u16 idx, old_active, vid;
	int cpu, ret, i;

	dsa_switch_for_each_cpu_port(cpu_dp, ds)
		break;

	cpu = cpu_dp->index;
	blk = &priv->ports[cpu].ingress_evlan;

	old_active = blk->n_active;
	idx = 0;

	dsa_switch_for_each_user_port(dp, ds) {
		p = &priv->ports[dp->index];
		vid = p->tag_8021q_vid;

		if (!vid)
			continue;

		for (i = 0; i < ARRAY_SIZE(cpu_ingress_reassign); i++) {
			rule = cpu_ingress_reassign[i];

			rule.bridge_port_id = p->bridge_port_cpu;
			ret = mxl862xx_evlan_write_rule(priv, blk->block_id,
							idx++, &rule, vid,
							false, 0, 0);
			if (ret)
				return ret;
		}
	}

	blk->n_active = idx;

	/* Deactivate stale entries beyond the new active range */
	for (; idx < old_active; idx++) {
		ret = mxl862xx_evlan_deactivate_entry(priv, blk->block_id,
						      idx);
		if (ret)
			return ret;
	}
	blk->in_use = blk->n_active > 0;

	return mxl862xx_set_cpu_ctp_ingress_evlan(ds, cpu);
}

static int mxl862xx_tag_8021q_cpu_vlan_add(struct dsa_switch *ds, int port,
					   u16 vid)
{
	struct mxl862xx_priv *priv = ds->priv;
	int ret;

	/* Add VID to CPU port's VF block so firmware accepts frames
	 * tagged with this VID on CPU port ingress.
	 */
	ret = mxl862xx_vf_add_vid(priv, &priv->ports[port].vf, vid, false);
	if (ret)
		return ret;

	return mxl862xx_tag_8021q_cpu_vlan_program(ds);
}

static int mxl862xx_tag_8021q_cpu_vlan_del(struct dsa_switch *ds, int port,
					   u16 vid)
{
	struct mxl862xx_priv *priv = ds->priv;
	int ret;

	ret = mxl862xx_vf_del_vid(priv, &priv->ports[port].vf, vid);
	if (ret)
		return ret;

	return mxl862xx_tag_8021q_cpu_vlan_program(ds);
}

static int mxl862xx_tag_8021q_vlan_add(struct dsa_switch *ds, int port,
				       u16 vid, u16 flags)
{
	struct mxl862xx_priv *priv = ds->priv;
	int ret;

	if (dsa_is_cpu_port(ds, port))
		return mxl862xx_tag_8021q_cpu_vlan_add(ds, port, vid);

	/* User port: store the tag_8021q VID and add to VF block */
	priv->ports[port].tag_8021q_vid = vid;

	ret = mxl862xx_vf_add_vid(priv, &priv->ports[port].vf, vid, false);
	if (ret)
		return ret;

	ret = mxl862xx_tag_8021q_program_cpu_egress(ds, port);
	if (ret)
		return ret;

	/* Rebuild CPU ingress EVLAN to include this port's management VID.
	 * The DSA framework may call the CPU port's tag_8021q_vlan_add
	 * before this user port's callback (ports iterate in index order),
	 * so the CPU ingress EVLAN rebuild triggered by the CPU callback
	 * might have run before tag_8021q_vid was set. Rebuild now to
	 * ensure this port's reassignment rule is present.
	 */
	return mxl862xx_tag_8021q_cpu_vlan_program(ds);
}

static int mxl862xx_tag_8021q_vlan_del(struct dsa_switch *ds, int port,
				       u16 vid)
{
	struct mxl862xx_priv *priv = ds->priv;

	if (dsa_is_cpu_port(ds, port))
		return mxl862xx_tag_8021q_cpu_vlan_del(ds, port, vid);

	if (priv->ports[port].tag_8021q_vid == vid) {
		priv->ports[port].tag_8021q_vid = 0;
		mxl862xx_tag_8021q_disable_cpu_egress(ds, port);
	}

	return mxl862xx_vf_del_vid(priv, &priv->ports[port].vf, vid);
}

/**
 * mxl862xx_refresh_cpu_targets - Update bridge ports and traps for new CPU target
 * @ds: DSA switch
 *
 * After switching between SpTag and tag_8021q, the CPU-side target in
 * each user port's bridge port map changes (physical CPU port vs. virtual
 * bridge port). Reinstalls bridge port config and link-local PCE traps.
 */
static int mxl862xx_refresh_cpu_targets(struct dsa_switch *ds)
{
	struct mxl862xx_priv *priv = ds->priv;
	struct dsa_port *dp;
	int ret, port;

	dsa_switch_for_each_user_port(dp, ds) {
		port = dp->index;

		/* Reprogram user port egress EVLAN to add or remove the
		 * tag_8021q management VID strip catchalls.
		 */
		ret = mxl862xx_evlan_program_egress(priv, port);
		if (ret)
			return ret;

		ret = mxl862xx_set_bridge_port(ds, port);
		if (ret)
			return ret;

		ret = mxl862xx_setup_link_local_trap(ds, port);
		if (ret)
			return ret;

		ret = mxl862xx_setup_snooping_traps(ds, port);
		if (ret)
			return ret;
	}

	return 0;
}

/**
 * mxl862xx_complete_tag_8021q_setup - Finish deferred tag_8021q initialization
 * @ds: DSA switch
 *
 * Called from change_tag_protocol() to configure the firmware for
 * tag_8021q mode. Requires each user port to already have an FID
 * (from add_single_port_bridge in setup()). Reconfigures CPU ports,
 * allocates virtual bridge ports and enables flooding on standalone
 * bridges. Link-local traps are refreshed separately after
 * dsa_tag_8021q_register() has set cpu_egress_evlan.in_use.
 */
static int mxl862xx_complete_tag_8021q_setup(struct dsa_switch *ds)
{
	struct mxl862xx_priv *priv = ds->priv;
	struct dsa_port *dp;
	int ret, port;

	/* Disable SpTag and reduce to a single CTP on CPU ports for
	 * 8021q mode. Without a special tag the PMAC cannot select a
	 * sub-CTP, so only CTP 0 must exist.
	 */
	dsa_switch_for_each_cpu_port(dp, ds) {
		ret = mxl862xx_configure_sp_tag_proto(ds, dp->index, false);
		if (ret)
			return ret;

		ret = mxl862xx_configure_ctp_port(ds, dp->index,
						  dp->index, 1);
		if (ret)
			return ret;

		ret = mxl862xx_setup_cpu_bridge(ds, dp->index);
		if (ret)
			return ret;
	}

	ret = mxl862xx_setup_tag_8021q(ds);
	if (ret)
		return ret;

	/* In tag_8021q mode TX goes through the bridge engine (CTP
	 * ingress EVLAN reassigns to a virtual bridge port), so
	 * unknown unicast and multicast must be flooded at the bridge
	 * level for frames from the CPU to reach user ports. The
	 * per-port bridges may have been created with flooding
	 * disabled (SpTag mode default), so update them now.
	 *
	 * Block unknown UC and MC on the VBP egress meters so frames
	 * to unknown destinations are not flooded to the host. DSA
	 * core will selectively enable host flooding via
	 * port_set_host_flood when needed (e.g. promisc mode).
	 */
	dsa_switch_for_each_user_port(dp, ds) {
		port = dp->index;

		if (dp->bridge)
			continue;

		ret = mxl862xx_bridge_config_fwd(ds,
						 priv->ports[port].fid,
						 true, true, true);
		if (ret)
			return ret;

		priv->ports[port].host_flood_block =
			BIT(MXL862XX_BRIDGE_PORT_EGRESS_METER_UNKNOWN_UC) |
			BIT(MXL862XX_BRIDGE_PORT_EGRESS_METER_UNKNOWN_MC_IP) |
			BIT(MXL862XX_BRIDGE_PORT_EGRESS_METER_UNKNOWN_MC_NON_IP);

		ret = mxl862xx_set_cpu_vbp(ds, port);
		if (ret)
			return ret;
	}

	return 0;
}

static int mxl862xx_change_tag_protocol(struct dsa_switch *ds,
					enum dsa_tag_protocol proto)
{
	struct mxl862xx_priv *priv = ds->priv;
	enum dsa_tag_protocol old_proto = priv->tag_proto;
	struct dsa_port *dp;
	int ret, port;

	/* Flush all MAC entries on tag protocol change. Host entries
	 * installed via portmap (tag_8021q VBP-based) vs single port_id
	 * (SpTag) are not compatible across modes.
	 */
	if (ds->setup)
		mxl862xx_api_wrap(priv, MXL862XX_MAC_TABLECLEAR,
				  NULL, 0, false, false);

	/* Set tag_proto early so that helpers called below (e.g.
	 * mxl862xx_setup_cpu_bridge) see the target protocol.
	 * Restored on failure.
	 */
	priv->tag_proto = proto;

	switch (proto) {
	case DSA_TAG_PROTO_MXL862:
		if (ds->tag_8021q_ctx) {
			dsa_tag_8021q_unregister(ds);
			mxl862xx_teardown_tag_8021q(ds);

			/* Virtual bridge ports are gone; revert portmaps
			 * and traps to target the physical CPU port.
			 */
			ret = mxl862xx_refresh_cpu_targets(ds);
			if (ret)
				goto err_restore;

			/* Revert standalone bridges to SpTag mode
			 * defaults: discard unknown UC/MC (SpTag TX
			 * bypasses bridge engine) while keeping
			 * broadcast flooding.
			 */
			dsa_switch_for_each_user_port(dp, ds) {
				port = dp->index;

				if (dp->bridge)
					continue;

				mxl862xx_bridge_config_fwd(ds,
							  priv->ports[port].fid,
							  priv->ports[port].host_flood_uc,
							  priv->ports[port].host_flood_mc,
							  true);
			}
		}
		dsa_switch_for_each_cpu_port(dp, ds) {
			ret = mxl862xx_configure_sp_tag_proto(ds, dp->index,
							      true);
			if (ret)
				goto err_restore;

			/* Restore multiple CTPs so the special tag's
			 * sub_if_id can select per-port sub-CTPs.
			 */
			ret = mxl862xx_configure_ctp_port(ds, dp->index,
							  dp->index,
							  32 - dp->index);
			if (ret)
				goto err_restore;

			/* Restore CPU portmap: SpTag mode needs all user
			 * ports in the CPU's bridge_port_map. tag_8021q
			 * mode clears it to prevent FID 0 flooding.
			 */
			ret = mxl862xx_setup_cpu_bridge(ds, dp->index);
			if (ret)
				goto err_restore;
		}
		break;

	case DSA_TAG_PROTO_MXL862_8021Q:
		ret = mxl862xx_complete_tag_8021q_setup(ds);
		if (ret)
			goto err_restore;

		/* RTNL is held by the DSA core when calling
		 * change_tag_protocol(), both during initial setup
		 * and at runtime.
		 */
		ret = dsa_tag_8021q_register(ds, htons(ETH_P_8021Q));
		if (ret) {
			mxl862xx_teardown_tag_8021q(ds);
			goto err_restore;
		}

		/* Refresh link-local traps now that tag_8021q_vlan_add
		 * callbacks have set cpu_egress_evlan.in_use, so the
		 * PCE rules get the correct EVLAN treatment.
		 */
		ret = mxl862xx_refresh_cpu_targets(ds);
		if (ret) {
			dsa_tag_8021q_unregister(ds);
			mxl862xx_teardown_tag_8021q(ds);
			goto err_restore;
		}
		break;

	default:
		ret = -EPROTONOSUPPORT;
		goto err_restore;
	}

	return 0;

err_restore:
	priv->tag_proto = old_proto;
	return ret;
}

static void mxl862xx_teardown(struct dsa_switch *ds)
{
	/* tag_8021q teardown is handled in mxl862xx_remove() under
	 * RTNL, before dsa_unregister_switch() takes dsa2_mutex.
	 * dsa_tag_8021q_unregister() needs RTNL for vlan_vid_del(),
	 * and acquiring RTNL inside teardown() (which runs under
	 * dsa2_mutex) would invert the RTNL -> dsa2_mutex lock order.
	 */
}

static int mxl862xx_port_setup(struct dsa_switch *ds, int port)
{
	struct mxl862xx_priv *priv = ds->priv;
	struct dsa_port *dp = dsa_to_port(ds, port);
	bool is_cpu_port = dsa_port_is_cpu(dp);
	int ret;

	ret = mxl862xx_port_state(ds, port, false);
	if (ret)
		return ret;

	mxl862xx_port_fast_age(ds, port);

	if (dsa_port_is_unused(dp))
		return 0;

	if (dsa_port_is_dsa(dp)) {
		dev_err(ds->dev, "port %d: DSA links not supported\n", port);
		return -EOPNOTSUPP;
	}

	/* configure tag protocol: SpTag for native, disable for 8021q */
	ret = mxl862xx_configure_sp_tag_proto(ds, port,
					      is_cpu_port &&
					      priv->tag_proto == DSA_TAG_PROTO_MXL862);
	if (ret)
		return ret;

	ret = mxl862xx_configure_ctp_port(ds, port, port,
					  (is_cpu_port &&
					   priv->tag_proto == DSA_TAG_PROTO_MXL862) ?
					  32 - port : 1);
	if (ret)
		return ret;

	if (is_cpu_port)
		return mxl862xx_setup_cpu_bridge(ds, port);

	/* The FID and initial bridge port config were set up in setup()
	 * before change_tag_protocol() runs. Reconfigure here now that
	 * the per-port CTP and SpTag settings are in place.
	 *
	 * In tag_8021q mode the TX path goes through the bridge engine
	 * (CTP ingress EVLAN reassigns to a virtual bridge port which
	 * then forwards via the bridge). With learning disabled on
	 * standalone ports, unknown unicast must be flooded so that
	 * frames from the host can reach the user port.
	 *
	 * In SpTag mode TX bypasses the bridge engine entirely (the
	 * special tag selects the egress port directly), so flood
	 * control only affects CPU-bound traffic and can be restrictive.
	 * Block unknown UC/MC on the VBP egress meters in tag_8021q
	 * mode so frames to unknown destinations are not forwarded to
	 * the host. The DSA core re-enables selectively via
	 * port_set_host_flood when needed (e.g. promisc mode).
	 */
	if (priv->tag_proto == DSA_TAG_PROTO_MXL862_8021Q) {
		ret = mxl862xx_bridge_config_fwd(ds, priv->ports[port].fid,
						 true, true, true);
		priv->ports[port].host_flood_block =
			BIT(MXL862XX_BRIDGE_PORT_EGRESS_METER_UNKNOWN_UC) |
			BIT(MXL862XX_BRIDGE_PORT_EGRESS_METER_UNKNOWN_MC_IP) |
			BIT(MXL862XX_BRIDGE_PORT_EGRESS_METER_UNKNOWN_MC_NON_IP);
	} else {
		ret = mxl862xx_bridge_config_fwd(ds, priv->ports[port].fid,
						 false, false, true);
	}
	if (ret)
		return ret;
	ret = mxl862xx_set_bridge_port(ds, port);
	if (ret)
		return ret;

	/* install link-local and multicast snooping traps */
	ret = mxl862xx_setup_link_local_trap(ds, port);
	if (ret)
		return ret;

	ret = mxl862xx_setup_snooping_traps(ds, port);
	if (ret)
		return ret;

	priv->ports[port].setup_done = true;
	return 0;
}

static void mxl862xx_port_teardown(struct dsa_switch *ds, int port)
{
	struct mxl862xx_priv *priv = ds->priv;
	struct dsa_port *dp = dsa_to_port(ds, port);

	if (dsa_port_is_unused(dp))
		return;

	/* Prevent deferred host_flood_work from acting on stale state.
	 * The flag is checked under rtnl_lock() by the worker; since
	 * teardown also runs under RTNL, this is race-free.
	 *
	 * HW EVLAN/VF blocks are not freed here -- the firmware receives
	 * a full reset on the next probe, which reclaims all resources.
	 */
	priv->ports[port].setup_done = false;
}

static int mxl862xx_get_fid(struct dsa_switch *ds, const struct dsa_db db)
{
	struct mxl862xx_priv *priv = ds->priv;

	switch (db.type) {
	case DSA_DB_PORT:
		return priv->ports[db.dp->index].fid;

	case DSA_DB_BRIDGE:
		if (!priv->bridges[db.bridge.num])
			return -ENOENT;
		return priv->bridges[db.bridge.num];

	default:
		return -EOPNOTSUPP;
	}
}

/**
 * mxl862xx_fdb_bridge_port - Translate port to effective bridge port ID
 * @ds: DSA switch
 * @port: port number passed by DSA (usually the CPU port for host entries)
 * @db: database context identifying the user port or bridge
 *
 * Returns the firmware bridge port ID that should be used for MAC table
 * entries targeting @port:
 *  - CPU port in tag_8021q standalone mode: the virtual bridge port
 *    (bridge_port_cpu) so known traffic exits through egress EVLAN
 *  - User port in a LAG: the LAG's dedicated firmware bridge port
 *  - Otherwise: the port index itself
 */
static int mxl862xx_fdb_bridge_port(struct dsa_switch *ds, int port,
				    const struct dsa_db db)
{
	struct mxl862xx_priv *priv = ds->priv;
	u16 bp_cpu;

	if (dsa_is_cpu_port(ds, port) && priv->tag_proto == DSA_TAG_PROTO_MXL862_8021Q &&
	    db.type == DSA_DB_PORT) {
		bp_cpu = priv->ports[db.dp->index].bridge_port_cpu;

		if (bp_cpu)
			return bp_cpu;
	}

	return mxl862xx_lag_bridge_port(priv, port);
}

/**
 * mxl862xx_fdb_add_per_fid - Install a unicast FDB entry in one FID
 */
static int mxl862xx_fdb_add_per_fid(struct dsa_switch *ds,
				     const unsigned char *addr, u16 vid,
				     u16 fid, int port_id)
{
	struct mxl862xx_mac_table_add param = {};
	struct mxl862xx_priv *priv = ds->priv;

	param.port_id = cpu_to_le32(port_id);
	param.static_entry = true;
	param.fid = cpu_to_le16(fid);
	param.tci = cpu_to_le16(FIELD_PREP(MXL862XX_TCI_VLAN_ID, vid));
	ether_addr_copy(param.mac, addr);

	return MXL862XX_API_WRITE(priv, MXL862XX_MAC_TABLEENTRYADD, param);
}

/**
 * mxl862xx_fdb_del_per_fid - Remove a unicast FDB entry from one FID
 */
static int mxl862xx_fdb_del_per_fid(struct dsa_switch *ds,
				     const unsigned char *addr, u16 vid,
				     u16 fid)
{
	struct mxl862xx_mac_table_remove param = {};
	struct mxl862xx_priv *priv = ds->priv;

	param.fid = cpu_to_le16(fid);
	param.tci = cpu_to_le16(FIELD_PREP(MXL862XX_TCI_VLAN_ID, vid));
	ether_addr_copy(param.mac, addr);

	return MXL862XX_API_WRITE(priv, MXL862XX_MAC_TABLEENTRYREMOVE, param);
}

static int mxl862xx_port_fdb_add(struct dsa_switch *ds, int port,
				 const unsigned char *addr, u16 vid, const struct dsa_db db)
{
	struct mxl862xx_priv *priv = ds->priv;
	struct dsa_port *target_dp;
	int fid, ret;

	/* tag_8021q host FDB for bridged ports: portmap with all VBPs */
	if (priv->tag_proto == DSA_TAG_PROTO_MXL862_8021Q && dsa_is_cpu_port(ds, port) &&
	    db.type == DSA_DB_BRIDGE) {
		if (!priv->bridges[db.bridge.num])
			return -ENOENT;

		return mxl862xx_mac_add_host_bridge(ds, addr, vid, &db.bridge);
	}

	/* tag_8021q standalone host FDB for bridged ports: also mirror
	 * into the bridge FID. DSA installs VID-specific host entries
	 * via the standalone path (DSA_DB_PORT), but with IVL enabled
	 * the firmware needs matching entries in the bridge FID for
	 * VID-keyed lookups to succeed.
	 */
	if (priv->tag_proto == DSA_TAG_PROTO_MXL862_8021Q && dsa_is_cpu_port(ds, port) &&
	    db.type == DSA_DB_PORT && vid > 0) {
		target_dp = dsa_to_port(ds, db.dp->index);

		if (target_dp->bridge) {
			ret = mxl862xx_mac_add_host_bridge(ds, addr, vid,
							   target_dp->bridge);
			if (ret)
				return ret;
		}
	}

	fid = mxl862xx_get_fid(ds, db);
	if (fid < 0)
		return fid;

	ret = mxl862xx_fdb_add_per_fid(ds, addr, vid, fid,
				       mxl862xx_fdb_bridge_port(ds, port, db));
	if (ret)
		dev_err(ds->dev, "failed to add FDB entry on port %d\n", port);

	return ret;
}

static int mxl862xx_port_fdb_del(struct dsa_switch *ds, int port,
				 const unsigned char *addr, u16 vid, const struct dsa_db db)
{
	struct mxl862xx_priv *priv = ds->priv;
	struct dsa_port *target_dp;
	int fid, ret;

	/* Mirror of the standalone->bridge FID path in fdb_add */
	if (priv->tag_proto == DSA_TAG_PROTO_MXL862_8021Q && dsa_is_cpu_port(ds, port) &&
	    db.type == DSA_DB_PORT && vid > 0) {
		target_dp = dsa_to_port(ds, db.dp->index);

		if (target_dp->bridge && priv->bridges[target_dp->bridge->num])
			mxl862xx_fdb_del_per_fid(ds, addr, vid,
						 priv->bridges[target_dp->bridge->num]);
	}

	fid = mxl862xx_get_fid(ds, db);
	if (fid < 0)
		return fid;

	ret = mxl862xx_fdb_del_per_fid(ds, addr, vid, fid);
	if (ret)
		dev_err(ds->dev, "failed to remove FDB entry on port %d\n", port);

	return ret;
}

static int mxl862xx_lag_fdb_add(struct dsa_switch *ds, const struct dsa_lag lag,
				const unsigned char *addr, u16 vid,
				const struct dsa_db db)
{
	struct mxl862xx_priv *priv = ds->priv;
	u16 lag_bp = priv->lag_bridge_ports[lag.id];
	int fid;

	if (!lag_bp)
		return -ENOENT;

	fid = mxl862xx_get_fid(ds, db);
	if (fid < 0)
		return fid;

	return mxl862xx_fdb_add_per_fid(ds, addr, vid, fid, lag_bp);
}

static int mxl862xx_lag_fdb_del(struct dsa_switch *ds, const struct dsa_lag lag,
				const unsigned char *addr, u16 vid,
				const struct dsa_db db)
{
	int fid;

	fid = mxl862xx_get_fid(ds, db);
	if (fid < 0)
		return fid;

	return mxl862xx_fdb_del_per_fid(ds, addr, vid, fid);
}

static int mxl862xx_port_fdb_dump(struct dsa_switch *ds, int port,
				  dsa_fdb_dump_cb_t *cb, void *data)
{
	struct mxl862xx_mac_table_read param = { .initial = 1 };
	struct mxl862xx_priv *priv = ds->priv;
	u16 lag_bp = mxl862xx_lag_bridge_port(priv, port);
	u32 entry_port_id;
	int ret;

	while (true) {
		ret = MXL862XX_API_READ(priv, MXL862XX_MAC_TABLEENTRYREAD, param);
		if (ret)
			return ret;

		if (param.last)
			break;

		entry_port_id = le32_to_cpu(param.port_id);

		if (entry_port_id == port || entry_port_id == lag_bp) {
			ret = cb(param.mac, FIELD_GET(MXL862XX_TCI_VLAN_ID,
						      le16_to_cpu(param.tci)),
				 param.static_entry, data);
			if (ret)
				return ret;
		}

		memset(&param, 0, sizeof(param));
	}

	return 0;
}

/**
 * mxl862xx_mdb_add_to_fid - Add a port bit to an MDB entry in one FID
 * @ds: DSA switch
 * @mdb: multicast group address and VID
 * @fid: firmware FID to operate on
 * @port_bit: port index to set in the portmap
 * @vid: VLAN ID for the MAC table entry
 */
static int mxl862xx_mdb_add_to_fid(struct dsa_switch *ds,
				    const struct switchdev_obj_port_mdb *mdb,
				    u16 fid, int port_bit, u16 vid)
{
	__le16 add_map[MXL862XX_FW_PORTMAP_WORDS] = {};

	mxl862xx_fw_portmap_set_bit(add_map, port_bit);

	return mxl862xx_mac_portmap_add(ds->priv, mdb->addr, fid, vid,
					add_map);
}

/**
 * mxl862xx_mdb_del_from_fid - Remove a port bit from an MDB entry in one FID
 * @ds: DSA switch
 * @mdb: multicast group address
 * @fid: firmware FID to operate on
 * @port_bit: port index to clear from the portmap
 * @vid: VLAN ID for the MAC table entry (0 for SVL/tag_8021q mode)
 */
static int mxl862xx_mdb_del_from_fid(struct dsa_switch *ds,
				      const struct switchdev_obj_port_mdb *mdb,
				      u16 fid, int port_bit, u16 vid)
{
	__le16 del_map[MXL862XX_FW_PORTMAP_WORDS] = {};

	mxl862xx_fw_portmap_set_bit(del_map, port_bit);

	return mxl862xx_mac_portmap_del(ds->priv, mdb->addr, fid, vid,
					del_map);
}

static int mxl862xx_port_mdb_add(struct dsa_switch *ds, int port,
				 const struct switchdev_obj_port_mdb *mdb,
				 const struct dsa_db db)
{
	struct mxl862xx_priv *priv = ds->priv;
	int fid, ret;

	/* tag_8021q host MDB for bridged ports: portmap with all VBPs */
	if (priv->tag_proto == DSA_TAG_PROTO_MXL862_8021Q && dsa_is_cpu_port(ds, port) &&
	    db.type == DSA_DB_BRIDGE) {
		if (!priv->bridges[db.bridge.num])
			return -ENOENT;

		return mxl862xx_mac_add_host_bridge(ds, mdb->addr,
						    mdb->vid, &db.bridge);
	}

	fid = mxl862xx_get_fid(ds, db);
	if (fid < 0)
		return fid;

	ret = mxl862xx_mdb_add_to_fid(ds, mdb, fid,
				       mxl862xx_fdb_bridge_port(ds, port, db),
				       mdb->vid);
	if (ret)
		return ret;

	/* In tag_8021q mode, standalone host MDB entries need both the VBP
	 * and the physical port in the portmap. The TX path goes through
	 * the bridge engine (CPU -> VBP -> MAC lookup), so source-port
	 * filtering would remove the sole VBP entry, dropping the frame.
	 * With both bits set:
	 *   TX: VBP source-filtered -> physical port remains -> frame exits
	 *   RX: physical port source-filtered -> VBP remains -> CPU receives
	 */
	if (priv->tag_proto == DSA_TAG_PROTO_MXL862_8021Q && db.type == DSA_DB_PORT)
		ret = mxl862xx_mdb_add_to_fid(ds, mdb, fid, db.dp->index,
					       mdb->vid);

	return ret;
}

static int mxl862xx_port_mdb_del(struct dsa_switch *ds, int port,
				 const struct switchdev_obj_port_mdb *mdb,
				 const struct dsa_db db)
{
	struct mxl862xx_priv *priv = ds->priv;
	int fid, ret;

	/* tag_8021q host MDB for bridged ports: clear all VBP bits */
	if (priv->tag_proto == DSA_TAG_PROTO_MXL862_8021Q && dsa_is_cpu_port(ds, port) &&
	    db.type == DSA_DB_BRIDGE) {
		if (!priv->bridges[db.bridge.num])
			return -ENOENT;

		return mxl862xx_mac_del_host_bridge(ds, mdb->addr,
						    mdb->vid, &db.bridge);
	}

	fid = mxl862xx_get_fid(ds, db);
	if (fid < 0)
		return fid;

	ret = mxl862xx_mdb_del_from_fid(ds, mdb, fid,
					 mxl862xx_fdb_bridge_port(ds, port, db),
					 mdb->vid);
	if (ret)
		return ret;

	/* In tag_8021q mode, standalone host MDB entries have both the VBP
	 * and the physical port in the portmap -- remove both bits.
	 */
	if (priv->tag_proto == DSA_TAG_PROTO_MXL862_8021Q && db.type == DSA_DB_PORT)
		ret = mxl862xx_mdb_del_from_fid(ds, mdb, fid, db.dp->index,
						 mdb->vid);

	return ret;
}

static int mxl862xx_set_ageing_time(struct dsa_switch *ds, unsigned int msecs)
{
	struct mxl862xx_cfg param = {};
	int ret;

	ret = MXL862XX_API_READ(ds->priv, MXL862XX_COMMON_CFGGET, param);
	if (ret) {
		dev_err(ds->dev, "failed to read switch config\n");
		return ret;
	}

	param.mac_table_age_timer = cpu_to_le32(MXL862XX_AGETIMER_CUSTOM);
	param.age_timer = cpu_to_le32(msecs / 1000);
	ret = MXL862XX_API_WRITE(ds->priv, MXL862XX_COMMON_CFGSET, param);
	if (ret)
		dev_err(ds->dev, "failed to set ageing\n");

	return ret;
}

static int mxl862xx_port_change_mtu(struct dsa_switch *ds, int port,
				    int new_mtu)
{
	struct mxl862xx_priv *priv = ds->priv;
	struct mxl862xx_cfg param = {};
	int i, old_max = 0, new_max = 0;
	int ret;

	for (i = 0; i < ds->num_ports; i++) {
		if (priv->ports[i].mtu > old_max)
			old_max = priv->ports[i].mtu;
	}

	priv->ports[port].mtu = new_mtu;

	for (i = 0; i < ds->num_ports; i++) {
		if (priv->ports[i].mtu > new_max)
			new_max = priv->ports[i].mtu;
	}

	if (new_max != old_max) {
		ret = MXL862XX_API_READ(priv, MXL862XX_COMMON_CFGGET,
					param);
		if (ret)
			return ret;

		param.max_packet_len = cpu_to_le16(new_max +
						   VLAN_ETH_HLEN +
						   ETH_FCS_LEN);
		ret = MXL862XX_API_WRITE(priv, MXL862XX_COMMON_CFGSET,
					 param);
		if (ret) {
			dev_err(ds->dev,
				"failed to set MTU to %d: %pe\n",
				new_mtu, ERR_PTR(ret));
			return ret;
		}
	}

	return 0;
}

static int mxl862xx_port_max_mtu(struct dsa_switch *ds, int port)
{
	return U16_MAX - VLAN_ETH_HLEN - ETH_FCS_LEN;
}

static void mxl862xx_port_stp_state_set(struct dsa_switch *ds, int port,
					u8 state)
{
	struct mxl862xx_stp_port_cfg param = {
		.port_id = cpu_to_le16(port),
	};
	struct mxl862xx_priv *priv = ds->priv;
	int ret;

	switch (state) {
	case BR_STATE_DISABLED:
		param.port_state = cpu_to_le32(MXL862XX_STP_PORT_STATE_DISABLE);
		break;
	case BR_STATE_BLOCKING:
	case BR_STATE_LISTENING:
		param.port_state = cpu_to_le32(MXL862XX_STP_PORT_STATE_BLOCKING);
		break;
	case BR_STATE_LEARNING:
		param.port_state = cpu_to_le32(MXL862XX_STP_PORT_STATE_LEARNING);
		break;
	case BR_STATE_FORWARDING:
		param.port_state = cpu_to_le32(MXL862XX_STP_PORT_STATE_FORWARD);
		break;
	default:
		dev_err(ds->dev, "invalid STP state: %d\n", state);
		return;
	}

	ret = MXL862XX_API_WRITE(priv, MXL862XX_STP_PORTCFGSET, param);
	if (ret) {
		dev_err(ds->dev, "failed to set STP state on port %d\n", port);
		return;
	}

	/* The firmware may re-enable MAC learning as a side-effect of entering
	 * LEARNING or FORWARDING state (per 802.1D defaults).
	 * Re-apply the driver's intended learning and metering config so that
	 * standalone ports keep learning disabled.
	 */
	ret = mxl862xx_set_bridge_port(ds, port);
	if (ret)
		dev_err(ds->dev, "failed to reapply brport flags on port %d\n",
			port);

	mxl862xx_port_fast_age(ds, port);
}

/* Deferred work handler for host flood configuration.
 *
 * port_set_host_flood is called from atomic context (under
 * netif_addr_lock), so firmware calls must be deferred. The worker
 * acquires rtnl_lock() to serialize with DSA callbacks that access the
 * same driver state.
 */
static void mxl862xx_host_flood_work_fn(struct work_struct *work)
{
	struct mxl862xx_port *p = container_of(work, struct mxl862xx_port,
					       host_flood_work);
	struct mxl862xx_priv *priv = p->priv;
	struct dsa_switch *ds = priv->ds;
	int port = p - priv->ports;
	unsigned long block;
	int ret;

	rtnl_lock();

	/* Port may have been torn down between scheduling and now. */
	if (!p->setup_done) {
		rtnl_unlock();
		return;
	}

	if (priv->tag_proto == DSA_TAG_PROTO_MXL862_8021Q) {
		block = 0;

		if (!p->host_flood_uc)
			block |= BIT(MXL862XX_BRIDGE_PORT_EGRESS_METER_UNKNOWN_UC);
		if (!p->host_flood_mc) {
			block |= BIT(MXL862XX_BRIDGE_PORT_EGRESS_METER_UNKNOWN_MC_IP);
			block |= BIT(MXL862XX_BRIDGE_PORT_EGRESS_METER_UNKNOWN_MC_NON_IP);
		}

		if (block != p->host_flood_block) {
			p->host_flood_block = block;
			ret = mxl862xx_set_cpu_vbp(ds, port);
			if (ret)
				dev_err(ds->dev,
					"failed to set host flood on port %d: %pe\n",
					port, ERR_PTR(ret));
		}
	} else {
		/* Always write to the standalone FID. When standalone it takes
		 * effect immediately; when bridged the port uses the shared
		 * bridge FID so the write is a no-op for current forwarding,
		 * but the state is preserved in hardware and is ready once the
		 * port returns to standalone.
		 */
		mxl862xx_bridge_config_fwd(ds, p->fid, p->host_flood_uc,
					   p->host_flood_mc, true);
	}

	rtnl_unlock();
}

static void mxl862xx_port_set_host_flood(struct dsa_switch *ds, int port,
					 bool uc, bool mc)
{
	struct mxl862xx_priv *priv = ds->priv;
	struct mxl862xx_port *p = &priv->ports[port];

	p->host_flood_uc = uc;
	p->host_flood_mc = mc;
	schedule_work(&p->host_flood_work);
}

static int mxl862xx_port_pre_bridge_flags(struct dsa_switch *ds, int port,
					  const struct switchdev_brport_flags flags,
					  struct netlink_ext_ack *extack)
{
	if (flags.mask & ~(BR_FLOOD | BR_MCAST_FLOOD | BR_BCAST_FLOOD |
			   BR_LEARNING | BR_HAIRPIN_MODE | BR_ISOLATED))
		return -EINVAL;

	return 0;
}

static int mxl862xx_port_bridge_flags(struct dsa_switch *ds, int port,
				      const struct switchdev_brport_flags flags,
				      struct netlink_ext_ack *extack)
{
	struct mxl862xx_priv *priv = ds->priv;
	unsigned long old_block = priv->ports[port].flood_block;
	unsigned long block = old_block;
	struct dsa_port *dp;
	int ret;

	if (flags.mask & BR_FLOOD) {
		if (flags.val & BR_FLOOD)
			block &= ~BIT(MXL862XX_BRIDGE_PORT_EGRESS_METER_UNKNOWN_UC);
		else
			block |= BIT(MXL862XX_BRIDGE_PORT_EGRESS_METER_UNKNOWN_UC);
	}

	if (flags.mask & BR_MCAST_FLOOD) {
		if (flags.val & BR_MCAST_FLOOD) {
			block &= ~BIT(MXL862XX_BRIDGE_PORT_EGRESS_METER_UNKNOWN_MC_IP);
			block &= ~BIT(MXL862XX_BRIDGE_PORT_EGRESS_METER_UNKNOWN_MC_NON_IP);
		} else {
			block |= BIT(MXL862XX_BRIDGE_PORT_EGRESS_METER_UNKNOWN_MC_IP);
			block |= BIT(MXL862XX_BRIDGE_PORT_EGRESS_METER_UNKNOWN_MC_NON_IP);
		}
	}

	if (flags.mask & BR_BCAST_FLOOD) {
		if (flags.val & BR_BCAST_FLOOD)
			block &= ~BIT(MXL862XX_BRIDGE_PORT_EGRESS_METER_BROADCAST);
		else
			block |= BIT(MXL862XX_BRIDGE_PORT_EGRESS_METER_BROADCAST);
	}

	if (flags.mask & BR_LEARNING)
		priv->ports[port].learning = !!(flags.val & BR_LEARNING);

	if (flags.mask & BR_HAIRPIN_MODE)
		priv->ports[port].hairpin = !!(flags.val & BR_HAIRPIN_MODE);

	if ((block != old_block) ||
	    (flags.mask & (BR_LEARNING | BR_HAIRPIN_MODE))) {
		priv->ports[port].flood_block = block;
		ret = mxl862xx_set_bridge_port(ds, port);
		if (ret)
			return ret;
	}

	if (flags.mask & BR_ISOLATED) {
		dp = dsa_to_port(ds, port);
		priv->ports[port].isolated = !!(flags.val & BR_ISOLATED);

		/* Isolation affects all bridge members' portmaps:
		 * isolated ports must be removed from each other's
		 * portmaps. Rebuild all portmaps for this bridge.
		 */
		if (dp->bridge) {
			ret = mxl862xx_sync_bridge_members(ds, dp->bridge);
			if (ret)
				return ret;
		}
	}

	return 0;
}

static void mxl862xx_get_strings(struct dsa_switch *ds, int port,
				 u32 stringset, u8 *data)
{
	int i;

	if (stringset != ETH_SS_STATS)
		return;

	for (i = 0; i < ARRAY_SIZE(mxl862xx_mib); i++)
		ethtool_puts(&data, mxl862xx_mib[i].name);

	mxl862xx_serdes_get_strings(ds, port, data);
}

static int mxl862xx_get_sset_count(struct dsa_switch *ds, int port, int sset)
{
	if (sset != ETH_SS_STATS)
		return 0;

	return ARRAY_SIZE(mxl862xx_mib) + mxl862xx_serdes_stats_count(ds, port);
}

static int mxl862xx_read_rmon(struct dsa_switch *ds, int port,
			      struct mxl862xx_rmon_port_cnt *cnt)
{
	memset(cnt, 0, sizeof(*cnt));
	cnt->port_type = cpu_to_le32(MXL862XX_CTP_PORT);
	cnt->port_id = cpu_to_le16(port);

	return MXL862XX_API_READ(ds->priv, MXL862XX_RMON_PORT_GET, *cnt);
}

static void mxl862xx_get_ethtool_stats(struct dsa_switch *ds, int port,
				       u64 *data)
{
	const struct mxl862xx_mib_desc *mib;
	struct mxl862xx_rmon_port_cnt cnt;
	int ret, i;
	void *field;

	ret = mxl862xx_read_rmon(ds, port, &cnt);
	if (ret) {
		dev_err(ds->dev, "failed to read RMON stats on port %d\n", port);
		return;
	}

	for (i = 0; i < ARRAY_SIZE(mxl862xx_mib); i++) {
		mib = &mxl862xx_mib[i];
		field = (u8 *)&cnt + mib->offset;

		if (mib->size == 1)
			*data++ = le32_to_cpu(*(__le32 *)field);
		else
			*data++ = le64_to_cpu(*(__le64 *)field);
	}

	mxl862xx_serdes_get_stats(ds, port, data);
}

static void mxl862xx_get_eth_mac_stats(struct dsa_switch *ds, int port,
				       struct ethtool_eth_mac_stats *mac_stats)
{
	struct mxl862xx_rmon_port_cnt cnt;

	if (mxl862xx_read_rmon(ds, port, &cnt))
		return;

	mac_stats->FramesTransmittedOK = le32_to_cpu(cnt.tx_good_pkts);
	mac_stats->SingleCollisionFrames = le32_to_cpu(cnt.tx_single_coll_count);
	mac_stats->MultipleCollisionFrames = le32_to_cpu(cnt.tx_mult_coll_count);
	mac_stats->FramesReceivedOK = le32_to_cpu(cnt.rx_good_pkts);
	mac_stats->FrameCheckSequenceErrors = le32_to_cpu(cnt.rx_fcserror_pkts);
	mac_stats->AlignmentErrors = le32_to_cpu(cnt.rx_align_error_pkts);
	mac_stats->OctetsTransmittedOK = le64_to_cpu(cnt.tx_good_bytes);
	mac_stats->LateCollisions = le32_to_cpu(cnt.tx_late_coll_count);
	mac_stats->FramesAbortedDueToXSColls = le32_to_cpu(cnt.tx_excess_coll_count);
	mac_stats->OctetsReceivedOK = le64_to_cpu(cnt.rx_good_bytes);
	mac_stats->MulticastFramesXmittedOK = le32_to_cpu(cnt.tx_multicast_pkts);
	mac_stats->BroadcastFramesXmittedOK = le32_to_cpu(cnt.tx_broadcast_pkts);
	mac_stats->MulticastFramesReceivedOK = le32_to_cpu(cnt.rx_multicast_pkts);
	mac_stats->BroadcastFramesReceivedOK = le32_to_cpu(cnt.rx_broadcast_pkts);
	mac_stats->FrameTooLongErrors = le32_to_cpu(cnt.rx_oversize_error_pkts);
}

static void mxl862xx_get_eth_ctrl_stats(struct dsa_switch *ds, int port,
					struct ethtool_eth_ctrl_stats *ctrl_stats)
{
	struct mxl862xx_rmon_port_cnt cnt;

	if (mxl862xx_read_rmon(ds, port, &cnt))
		return;

	ctrl_stats->MACControlFramesTransmitted = le32_to_cpu(cnt.tx_pause_count);
	ctrl_stats->MACControlFramesReceived = le32_to_cpu(cnt.rx_good_pause_pkts);
}

static void mxl862xx_get_pause_stats(struct dsa_switch *ds, int port,
				     struct ethtool_pause_stats *pause_stats)
{
	struct mxl862xx_rmon_port_cnt cnt;

	if (mxl862xx_read_rmon(ds, port, &cnt))
		return;

	pause_stats->tx_pause_frames = le32_to_cpu(cnt.tx_pause_count);
	pause_stats->rx_pause_frames = le32_to_cpu(cnt.rx_good_pause_pkts);
}

static void mxl862xx_get_rmon_stats(struct dsa_switch *ds, int port,
				    struct ethtool_rmon_stats *rmon_stats,
				    const struct ethtool_rmon_hist_range **ranges)
{
	struct mxl862xx_rmon_port_cnt cnt;

	if (mxl862xx_read_rmon(ds, port, &cnt))
		return;

	rmon_stats->undersize_pkts = le32_to_cpu(cnt.rx_under_size_good_pkts);
	rmon_stats->oversize_pkts = le32_to_cpu(cnt.rx_oversize_good_pkts);
	rmon_stats->fragments = le32_to_cpu(cnt.rx_under_size_error_pkts);
	rmon_stats->jabbers = le32_to_cpu(cnt.rx_oversize_error_pkts);

	rmon_stats->hist[0] = le32_to_cpu(cnt.rx64byte_pkts);
	rmon_stats->hist[1] = le32_to_cpu(cnt.rx127byte_pkts);
	rmon_stats->hist[2] = le32_to_cpu(cnt.rx255byte_pkts);
	rmon_stats->hist[3] = le32_to_cpu(cnt.rx511byte_pkts);
	rmon_stats->hist[4] = le32_to_cpu(cnt.rx1023byte_pkts);
	rmon_stats->hist[5] = le32_to_cpu(cnt.rx_max_byte_pkts);

	rmon_stats->hist_tx[0] = le32_to_cpu(cnt.tx64byte_pkts);
	rmon_stats->hist_tx[1] = le32_to_cpu(cnt.tx127byte_pkts);
	rmon_stats->hist_tx[2] = le32_to_cpu(cnt.tx255byte_pkts);
	rmon_stats->hist_tx[3] = le32_to_cpu(cnt.tx511byte_pkts);
	rmon_stats->hist_tx[4] = le32_to_cpu(cnt.tx1023byte_pkts);
	rmon_stats->hist_tx[5] = le32_to_cpu(cnt.tx_max_byte_pkts);

	*ranges = mxl862xx_rmon_ranges;
}

/* Compute the delta between two 32-bit free-running counter snapshots,
 * handling a single wrap-around correctly via unsigned subtraction.
 */
static u64 mxl862xx_delta32(u32 cur, u32 prev)
{
	return (u32)(cur - prev);
}

/**
 * mxl862xx_stats_poll - Read RMON counters and accumulate into 64-bit stats
 * @ds: DSA switch
 * @port: port index
 *
 * The firmware RMON counters are free-running 32-bit values (64-bit for
 * byte counters). This function reads the hardware via MDIO (may sleep),
 * computes deltas from the previous snapshot, and accumulates them into
 * 64-bit per-port stats under a spinlock.
 *
 * Called only from the stats polling workqueue -- serialized by the
 * single-threaded delayed_work, so no MDIO locking is needed here.
 */
static void mxl862xx_stats_poll(struct dsa_switch *ds, int port)
{
	struct mxl862xx_priv *priv = ds->priv;
	struct mxl862xx_port_stats *s = &priv->ports[port].stats;
	u32 rx_fcserr, rx_under, rx_over, rx_align, tx_drop;
	u32 rx_drop, rx_evlan, mtu_exc, tx_acm;
	struct mxl862xx_rmon_port_cnt cnt;
	u64 rx_bytes, tx_bytes;
	u32 rx_mcast, tx_coll;
	u32 rx_pkts, tx_pkts;

	/* MDIO read -- may sleep, done outside the spinlock. */
	if (mxl862xx_read_rmon(ds, port, &cnt))
		return;

	rx_pkts   = le32_to_cpu(cnt.rx_good_pkts);
	tx_pkts   = le32_to_cpu(cnt.tx_good_pkts);
	rx_bytes  = le64_to_cpu(cnt.rx_good_bytes);
	tx_bytes  = le64_to_cpu(cnt.tx_good_bytes);
	rx_fcserr = le32_to_cpu(cnt.rx_fcserror_pkts);
	rx_under  = le32_to_cpu(cnt.rx_under_size_error_pkts);
	rx_over   = le32_to_cpu(cnt.rx_oversize_error_pkts);
	rx_align  = le32_to_cpu(cnt.rx_align_error_pkts);
	tx_drop   = le32_to_cpu(cnt.tx_dropped_pkts);
	rx_drop   = le32_to_cpu(cnt.rx_dropped_pkts);
	rx_evlan  = le32_to_cpu(cnt.rx_extended_vlan_discard_pkts);
	mtu_exc   = le32_to_cpu(cnt.mtu_exceed_discard_pkts);
	tx_acm    = le32_to_cpu(cnt.tx_acm_dropped_pkts);
	rx_mcast  = le32_to_cpu(cnt.rx_multicast_pkts);
	tx_coll   = le32_to_cpu(cnt.tx_coll_count);

	/* Accumulate deltas under spinlock -- .get_stats64 reads these. */
	spin_lock_bh(&priv->ports[port].stats_lock);

	s->rx_packets += mxl862xx_delta32(rx_pkts, s->prev_rx_good_pkts);
	s->tx_packets += mxl862xx_delta32(tx_pkts, s->prev_tx_good_pkts);
	s->rx_bytes   += rx_bytes - s->prev_rx_good_bytes;
	s->tx_bytes   += tx_bytes - s->prev_tx_good_bytes;

	s->rx_errors +=
		mxl862xx_delta32(rx_fcserr, s->prev_rx_fcserror_pkts) +
		mxl862xx_delta32(rx_under, s->prev_rx_under_size_error_pkts) +
		mxl862xx_delta32(rx_over, s->prev_rx_oversize_error_pkts) +
		mxl862xx_delta32(rx_align, s->prev_rx_align_error_pkts);

	s->rx_dropped +=
		mxl862xx_delta32(rx_drop, s->prev_rx_dropped_pkts) +
		mxl862xx_delta32(rx_evlan, s->prev_rx_evlan_discard_pkts) +
		mxl862xx_delta32(mtu_exc, s->prev_mtu_exceed_discard_pkts);
	s->tx_dropped +=
		mxl862xx_delta32(tx_drop, s->prev_tx_dropped_pkts) +
		mxl862xx_delta32(tx_acm, s->prev_tx_acm_dropped_pkts);

	s->multicast  += mxl862xx_delta32(rx_mcast, s->prev_rx_multicast_pkts);
	s->collisions += mxl862xx_delta32(tx_coll, s->prev_tx_coll_count);

	s->rx_length_errors +=
		mxl862xx_delta32(rx_under, s->prev_rx_under_size_error_pkts) +
		mxl862xx_delta32(rx_over, s->prev_rx_oversize_error_pkts);
	s->rx_crc_errors +=
		mxl862xx_delta32(rx_fcserr, s->prev_rx_fcserror_pkts);
	s->rx_frame_errors +=
		mxl862xx_delta32(rx_align, s->prev_rx_align_error_pkts);

	s->prev_rx_good_pkts             = rx_pkts;
	s->prev_tx_good_pkts             = tx_pkts;
	s->prev_rx_good_bytes            = rx_bytes;
	s->prev_tx_good_bytes            = tx_bytes;
	s->prev_rx_fcserror_pkts         = rx_fcserr;
	s->prev_rx_under_size_error_pkts = rx_under;
	s->prev_rx_oversize_error_pkts   = rx_over;
	s->prev_rx_align_error_pkts      = rx_align;
	s->prev_tx_dropped_pkts          = tx_drop;
	s->prev_rx_dropped_pkts          = rx_drop;
	s->prev_rx_evlan_discard_pkts    = rx_evlan;
	s->prev_mtu_exceed_discard_pkts  = mtu_exc;
	s->prev_tx_acm_dropped_pkts      = tx_acm;
	s->prev_rx_multicast_pkts        = rx_mcast;
	s->prev_tx_coll_count            = tx_coll;

	spin_unlock_bh(&priv->ports[port].stats_lock);
}

static void mxl862xx_stats_work_fn(struct work_struct *work)
{
	struct mxl862xx_priv *priv =
		container_of(work, struct mxl862xx_priv, stats_work.work);
	struct dsa_switch *ds = priv->ds;
	struct dsa_port *dp;

	dsa_switch_for_each_available_port(dp, ds)
		mxl862xx_stats_poll(ds, dp->index);

	if (!test_bit(MXL862XX_FLAG_WORK_STOPPED, &priv->flags))
		schedule_delayed_work(&priv->stats_work,
				      MXL862XX_STATS_POLL_INTERVAL);
}

static void mxl862xx_get_stats64(struct dsa_switch *ds, int port,
				 struct rtnl_link_stats64 *s)
{
	struct mxl862xx_priv *priv = ds->priv;
	struct mxl862xx_port_stats *ps = &priv->ports[port].stats;

	spin_lock_bh(&priv->ports[port].stats_lock);

	s->rx_packets = ps->rx_packets;
	s->tx_packets = ps->tx_packets;
	s->rx_bytes = ps->rx_bytes;
	s->tx_bytes = ps->tx_bytes;
	s->rx_errors = ps->rx_errors;
	s->tx_errors = ps->tx_errors;
	s->rx_dropped = ps->rx_dropped;
	s->tx_dropped = ps->tx_dropped;
	s->multicast = ps->multicast;
	s->collisions = ps->collisions;
	s->rx_length_errors = ps->rx_length_errors;
	s->rx_crc_errors = ps->rx_crc_errors;
	s->rx_frame_errors = ps->rx_frame_errors;

	spin_unlock_bh(&priv->ports[port].stats_lock);

	/* Trigger a fresh poll so the next read sees up-to-date counters.
	 * No-op if the work is already pending, running, or teardown started.
	 */
	if (!test_bit(MXL862XX_FLAG_WORK_STOPPED, &priv->flags))
		schedule_delayed_work(&priv->stats_work, 0);
}

static const struct dsa_switch_ops mxl862xx_switch_ops = {
	.get_tag_protocol = mxl862xx_get_tag_protocol,
	.change_tag_protocol = mxl862xx_change_tag_protocol,
	.setup = mxl862xx_setup,
	.teardown = mxl862xx_teardown,
	.port_setup = mxl862xx_port_setup,
	.port_teardown = mxl862xx_port_teardown,
	.phylink_get_caps = mxl862xx_phylink_get_caps,
	.port_enable = mxl862xx_port_enable,
	.port_disable = mxl862xx_port_disable,
	.port_fast_age = mxl862xx_port_fast_age,
	.set_ageing_time = mxl862xx_set_ageing_time,
	.port_change_mtu = mxl862xx_port_change_mtu,
	.port_max_mtu = mxl862xx_port_max_mtu,
	.port_bridge_join = mxl862xx_port_bridge_join,
	.port_bridge_leave = mxl862xx_port_bridge_leave,
	.port_pre_bridge_flags = mxl862xx_port_pre_bridge_flags,
	.port_bridge_flags = mxl862xx_port_bridge_flags,
	.port_stp_state_set = mxl862xx_port_stp_state_set,
	.port_set_host_flood = mxl862xx_port_set_host_flood,
	.port_fdb_add = mxl862xx_port_fdb_add,
	.port_fdb_del = mxl862xx_port_fdb_del,
	.port_fdb_dump = mxl862xx_port_fdb_dump,
	.port_mdb_add = mxl862xx_port_mdb_add,
	.port_mdb_del = mxl862xx_port_mdb_del,
	.port_mirror_add = mxl862xx_port_mirror_add,
	.port_mirror_del = mxl862xx_port_mirror_del,
	.port_lag_join = mxl862xx_port_lag_join,
	.port_lag_leave = mxl862xx_port_lag_leave,
	.port_lag_change = mxl862xx_port_lag_change,
	.lag_fdb_add = mxl862xx_lag_fdb_add,
	.lag_fdb_del = mxl862xx_lag_fdb_del,
	.port_vlan_filtering = mxl862xx_port_vlan_filtering,
	.port_vlan_add = mxl862xx_port_vlan_add,
	.port_vlan_del = mxl862xx_port_vlan_del,
	.tag_8021q_vlan_add = mxl862xx_tag_8021q_vlan_add,
	.tag_8021q_vlan_del = mxl862xx_tag_8021q_vlan_del,
	.get_strings = mxl862xx_get_strings,
	.get_sset_count = mxl862xx_get_sset_count,
	.get_ethtool_stats = mxl862xx_get_ethtool_stats,
	.get_eth_mac_stats = mxl862xx_get_eth_mac_stats,
	.get_eth_ctrl_stats = mxl862xx_get_eth_ctrl_stats,
	.get_pause_stats = mxl862xx_get_pause_stats,
	.get_rmon_stats = mxl862xx_get_rmon_stats,
	.get_stats64 = mxl862xx_get_stats64,
	.self_test = mxl862xx_serdes_self_test,
	.devlink_info_get = mxl862xx_devlink_info_get,
	.devlink_flash_update = mxl862xx_devlink_flash_update,

};

static void sfp_monitor_work_func(struct work_struct *work)
{
	struct combo_port_mux *mux = container_of(work, struct combo_port_mux, sfp_monitor_work.work);
	struct dsa_switch *ds = mux->dp->ds;
	struct dsa_port *dp = mux->dp;
	struct net_device *dev = mux->dp->user;
	unsigned int new_channel;
	int sfp_present;

	if (IS_ERR(mux->mod_def0_gpio) || IS_ERR(mux->chan_sel_gpio))
		goto reschedule;

	if (!netif_running(dev))
		goto reschedule;

	sfp_present = gpiod_get_value_cansleep(mux->mod_def0_gpio);
	new_channel = sfp_present ? mux->sfp_present_channel : !mux->sfp_present_channel;

	if (mux->initialized && mux->channel == new_channel)
		goto reschedule;

	rtnl_lock();

	phylink_stop(dp->pl);
	phylink_disconnect_phy(dp->pl);

	dp->dn = mux->data[new_channel]->of_node;
	dp->pl = mux->data[new_channel]->phylink;

	phylink_of_phy_connect(dp->pl, dp->dn, 0);
	phylink_start(dp->pl);

	dev_info(ds->dev, "dsa mux: switch to channel%d\n", new_channel);

	gpiod_set_value_cansleep(mux->chan_sel_gpio, new_channel);

	rtnl_unlock();

	mux->channel = new_channel;
	mux->initialized = true;

reschedule:
	mod_delayed_work(system_wq, &mux->sfp_monitor_work, msecs_to_jiffies(100));
}

static int ds_add_mux_channel(struct combo_port_mux *mux, struct device_node *np)
{
	const __be32 *_id = of_get_property(np, "reg", NULL);
	struct dsa_switch *ds = mux->dp->ds;
	struct dp_mux_data *data;
	struct phylink *phylink;
	phy_interface_t phy_mode;
	int id, err;

	if (!_id) {
		dev_err(ds->dev, "missing mux channel id\n");
		return -EINVAL;
	}

	id = be32_to_cpup(_id);
	if (id < 0 || id > 1) {
		dev_err(ds->dev, "%d is not a valid mux channel id\n", id);
		return -EINVAL;
	}

	data = kmalloc(sizeof(*data), GFP_KERNEL);
	if (unlikely(!data)) {
		dev_err(ds->dev, "failed to create mux data structure\n");
		return -ENOMEM;
	}

	err = of_get_phy_mode(np, &phy_mode);
	if (err) {
		dev_err(ds->dev, "incorrect phy-mode\n");
		goto err_free_data;
	}

	phylink = phylink_create(&mux->dp->pl_config,
				 of_fwnode_handle(np),
				 phy_mode, ds->phylink_mac_ops);
	if (IS_ERR(phylink)) {
		dev_err(ds->dev, "failed to create phylink structure\n");
		err = PTR_ERR(phylink);
		goto err_free_data;
	}

	data->of_node = np;
	data->phylink = phylink;
	mux->data[id] = data;

	return 0;

err_free_data:
	kfree(data);
	return err;
}

static int ds_add_mux(struct mxl862xx_priv *priv, struct device_node *np)
{
	const __be32 *_id = of_get_property(np, "reg", NULL);
	struct device_node *child;
	struct combo_port_mux *mux;
	unsigned int id;
	int err;

	if (!_id) {
		dev_err(priv->ds->dev, "missing attach dp id\n");
		return -EINVAL;
	}

	id = be32_to_cpup(_id);
	if (id < 0 || id >= MXL862XX_MAX_PORTS) {
		dev_err(priv->ds->dev, "%d is not a valid attach dp id\n", id);
		return -EINVAL;
	}

	mux = kmalloc(sizeof(struct combo_port_mux), GFP_KERNEL);
	if (unlikely(!mux)) {
		dev_err(priv->ds->dev, "failed to create mux structure\n");
		return -ENOMEM;
	}

	mux->mod_def0_gpio = fwnode_gpiod_get_index(of_fwnode_handle(np),
				"mod-def0", 0, GPIOD_IN |
				GPIOD_FLAGS_BIT_NONEXCLUSIVE, "?");

	if (IS_ERR(mux->mod_def0_gpio)) {
		dev_err(priv->ds->dev, "failed to requset gpio for mod-def0\n");
		err = PTR_ERR(mux->mod_def0_gpio);
		goto err_free_mux;
	}

	mux->chan_sel_gpio = fwnode_gpiod_get_index(of_fwnode_handle(np),
				"chan-sel", 0, GPIOD_OUT_LOW, "?");

	if (IS_ERR(mux->chan_sel_gpio)) {
		dev_err(priv->ds->dev, "failed to requset gpio for chan-sel\n");
		err = PTR_ERR(mux->chan_sel_gpio);
		goto err_put_mod_def0;
	}

	of_property_read_u32(np, "sfp-present-channel",
		&mux->sfp_present_channel);

	priv->ds_mux[id] = mux;
	mux->dp = dsa_to_port(priv->ds, id);
	/* configure default channel to 10G PHY */
	mux->channel = !mux->sfp_present_channel;
	mux->initialized = false;

	for_each_child_of_node(np, child) {
		err = ds_add_mux_channel(mux, child);
		if (err) {
			dev_err(priv->ds->dev, "failed to add ds_mux\n");
			of_node_put(child);
			goto err_put_chan_sel;
		}
	}

	INIT_DELAYED_WORK(&mux->sfp_monitor_work, sfp_monitor_work_func);
	mod_delayed_work(system_wq, &mux->sfp_monitor_work, msecs_to_jiffies(3000));

	return 0;

err_put_chan_sel:
	gpiod_put(mux->chan_sel_gpio);
err_put_mod_def0:
	gpiod_put(mux->mod_def0_gpio);
err_free_mux:
	kfree(mux);
	priv->ds_mux[id] = NULL;
	return err;
}

static void mxl862xx_release_mux(struct mxl862xx_priv *priv, int id)
{
	struct combo_port_mux *mux = priv->ds_mux[id];
	int i;

	if (!mux)
		return;

	cancel_delayed_work_sync(&mux->sfp_monitor_work);

	if (!IS_ERR_OR_NULL(mux->mod_def0_gpio))
		gpiod_put(mux->mod_def0_gpio);

	if (!IS_ERR_OR_NULL(mux->chan_sel_gpio))
		gpiod_put(mux->chan_sel_gpio);

	for (i = 0; i < 2; i++) {
		if (mux->data[i]) {
			if (mux->data[i]->phylink)
				phylink_destroy(mux->data[i]->phylink);
			kfree(mux->data[i]);
		}
	}
	kfree(mux);
	priv->ds_mux[id] = NULL;
}

static void mxl862xx_release_all_muxes(struct mxl862xx_priv *priv)
{
	int i;
	for (i = 0; i < MXL862XX_MAX_PORTS; i++)
		mxl862xx_release_mux(priv, i);
}

static int mxl862xx_probe(struct mdio_device *mdiodev)
{
	struct device *dev = &mdiodev->dev;
	struct mxl862xx_priv *priv;
	struct device_node *mux_np;
	struct dsa_switch *ds;
	int err, i;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->mdiodev = mdiodev;

	ds = devm_kzalloc(dev, sizeof(*ds), GFP_KERNEL);
	if (!ds)
		return -ENOMEM;

	priv->ds = ds;
	ds->dev = dev;
	ds->priv = priv;
	ds->ops = &mxl862xx_switch_ops;
	ds->phylink_mac_ops = &mxl862xx_phylink_mac_ops;
	ds->num_ports = MXL862XX_MAX_PORTS;
	ds->fdb_isolation = true;
	ds->max_num_bridges = MXL862XX_MAX_BRIDGES;
	ds->num_lag_ids = MXL862XX_MAX_LAG_IDS;

	mxl862xx_host_init(priv);

	for (i = 0; i < MXL862XX_MAX_PORTS; i++) {
		priv->ports[i].priv = priv;
		INIT_WORK(&priv->ports[i].host_flood_work,
			  mxl862xx_host_flood_work_fn);
		spin_lock_init(&priv->ports[i].stats_lock);
	}

	INIT_DELAYED_WORK(&priv->stats_work, mxl862xx_stats_work_fn);

	priv->tag_proto = DSA_TAG_PROTO_MXL862;

	dev_set_drvdata(dev, ds);

	err = dsa_register_switch(ds);
	if (err) {
		set_bit(MXL862XX_FLAG_WORK_STOPPED, &priv->flags);
		cancel_delayed_work_sync(&priv->stats_work);
		mxl862xx_host_shutdown(priv);
		for (i = 0; i < MXL862XX_MAX_PORTS; i++)
			cancel_work_sync(&priv->ports[i].host_flood_work);
		return err;
	}

	mux_np = of_get_child_by_name(priv->ds->dev->of_node, "ds-mux-bus");
	if (mux_np) {
		struct device_node *child;

		for_each_available_child_of_node(mux_np, child) {
			if (!of_device_is_compatible(child,
						     "mxl862xx,ds-mux"))
				continue;

			if (!of_device_is_available(child))
				continue;

			err = ds_add_mux(priv, child);
			if (err)
				dev_err(dev, "failed to add mux\n");

			of_node_put(mux_np);
		};
	}

	return 0;
}

static void mxl862xx_remove(struct mdio_device *mdiodev)
{
	struct dsa_switch *ds = dev_get_drvdata(&mdiodev->dev);
	struct mxl862xx_priv *priv;
	int i;

	if (!ds)
		return;

	priv = ds->priv;

	set_bit(MXL862XX_FLAG_WORK_STOPPED, &priv->flags);
	cancel_delayed_work_sync(&priv->stats_work);

	/* Tear down tag_8021q under RTNL before dsa_unregister_switch().
	 * dsa_tag_8021q_unregister() calls vlan_vid_del() which needs
	 * RTNL. dsa_unregister_switch() takes dsa2_mutex, and other
	 * paths take RTNL -> dsa2_mutex, so RTNL must be acquired
	 * before dsa2_mutex to avoid lock inversion.
	 */
	if (ds->tag_8021q_ctx) {
		rtnl_lock();
		dsa_tag_8021q_unregister(ds);
		mxl862xx_teardown_tag_8021q(ds);
		rtnl_unlock();
	}

	mxl862xx_release_all_muxes(ds->priv);
	dsa_unregister_switch(ds);

	mxl862xx_host_shutdown(priv);

	/* Cancel any pending host flood work. dsa_unregister_switch()
	 * has already called port_teardown (which sets setup_done=false),
	 * but a worker could still be blocked on rtnl_lock(). Since we
	 * are now outside RTNL, cancel_work_sync() will not deadlock.
	 */
	for (i = 0; i < MXL862XX_MAX_PORTS; i++)
		cancel_work_sync(&priv->ports[i].host_flood_work);
}

static void mxl862xx_shutdown(struct mdio_device *mdiodev)
{
	struct dsa_switch *ds = dev_get_drvdata(&mdiodev->dev);
	struct mxl862xx_priv *priv;
	int i;

	if (!ds)
		return;

	priv = ds->priv;

	dsa_switch_shutdown(ds);

	set_bit(MXL862XX_FLAG_WORK_STOPPED, &priv->flags);
	cancel_delayed_work_sync(&priv->stats_work);

	mxl862xx_host_shutdown(priv);

	for (i = 0; i < MXL862XX_MAX_PORTS; i++)
		cancel_work_sync(&priv->ports[i].host_flood_work);

	dev_set_drvdata(&mdiodev->dev, NULL);
}

static const struct of_device_id mxl862xx_of_match[] = {
	{ .compatible = "maxlinear,mxl86282" },
	{ .compatible = "maxlinear,mxl86252" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, mxl862xx_of_match);

static struct mdio_driver mxl862xx_driver = {
	.probe  = mxl862xx_probe,
	.remove = mxl862xx_remove,
	.shutdown = mxl862xx_shutdown,
	.mdiodrv.driver = {
		.name = "mxl862xx",
		.of_match_table = mxl862xx_of_match,
	},
};

mdio_module_driver(mxl862xx_driver);

MODULE_DESCRIPTION("Driver for MaxLinear MxL862xx switch family");
MODULE_LICENSE("GPL");
