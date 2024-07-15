/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LINUX_PCS_STANDALONE_H
#define __LINUX_PCS_STANDALONE_H

#include <linux/device.h>
#include <linux/netdevice.h>
#include <linux/of.h>
#include <linux/phylink.h>
#include <linux/phy/phy.h>

#if IS_ENABLED(CONFIG_PCS_STANDALONE)
int devm_pcs_register(struct device *dev,
		      uint32_t index,
		      struct phylink_pcs *pcs);
struct phylink_pcs *devm_of_pcs_select(struct device *dev,
				       struct net_device *netdev,
				       const struct device_node *np,
				       phy_interface_t interface);
int of_pcs_unavailable(struct device_node *np);
#else
static inline int devm_pcs_register(struct device *dev,
				    uint32_t index,
				    struct phylink_pcs *pcs)
{
	return -ENOTSUPP;
}

static inline struct phylink_pcs *devm_of_pcs_select(struct device *dev,
						     struct net_device *netdev,
						     const struct device_node *np,
						     phy_interface_t interface)
{
	return ERR_PTR(-ENOTSUPP);
}

static inline int of_pcs_unavailable(struct device_node *np) {
	return -ENOTSUPP;
}
#endif /* CONFIG_PCS_STANDALONE */
#endif /* __LINUX_PCS_STANDALONE_H */
