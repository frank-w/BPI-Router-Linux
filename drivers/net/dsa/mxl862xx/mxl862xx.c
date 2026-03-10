// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Driver for MaxLinear MxL862xx switch family
 *
 * Copyright (C) 2024 MaxLinear Inc.
 * Copyright (C) 2025 John Crispin <john@phrozen.org>
 * Copyright (C) 2025 Daniel Golle <daniel@makrotopia.org>
 */

#include <linux/module.h>
#include <linux/delay.h>
#include <linux/if_bridge.h>
#include <linux/of_device.h>
#include <linux/of_mdio.h>
#include <linux/phy.h>
#include <linux/phylink.h>
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

#define MXL862XX_SDMA_PCTRLP(p)		(0xbc0 + ((p) * 0x6))
#define MXL862XX_SDMA_PCTRL_EN		BIT(0)

#define MXL862XX_FDMA_PCTRLP(p)		(0xa80 + ((p) * 0x6))
#define MXL862XX_FDMA_PCTRL_EN		BIT(0)

#define MXL862XX_READY_TIMEOUT_MS	10000
#define MXL862XX_READY_POLL_MS		100

static const int mxl862xx_flood_meters[] = {
	MXL862XX_BRIDGE_PORT_EGRESS_METER_UNKNOWN_UC,
	MXL862XX_BRIDGE_PORT_EGRESS_METER_UNKNOWN_MC_IP,
	MXL862XX_BRIDGE_PORT_EGRESS_METER_UNKNOWN_MC_NON_IP,
	MXL862XX_BRIDGE_PORT_EGRESS_METER_BROADCAST,
};

static enum dsa_tag_protocol mxl862xx_get_tag_protocol(struct dsa_switch *ds,
						       int port,
						       enum dsa_tag_protocol m)
{
	return DSA_TAG_PROTO_MXL862;
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
	struct mxl862xx_bridge_config bridge_config = { };
	struct mxl862xx_priv *priv = ds->priv;
	int ret;

	bridge_config.mask = cpu_to_le32(MXL862XX_BRIDGE_CONFIG_MASK_FORWARDING_MODE);
	bridge_config.bridge_id = cpu_to_le16(bridge_id);

	bridge_config.forward_unknown_unicast = ucast_flood ?
		MXL862XX_BRIDGE_FORWARD_FLOOD : MXL862XX_BRIDGE_FORWARD_DISCARD;

	bridge_config.forward_unknown_multicast_ip = mcast_flood ?
		MXL862XX_BRIDGE_FORWARD_FLOOD : MXL862XX_BRIDGE_FORWARD_DISCARD;
	bridge_config.forward_unknown_multicast_non_ip =
		bridge_config.forward_unknown_multicast_ip;

	bridge_config.forward_broadcast = bcast_flood ?
		MXL862XX_BRIDGE_FORWARD_FLOOD : MXL862XX_BRIDGE_FORWARD_DISCARD;

	ret = MXL862XX_API_WRITE(priv, MXL862XX_BRIDGE_CONFIGSET, bridge_config);
	if (ret)
		dev_err(ds->dev, "failed to configure bridge %u forwarding: %d\n",
			bridge_id, ret);

	return ret;
}

/* Allocate a single zero-rate meter shared by all ports and flood types.
 * All flood-blocking egress sub-meters point to this one meter so that
 * the single CBS=64 token bucket fills as quickly as possible, minimising
 * the one-packet leak inherent with the hardware minimum CBS.
 */
static int mxl862xx_setup_drop_meter(struct dsa_switch *ds)
{
	struct mxl862xx_qos_meter_cfg meter = {};
	struct mxl862xx_priv *priv = ds->priv;
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

	return 0;
}

