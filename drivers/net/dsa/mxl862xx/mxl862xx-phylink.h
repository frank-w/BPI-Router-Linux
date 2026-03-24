/* SPDX-License-Identifier: GPL-2.0-or-later */

#ifndef __MXL862XX_PHYLINK_H
#define __MXL862XX_PHYLINK_H

#include <linux/ethtool.h>
#include <linux/phylink.h>

#include "mxl862xx.h"

extern const struct phylink_mac_ops mxl862xx_phylink_mac_ops;
void mxl862xx_phylink_get_caps(struct dsa_switch *ds, int port,
			       struct phylink_config *config);
void mxl862xx_setup_pcs(struct mxl862xx_priv *priv, struct mxl862xx_pcs *pcs,
			int port);
int mxl862xx_serdes_stats_count(struct dsa_switch *ds, int port);
void mxl862xx_serdes_get_strings(struct dsa_switch *ds, int port, u8 *data);
void mxl862xx_serdes_get_stats(struct dsa_switch *ds, int port, u64 *data);
void mxl862xx_serdes_self_test(struct dsa_switch *ds, int port,
			struct ethtool_test *etest, u64 *data);

#endif /* __MXL862XX_PHYLINK_H */
