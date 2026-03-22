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
#include <linux/if_bridge.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/of_mdio.h>
#include <linux/phy.h>
#include <linux/phylink.h>
#include <linux/dsa/8021q.h>
#include <net/dsa.h>

#include "mxl862xx.h"
#include "mxl862xx-api.h"
#include "mxl862xx-cmd.h"
#include "mxl862xx-host.h"

#define MXL862XX_API_WRITE(dev, cmd, data) \
	mxl862xx_api_wrap(dev, cmd, &(data), sizeof((data)), false, false)
#define MXL862XX_API_READ(dev, cmd, data) \
	mxl862xx_api_wrap(dev, cmd, &(data), sizeof((data)), true, false)
#define MXL862XX_API_READ_QUIET(dev, cmd, data) \
	mxl862xx_api_wrap(dev, cmd, &(data), sizeof((data)), true, true)

/* Polling interval for RMON counter accumulation.  At 2.5 Gbps with
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

static const struct mxl862xx_mib_desc mxl862xx_mib[] = {
	MIB_DESC(1, "TxGoodPkts", tx_good_pkts),
	MIB_DESC(1, "TxUnicastPkts", tx_unicast_pkts),
	MIB_DESC(1, "TxBroadcastPkts", tx_broadcast_pkts),
	MIB_DESC(1, "TxMulticastPkts", tx_multicast_pkts),
	MIB_DESC(1, "Tx64BytePkts", tx64byte_pkts),
	MIB_DESC(1, "Tx127BytePkts", tx127byte_pkts),
	MIB_DESC(1, "Tx255BytePkts", tx255byte_pkts),
	MIB_DESC(1, "Tx511BytePkts", tx511byte_pkts),
	MIB_DESC(1, "Tx1023BytePkts", tx1023byte_pkts),
	MIB_DESC(1, "TxMaxBytePkts", tx_max_byte_pkts),
	MIB_DESC(1, "TxDroppedPkts", tx_dropped_pkts),
	MIB_DESC(1, "TxAcmDroppedPkts", tx_acm_dropped_pkts),
	MIB_DESC(2, "TxGoodBytes", tx_good_bytes),
	MIB_DESC(1, "TxSingleCollCount", tx_single_coll_count),
	MIB_DESC(1, "TxMultCollCount", tx_mult_coll_count),
	MIB_DESC(1, "TxLateCollCount", tx_late_coll_count),
	MIB_DESC(1, "TxExcessCollCount", tx_excess_coll_count),
	MIB_DESC(1, "TxCollCount", tx_coll_count),
	MIB_DESC(1, "TxPauseCount", tx_pause_count),
	MIB_DESC(1, "RxGoodPkts", rx_good_pkts),
	MIB_DESC(1, "RxUnicastPkts", rx_unicast_pkts),
	MIB_DESC(1, "RxBroadcastPkts", rx_broadcast_pkts),
	MIB_DESC(1, "RxMulticastPkts", rx_multicast_pkts),
	MIB_DESC(1, "RxFCSErrorPkts", rx_fcserror_pkts),
	MIB_DESC(1, "RxUnderSizeGoodPkts", rx_under_size_good_pkts),
	MIB_DESC(1, "RxOversizeGoodPkts", rx_oversize_good_pkts),
	MIB_DESC(1, "RxUnderSizeErrorPkts", rx_under_size_error_pkts),
	MIB_DESC(1, "RxOversizeErrorPkts", rx_oversize_error_pkts),
	MIB_DESC(1, "RxFilteredPkts", rx_filtered_pkts),
	MIB_DESC(1, "Rx64BytePkts", rx64byte_pkts),
	MIB_DESC(1, "Rx127BytePkts", rx127byte_pkts),
	MIB_DESC(1, "Rx255BytePkts", rx255byte_pkts),
	MIB_DESC(1, "Rx511BytePkts", rx511byte_pkts),
	MIB_DESC(1, "Rx1023BytePkts", rx1023byte_pkts),
	MIB_DESC(1, "RxMaxBytePkts", rx_max_byte_pkts),
	MIB_DESC(1, "RxDroppedPkts", rx_dropped_pkts),
	MIB_DESC(1, "RxExtendedVlanDiscardPkts", rx_extended_vlan_discard_pkts),
	MIB_DESC(1, "MtuExceedDiscardPkts", mtu_exceed_discard_pkts),
	MIB_DESC(2, "RxGoodBytes", rx_good_bytes),
	MIB_DESC(2, "RxBadBytes", rx_bad_bytes),
	MIB_DESC(1, "RxGoodPausePkts", rx_good_pause_pkts),
	MIB_DESC(1, "RxAlignErrorPkts", rx_align_error_pkts),
};

static const char mxl862xx_serdes_stats[][ETH_GSTRING_LEN] = {
	"serdes_tx_main",
	"serdes_tx_pre",
	"serdes_tx_post",
	"serdes_tx_iboost",
	"serdes_tx_vboost",
	"serdes_tx_vboost_en",
	"serdes_rx_att",
	"serdes_rx_vga1",
	"serdes_rx_vga2",
	"serdes_rx_ctle_boost",
	"serdes_rx_ctle_pole",
	"serdes_rx_dfe_tap1",
	"serdes_rx_dfe_bypass",
	"serdes_rx_adapt_mode",
	"serdes_rx_adapt_sel",
	"serdes_rx_signal",
	"serdes_pma_link",
	"serdes_link_fault",
	"serdes_in_reset",
};

static int mxl862xx_xpcs_port_id(int port);

static bool mxl862xx_port_has_serdes_stats(struct dsa_switch *ds, int port)
{
	struct mxl862xx_priv *priv = ds->priv;

	return port >= 9 && port <= 16 &&
	       MXL862XX_FW_VER_MIN(priv, 1, 0, 80);
}

#define MXL862XX_SDMA_PCTRLP(p)		(0xbc0 + ((p) * 0x6))
#define MXL862XX_SDMA_PCTRL_EN		BIT(0)

#define MXL862XX_FDMA_PCTRLP(p)		(0xa80 + ((p) * 0x6))
#define MXL862XX_FDMA_PCTRL_EN		BIT(0)

#define MXL862XX_READY_TIMEOUT_MS	10000
#define MXL862XX_READY_POLL_MS		100

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
	EVLAN_DISCARD,			/* discard upstream */
	EVLAN_PVID_OR_DISCARD,		/* insert PVID tag or discard if no PVID */
	EVLAN_PVID_OR_PASS,		/* insert PVID tag or pass-through */
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
 * The 802.1Q ACCEPT rules (indices 3–4) must appear BEFORE the
 * NO_FILTER catchalls (indices 5–6). NO_FILTER matches any tag
 * regardless of TPID, so without the ACCEPT guard, it would also
 * catch standard 802.1Q VID>0 frames and corrupt them. With the
 * guard, 802.1Q VID>0 frames match the ACCEPT rules first and
 * pass through untouched; only non-8021Q TPID frames fall through
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
 * VID-specific accept rules for VLAN-unaware egress.
 * The HW sees the MxL tag as outer, real VLAN tag as inner.
 * match on inner VID with outer=NO_FILTER.
 */
