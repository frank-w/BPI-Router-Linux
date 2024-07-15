// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Helpers for standalone PCS drivers
 *
 * Copyright (C) 2025 Daniel Golle <daniel@makrotopia.org>
 */

#include <linux/mutex.h>
#include <linux/pcs/pcs-standalone.h>
#include <linux/phylink.h>
#include <linux/rtnetlink.h>

static LIST_HEAD(pcs_list);
static DEFINE_MUTEX(pcs_mutex);

/* owned by PCS user */
struct pcs_standalone_user {
	struct phylink_pcs proxy_pcs;
	struct phylink_pcs_ops proxy_pcs_ops;
	struct pcs_standalone *pcssa;
	struct net_device *netdev;
	bool enabled;
	struct mutex mutex;
};

/* owned by PCS provider */
struct pcs_standalone {
	struct pcs_standalone_user *user;
	struct phylink_pcs *pcs;
	struct device *dev;
	uint32_t index;
	struct list_head list;
};

static int pcssa_pcs_validate(struct phylink_pcs *pcs, unsigned long *supported,
			      const struct phylink_link_state *state)
{
	struct pcs_standalone_user *user = container_of(pcs, struct pcs_standalone_user, proxy_pcs);
	int ret = -ENODEV;

	mutex_lock(&user->mutex);
	if (user->pcssa && user->pcssa->pcs && user->pcssa->pcs->ops->pcs_validate)
		ret = user->pcssa->pcs->ops->pcs_validate(user->pcssa->pcs, supported, state);
	mutex_unlock(&user->mutex);

	return ret;
}

static unsigned int pcssa_pcs_inband_caps(struct phylink_pcs *pcs,
					  phy_interface_t interface)
{
	struct pcs_standalone_user *user = container_of(pcs, struct pcs_standalone_user, proxy_pcs);
	int ret = 0;

	mutex_lock(&user->mutex);
	if (user->pcssa && user->pcssa->pcs && user->pcssa->pcs->ops->pcs_inband_caps)
		ret = user->pcssa->pcs->ops->pcs_inband_caps(user->pcssa->pcs, interface);
	mutex_unlock(&user->mutex);

	return ret;
}

static int pcssa_pcs_enable(struct phylink_pcs *pcs)
{
	struct pcs_standalone_user *user = container_of(pcs, struct pcs_standalone_user, proxy_pcs);
	int ret = 0;

	mutex_lock(&user->mutex);
	if (user->pcssa && user->pcssa->pcs && user->pcssa->pcs->ops->pcs_enable)
		ret = user->pcssa->pcs->ops->pcs_enable(user->pcssa->pcs);

	if (!ret)
		user->enabled = true;

	mutex_unlock(&user->mutex);

	return ret;
}

static void pcssa_pcs_disable(struct phylink_pcs *pcs)
{
	struct pcs_standalone_user *user = container_of(pcs, struct pcs_standalone_user, proxy_pcs);

	mutex_lock(&user->mutex);
	if (user->pcssa && user->pcssa->pcs && user->pcssa->pcs->ops->pcs_disable)
		user->pcssa->pcs->ops->pcs_disable(user->pcssa->pcs);

	user->enabled = false;
	mutex_unlock(&user->mutex);
}

static void pcssa_pcs_pre_config(struct phylink_pcs *pcs,
				 phy_interface_t interface)
{
	struct pcs_standalone_user *user = container_of(pcs, struct pcs_standalone_user, proxy_pcs);

	mutex_lock(&user->mutex);
	if (user->pcssa && user->pcssa->pcs && user->pcssa->pcs->ops->pcs_pre_config)
		user->pcssa->pcs->ops->pcs_pre_config(user->pcssa->pcs, interface);
	mutex_unlock(&user->mutex);
}

static int pcssa_pcs_post_config(struct phylink_pcs *pcs,
				 phy_interface_t interface)
{
	struct pcs_standalone_user *user = container_of(pcs, struct pcs_standalone_user, proxy_pcs);
	int ret = -ENODEV;

	mutex_lock(&user->mutex);
	if (user->pcssa && user->pcssa->pcs && user->pcssa->pcs->ops->pcs_post_config)
		ret = user->pcssa->pcs->ops->pcs_post_config(user->pcssa->pcs, interface);
	mutex_unlock(&user->mutex);

	return ret;
}

