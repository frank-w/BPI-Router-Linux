/* SPDX-License-Identifier: GPL-2.0-or-later */

#ifndef __MXL862XX_H
#define __MXL862XX_H

#include <linux/mdio.h>
#include <net/dsa.h>

#define MXL862XX_MAX_PORTS		17
#define MXL862XX_DEFAULT_BRIDGE		0
#define MXL862XX_MAX_BRIDGES		48
#define MXL862XX_MAX_BRIDGE_PORTS	128

struct mxl862xx_bridge {
	unsigned int dsa_bridge_num;
	u16 bridge_id;
	DECLARE_BITMAP(portmap, MXL862XX_MAX_BRIDGE_PORTS);
	struct list_head list;
};

struct mxl862xx_port {
	u16 fid; /* single-port bridge ID (permanent) */
	struct mxl862xx_bridge *bridge;
	DECLARE_BITMAP(portmap, MXL862XX_MAX_BRIDGE_PORTS);
	unsigned long flood_block; /* bitmask of meter indices with metering on */
	bool learning;
};

struct mxl862xx_priv {
	struct dsa_switch *ds;
	struct mdio_device *mdiodev;
	u16 drop_meter; /* single zero-rate meter shared by all ports */
	struct mxl862xx_port ports[MXL862XX_MAX_PORTS];
	struct list_head bridges;
};

#endif /* __MXL862XX_H */
