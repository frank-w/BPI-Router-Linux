// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Helpers for standalone PCS drivers
 *
 * Copyright (C) 2024 Daniel Golle <daniel@makrotopia.org>
 */

#include <linux/pcs/pcs-standalone.h>
#include <linux/phylink.h>

static LIST_HEAD(pcs_list);
static DEFINE_MUTEX(pcs_mutex);

static const struct phy_ops phylink_pcs_phy_ops = {
	.owner = THIS_MODULE,
};

struct pcs_standalone {
	struct device *dev;
	struct phylink_pcs *pcs;
	struct list_head list;
};

static void devm_pcs_provider_release(struct device *dev, void *res)
{
	struct pcs_standalone *pcssa = (struct pcs_standalone *)res;

	mutex_lock(&pcs_mutex);
	list_del(&pcssa->list);
	mutex_unlock(&pcs_mutex);
}

int devm_pcs_register(struct device *dev, struct phylink_pcs *pcs)
{
	struct pcs_standalone *pcssa;

	pcssa = devres_alloc(devm_pcs_provider_release, sizeof(*pcssa),
			     GFP_KERNEL);
	if (!pcssa)
		return -ENOMEM;

	devres_add(dev, pcssa);
	pcssa->pcs = pcs;
	pcssa->dev = dev;

	mutex_lock(&pcs_mutex);
	list_add_tail(&pcssa->list, &pcs_list);
	mutex_unlock(&pcs_mutex);

	return 0;
}
EXPORT_SYMBOL_GPL(devm_pcs_register);

static struct pcs_standalone *of_pcs_locate(const struct device_node *_np, u32 index)
{
	struct device_node *np;
	struct pcs_standalone *iter, *pcssa = NULL;

	if (!_np)
		return NULL;

	np = of_parse_phandle(_np, "pcs-handle", index);
	if (!np)
		return NULL;

	mutex_lock(&pcs_mutex);
	list_for_each_entry(iter, &pcs_list, list) {
		if (iter->dev->of_node != np)
			continue;

		pcssa = iter;
		break;
	}
	mutex_unlock(&pcs_mutex);

	of_node_put(np);

	return pcssa ?: ERR_PTR(-ENODEV);
}

struct phylink_pcs *devm_of_pcs_get(struct device *dev,
				    const struct device_node *np,
				    unsigned int index)
{
	struct pcs_standalone *pcssa;

	pcssa = of_pcs_locate(np ?: dev->of_node, index);
	if (IS_ERR_OR_NULL(pcssa))
		return ERR_PTR(PTR_ERR(pcssa));

	device_link_add(dev, pcssa->dev, DL_FLAG_AUTOREMOVE_CONSUMER);

	return pcssa->pcs;
}
EXPORT_SYMBOL_GPL(devm_of_pcs_get);

MODULE_DESCRIPTION("Helper for standalone PCS drivers");
MODULE_AUTHOR("Daniel Golle <daniel@makrotopia.org>");
MODULE_LICENSE("GPL");
