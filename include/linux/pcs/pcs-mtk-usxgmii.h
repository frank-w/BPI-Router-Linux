/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LINUX_PCS_MTK_USXGMII_H
#define __LINUX_PCS_MTK_USXGMII_H

#include <linux/phylink.h>

struct phylink_pcs *mtk_usxgmii_select_pcs(struct device_node *np, phy_interface_t mode);

#endif /* __LINUX_PCS_MTK_USXGMII_H */