static void pcssa_pcs_get_state(struct phylink_pcs *pcs, unsigned int neg_mode,
				struct phylink_link_state *state)
{
	struct pcs_standalone_user *user = container_of(pcs, struct pcs_standalone_user, proxy_pcs);

	mutex_lock(&user->mutex);
	if (user->pcssa && user->pcssa->pcs && user->pcssa->pcs->ops->pcs_get_state)
		user->pcssa->pcs->ops->pcs_get_state(user->pcssa->pcs, neg_mode, state);
	else {
		linkmode_zero(state->advertising);
		linkmode_zero(state->lp_advertising);
		state->interface = PHY_INTERFACE_MODE_NA;
		state->speed = SPEED_UNKNOWN;
		state->duplex = DUPLEX_UNKNOWN;
		state->pause = 0;
		state->rate_matching = RATE_MATCH_NONE;
		state->link = false;
		state->an_complete = false;
	}
	mutex_unlock(&user->mutex);
}

static int pcssa_pcs_config(struct phylink_pcs *pcs, unsigned int neg_mode,
			    phy_interface_t interface,
			    const unsigned long *advertising,
			    bool permit_pause_to_mac)
{
	struct pcs_standalone_user *user = container_of(pcs, struct pcs_standalone_user, proxy_pcs);
	int ret = -ENODEV;

	mutex_lock(&user->mutex);
	if (user->pcssa && user->pcssa->pcs && user->pcssa->pcs->ops->pcs_config)
		ret = user->pcssa->pcs->ops->pcs_config(user->pcssa->pcs, neg_mode, interface,
						  advertising, permit_pause_to_mac);
	mutex_unlock(&user->mutex);

	return ret;
}

static void pcssa_pcs_an_restart(struct phylink_pcs *pcs)
{
	struct pcs_standalone_user *user = container_of(pcs, struct pcs_standalone_user, proxy_pcs);

	mutex_lock(&user->mutex);
	if (user->pcssa && user->pcssa->pcs && user->pcssa->pcs->ops->pcs_an_restart)
		user->pcssa->pcs->ops->pcs_an_restart(user->pcssa->pcs);
	mutex_unlock(&user->mutex);
}

static void pcssa_pcs_link_up(struct phylink_pcs *pcs, unsigned int neg_mode,
			      phy_interface_t interface, int speed, int duplex)
{
	struct pcs_standalone_user *user = container_of(pcs, struct pcs_standalone_user, proxy_pcs);

	mutex_lock(&user->mutex);
	if (user->pcssa && user->pcssa->pcs && user->pcssa->pcs->ops->pcs_link_up)
		user->pcssa->pcs->ops->pcs_link_up(user->pcssa->pcs, neg_mode, interface, speed, duplex);
	mutex_unlock(&user->mutex);
}

static void pcssa_pcs_disable_eee(struct phylink_pcs *pcs)
{
	struct pcs_standalone_user *user = container_of(pcs, struct pcs_standalone_user, proxy_pcs);

	mutex_lock(&user->mutex);
	if (user->pcssa && user->pcssa->pcs && user->pcssa->pcs->ops->pcs_disable_eee)
		user->pcssa->pcs->ops->pcs_disable_eee(user->pcssa->pcs);
	mutex_unlock(&user->mutex);
}

static void pcssa_pcs_enable_eee(struct phylink_pcs *pcs)
{
	struct pcs_standalone_user *user = container_of(pcs, struct pcs_standalone_user, proxy_pcs);

	mutex_lock(&user->mutex);
	if (user->pcssa && user->pcssa->pcs && user->pcssa->pcs->ops->pcs_enable_eee)
		user->pcssa->pcs->ops->pcs_enable_eee(user->pcssa->pcs);
	mutex_unlock(&user->mutex);
}

static int pcssa_pcs_pre_init(struct phylink_pcs *pcs)
{
	struct pcs_standalone_user *user = container_of(pcs, struct pcs_standalone_user, proxy_pcs);
	int ret = -ENODEV;

	mutex_lock(&user->mutex);
	if (user->pcssa && user->pcssa->pcs && user->pcssa->pcs->ops->pcs_pre_init)
		ret = user->pcssa->pcs->ops->pcs_pre_init(user->pcssa->pcs);
	mutex_unlock(&user->mutex);

	return ret;
}

