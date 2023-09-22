/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LINUX_PCS_MTK_USXGMII_H
#define __LINUX_PCS_MTK_USXGMII_H

#include <linux/phylink.h>

/**
 * mtk_usxgmii_select_pcs() - Get MediaTek PCS instance
 * @np:		Pointer to device node indentifying a MediaTek USXGMII PCS
 * @mode:	Ethernet PHY interface mode
 *
 * Return PCS identified by a device node and the PHY interface mode in use
 *
 * Return:	Pointer to phylink PCS instance of NULL
 */
#if IS_ENABLED(CONFIG_PCS_MTK_USXGMII)
struct phylink_pcs *mtk_usxgmii_select_pcs(struct device_node *np, phy_interface_t mode);
#else
static inline struct phylink_pcs *mtk_usxgmii_select_pcs(struct device_node *np, phy_interface_t mode)
{
	return NULL
};
#endif /* IS_ENABLED(CONFIG_PCS_MTK_USXGMII) */

#endif /* __LINUX_PCS_MTK_USXGMII_H */
