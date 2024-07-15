/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LINUX_PCS_STANDALONE_H
#define __LINUX_PCS_STANDALONE_H

#include <linux/device.h>
#include <linux/phylink.h>
#include <linux/phy/phy.h>
#include <linux/of.h>

#if IS_ENABLED(CONFIG_PCS_STANDALONE)
int devm_pcs_register(struct device *dev, struct phylink_pcs *pcs);
struct phylink_pcs *devm_of_pcs_get(struct device *dev,
				    const struct device_node *np, unsigned int index);
#else
static inline int devm_pcs_register(struct device *dev, struct phylink_pcs *pcs);
	return -ENOTSUPP;
}
static inline struct phylink_pcs *devm_of_pcs_get(struct device *dev,
						  const struct device_node *np,
						  unsigned int index)
{
	return ERR_PTR(-ENOTSUPP);
}
#endif /* CONFIG_PCS_STANDALONE */
#endif /* __LINUX_PCS_STANDALONE_H */