/**
 * devm_pcs_provider_release - Release the PCS provider resources.
 * @dev: The device associated with the PCS provider.
 * @res: The resource to be released, cast to a pcs_standalone structure.
 *
 * This function releases the resources associated with a PCS provider.
 * Is is used as the release function for devm_pcs_register().
 *
 * This function is intended to be used with devres_alloc, which manages
 * the allocation and release of resources associated with a device.
 */
static void devm_pcs_provider_release(struct device *dev, void *res)
{
	struct pcs_standalone *pcssa = (struct pcs_standalone *)res;
	struct pcs_standalone_user *user;

	mutex_lock(&pcs_mutex);
	user = pcssa->user;
	pcssa->pcs = NULL;
	list_del(&pcssa->list);
	mutex_unlock(&pcs_mutex);

	if (!user)
		return;

	mutex_lock(&user->mutex);
	user->pcssa = NULL;
	memset(&user->proxy_pcs.supported_interfaces, 0, sizeof(user->proxy_pcs.supported_interfaces));
	mutex_unlock(&user->mutex);

	if (user->enabled) {
		rtnl_lock();
		dev_close(user->netdev);
		rtnl_unlock();
	}
}


/**
 * devm_pcs_user_release - Release a PCS standalone user
 * @dev: The device associated with the PCS standalone user
 * @res: The resource to be released, cast to a pcs_standalone_user structure
 *
 * This function releases a PCS standalone user by setting the user field
 * of the associated pcs_standalone structure to NULL. It locks the pcs_mutex
 * to ensure thread safety while modifying the user field.
 *
 * This function is intended to be used with devres_alloc, which manages
 * the allocation and release of resources associated with a device.
 */
static void devm_pcs_user_release(struct device *dev, void *res)
{
	struct pcs_standalone_user *user = (struct pcs_standalone_user *)res;
	struct pcs_standalone *pcssa = user->pcssa;

	mutex_lock(&pcs_mutex);
	pcssa->user = NULL;
	mutex_unlock(&pcs_mutex);
}

/**
 * devm_pcs_register - Register a phylink PCS (Physical Coding Sublayer) device
 * @dev: The device to which the PCS is associated
 * @index: The index of the PCS
 * @pcs: Pointer to the phylink_pcs structure
 *
 * This function registers a PCS device with the given device and index.
 * It allocates memory for a pcs_standalone structure, initializes it,
 * and adds it to the global PCS list. The function ensures that the
 * registration process is thread-safe by using a mutex.
 *
 * Return: 0 on success, -ENOMEM if memory allocation fails.
 */
int devm_pcs_register(struct device *dev, uint32_t index, struct phylink_pcs *pcs)
{
	struct pcs_standalone *pcssa;

	pcssa = devres_alloc(devm_pcs_provider_release, sizeof(*pcssa),
			     GFP_KERNEL);
	if (!pcssa)
		return -ENOMEM;

	devres_add(dev, pcssa);
	pcssa->pcs = pcs;
	pcssa->dev = dev;
	pcssa->index = index;

	mutex_lock(&pcs_mutex);
	list_add_tail(&pcssa->list, &pcs_list);
	mutex_unlock(&pcs_mutex);

	return 0;
}
EXPORT_SYMBOL_GPL(devm_pcs_register);

/**
 * pcs_standalone_populate_user - Populate the pcs_standalone_user structure
 * @user: Pointer to the pcs_standalone_user structure to be populated
 * @pcssa: Pointer to the pcs_standalone structure containing the original PCS
 *
 * This function initializes the pcs_standalone_user structure by copying
 * relevant data from the pcs_standalone structure. It sets up the proxy PCS
 * operations and copies the supported interfaces. The function also initializes
 * the mutex for the user structure.
 *
 * The following operations are set up if they are defined for the calling
 * phylink_pcs:
 * - pcs_validate
 * - pcs_inband_caps
 * - pcs_pre_config
 * - pcs_post_config
 * - pcs_config
 * - pcs_an_restart
 * - pcs_link_up
 * - pcs_disable_eee
 * - pcs_enable_eee
 * - pcs_pre_init
 *
 * The proxy PCS structure is then configured with the copied operations, poll
 * function, and rxc_always_on flag.
 */
static void pcs_standalone_populate_user(struct pcs_standalone_user *user,
					 struct pcs_standalone *pcssa)
{
	struct phylink_pcs *pcs = pcssa->pcs;
	struct phylink_pcs *proxy = &user->proxy_pcs;
	struct phylink_pcs_ops *ops = &user->proxy_pcs_ops;

	mutex_init(&user->mutex);

	user->pcssa = pcssa;

