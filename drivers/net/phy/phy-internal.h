/* SPDX-License-Identifier: GPL-2.0-or-later */

#ifndef __PHY_INTERNAL_H
#define __PHY_INTERNAL_H

#include <linux/lockdep.h>
#include <linux/phy.h>

static inline int __phy_get_rate_matching(struct phy_device *phydev,
					  phy_interface_t iface)
{
	int ret = RATE_MATCH_NONE;

	lockdep_assert_held(&phydev->lock);

	if (phydev->drv->get_rate_matching)
		return phydev->drv->get_rate_matching(phydev, iface);

	return ret;
}

#endif /* __PHY_INTERNAL_H */
