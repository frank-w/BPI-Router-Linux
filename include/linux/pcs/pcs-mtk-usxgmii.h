/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LINUX_PCS_MTK_USXGMII_H
#define __LINUX_PCS_MTK_USXGMII_H

#include <linux/phylink.h>

/**
 * mtk_usxgmii_select_pcs
 * Return PCS indentified by a device node and the PHY interface mode in use
 *
 * @param np	Pointer to device node indentifying a MediaTek USXGMII PCS
 * @param mode	Ethernet PHY interface mode
 *
 * @return	Pointer to phylink PCS instance of NULL
 */
struct phylink_pcs *mtk_usxgmii_select_pcs(struct device_node *np, phy_interface_t mode);

#endif /* __LINUX_PCS_MTK_USXGMII_H */