	memcpy(&user->proxy_pcs.supported_interfaces, &pcs->supported_interfaces,
		sizeof(pcs->supported_interfaces));

	ops->pcs_enable = pcssa_pcs_enable;
	ops->pcs_disable = pcssa_pcs_disable;
	ops->pcs_get_state = pcssa_pcs_get_state;

	if (pcs->ops->pcs_validate)
		ops->pcs_validate = pcssa_pcs_validate;

	if (pcs->ops->pcs_inband_caps)
		ops->pcs_inband_caps = pcssa_pcs_inband_caps;

	if (pcs->ops->pcs_pre_config)
		ops->pcs_pre_config = pcssa_pcs_pre_config;

	if (pcs->ops->pcs_post_config)
		ops->pcs_post_config = pcssa_pcs_post_config;

	if (pcs->ops->pcs_config)
		ops->pcs_config = pcssa_pcs_config;

	if (pcs->ops->pcs_an_restart)
		ops->pcs_an_restart = pcssa_pcs_an_restart;

	if (pcs->ops->pcs_link_up)
		ops->pcs_link_up = pcssa_pcs_link_up;

	if (pcs->ops->pcs_disable_eee)
		ops->pcs_disable_eee = pcssa_pcs_disable_eee;

	if (pcs->ops->pcs_enable_eee)
		ops->pcs_enable_eee = pcssa_pcs_enable_eee;

	if (pcs->ops->pcs_pre_init)
		ops->pcs_pre_init = pcssa_pcs_pre_init;

	proxy->ops = ops;
	proxy->poll = pcs->poll;
	proxy->rxc_always_on = pcs->rxc_always_on;
}

/**
 * pcs_standalone_get - Retrieve a pcs_standalone structure based on device node, index, and interface.
 * @np: Pointer to the device node to match.
 * @index: Index to match, or -1 to ignore index.
 * @interface: PHY interface type to match, or PHY_INTERFACE_MODE_NA to ignore interface.
 *
 * This function searches through the list of pcs_standalone structures and returns the one
 * that matches the given device node, index, and interface. If no matching structure is found,
 * it returns NULL.
 *
 * Return: Pointer to the matching pcs_standalone structure, or NULL if no match is found.
 */
static struct pcs_standalone *pcs_standalone_get(const struct device_node *np,
						 int index,
						 phy_interface_t interface)
{
	struct pcs_standalone *iter, *pcssa = NULL;

	mutex_lock(&pcs_mutex);
	list_for_each_entry(iter, &pcs_list, list) {
		if (iter->dev->of_node != np)
			continue;

		if (index >= 0 && iter->index != index)
			continue;

		if (interface != PHY_INTERFACE_MODE_NA &&
		    !test_bit(interface, iter->pcs->supported_interfaces))
			continue;

		pcssa = iter;
		break;
	}
	mutex_unlock(&pcs_mutex);

	return pcssa;
}

/**
 * of_pcs_match_interface - Matches a PCS standalone interface from a device node
 * @np: Pointer to the device node
 * @interface: PHY interface type
 *
 * This function searches for a PCS standalone interface that matches the given
 * device node and PHY interface type. It parses the "pcs-handle" property of the
 * device node to find an appropriate PCS standalone instance.
 *
 * Return: Pointer to the matched pcs_standalone structure on success, or an
 * ERR_PTR on failure.
 * Possible error codes:
 * - -ENODEV: No device node provided
 * - -ENOENT: No PCS handle found
 * - -EINVAL: Invalid PCS handle arguments
 */
static struct pcs_standalone *of_pcs_match_interface(const struct device_node *np,
						     phy_interface_t interface)
{
	struct pcs_standalone *pcssa = NULL;
	struct of_phandle_args args;
	int count, i, ret = -ENODEV;

	if (!np)
		return NULL;

	count = of_count_phandle_with_args(np, "pcs-handle", "#pcs-cells");
	if (count < 0)
		return ERR_PTR(count);

	if (count == 0)
		return ERR_PTR(-ENOENT);

	for (i = 0; i < count; i++) {
		ret = of_parse_phandle_with_args(np, "pcs-handle", "#pcs-cells", i, &args);
		if (ret == -ENOENT)
			continue;

		if (ret)
			return ERR_PTR(ret);

		if (args.args_count > 1) {
			of_node_put(args.np);
			return ERR_PTR(-EINVAL);
		}

		pcssa = pcs_standalone_get(args.np,
					   args.args_count ? args.args[0] : -1,
					   interface);
		if (pcssa)
			break;
	}

	if (!pcssa)
		return ERR_PTR(-ENOENT);

	of_node_put(args.np);

	return pcssa;
}

