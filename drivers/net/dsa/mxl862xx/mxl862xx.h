/* SPDX-License-Identifier: GPL-2.0-or-later */

#ifndef __MXL862XX_H
#define __MXL862XX_H

#include <linux/mdio.h>
#include <net/dsa.h>

#define MXL862XX_MAX_PORTS		17

typedef union {
    struct {
        u8 major;
        u8 minor;
        u16 revision;
        u32 build;
    } v;
    u64 raw;
} mxl862xx_version;

struct mxl862xx_priv {
	struct dsa_switch *ds;
	struct mdio_device *mdiodev;
	mxl862xx_version version;
};

#endif /* __MXL862XX_H */