static const struct mxl862xx_evlan_rule_desc vid_accept_egress_unaware[] = {
	{ FT_NO_FILTER, FT_NORMAL, TP_NONE, TP_8021Q, true, EVLAN_STRIP_IF_UNTAGGED },
	{ FT_NO_FILTER, FT_NO_TAG, TP_NONE, TP_NONE,  true, EVLAN_STRIP_IF_UNTAGGED },
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
 * ingress EVLAN block.  They match the management VID as outer 802.1Q tag
 * and reassign the frame to the user port's virtual bridge port.
 *
 * NO_FILTER is used for the inner position so that frames with any inner
 * TPID (including non-802.1Q TPIDs like 802.1ad 0x88A8) are routed
 * correctly.  The management VID tag is kept and stripped later by the
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
 * Strip the outer management VID tag from CPU→user frames that were
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
 * All flood-blocking egress sub-meters point to this one meter so that
 * any packet hitting this meter is unconditionally dropped.
 *
 * The firmware API requires CBS >= 64 (its bs2ls encoder clamps smaller
 * values), so the meter is initially configured with CBS=EBS=64.
 * A zero-rate bucket starts full at CBS bytes, which would let one packet
 * through before the bucket empties. To eliminate this one-packet leak
 * we override CBS and EBS to zero via direct register writes after the
 * API call - the hardware accepts CBS=0 and immediately flags the bucket
 * as exceeded, so no traffic can ever pass.
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


/* Per-CTP offset used for the link-local trap rule.  Each port's CTP
 * flow-table block is pre-allocated by the firmware during init (44
 * entries per port on a 10-port SKU, of which offset 0 is reserved
 * for flow-control marking).  Offset 1 is the first unused slot.
 */
#define MXL862XX_LINK_LOCAL_CTP_OFFSET		1

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
 * bridge port level (BRIDGEPORT_CONFIGSET).  The GSWIP ingress
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

/* Install a PCE rule that traps IEEE 802.1D link-local frames
 * (01:80:c2:00:00:0x) to the CPU port for a single user port,
 * preventing the hardware bridge from flooding them to other ports.
 * The firmware does not install this rule by default because its own
 * STP module is not used when DSA manages STP.
 *
 * The rule is written into the port's per-CTP flow table at offset 1.
 * The firmware already allocates a 44-entry block for every CTP during
 * init (8 entries exposed initially, expandable), so no dynamic
 * allocation via PCERULEALLOC is needed.  Using region=CTP causes the
 * firmware to translate the CTP-relative offset into an absolute
 * hardware index.
 *
 * Cross-state is enabled so that link-local frames reach the CPU even
 * when the bridge port is in BLOCKING or LEARNING state.
 */
static int mxl862xx_setup_link_local_trap(struct dsa_switch *ds, int port)
{
	DECLARE_BITMAP(portmap, MXL862XX_MAX_BRIDGE_PORTS);
	struct dsa_port *dp = dsa_to_port(ds, port);
	struct mxl862xx_priv *priv = ds->priv;
	struct mxl862xx_pce_rule rule = {};
	int cpu_port = dp->cpu_dp->index;
	struct mxl862xx_port *p;
	int i;

	p = &priv->ports[port];

	/* Address this port's CTP flow-table block */
	rule.logicalportid = port;
	rule.subifidgroup = 0;
	rule.region = cpu_to_le32(MXL862XX_PCE_RULE_CTP);

	/* Pattern: link-local MAC on this specific ingress port */
	rule.pattern.index = cpu_to_le16(MXL862XX_LINK_LOCAL_CTP_OFFSET);
	rule.pattern.enable = 1;
	rule.pattern.mac_dst_enable = 1;
	memcpy(rule.pattern.mac_dst, eth_reserved_addr_base, ETH_ALEN);
	rule.pattern.mac_dst_mask = cpu_to_le16(0x0001);

	/* Action: forward to the CPU port via explicit portmap */
	rule.action.port_map_action =
		cpu_to_le32(MXL862XX_PCE_ACTION_PORTMAP_ALTERNATIVE);

	bitmap_zero(portmap, MXL862XX_MAX_BRIDGE_PORTS);
	__set_bit(cpu_port, portmap);
	for (i = 0; i < ARRAY_SIZE(rule.action.forward_port_map); i++)
		rule.action.forward_port_map[i] =
			cpu_to_le16(bitmap_read(portmap, i * 16, 16));

	if (priv->tag_proto == DSA_TAG_PROTO_MXL862_8021Q &&
	    p->cpu_egress_evlan.in_use) {
		rule.action.extended_vlan_enable = 1;
		rule.action.extended_vlan_block_id =
			cpu_to_le16(p->cpu_egress_evlan.block_id);
	}

	/* Bypass STP port state */
	rule.action.cross_state_action =
		cpu_to_le32(MXL862XX_PCE_ACTION_CROSS_STATE_CROSS);

	return MXL862XX_API_WRITE(priv, MXL862XX_TFLOW_PCERULEWRITE,
				  rule);
}

static int mxl862xx_set_bridge_port(struct dsa_switch *ds, int port)
{
	struct mxl862xx_bridge_port_config br_port_cfg = {};
	struct dsa_port *dp = dsa_to_port(ds, port);
	struct mxl862xx_priv *priv = ds->priv;
	struct mxl862xx_port *p = &priv->ports[port];
	u16 bridge_id = dp->bridge ?
		priv->bridges[dp->bridge->num] : p->fid;
	bool enable;
	int i, idx;

	br_port_cfg.bridge_port_id = cpu_to_le16(port);
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
		u16 vf_scan = max_t(u16, p->vf.active_count, 1);

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

	mxl862xx_fw_portmap_from_bitmap(br_port_cfg.bridge_port_map, p->portmap);

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

static int mxl862xx_sync_bridge_members(struct dsa_switch *ds,
					const struct dsa_bridge *bridge)
{
	struct mxl862xx_priv *priv = ds->priv;
	struct dsa_port *dp, *member_dp;
	int err, ret = 0;

	dsa_bridge_for_each_member(dp, ds, bridge) {
		int port = dp->index;

		bitmap_zero(priv->ports[port].portmap,
			    MXL862XX_MAX_BRIDGE_PORTS);

		dsa_bridge_for_each_member(member_dp, ds, bridge) {
			if (member_dp->index != port)
				__set_bit(member_dp->index,
					  priv->ports[port].portmap);
		}
		__set_bit(mxl862xx_cpu_bridge_port_id(ds, port),
			  priv->ports[port].portmap);

		err = mxl862xx_set_bridge_port(ds, port);
		if (err)
			ret = err;
	}

	return ret;
}

static void mxl862xx_evlan_block_init(struct mxl862xx_evlan_block *blk,
				      u16 size)
{
	blk->allocated = false;
	blk->in_use = false;
	blk->block_id = 0;
	blk->block_size = size;
	blk->n_active = 0;
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

/**
 * mxl862xx_vf_init - Initialize per-port VF block software state
 * @vf: VLAN Filter block to initialize
 * @size: block size (entries per port)
 */
static void mxl862xx_vf_init(struct mxl862xx_vf_block *vf, u16 size)
{
	vf->allocated = false;
	vf->block_id = 0;
	vf->block_size = size;
	vf->active_count = 0;
	INIT_LIST_HEAD(&vf->vids);
}

/**
 * mxl862xx_vf_block_alloc - Allocate a VLAN Filter block from firmware
 * @priv: driver private data
 * @size: number of entries to allocate
 * @block_id: output -- block ID assigned by firmware
 *
 * Allocates a contiguous VLAN Filter block and configures it to discard
 * unmatched tagged frames (VID membership enforcement).
 */
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

/**
 * mxl862xx_vf_entry_discard - Write a DISCARD entry to plug an unused slot
 * @priv: driver private data
 * @block_id: HW VLAN Filter block ID
 * @index: entry index within the block
 *
 * Unwritten VLAN Filter entries default to VID=0 / ALLOW which would
 * leak VID 0 traffic. This writes a DISCARD entry to plug the slot.
 */
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

/**
 * mxl862xx_vf_alloc - Allocate the port's VF HW block
 * @priv: driver private data
 * @vf: VLAN Filter block (must have been initialized via mxl862xx_vf_init)
 *
 * Allocates the block and writes a DISCARD sentinel at index 0 so that
 * when active_count is 0, the single-entry scan window blocks VID-0
 * (which would otherwise match the zeroed-out default and be allowed).
 * Called once per port from port_setup.
 */
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

/**
 * mxl862xx_allocate_bridge - Allocate a firmware bridge instance
 * @priv: driver private data
 * @bridge_id: output -- firmware bridge ID assigned by the firmware
 *
 * Newly allocated bridges default to flooding all traffic classes
 * (unknown unicast, multicast, broadcast).  Callers that need
 * different forwarding behavior must call mxl862xx_bridge_config_fwd()
 * after allocation.
 *
 * Return: 0 on success, negative errno on failure.
 */
static int mxl862xx_allocate_bridge(struct mxl862xx_priv *priv, u16 *bridge_id)
{
	struct mxl862xx_bridge_alloc br_alloc = {};
	int ret;

	ret = MXL862XX_API_READ(priv, MXL862XX_BRIDGE_ALLOC, br_alloc);
	if (ret)
		return ret;

	*bridge_id = le16_to_cpu(br_alloc.bridge_id);
	return 0;
}

static void mxl862xx_free_bridge(struct dsa_switch *ds,
				 struct dsa_bridge *bridge)
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

static int mxl862xx_add_single_port_bridge(struct dsa_switch *ds, int port)
{
	struct mxl862xx_priv *priv = ds->priv;
	int ret;

	ret = mxl862xx_allocate_bridge(priv, &priv->ports[port].fid);
	if (ret) {
		dev_err(ds->dev, "failed to allocate a bridge for port %d\n", port);
		return ret;
	}

	priv->ports[port].learning = false;
	bitmap_zero(priv->ports[port].portmap, MXL862XX_MAX_BRIDGE_PORTS);
	__set_bit(mxl862xx_cpu_bridge_port_id(ds, port),
		  priv->ports[port].portmap);

	ret = mxl862xx_set_bridge_port(ds, port);
	if (ret)
		return ret;

	/* In tag_8021q mode the TX path goes through the bridge engine
	 * (CTP ingress EVLAN reassigns to a virtual bridge port which
	 * then forwards via the bridge).  With learning disabled on
	 * standalone ports, unknown unicast must be flooded so that
	 * frames from the host can reach the user port.
	 *
	 * In native SpTag mode, TX bypasses the bridge engine entirely
	 * (the special tag selects the egress port directly), so flood
	 * control only affects CPU-bound traffic and can be restrictive.
	 */
	if (priv->tag_proto == DSA_TAG_PROTO_MXL862_8021Q)
		return mxl862xx_bridge_config_fwd(ds, priv->ports[port].fid,
						  true, true, true);

	return mxl862xx_bridge_config_fwd(ds, priv->ports[port].fid,
					 false, false, true);
}

static int mxl862xx_setup(struct dsa_switch *ds)
{
	struct mxl862xx_priv *priv = ds->priv;
	int n_user_ports = 0, n_cpu_ports = 0, max_vlans;
	struct dsa_port *dp;
	int ret;

	ret = mxl862xx_reset(priv);
	if (ret)
		return ret;

	ret = mxl862xx_wait_ready(ds);
	if (ret)
		return ret;

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
	 *   ≤ 1024.
	 * Total VF budget:
	 *   (n_user_ports + n_cpu_ports) * vf_block_size ≤ 1024.
	 */
	dsa_switch_for_each_user_port(dp, ds)
		n_user_ports++;
	dsa_switch_for_each_cpu_port(dp, ds)
		n_cpu_ports++;

	if (n_user_ports) {
		int ingress_finals = ARRAY_SIZE(ingress_aware_final);
		int vid_rules = ARRAY_SIZE(vid_accept_standard);
		int cpu_egress_rules = ARRAY_SIZE(cpu_egress_tag_8021q);
		int cpu_ingress_per_port = ARRAY_SIZE(cpu_ingress_reassign);
		int egress_catchalls = ARRAY_SIZE(tag_8021q_egress_strip);
		int evlan_reserved;

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

	ret = mxl862xx_setup_drop_meter(ds);
	if (ret)
		return ret;

	if (!MXL862XX_FW_VER_MIN(priv, 1, 0, 80))
		dev_warn(ds->dev, "firmware < 1.0.80 installs global PCE rules "
			 "that interfere with DSA operation, please update\n");

	/* Pre-allocate firmware resources for all ports.  The DSA core
	 * calls change_tag_protocol() between setup() and port_setup(),
	 * and in tag_8021q mode that triggers dsa_tag_8021q_register()
	 * which fires tag_8021q_vlan_add callbacks that need EVLAN and
	 * VF blocks.  complete_tag_8021q_setup() also needs per-port
	 * FIDs from add_single_port_bridge().
	 *
	 * Per-port configuration (SpTag, CTP, portmaps, link-local
	 * traps) is deferred to port_setup().
	 */
	dsa_switch_for_each_cpu_port(dp, ds) {
		int port = dp->index;

		mxl862xx_vf_init(&priv->ports[port].vf,
				 priv->vf_block_size);
		mxl862xx_evlan_block_init(&priv->ports[port].ingress_evlan,
					  priv->cpu_evlan_ingress_size);
		ret = mxl862xx_evlan_block_alloc(priv,
						 &priv->ports[port].ingress_evlan);
		if (ret)
			return ret;

		ret = mxl862xx_vf_alloc(priv, &priv->ports[port].vf);
		if (ret)
			return ret;
	}

	dsa_switch_for_each_user_port(dp, ds) {
		int port = dp->index;

		mxl862xx_evlan_block_init(&priv->ports[port].ingress_evlan,
					  priv->evlan_ingress_size);
		ret = mxl862xx_evlan_block_alloc(priv,
						 &priv->ports[port].ingress_evlan);
		if (ret)
			return ret;

		mxl862xx_evlan_block_init(&priv->ports[port].egress_evlan,
					  priv->evlan_egress_size);
		ret = mxl862xx_evlan_block_alloc(priv,
						 &priv->ports[port].egress_evlan);
		if (ret)
			return ret;

		mxl862xx_vf_init(&priv->ports[port].vf,
				 priv->vf_block_size);
		ret = mxl862xx_vf_alloc(priv, &priv->ports[port].vf);
		if (ret)
			return ret;

		mxl862xx_evlan_block_init(&priv->ports[port].cpu_egress_evlan,
					  ARRAY_SIZE(cpu_egress_tag_8021q));
		ret = mxl862xx_evlan_block_alloc(priv,
						 &priv->ports[port].cpu_egress_evlan);
		if (ret)
			return ret;

		ret = mxl862xx_add_single_port_bridge(ds, port);
		if (ret)
			return ret;
	}

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
 * sits on the CPU RX path.  The VBP lives in the user port's permanent
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

	case EVLAN_DISCARD:
		cfg.treatment.remove_tag =
			cpu_to_le32(MXL862XX_EXTENDEDVLAN_TREATMENT_DISCARD_UPSTREAM);
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

	case EVLAN_PVID_OR_PASS:
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
				cpu_to_le32(MXL862XX_EXTENDEDVLAN_TREATMENT_NOT_REMOVE_TAG);
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

/**
 * mxl862xx_evlan_deactivate_entry - Reset an Extended VLAN entry to no-op
 * @priv: driver private data
 * @block_id: HW Extended VLAN block ID
 * @entry_index: entry index within the block
 *
 * Writes a zeroed-out config to the firmware, which deactivates the
 * rule (making it transparent / no-op).
 */
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

/**
 * mxl862xx_evlan_write_final_rules - Write catchall rules to the ingress block
 * @priv: driver private data
 * @blk: Extended VLAN block (already allocated)
 * @rules: array of rule descriptors for the final rules
 * @n_rules: number of final rules
 * @pvid: port VLAN ID (for PVID insertion rules)
 *
 * Writes final catchall rules starting at block_size - n_rules. With
 * VLAN Filter handling VID membership, only the ingress block uses
 * finals, and the block is sized to exactly fit them (no VID entries),
 * so the rules fill the entire block.
 */
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

/**
 * mxl862xx_vf_entry_set - Write a single VLAN Filter entry
 * @priv: driver private data
 * @block_id: HW VLAN Filter block ID
 * @index: entry index within the block
 * @vid: VLAN ID to allow
 *
 * Writes an ALLOW entry (discard_matched=false) for the given VID.
 */
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

/**
 * mxl862xx_vf_find_vid - Find a VID entry in a VF block
 * @vf: VLAN Filter block to search
 * @vid: VLAN ID to find
 */
static struct mxl862xx_vf_vid *
mxl862xx_vf_find_vid(struct mxl862xx_vf_block *vf, u16 vid)
{
	struct mxl862xx_vf_vid *ve;

	list_for_each_entry(ve, &vf->vids, list)
		if (ve->vid == vid)
			return ve;

	return NULL;
}

/**
 * mxl862xx_vf_add_vid - Add a VID to a port's VLAN Filter block
 * @priv: driver private data
 * @vf: VLAN Filter block
 * @vid: VLAN ID to add
 * @untagged: whether this VID should strip tags on egress
 *
 * Idempotent. Writes an ALLOW entry at active_count and increments
 * active_count. If the VID already exists, only the untagged flag
 * is updated. The HW block must be allocated before calling this.
 */
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

	ve = kzalloc(sizeof(*ve), GFP_KERNEL);
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

/**
 * mxl862xx_vf_del_vid - Remove a VID from a port's VLAN Filter block
 * @priv: driver private data
 * @vf: VLAN Filter block
 * @vid: VLAN ID to remove
 *
 * Swap-compacts: the last active entry is moved into the gap,
 * active_count is decremented, and the old last slot is plugged
 * with DISCARD. When active_count drops to 0, a DISCARD sentinel
 * is restored at index 0.
 */
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
		last_ve = NULL;
		list_for_each_entry(last_ve, &vf->vids, list)
			if (last_ve->index == last)
				break;

		if (WARN_ON(!last_ve || last_ve->index != last))
			return -EINVAL;

		ret = mxl862xx_vf_entry_set(priv, vf->block_id,
					    gap, last_ve->vid);
		if (ret)
			return ret;

		last_ve->index = gap;

		/* Plug the old last slot with DISCARD */
		ret = mxl862xx_vf_entry_discard(priv, vf->block_id, last);
		if (ret)
			return ret;
	} else {
		/* Deleting the last entry -- just plug it */
		ret = mxl862xx_vf_entry_discard(priv, vf->block_id, last);
		if (ret)
			return ret;
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

/**
 * mxl862xx_evlan_program_egress - Reprogram all egress tag-stripping rules
 * @priv: driver private data
 * @port: port number
 *
 * Walks the port's VF VID list and writes 2 EVLAN rules per VID that
 * needs egress tag stripping. In VLAN-aware mode only untagged VIDs
 * need rules (tagged VIDs pass through EVLAN untouched). In unaware
 * mode every VID gets rules.
 *
 * Entries are packed starting at index 0, and the scan window
 * (n_active) is narrowed so stale entries beyond it are never matched.
 */
static int mxl862xx_evlan_program_egress(struct mxl862xx_priv *priv, int port)
{
	struct mxl862xx_port *p = &priv->ports[port];
	struct mxl862xx_evlan_block *blk = &p->egress_evlan;
	const struct mxl862xx_evlan_rule_desc *vid_rules;
	struct mxl862xx_vf_vid *vfv;
	u16 old_active = blk->n_active;
	u16 idx = 0, i;
	int n_vid, ret;

	if (p->vlan_filtering) {
		vid_rules = vid_accept_standard;
		n_vid = ARRAY_SIZE(vid_accept_standard);
	} else {
		vid_rules = vid_accept_egress_unaware;
		n_vid = ARRAY_SIZE(vid_accept_egress_unaware);
	}

	list_for_each_entry(vfv, &p->vf.vids, list) {
		/* In VLAN-aware mode tagged-only VIDs need no EVLAN
		 * rules -- VLAN Filter handles membership.
		 */
		if (p->vlan_filtering && !vfv->untagged)
			continue;

		/* Skip the tag_8021q management VID -- it must NOT get
		 * per-VID egress rules.  The management VID arrives as
		 * the outer tag on CPU→user frames and is stripped by
		 * the catchall rules appended below.  A per-VID rule
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
	 * management VID tag from CPU→user frames.  The management VID
	 * is kept through the forwarding pipeline (CPU ingress EVLAN
	 * only reassigns the bridge port, without stripping) and must
	 * be removed here before the frame exits the user port.
	 */
	if (priv->tag_proto == DSA_TAG_PROTO_MXL862_8021Q) {
		int n_catch = ARRAY_SIZE(tag_8021q_egress_strip);

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
	bool changed = (p->vlan_filtering != vlan_filtering);
	int ret;

	p->vlan_filtering = vlan_filtering;

	/* Reprogram Extended VLAN rules if filtering mode changed */
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
			return ret;

		ret = mxl862xx_evlan_program_egress(priv, port);
		if (ret)
			return ret;
	}

	/* Push VLAN-based MAC learning flags and (possibly newly
	 * allocated) ingress block to hardware.
	 */
	return mxl862xx_set_bridge_port(ds, port);
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
			goto err_pvid;
	}

	/* Reprogram egress tag-stripping rules (walks VF VID list) */
	ret = mxl862xx_evlan_program_egress(priv, port);
	if (ret)
		goto err_pvid;

	/* Apply VLAN block IDs and MAC learning flags to bridge port */
	ret = mxl862xx_set_bridge_port(ds, port);
	if (ret)
		goto err_pvid;

	return 0;

err_pvid:
	p->pvid = old_pvid;
	return ret;
}

static int mxl862xx_port_vlan_del(struct dsa_switch *ds, int port,
				  const struct switchdev_obj_port_vlan *vlan)
{
	struct mxl862xx_priv *priv = ds->priv;
	struct mxl862xx_port *p = &priv->ports[port];
	u16 vid = vlan->vid;
	bool pvid_changed = false;
	int ret;

	if (dsa_is_cpu_port(ds, port))
		return 0;

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
		return ret;

	/* Reprogram egress tag-stripping rules (VID is now gone) */
	ret = mxl862xx_evlan_program_egress(priv, port);
	if (ret)
		return ret;

	/* If PVID changed, reprogram ingress finals */
	if (pvid_changed) {
		ret = mxl862xx_evlan_program_ingress(priv, port);
		if (ret)
			return ret;
	}

	return mxl862xx_set_bridge_port(ds, port);
}

static int mxl862xx_setup_cpu_bridge(struct dsa_switch *ds, int port)
{
	struct mxl862xx_priv *priv = ds->priv;
	struct mxl862xx_port *p = &priv->ports[port];
	struct dsa_port *dp;

	p->fid = MXL862XX_DEFAULT_BRIDGE;
	p->learning = true;

	/* EVLAN is left disabled on CPU ports -- frames pass through
	 * without EVLAN processing. Only the portmap and bridge
	 * assignment need to be configured.
	 */

	/* include all assigned user ports in the CPU portmap */
	bitmap_zero(p->portmap, MXL862XX_MAX_BRIDGE_PORTS);
	if (priv->tag_proto != DSA_TAG_PROTO_MXL862_8021Q) {
		dsa_switch_for_each_user_port(dp, ds) {
			/* it's safe to rely on cpu_dp being valid for user ports */
			if (dp->cpu_dp->index != port)
				continue;

			__set_bit(dp->index, p->portmap);
		}
	}

	return mxl862xx_set_bridge_port(ds, port);
}

static int mxl862xx_port_bridge_join(struct dsa_switch *ds, int port,
				     struct dsa_bridge bridge,
				     bool *tx_fwd_offload,
				     struct netlink_ext_ack *extack)
{
	struct mxl862xx_priv *priv = ds->priv;
	u16 fw_id;
	int ret;

	if (!priv->bridges[bridge.num]) {
		ret = mxl862xx_allocate_bridge(priv, &fw_id);
		if (ret)
			return ret;

		priv->bridges[bridge.num] = fw_id;
	}

	return mxl862xx_sync_bridge_members(ds, &bridge);
}

static void mxl862xx_port_bridge_leave(struct dsa_switch *ds, int port,
				       struct dsa_bridge bridge)
{
	struct mxl862xx_priv *priv = ds->priv;
	struct mxl862xx_port *p = &priv->ports[port];
	int err;

	err = mxl862xx_sync_bridge_members(ds, &bridge);
	if (err)
		dev_err(ds->dev,
			"failed to sync bridge members after port %d left: %pe\n",
			port, ERR_PTR(err));

	/* Revert leaving port, omitted by the sync above, to its
	 * single-port bridge
	 */
	bitmap_zero(p->portmap, MXL862XX_MAX_BRIDGE_PORTS);
	__set_bit(mxl862xx_cpu_bridge_port_id(ds, port), p->portmap);
	p->flood_block = 0;
	p->host_flood_block = 0;

	/* Reset VLAN state for standalone mode.  Ingress EVLAN is not
	 * needed outside a VLAN-aware bridge.  Egress EVLAN is
	 * reprogrammed below -- in tag_8021q mode it gets the
	 * management VID strip catchalls, in SpTag mode it is cleared.
	 *
	 * Do NOT clear the VF VID list here.  Bridge VLANs are already
	 * removed by port_vlan_del during the switchdev replay in
	 * dsa_port_pre_bridge_leave.  The remaining VIDs (e.g. the
	 * tag_8021q management VID) must survive bridge leave.
	 */
	p->pvid = 0;
	p->ingress_evlan.in_use = false;

	err = mxl862xx_evlan_program_egress(priv, port);
	if (err)
		dev_err(ds->dev,
			"failed to restore egress EVLAN on port %d: %pe\n",
			port, ERR_PTR(err));

	/* Push the complete standalone port state to firmware.  The
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

	dsa_switch_for_each_user_port(dp, ds) {
		mxl862xx_free_virtual_bridge_port(ds, dp->index);
		priv->ports[dp->index].tag_8021q_vid = 0;
	}

	/* Disable CPU port EVLAN engine and clear VF VID entries.
	 * The HW blocks stay allocated (freed in port_teardown).
	 */
	dsa_switch_for_each_cpu_port(dp, ds) {
		int cpu = dp->index;

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
 * with @port.  The block is pre-allocated in port_setup.  The rules insert the
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
 * every tag_8021q VID currently in use.  Called whenever a tag_8021q
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
	struct mxl862xx_priv *priv = ds->priv;
	struct mxl862xx_evlan_block *blk;
	struct dsa_port *cpu_dp, *dp;
	u16 idx, old_active;
	int cpu, ret;

	dsa_switch_for_each_cpu_port(cpu_dp, ds)
		break;

	cpu = cpu_dp->index;
	blk = &priv->ports[cpu].ingress_evlan;

	old_active = blk->n_active;
	idx = 0;

	dsa_switch_for_each_user_port(dp, ds) {
		struct mxl862xx_port *p = &priv->ports[dp->index];
		u16 vid = p->tag_8021q_vid;
		int i;

		if (!vid)
			continue;

		for (i = 0; i < ARRAY_SIZE(cpu_ingress_reassign); i++) {
			struct mxl862xx_evlan_rule_desc rule =
				cpu_ingress_reassign[i];

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
 * mxl862xx_refresh_cpu_targets - Update portmaps and traps for new CPU target
 * @ds: DSA switch
 *
 * After switching between SpTag and tag_8021q, the CPU-side target in
 * each user port's portmap changes (physical CPU port vs. virtual
 * bridge port).  This rebuilds every user port's portmap with the
 * correct CPU target and reinstalls the link-local PCE trap.
 */
static int mxl862xx_refresh_cpu_targets(struct dsa_switch *ds)
{
	struct mxl862xx_priv *priv = ds->priv;
	struct dsa_port *dp;
	int ret;

	dsa_switch_for_each_user_port(dp, ds) {
		int port = dp->index;
		struct mxl862xx_port *p = &priv->ports[port];

		bitmap_zero(p->portmap, MXL862XX_MAX_BRIDGE_PORTS);
		if (dp->bridge) {
			struct dsa_port *member_dp;

			dsa_bridge_for_each_member(member_dp, ds, dp->bridge) {
				if (member_dp->index != port)
					__set_bit(member_dp->index, p->portmap);
			}
		}
		__set_bit(mxl862xx_cpu_bridge_port_id(ds, port), p->portmap);

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
	int ret;

	/* Disable SpTag and reduce to a single CTP on CPU ports for
	 * 8021q mode.  Without a special tag the PMAC cannot select a
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
	 * level for frames from the CPU to reach user ports.  The
	 * per-port bridges may have been created with flooding
	 * disabled (SpTag mode default), so update them now.
	 *
	 * Block unknown UC and MC on the VBP egress meters so frames
	 * to unknown destinations are not flooded to the host.  DSA
	 * core will selectively enable host flooding via
	 * port_set_host_flood when needed (e.g. promisc mode).
	 */
	dsa_switch_for_each_user_port(dp, ds) {
		int port = dp->index;

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
	int ret;

	/* Flush all MAC entries on tag protocol change.  Host entries
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
				int port = dp->index;

				if (dp->bridge)
					continue;

				mxl862xx_bridge_config_fwd(ds,
							  priv->ports[port].fid,
							  false, false, true);
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
			 * ports in the CPU's bridge_port_map.  tag_8021q
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
	 * dsa2_mutex) would invert the RTNL → dsa2_mutex lock order.
	 */
}

static int mxl862xx_port_setup(struct dsa_switch *ds, int port)
{
	struct mxl862xx_priv *priv = ds->priv;
	struct dsa_port *dp = dsa_to_port(ds, port);
	bool is_cpu_port = dsa_port_is_cpu(dp);
	int ret;

	/* disable port and flush MAC entries */
	ret = mxl862xx_port_state(ds, port, false);
	if (ret)
		return ret;

	mxl862xx_port_fast_age(ds, port);

	/* skip setup for unused and DSA ports */
	if (dsa_port_is_unused(dp) ||
	    dsa_port_is_dsa(dp))
		return 0;

	/* configure tag protocol: SpTag for native, disable for 8021q */
	ret = mxl862xx_configure_sp_tag_proto(ds, port,
					      is_cpu_port &&
					      priv->tag_proto == DSA_TAG_PROTO_MXL862);
	if (ret)
		return ret;

	/* assign CTP port IDs */
	ret = mxl862xx_configure_ctp_port(ds, port, port,
					  (is_cpu_port &&
					   priv->tag_proto == DSA_TAG_PROTO_MXL862) ?
					  32 - port : 1);
	if (ret)
		return ret;

	if (is_cpu_port)
		return mxl862xx_setup_cpu_bridge(ds, port);

	/* install link-local trap for this user port */
	ret = mxl862xx_setup_link_local_trap(ds, port);
	if (ret)
		return ret;

	priv->ports[port].setup_done = true;
	return 0;
}

static void mxl862xx_port_teardown(struct dsa_switch *ds, int port)
{
	struct mxl862xx_priv *priv = ds->priv;
	struct dsa_port *dp = dsa_to_port(ds, port);

	if (dsa_port_is_unused(dp) || dsa_port_is_dsa(dp))
		return;

	/* Prevent deferred host_flood_work from acting on stale state.
	 * The flag is checked under rtnl_lock() by the worker; since
	 * teardown also runs under RTNL, this is race-free.
	 *
	 * HW EVLAN/VF blocks are not freed here — the firmware receives
	 * a full reset on the next probe, which reclaims all resources.
	 */
	priv->ports[port].setup_done = false;
}

static void mxl862xx_phylink_get_caps(struct dsa_switch *ds, int port,
				      struct phylink_config *config)
{
	config->mac_capabilities = MAC_ASYM_PAUSE | MAC_SYM_PAUSE | MAC_10 |
				   MAC_100 | MAC_1000 | MAC_2500FD;

	switch (port) {
	case 1 ... 8:
		__set_bit(PHY_INTERFACE_MODE_INTERNAL,
			  config->supported_interfaces);
		break;
	case 9:
	case 13:
		__set_bit(PHY_INTERFACE_MODE_SGMII, config->supported_interfaces);
		__set_bit(PHY_INTERFACE_MODE_1000BASEX, config->supported_interfaces);
		__set_bit(PHY_INTERFACE_MODE_2500BASEX, config->supported_interfaces);
		__set_bit(PHY_INTERFACE_MODE_10GBASER, config->supported_interfaces);
		__set_bit(PHY_INTERFACE_MODE_USXGMII, config->supported_interfaces);
		config->mac_capabilities |= MAC_10000FD | MAC_5000FD;
		fallthrough;
	case 10 ... 12:
	case 14 ... 16:
		__set_bit(PHY_INTERFACE_MODE_10G_QXGMII, config->supported_interfaces);
		break;
	default:
		break;
	}
}

static int mxl862xx_get_fid(struct dsa_switch *ds, struct dsa_db db)
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
 * mxl862xx_fdb_bridge_port - Translate port for MAC table in tag_8021q mode
 * @ds: DSA switch
 * @port: port number passed by DSA (usually the CPU port for host entries)
 * @db: database context identifying the user port or bridge
 *
 * In tag_8021q mode, host FDB/MDB entries for standalone ports must use
 * the virtual bridge port (bridge_port_cpu) as the MAC table destination
 * so that known-unicast and known-multicast frames exit through the
 * virtual bridge port's egress EVLAN, which inserts the management VID.
 * Without this, the firmware forwards known traffic directly to the
 * physical CPU bridge port, bypassing management VID insertion, and DSA
 * drops the untagged frame.
 */
static int mxl862xx_fdb_bridge_port(struct dsa_switch *ds, int port,
				    struct dsa_db db)
{
	struct mxl862xx_priv *priv = ds->priv;

	if (dsa_is_cpu_port(ds, port) && priv->tag_proto == DSA_TAG_PROTO_MXL862_8021Q &&
	    db.type == DSA_DB_PORT) {
		u16 bp_cpu = priv->ports[db.dp->index].bridge_port_cpu;

		if (bp_cpu)
			return bp_cpu;
	}

	return port;
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

/**
 * mxl862xx_mac_portmap_add - Set port bits in a MAC table entry's portmap
 * @priv: driver private data
 * @addr: MAC address
 * @fid: firmware FID
 * @vid: VLAN ID
 * @add_map: firmware-format portmap of bits to set
 *
 * Queries the existing MAC table entry by {addr, fid, vid}.  If found,
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
 * Queries the existing MAC table entry.  If not found, returns 0.
 * Clears all @del_map bits from the portmap.  If the portmap becomes
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
 * bridge_port_map, which contains only that port's own VBP.  This
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

	dsa_bridge_for_each_member(member_dp, ds, bridge)
		mxl862xx_fw_portmap_set_bit(add_map,
					    priv->ports[member_dp->index].bridge_port_cpu);

	return mxl862xx_mac_portmap_add(priv, addr, fid, vid, add_map);
}

static int mxl862xx_port_fdb_add(struct dsa_switch *ds, int port,
				 const unsigned char *addr, u16 vid, struct dsa_db db)
{
	struct mxl862xx_priv *priv = ds->priv;
	int fid, ret;

	/* tag_8021q host FDB for bridged ports: portmap with all VBPs */
	if (priv->tag_proto == DSA_TAG_PROTO_MXL862_8021Q && dsa_is_cpu_port(ds, port) &&
	    db.type == DSA_DB_BRIDGE) {
		if (!priv->bridges[db.bridge.num])
			return -ENOENT;

		return mxl862xx_mac_add_host_bridge(ds, addr, vid, &db.bridge);
	}

	/* tag_8021q standalone host FDB for bridged ports: also mirror
	 * into the bridge FID.  DSA installs VID-specific host entries
	 * via the standalone path (DSA_DB_PORT), but with IVL enabled
	 * the firmware needs matching entries in the bridge FID for
	 * VID-keyed lookups to succeed.
	 */
	if (priv->tag_proto == DSA_TAG_PROTO_MXL862_8021Q && dsa_is_cpu_port(ds, port) &&
	    db.type == DSA_DB_PORT && vid > 0) {
		struct dsa_port *target_dp = dsa_to_port(ds, db.dp->index);

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
				 const unsigned char *addr, u16 vid, struct dsa_db db)
{
	struct mxl862xx_priv *priv = ds->priv;
	int fid, ret;

	/* Mirror of the standalone→bridge FID path in fdb_add */
	if (priv->tag_proto == DSA_TAG_PROTO_MXL862_8021Q && dsa_is_cpu_port(ds, port) &&
	    db.type == DSA_DB_PORT && vid > 0) {
		struct dsa_port *target_dp = dsa_to_port(ds, db.dp->index);

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

static int mxl862xx_port_fdb_dump(struct dsa_switch *ds, int port,
				  dsa_fdb_dump_cb_t *cb, void *data)
{
	struct mxl862xx_mac_table_read param = {};
	struct mxl862xx_priv *priv = ds->priv;
	u32 entry_port_id;
	int ret;

	while (true) {
		ret = MXL862XX_API_READ(priv, MXL862XX_MAC_TABLEENTRYREAD, param);
		if (ret)
			return ret;

		if (param.last)
			break;

		entry_port_id = le32_to_cpu(param.port_id);

		if (entry_port_id == port) {
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
				 struct dsa_db db)
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
	 * and the physical port in the portmap.  The TX path goes through
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

/**
 * mxl862xx_mac_del_host_bridge - Remove VBP bits from a host FDB/MDB entry
 * @ds: DSA switch
 * @addr: MAC address
 * @vid: VLAN ID
 * @bridge: bridge whose members' VBPs to clear
 *
 * Clears all bridge member VBP bits from the portmap.  If the portmap
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

	dsa_bridge_for_each_member(member_dp, ds, bridge)
		mxl862xx_fw_portmap_set_bit(del_map,
					    priv->ports[member_dp->index].bridge_port_cpu);

	return mxl862xx_mac_portmap_del(priv, addr, fid, vid, del_map);
}

static int mxl862xx_port_mdb_del(struct dsa_switch *ds, int port,
				 const struct switchdev_obj_port_mdb *mdb,
				 struct dsa_db db)
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
	 * This is likely to get fixed in future firmware releases, however,
	 * the additional API call even then doesn't hurt much.
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
 * netif_addr_lock), so firmware calls must be deferred.  The worker
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
	bool uc, mc;

	rtnl_lock();

	/* Port may have been torn down between scheduling and now. */
	if (!p->setup_done) {
		rtnl_unlock();
		return;
	}

	uc = p->host_flood_uc;
	mc = p->host_flood_mc;

	if (priv->tag_proto == DSA_TAG_PROTO_MXL862_8021Q) {
		unsigned long block = 0;

		if (!uc)
			block |= BIT(MXL862XX_BRIDGE_PORT_EGRESS_METER_UNKNOWN_UC);
		if (!mc) {
			block |= BIT(MXL862XX_BRIDGE_PORT_EGRESS_METER_UNKNOWN_MC_IP);
			block |= BIT(MXL862XX_BRIDGE_PORT_EGRESS_METER_UNKNOWN_MC_NON_IP);
		}

		if (block != p->host_flood_block) {
			int ret;

			p->host_flood_block = block;
			ret = mxl862xx_set_cpu_vbp(ds, port);
			if (ret)
				dev_err(ds->dev,
					"failed to set host flood on port %d: %pe\n",
					port, ERR_PTR(ret));
		}
	} else {
		/* SpTag mode: per-FID forwarding, only works for
		 * standalone ports (each has its own FID).
		 */
		if (!dsa_port_bridge_dev_get(dsa_to_port(ds, port)))
			mxl862xx_bridge_config_fwd(ds, p->fid, uc, mc, true);
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
					  struct switchdev_brport_flags flags,
					  struct netlink_ext_ack *extack)
{
	if (flags.mask & ~(BR_FLOOD | BR_MCAST_FLOOD | BR_BCAST_FLOOD |
			   BR_LEARNING))
		return -EINVAL;

	return 0;
}

static int mxl862xx_port_bridge_flags(struct dsa_switch *ds, int port,
				      struct switchdev_brport_flags flags,
				      struct netlink_ext_ack *extack)
{
	struct mxl862xx_priv *priv = ds->priv;
	unsigned long old_block = priv->ports[port].flood_block;
	unsigned long block = old_block;
	bool need_update = false;
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

	need_update = (block != old_block) || (flags.mask & BR_LEARNING);
	if (need_update) {
		priv->ports[port].flood_block = block;
		ret = mxl862xx_set_bridge_port(ds, port);
		if (ret)
			return ret;
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

	if (mxl862xx_port_has_serdes_stats(ds, port)) {
		for (i = 0; i < ARRAY_SIZE(mxl862xx_serdes_stats); i++)
			ethtool_puts(&data, mxl862xx_serdes_stats[i]);
	}
}

static int mxl862xx_get_sset_count(struct dsa_switch *ds, int port, int sset)
{
	if (sset != ETH_SS_STATS)
		return 0;

	if (mxl862xx_port_has_serdes_stats(ds, port))
		return ARRAY_SIZE(mxl862xx_mib) + ARRAY_SIZE(mxl862xx_serdes_stats);

	return ARRAY_SIZE(mxl862xx_mib);
}

static int mxl862xx_read_rmon(struct dsa_switch *ds, int port,
			      struct mxl862xx_rmon_port_cnt *cnt)
{
	memset(cnt, 0, sizeof(*cnt));
	cnt->port_type = MXL862XX_CTP_PORT;
	cnt->port_id = cpu_to_le16(port);

	return MXL862XX_API_READ(ds->priv, MXL862XX_RMON_PORT_GET, *cnt);
}

static void mxl862xx_get_ethtool_stats(struct dsa_switch *ds, int port,
				       u64 *data)
{
	struct mxl862xx_rmon_port_cnt cnt;
	int ret, i;

	ret = mxl862xx_read_rmon(ds, port, &cnt);
	if (ret) {
		dev_err(ds->dev, "failed to read RMON stats on port %d\n", port);
		return;
	}

	for (i = 0; i < ARRAY_SIZE(mxl862xx_mib); i++) {
		const struct mxl862xx_mib_desc *mib = &mxl862xx_mib[i];
		void *field = (u8 *)&cnt + mib->offset;

		if (mib->size == 1)
			*data++ = le32_to_cpu(*(__le32 *)field);
		else
			*data++ = le64_to_cpu(*(__le64 *)field);
	}

	if (mxl862xx_port_has_serdes_stats(ds, port)) {
		struct mxl862xx_xpcs_eq_get eq = {
			.port_id = mxl862xx_xpcs_port_id(port),
		};
		struct mxl862xx_xpcs_signal_detect sig = {};

		sig.port_id = mxl862xx_xpcs_port_id(port);

		if (!MXL862XX_API_READ(ds->priv, MXL862XX_XPCS_EQ_GET, eq)) {
			*data++ = eq.tx.main.value;
			*data++ = eq.tx.pre.value;
			*data++ = eq.tx.post.value;
			*data++ = eq.tx.iboost_lvl.value;
			*data++ = eq.tx.vboost_lvl.value;
			*data++ = eq.tx.vboost_en.value;
			*data++ = eq.rx.att_lvl.value;
			*data++ = eq.rx.vga1_gain.value;
			*data++ = eq.rx.vga2_gain.value;
			*data++ = eq.rx.ctle_boost.value;
			*data++ = eq.rx.ctle_pole.value;
			*data++ = eq.rx.dfe_tap1.value;
			*data++ = eq.rx.dfe_bypass.value;
			*data++ = eq.rx.adapt_mode.value;
			*data++ = eq.rx.adapt_sel.value;
		} else {
			data += 15;
		}

		if (!MXL862XX_API_READ(ds->priv, MXL862XX_XPCS_SIGNAL_DETECT, sig)) {
			*data++ = sig.rx_signal;
			*data++ = sig.pma_link;
			*data++ = sig.link_fault;
			*data++ = sig.in_reset;
		} else {
			data += 4;
		}
	}
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
 * byte counters).  This function reads the hardware via MDIO (may sleep),
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
	s->tx_errors +=
		mxl862xx_delta32(tx_drop, s->prev_tx_dropped_pkts);

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
	 * No-op if the work is already pending or running.
	 */
	schedule_delayed_work(&priv->stats_work, 0);
}

static void mxl862xx_self_test(struct dsa_switch *ds, int port,
			       struct ethtool_test *etest, u64 *data)
{
	struct mxl862xx_priv *priv = ds->priv;
	int xpcs_id = mxl862xx_xpcs_port_id(port);
	int i = 0;

	if (!mxl862xx_port_has_serdes_stats(ds, port))
		return;

	/* Test 1: PCS PRBS31 */
	{
		struct mxl862xx_xpcs_prbs_cfg prbs = {};
		int ret;

		prbs.port_id = xpcs_id;
		prbs.tx_en = 1;
		prbs.rx_en = 1;
		ret = MXL862XX_API_WRITE(priv, MXL862XX_XPCS_PRBS_CFG, prbs);
		if (ret) {
			data[i++] = 1;
			goto skip_prbs;
		}

		msleep(100);

		memset(&prbs, 0, sizeof(prbs));
		prbs.port_id = xpcs_id;
		prbs.read_err = 1;
		ret = MXL862XX_API_READ(priv, MXL862XX_XPCS_PRBS_CFG, prbs);

		/* Disable PRBS */
		{
			struct mxl862xx_xpcs_prbs_cfg off = {};

			off.port_id = xpcs_id;
			MXL862XX_API_WRITE(priv, MXL862XX_XPCS_PRBS_CFG, off);
		}

		if (ret) {
			data[i++] = 1;
		} else {
			data[i] = le16_to_cpu(prbs.rx_err_cnt) ? 1 : 0;
			if (data[i])
				etest->flags |= ETH_TEST_FL_FAILED;
			i++;
		}
	}
skip_prbs:

	/* Test 2: SerDes BERT PRBS31 */
	{
		struct mxl862xx_xpcs_bert_cfg bert = {};
		int ret;

		/* Clear error counter first */
		bert.port_id = xpcs_id;
		bert.clear_err = 1;
		MXL862XX_API_WRITE(priv, MXL862XX_XPCS_BERT_CFG, bert);

		/* Enable BERT with PRBS31 pattern (6) */
		memset(&bert, 0, sizeof(bert));
		bert.port_id = xpcs_id;
		bert.tx_en = 1;
		bert.rx_en = 1;
		bert.pattern = 6; /* PRBS31 */
		ret = MXL862XX_API_WRITE(priv, MXL862XX_XPCS_BERT_CFG, bert);
		if (ret) {
			data[i++] = 1;
			goto done;
		}

		msleep(100);

		/* Read error count */
		memset(&bert, 0, sizeof(bert));
		bert.port_id = xpcs_id;
		bert.read_err = 1;
		ret = MXL862XX_API_READ(priv, MXL862XX_XPCS_BERT_CFG, bert);

		/* Disable BERT */
		{
			struct mxl862xx_xpcs_bert_cfg off = {};

			off.port_id = xpcs_id;
			MXL862XX_API_WRITE(priv, MXL862XX_XPCS_BERT_CFG, off);
		}

		if (ret) {
			data[i++] = 1;
		} else {
			data[i] = le16_to_cpu(bert.rx_err_cnt) ? 1 : 0;
			if (data[i])
				etest->flags |= ETH_TEST_FL_FAILED;
			i++;
		}
	}
done:
	return;
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
	.get_stats64 = mxl862xx_get_stats64,
	.self_test = mxl862xx_self_test,
};

static struct mxl862xx_pcs *pcs_to_mxl862xx_pcs(struct phylink_pcs *pcs)
{
	return container_of(pcs, struct mxl862xx_pcs, pcs);
}

static int mxl862xx_xpcs_port_id(int port)
{
	if (port == 9 || (port >= 10 && port <= 12))
		return 0;
	return 1;
}

static int mxl862xx_xpcs_if_mode(phy_interface_t interface)
{
	switch (interface) {
	case PHY_INTERFACE_MODE_SGMII:
		return MXL862XX_XPCS_IF_SGMII;
	case PHY_INTERFACE_MODE_1000BASEX:
		return MXL862XX_XPCS_IF_1000BASEX;
	case PHY_INTERFACE_MODE_2500BASEX:
		return MXL862XX_XPCS_IF_2500BASEX;
	case PHY_INTERFACE_MODE_USXGMII:
		return MXL862XX_XPCS_IF_USXGMII;
	case PHY_INTERFACE_MODE_10GBASER:
		return MXL862XX_XPCS_IF_10GBASER;
	case PHY_INTERFACE_MODE_10G_QXGMII:
		return MXL862XX_XPCS_IF_QSGMII;
	default:
		return -EINVAL;
	}
}

static int mxl862xx_xpcs_neg_mode(unsigned int neg_mode)
{
	if (neg_mode == PHYLINK_PCS_NEG_NONE)
		return MXL862XX_XPCS_NEG_NONE;
	if (neg_mode == PHYLINK_PCS_NEG_INBAND_DISABLED)
		return MXL862XX_XPCS_NEG_INBAND_AN_OFF;
	return MXL862XX_XPCS_NEG_INBAND_AN_ON;
}

static int mxl862xx_pcs_enable(struct phylink_pcs *pcs)
{
	struct mxl862xx_priv *priv = pcs_to_mxl862xx_pcs(pcs)->priv;
	int port = pcs_to_mxl862xx_pcs(pcs)->port;
	struct mxl862xx_xpcs_pcs_power pwr = {};

	if (port != 9 && port != 13)
		return 0;

	pwr.port_id = mxl862xx_xpcs_port_id(port);

	return MXL862XX_API_WRITE(priv, MXL862XX_XPCS_PCS_ENABLE, pwr);
}

static void mxl862xx_pcs_disable(struct phylink_pcs *pcs)
{
	struct mxl862xx_priv *priv = pcs_to_mxl862xx_pcs(pcs)->priv;
	int port = pcs_to_mxl862xx_pcs(pcs)->port;
	struct mxl862xx_xpcs_pcs_power pwr = {};

	if (port != 9 && port != 13)
		return;

	pwr.port_id = mxl862xx_xpcs_port_id(port);

	MXL862XX_API_WRITE(priv, MXL862XX_XPCS_PCS_DISABLE, pwr);
}

static void mxl862xx_pcs_pre_config(struct phylink_pcs *pcs,
				    phy_interface_t interface)
{
	struct mxl862xx_priv *priv = pcs_to_mxl862xx_pcs(pcs)->priv;
	int port = pcs_to_mxl862xx_pcs(pcs)->port;
	struct mxl862xx_xpcs_reset_cfg rst = {};

	if (port != 9 && port != 13)
		return;

	rst.port_id = mxl862xx_xpcs_port_id(port);
	rst.reset_type = MXL862XX_XPCS_RESET_VR;

	MXL862XX_API_WRITE(priv, MXL862XX_XPCS_RESET, rst);
}

static int mxl862xx_pcs_config(struct phylink_pcs *pcs, unsigned int neg_mode,
			       phy_interface_t interface,
			       const unsigned long *advertising,
			       bool permit_pause_to_mac)
{
	struct mxl862xx_priv *priv = pcs_to_mxl862xx_pcs(pcs)->priv;
	int port = pcs_to_mxl862xx_pcs(pcs)->port;
	struct mxl862xx_xpcs_pcs_cfg cfg = {};
	int if_mode, ret;

	/* Sub-interfaces are set up implicitly by the main interface */
	if (port != 9 && port != 13)
		return 0;

	if_mode = mxl862xx_xpcs_if_mode(interface);
	if (if_mode < 0) {
		dev_err(priv->ds->dev, "unsupported interface: %s\n",
			phy_modes(interface));
		return if_mode;
	}

	/* For fixed-link modes, disable AN first */
	if (neg_mode == PHYLINK_PCS_NEG_NONE ||
	    neg_mode == PHYLINK_PCS_NEG_INBAND_DISABLED) {
		struct mxl862xx_xpcs_an_disable an = {
			.port_id = mxl862xx_xpcs_port_id(port),
		};

		MXL862XX_API_WRITE(priv, MXL862XX_XPCS_AN_DISABLE, an);
	}

	cfg.port_id = mxl862xx_xpcs_port_id(port);
	cfg.interface = if_mode;
	cfg.neg_mode = mxl862xx_xpcs_neg_mode(neg_mode);
	cfg.permit_pause = permit_pause_to_mac ? 1 : 0;

	if (interface == PHY_INTERFACE_MODE_10G_QXGMII ||
	    interface == PHY_INTERFACE_MODE_USXGMII)
		cfg.usx_lane_mode = 1; /* quad */

	if (interface == PHY_INTERFACE_MODE_1000BASEX)
		cfg.advertising = cpu_to_le16(linkmode_adv_to_mii_adv_x(advertising,
							ETHTOOL_LINK_MODE_1000baseX_Full_BIT));

	ret = MXL862XX_API_WRITE(priv, MXL862XX_XPCS_PCS_CONFIG, cfg);
	if (ret)
		return ret;

	/* result > 0 means AN restart is needed */
	return le16_to_cpu(cfg.result) > 0 ? 1 : 0;
}

static void mxl862xx_pcs_get_state(struct phylink_pcs *pcs,
				   unsigned int neg_mode,
				   struct phylink_link_state *state)
{
	struct mxl862xx_priv *priv = pcs_to_mxl862xx_pcs(pcs)->priv;
	int port = pcs_to_mxl862xx_pcs(pcs)->port;
	struct mxl862xx_xpcs_pcs_state st = {};
	int if_mode, ret;

	if_mode = mxl862xx_xpcs_if_mode(state->interface);
	if (if_mode < 0)
		return;

	st.port_id = mxl862xx_xpcs_port_id(port);
	st.interface = if_mode;

	if (state->interface == PHY_INTERFACE_MODE_10G_QXGMII ||
	    state->interface == PHY_INTERFACE_MODE_USXGMII) {
		st.usx_lane_mode = 1; /* quad */
		/* Sub-ports: 10=0, 11=1, 12=2, 14=0, 15=1, 16=2 */
		if (port >= 10 && port <= 12)
			st.usx_subport = port - 10;
		else if (port >= 14 && port <= 16)
			st.usx_subport = port - 14;
	}

	ret = MXL862XX_API_READ(priv, MXL862XX_XPCS_PCS_GET_STATE, st);
	if (ret)
		return;

	state->link = st.link && !st.pcs_fault;
	state->an_complete = st.an_complete;
	state->duplex = st.duplex ? DUPLEX_FULL : DUPLEX_HALF;

	switch (le16_to_cpu(st.speed)) {
	case MXL862XX_XPCS_SPEED_10:
		state->speed = SPEED_10;
		break;
	case MXL862XX_XPCS_SPEED_100:
		state->speed = SPEED_100;
		break;
	case MXL862XX_XPCS_SPEED_1000:
		state->speed = SPEED_1000;
		break;
	case MXL862XX_XPCS_SPEED_2500:
		state->speed = SPEED_2500;
		break;
	case MXL862XX_XPCS_SPEED_5000:
		state->speed = SPEED_5000;
		break;
	case MXL862XX_XPCS_SPEED_10000:
		state->speed = SPEED_10000;
		break;
	default:
		state->speed = SPEED_UNKNOWN;
		break;
	}

	state->pause = 0;
	if (st.pause & BIT(0))
		state->pause |= MLO_PAUSE_TX | MLO_PAUSE_RX;
	if (st.pause & BIT(1))
		state->pause |= MLO_PAUSE_TX;
}

static void mxl862xx_pcs_an_restart(struct phylink_pcs *pcs)
{
	struct mxl862xx_priv *priv = pcs_to_mxl862xx_pcs(pcs)->priv;
	int port = pcs_to_mxl862xx_pcs(pcs)->port;
	struct mxl862xx_xpcs_an_restart an = {};

	if (port != 9 && port != 13)
		return;

	an.port_id = mxl862xx_xpcs_port_id(port);

	MXL862XX_API_WRITE(priv, MXL862XX_XPCS_AN_RESTART, an);
}

static void mxl862xx_pcs_link_up(struct phylink_pcs *pcs, unsigned int neg_mode,
				 phy_interface_t interface, int speed,
				 int duplex)
{
	struct mxl862xx_priv *priv = pcs_to_mxl862xx_pcs(pcs)->priv;
	int port = pcs_to_mxl862xx_pcs(pcs)->port;
	struct mxl862xx_xpcs_force_speed fs = {};

	/* Only SGMII needs explicit speed forcing */
	if (interface != PHY_INTERFACE_MODE_SGMII)
		return;

	if (port != 9 && port != 13)
		return;

	fs.port_id = mxl862xx_xpcs_port_id(port);
	fs.duplex = (duplex == DUPLEX_FULL) ? MXL862XX_XPCS_DUPLEX_FULL :
					       MXL862XX_XPCS_DUPLEX_HALF;
	fs.speed = cpu_to_le16(speed);

	MXL862XX_API_WRITE(priv, MXL862XX_XPCS_FORCE_SPEED, fs);
}

static unsigned int mxl862xx_pcs_inband_caps(struct phylink_pcs *pcs,
					     phy_interface_t interface)
{
	switch (interface) {
	case PHY_INTERFACE_MODE_10G_QXGMII:
	case PHY_INTERFACE_MODE_SGMII:
	case PHY_INTERFACE_MODE_USXGMII:
		return LINK_INBAND_ENABLE;
	case PHY_INTERFACE_MODE_1000BASEX:
		return LINK_INBAND_DISABLE | LINK_INBAND_ENABLE;
	case PHY_INTERFACE_MODE_10GBASER:
	case PHY_INTERFACE_MODE_2500BASEX:
		return LINK_INBAND_DISABLE;
	default:
		return 0;
	}
}

static const struct phylink_pcs_ops mxl862xx_pcs_ops = {
	.pcs_enable = mxl862xx_pcs_enable,
	.pcs_disable = mxl862xx_pcs_disable,
	.pcs_pre_config = mxl862xx_pcs_pre_config,
	.pcs_config = mxl862xx_pcs_config,
	.pcs_get_state = mxl862xx_pcs_get_state,
	.pcs_an_restart = mxl862xx_pcs_an_restart,
	.pcs_link_up = mxl862xx_pcs_link_up,
	.pcs_inband_caps = mxl862xx_pcs_inband_caps,
};

static struct phylink_pcs *
mxl862xx_phylink_mac_select_pcs(struct phylink_config *config,
			        phy_interface_t interface)
{
	struct dsa_port *dp = dsa_phylink_to_port(config);
	struct mxl862xx_priv *priv = dp->ds->priv;
	int port = dp->index;

	if (!MXL862XX_FW_VER_MIN(priv, 1, 0, 80))
		return NULL;

	switch (port) {
	case 9 ... 16:
		return &priv->serdes_ports[port - 9].pcs;
	default:
		return NULL;
	}
}

static void mxl862xx_phylink_mac_config(struct phylink_config *config,
					unsigned int mode,
					const struct phylink_link_state *state)
{
}

static void mxl862xx_phylink_mac_link_down(struct phylink_config *config,
					   unsigned int mode,
					   phy_interface_t interface)
{
}

static void mxl862xx_phylink_mac_link_up(struct phylink_config *config,
					 struct phy_device *phydev,
					 unsigned int mode,
					 phy_interface_t interface,
					 int speed, int duplex,
					 bool tx_pause, bool rx_pause)
{
}

static const struct phylink_mac_ops mxl862xx_phylink_mac_ops = {
	.mac_config = mxl862xx_phylink_mac_config,
	.mac_link_down = mxl862xx_phylink_mac_link_down,
	.mac_link_up = mxl862xx_phylink_mac_link_up,
	.mac_select_pcs = mxl862xx_phylink_mac_select_pcs,
};

static void mxl862xx_setup_pcs(struct mxl862xx_priv *priv, struct mxl862xx_pcs *pcs,
			       int port)
{
	pcs->priv = priv;
	pcs->port = port;

	pcs->pcs.ops = &mxl862xx_pcs_ops;
	pcs->pcs.poll = true; /* poll link changes */

	__set_bit(PHY_INTERFACE_MODE_10G_QXGMII, pcs->pcs.supported_interfaces);
	if (port != 9 && port != 13)
		return;

	__set_bit(PHY_INTERFACE_MODE_SGMII, pcs->pcs.supported_interfaces);
	__set_bit(PHY_INTERFACE_MODE_1000BASEX, pcs->pcs.supported_interfaces);
	__set_bit(PHY_INTERFACE_MODE_2500BASEX, pcs->pcs.supported_interfaces);
	__set_bit(PHY_INTERFACE_MODE_10GBASER, pcs->pcs.supported_interfaces);
	__set_bit(PHY_INTERFACE_MODE_USXGMII, pcs->pcs.supported_interfaces);
}

static int mxl862xx_probe(struct mdio_device *mdiodev)
{
	struct device *dev = &mdiodev->dev;
	struct mxl862xx_priv *priv;
	struct dsa_switch *ds;
	int i;

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

	mxl862xx_host_init(priv);

	for (i = 0; i < 8; i++)
		mxl862xx_setup_pcs(priv, &priv->serdes_ports[i], i + 9);

	for (i = 0; i < MXL862XX_MAX_PORTS; i++) {
		priv->ports[i].priv = priv;
		INIT_WORK(&priv->ports[i].host_flood_work,
			  mxl862xx_host_flood_work_fn);
		spin_lock_init(&priv->ports[i].stats_lock);
	}

	INIT_DELAYED_WORK(&priv->stats_work, mxl862xx_stats_work_fn);

	priv->tag_proto = DSA_TAG_PROTO_MXL862;

	dev_set_drvdata(dev, ds);

	return dsa_register_switch(ds);
}

static void mxl862xx_remove(struct mdio_device *mdiodev)
{
	struct dsa_switch *ds = dev_get_drvdata(&mdiodev->dev);
	struct mxl862xx_priv *priv;
	int i;

	if (!ds)
		return;

	priv = ds->priv;

	/* Tear down tag_8021q under RTNL before dsa_unregister_switch().
	 * dsa_tag_8021q_unregister() calls vlan_vid_del() which needs
	 * RTNL.  dsa_unregister_switch() takes dsa2_mutex, and other
	 * paths take RTNL → dsa2_mutex, so RTNL must be acquired
	 * before dsa2_mutex to avoid lock inversion.
	 */
	if (ds->tag_8021q_ctx) {
		rtnl_lock();
		dsa_tag_8021q_unregister(ds);
		mxl862xx_teardown_tag_8021q(ds);
		rtnl_unlock();
	}

	dsa_unregister_switch(ds);

	cancel_delayed_work_sync(&priv->stats_work);

	mxl862xx_host_shutdown(priv);

	/* Cancel any pending host flood work. dsa_unregister_switch()
	 * has already called port_teardown (which sets setup_done=false),
	 * but a worker could still be blocked on rtnl_lock().  Since we
	 * are now outside RTNL, cancel_work_sync() won't deadlock.
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