/**
 * devm_of_pcs_select - Select and initialize a phylink PCS (Physical Coding Sublayer)
 * @dev: The device pointer
 * @netdev: The network device pointer
 * @np: The device node pointer
 * @interface: The PHY interface type
 *
 * This function selects a PCS based on the provided device node and interface type.
 * It ensures that the PCS is not already in use by another network device. If the PCS
 * is available, it allocates and initializes a pcs_standalone_user structure to manage
 * the PCS for the given network device.
 *
 * Return: A pointer to the phylink_pcs structure on success, or an ERR_PTR on failure.
 * Possible error codes:
 * - -EINVAL: The netdev parameter is NULL.
 * - -ENOMEM: Memory allocation for pcs_standalone_user failed.
 * - -EBUSY: The PCS is already in use by another network device.
 */
struct phylink_pcs *devm_of_pcs_select(struct device *dev,
				       struct net_device *netdev,
				       const struct device_node *np,
				       phy_interface_t interface)
{
	struct pcs_standalone_user *user;
	struct pcs_standalone *pcssa;

	pcssa = of_pcs_match_interface(np, interface);
	if (IS_ERR_OR_NULL(pcssa))
		return ERR_CAST(pcssa);

	mutex_lock(&pcs_mutex);
	if (pcssa->user) {
		if (WARN_ON(pcssa->user->netdev != netdev)) {
			mutex_unlock(&pcs_mutex);
			return ERR_PTR(-EBUSY);
		}
		mutex_unlock(&pcs_mutex);
		return &pcssa->user->proxy_pcs;
	}
	mutex_unlock(&pcs_mutex);

	user = devres_alloc(devm_pcs_user_release, sizeof(*user), GFP_KERNEL);
	if (!user)
		return ERR_PTR(-ENOMEM);

	devres_add(dev, user);

	mutex_lock(&pcs_mutex);
	if (pcssa->user) {
		WARN_ON(pcssa->user->netdev != netdev);
		mutex_unlock(&pcs_mutex);
		devm_kfree(dev, user);
		return ERR_PTR(-EBUSY);
	}
	pcssa->user = user;
	mutex_unlock(&pcs_mutex);

	pcs_standalone_populate_user(user, pcssa);
	user->netdev = netdev;

	return &user->proxy_pcs;
}
EXPORT_SYMBOL_GPL(devm_of_pcs_select);

/**
 * of_pcs_unavailable - Check if PCS (Physical Coding Sublayer) is unavailable
 * @np: Pointer to the device node
 *
 * This function checks if any PCS is unavailable for the given device node.
 * It parses the "pcs-handle" property of the device node and verifies the
 * availability of each PCS. If any are unavailable, it returns an appropriate
 * error code.
 *
 * Return: 0 if all PCS are available, a negative error code otherwise.
 *         -EINVAL if the arguments count is greater than 1.
 *         -ENODEV if the PCS standalone instance is not found.
 *         Other negative error codes from of_parse_phandle_with_args() or
 *         pcs_standalone_get().
 */
int of_pcs_unavailable(struct device_node *np)
{
	struct of_phandle_args args;
	struct pcs_standalone *pcssa;
	int i, count, ret;

	count = of_count_phandle_with_args(np, "pcs-handle", "#pcs-cells");
	if (count == -ENOENT)
		return 0;

	if (count <= 0)
		return count;

	for (i = 0; i < count; i++) {
		ret = of_parse_phandle_with_args(np, "pcs-handle", "#pcs-cells", i, &args);
		if (ret < 0)
			return ret;

		if (args.args_count > 1) {
			of_node_put(args.np);
			return -EINVAL;
		}

		pcssa = pcs_standalone_get(args.np, args.args_count ? args.args[0] : -1, PHY_INTERFACE_MODE_NA);
		of_node_put(args.np);
		if (IS_ERR(pcssa))
			return PTR_ERR(pcssa);

		if (!pcssa)
			return -ENODEV;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(of_pcs_unavailable);

MODULE_DESCRIPTION("Helper for standalone PCS drivers");
MODULE_AUTHOR("Daniel Golle <daniel@makrotopia.org>");
MODULE_LICENSE("GPL");