static int mxl862xx_setup(struct dsa_switch *ds)
{
	struct mxl862xx_priv *priv = ds->priv;
	int ret;

	ret = mxl862xx_reset(priv);
	if (ret)
		return ret;

	ret = mxl862xx_wait_ready(ds);
	if (ret)
		return ret;

	ret = mxl862xx_bridge_config_fwd(ds, MXL862XX_DEFAULT_BRIDGE,
					 false, false, true);
	if (ret < 0)
		return ret;

	ret = mxl862xx_setup_drop_meter(ds);
	if (ret)
		return ret;

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

static int mxl862xx_set_bridge_port(struct dsa_switch *ds, int port)
{
	struct mxl862xx_bridge_port_config br_port_cfg = {};
	struct mxl862xx_priv *priv = ds->priv;
	struct mxl862xx_port *p = &priv->ports[port];
	u16 bridge_id = p->bridge ? p->bridge->bridge_id : p->fid;
	bool enable;
	int i, idx;

	br_port_cfg.bridge_port_id = cpu_to_le16(port);
	br_port_cfg.bridge_id = cpu_to_le16(bridge_id);
	br_port_cfg.mask = cpu_to_le32(MXL862XX_BRIDGE_PORT_CONFIG_MASK_BRIDGE_ID |
				       MXL862XX_BRIDGE_PORT_CONFIG_MASK_BRIDGE_PORT_MAP |
				       MXL862XX_BRIDGE_PORT_CONFIG_MASK_MC_SRC_MAC_LEARNING |
				       MXL862XX_BRIDGE_PORT_CONFIG_MASK_EGRESS_SUB_METER);
	br_port_cfg.src_mac_learning_disable = !p->learning;

	for (i = 0; i < ARRAY_SIZE(br_port_cfg.bridge_port_map); i++)
		br_port_cfg.bridge_port_map[i] =
			cpu_to_le16(bitmap_read(p->portmap, i * 16, 16));

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

static int mxl862xx_setup_cpu_bridge(struct dsa_switch *ds, int port)
{
	struct mxl862xx_priv *priv = ds->priv;
	struct dsa_port *dp;

	priv->ports[port].fid = MXL862XX_DEFAULT_BRIDGE;
	priv->ports[port].learning = true;

	/* include all assigned user ports in the CPU portmap */
	bitmap_zero(priv->ports[port].portmap, MXL862XX_MAX_BRIDGE_PORTS);
	dsa_switch_for_each_user_port(dp, ds) {
		/* it's safe to rely on cpu_dp being valid for user ports */
		if (dp->cpu_dp->index != port)
			continue;

		__set_bit(dp->index, priv->ports[port].portmap);
	}

	return mxl862xx_set_bridge_port(ds, port);
}

static int mxl862xx_add_single_port_bridge(struct dsa_switch *ds, int port)
{
	struct dsa_port *dp = dsa_to_port(ds, port);
	struct mxl862xx_bridge_alloc br_alloc = {};
	struct mxl862xx_priv *priv = ds->priv;
	int ret;

	ret = MXL862XX_API_READ(priv, MXL862XX_BRIDGE_ALLOC, br_alloc);
	if (ret) {
		dev_err(ds->dev, "failed to allocate a bridge for port %d\n", port);
		return ret;
	}

	priv->ports[port].fid = le16_to_cpu(br_alloc.bridge_id);
	priv->ports[port].learning = false;
	bitmap_zero(priv->ports[port].portmap, MXL862XX_MAX_BRIDGE_PORTS);
	__set_bit(dp->cpu_dp->index, priv->ports[port].portmap);

	ret = mxl862xx_set_bridge_port(ds, port);
	if (ret)
		return ret;

	/* Standalone ports should not flood unknown unicast or multicast
	 * towards the CPU by default; only broadcast is needed initially.
	 */
	return mxl862xx_bridge_config_fwd(ds, priv->ports[port].fid,
					 false, false, true);
}

static struct mxl862xx_bridge *mxl862xx_allocate_bridge(struct dsa_switch *ds,
							int num)
{
	struct mxl862xx_bridge_alloc br_alloc = {};
	struct mxl862xx_priv *priv = ds->priv;
	struct mxl862xx_bridge *mxlbridge;
	int ret;

	mxlbridge = kzalloc_obj(*mxlbridge);
	if (!mxlbridge)
		return ERR_PTR(-ENOMEM);

	ret = MXL862XX_API_READ(priv, MXL862XX_BRIDGE_ALLOC, br_alloc);
	if (ret) {
		kfree(mxlbridge);
		return ERR_PTR(ret);
	}

	mxlbridge->bridge_id = le16_to_cpu(br_alloc.bridge_id);
	mxlbridge->dsa_bridge_num = num;

	ret = mxl862xx_bridge_config_fwd(ds, mxlbridge->bridge_id,
					 true, true, true);
	if (ret) {
		br_alloc.bridge_id = cpu_to_le16(mxlbridge->bridge_id);
		MXL862XX_API_WRITE(priv, MXL862XX_BRIDGE_FREE, br_alloc);
		kfree(mxlbridge);
		return ERR_PTR(ret);
	}

	list_add(&mxlbridge->list, &priv->bridges);

	return mxlbridge;
}

static void mxl862xx_free_bridge(struct dsa_switch *ds,
				 struct mxl862xx_bridge *mxlbridge)
{
	struct mxl862xx_bridge_alloc br_alloc = {
		.bridge_id = cpu_to_le16(mxlbridge->bridge_id),
	};

	MXL862XX_API_WRITE(ds->priv, MXL862XX_BRIDGE_FREE, br_alloc);
	list_del(&mxlbridge->list);
	kfree(mxlbridge);
}

static struct mxl862xx_bridge *mxl862xx_find_bridge(struct dsa_switch *ds,
						    struct dsa_bridge bridge)
{
	struct mxl862xx_priv *priv = ds->priv;
	struct mxl862xx_bridge *mxlbridge;

	if (!bridge.num)
		return NULL;

	list_for_each_entry(mxlbridge, &priv->bridges, list) {
		if (mxlbridge->dsa_bridge_num == bridge.num)
			return mxlbridge;
	}

	return NULL;
}

static int mxl862xx_update_bridge(struct dsa_switch *ds,
				  struct mxl862xx_bridge *mxlbridge,
				  int port, bool join)
{
	struct mxl862xx_priv *priv = ds->priv;
	struct dsa_port *dp;
	int member, ret;

	if (join) {
		__set_bit(port, mxlbridge->portmap);
		priv->ports[port].bridge = mxlbridge;
	} else {
		__clear_bit(port, mxlbridge->portmap);
		priv->ports[port].bridge = NULL;
	}

	/* Update all current bridge members' portmaps */
	for_each_set_bit(member, mxlbridge->portmap,
			 MXL862XX_MAX_BRIDGE_PORTS) {
		dp = dsa_to_port(ds, member);

		/* Build portmap: CPU port + all bridge members except self */
		bitmap_copy(priv->ports[member].portmap, mxlbridge->portmap,
			    MXL862XX_MAX_BRIDGE_PORTS);
		__clear_bit(member, priv->ports[member].portmap);
		__set_bit(dp->cpu_dp->index, priv->ports[member].portmap);

		priv->ports[member].learning = true;
		ret = mxl862xx_set_bridge_port(ds, member);
		if (ret)
			return ret;
	}

	/* Revert leaving port to its single-port bridge */
	if (!join) {
		dp = dsa_to_port(ds, port);

		bitmap_zero(priv->ports[port].portmap, MXL862XX_MAX_BRIDGE_PORTS);
		__set_bit(dp->cpu_dp->index, priv->ports[port].portmap);
		priv->ports[port].flood_block = 0;
		priv->ports[port].learning = false;
		ret = mxl862xx_set_bridge_port(ds, port);
		if (ret)
			return ret;

		mxl862xx_port_fast_age(ds, port);
	}

	return 0;
}

static int mxl862xx_port_bridge_join(struct dsa_switch *ds, int port,
				     struct dsa_bridge bridge,
				     bool *tx_fwd_offload,
				     struct netlink_ext_ack *extack)
{
	struct mxl862xx_bridge *mxlbridge;

	mxlbridge = mxl862xx_find_bridge(ds, bridge);
	if (!mxlbridge) {
		mxlbridge = mxl862xx_allocate_bridge(ds, bridge.num);
		if (IS_ERR(mxlbridge))
			return PTR_ERR(mxlbridge);
	}

	return mxl862xx_update_bridge(ds, mxlbridge, port, true);
}

static void mxl862xx_port_bridge_leave(struct dsa_switch *ds, int port,
				       struct dsa_bridge bridge)
{
	struct mxl862xx_bridge *mxlbridge;

	mxlbridge = mxl862xx_find_bridge(ds, bridge);
	if (!mxlbridge)
		return;

	mxl862xx_update_bridge(ds, mxlbridge, port, false);

	if (bitmap_empty(mxlbridge->portmap, MXL862XX_MAX_BRIDGE_PORTS))
		mxl862xx_free_bridge(ds, mxlbridge);
}

static int mxl862xx_port_setup(struct dsa_switch *ds, int port)
{
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

	/* configure tag protocol */
	ret = mxl862xx_configure_sp_tag_proto(ds, port, is_cpu_port);
	if (ret)
		return ret;

	/* assign CTP port IDs */
	ret = mxl862xx_configure_ctp_port(ds, port, port,
					  is_cpu_port ? 32 - port : 1);
	if (ret)
		return ret;

	if (is_cpu_port)
		/* assign user ports to CPU port bridge */
		return mxl862xx_setup_cpu_bridge(ds, port);

	/* setup single-port bridge for user ports */
	return mxl862xx_add_single_port_bridge(ds, port);
}

static void mxl862xx_phylink_get_caps(struct dsa_switch *ds, int port,
				      struct phylink_config *config)
{
	config->mac_capabilities = MAC_ASYM_PAUSE | MAC_SYM_PAUSE | MAC_10 |
				   MAC_100 | MAC_1000 | MAC_2500FD;

	__set_bit(PHY_INTERFACE_MODE_INTERNAL,
		  config->supported_interfaces);
}

static int mxl862xx_port_fdb_add(struct dsa_switch *ds, int port,
				 const unsigned char *addr, u16 vid, struct dsa_db db)
{
	struct mxl862xx_mac_table_add param = { };
	struct mxl862xx_priv *priv = ds->priv;
	struct mxl862xx_bridge *mxlbridge;
	u16 fid;
	int ret;

	switch (db.type) {
	case DSA_DB_PORT:
		fid = priv->ports[db.dp->index].fid;
		break;

	case DSA_DB_BRIDGE:
		mxlbridge = mxl862xx_find_bridge(ds, db.bridge);
		if (!mxlbridge)
			return -ENOENT;
		fid = mxlbridge->bridge_id;
		break;

	default:
		return -EOPNOTSUPP;
	}

	param.port_id = cpu_to_le32(port);
	param.fid = cpu_to_le16(fid);
	param.static_entry = true;
	param.tci = cpu_to_le16(vid & 0xFFF);
	memcpy(param.mac, addr, ETH_ALEN);

	ret = MXL862XX_API_WRITE(priv, MXL862XX_MAC_TABLEENTRYADD, param);
	if (ret)
		dev_err(ds->dev, "failed to add FDB entry on port %d\n", port);

	return ret;
}

static int mxl862xx_port_fdb_del(struct dsa_switch *ds, int port,
				 const unsigned char *addr, u16 vid, struct dsa_db db)
{
	struct mxl862xx_mac_table_remove param = { };
	struct mxl862xx_priv *priv = ds->priv;
	struct mxl862xx_bridge *mxlbridge;
	u16 fid;
	int ret;

	switch (db.type) {
	case DSA_DB_PORT:
		fid = priv->ports[db.dp->index].fid;
		break;

	case DSA_DB_BRIDGE:
		/* Use multi-port bridge FID */
		mxlbridge = mxl862xx_find_bridge(ds, db.bridge);
		if (!mxlbridge)
			return -ENOENT;
		fid = mxlbridge->bridge_id;
		break;

	default:
		return -EOPNOTSUPP;
	}

	param.fid = cpu_to_le16(fid);
	param.tci = cpu_to_le16(vid & 0xFFF);
	memcpy(param.mac, addr, ETH_ALEN);

	ret = MXL862XX_API_WRITE(priv, MXL862XX_MAC_TABLEENTRYREMOVE, param);
	if (ret)
		dev_err(ds->dev, "failed to remove FDB entry on port %d\n", port);

	return ret;
}

static int mxl862xx_port_fdb_dump(struct dsa_switch *ds, int port,
				  dsa_fdb_dump_cb_t *cb, void *data)
{
	struct mxl862xx_mac_table_read param = { };
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

		if (entry_port_id == port)
			cb(param.mac, param.tci & 0x0FFF,
			   param.static_entry, data);

		memset(&param, 0, sizeof(param));
	}

	return 0;
}

static int mxl862xx_port_mdb_add(struct dsa_switch *ds, int port,
				 const struct switchdev_obj_port_mdb *mdb,
				 struct dsa_db db)
{
	return mxl862xx_port_fdb_add(ds, port, mdb->addr, mdb->vid, db);
}

static int mxl862xx_port_mdb_del(struct dsa_switch *ds, int port,
				 const struct switchdev_obj_port_mdb *mdb,
				 struct dsa_db db)
{
	return mxl862xx_port_fdb_del(ds, port, mdb->addr, mdb->vid, db);
}

static int mxl862xx_set_ageing_time(struct dsa_switch *ds, unsigned int msecs)
{
	struct mxl862xx_cfg param;
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
		param.port_state = MXL862XX_STP_PORT_STATE_DISABLE;
		break;
	case BR_STATE_BLOCKING:
	case BR_STATE_LISTENING:
		param.port_state = MXL862XX_STP_PORT_STATE_BLOCKING;
		break;
	case BR_STATE_LEARNING:
		param.port_state = MXL862XX_STP_PORT_STATE_LEARNING;
		break;
	case BR_STATE_FORWARDING:
		param.port_state = MXL862XX_STP_PORT_STATE_FORWARD;
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
		dev_err(ds->dev, "failed to reapply brport flags on port %d\n", port);
}

static void mxl862xx_port_set_host_flood(struct dsa_switch *ds, int port,
					 bool uc, bool mc)
{
	struct mxl862xx_priv *priv = ds->priv;

	if (priv->ports[port].bridge)
		return;

	/* Standalone ports must always keep broadcast flooding enabled
	 * towards the CPU so that ARP and other broadcast protocols work.
	 * Only unknown unicast and multicast should follow the host flood
	 * knobs driven by IFF_PROMISC / IFF_ALLMULTI.
	 */
	mxl862xx_bridge_config_fwd(ds, priv->ports[port].fid, uc, mc, true);
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

static const struct dsa_switch_ops mxl862xx_switch_ops = {
	.get_tag_protocol = mxl862xx_get_tag_protocol,
	.setup = mxl862xx_setup,
	.port_setup = mxl862xx_port_setup,
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
};

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
};

static int mxl862xx_probe(struct mdio_device *mdiodev)
{
	struct device *dev = &mdiodev->dev;
	struct mxl862xx_priv *priv;
	struct dsa_switch *ds;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->mdiodev = mdiodev;

	INIT_LIST_HEAD(&priv->bridges);

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

	dev_set_drvdata(dev, ds);

	return dsa_register_switch(ds);
}

static void mxl862xx_remove(struct mdio_device *mdiodev)
{
	struct dsa_switch *ds = dev_get_drvdata(&mdiodev->dev);

	if (!ds)
		return;

	dsa_unregister_switch(ds);
}

static void mxl862xx_shutdown(struct mdio_device *mdiodev)
{
	struct dsa_switch *ds = dev_get_drvdata(&mdiodev->dev);

	if (!ds)
		return;

	dsa_switch_shutdown(ds);

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
