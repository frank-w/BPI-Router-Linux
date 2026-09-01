/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef __LINUX_PCS_H
#define __LINUX_PCS_H

#include <linux/phylink.h>

enum fwnode_pcs_notify_event {
	FWNODE_PCS_PROVIDER_ADD,
	FWNODE_PCS_PROVIDER_DEL,
};

struct fwnode_pcs_provider;

#if IS_ENABLED(CONFIG_FWNODE_PCS)
/**
 * register_fwnode_pcs_notifier - Register a notifier block for fwnode
 *				  PCS events
 * @nb: pointer to the notifier block
 *
 * Registers a notifier block to the fwnode_pcs_notify_list blocking
 * notifier chain. This allows phylink instance to subscribe for
 * PCS provider events.
 *
 * Returns: 0 or a negative error.
 */
int register_fwnode_pcs_notifier(struct notifier_block *nb);

/**
 * unregister_fwnode_pcs_notifier - Unregister a notifier block for fwnode
 *				    PCS events
 * @nb: pointer to the notifier block
 *
 * Unregisters a notifier block to the fwnode_pcs_notify_list blocking
 * notifier chain.
 *
 * Returns: 0 or a negative error.
 */
int unregister_fwnode_pcs_notifier(struct notifier_block *nb);

/**
 * fwnode_pcs_get - Retrieves a PCS from a firmware node
 * @fwnode: firmware node
 * @index: index fwnode PCS handle in firmware node
 *
 * Get a PCS from the firmware node at index.
 *
 * Returns: a pointer to the phylink_pcs or a negative error pointer. Can
 * return -ENODEV if the PCS is not present in global providers list (either
 * due to driver still needs to be probed or it failed to probe/removed).
 */
struct phylink_pcs *fwnode_pcs_get(const struct fwnode_handle *fwnode,
				   unsigned int index);

/**
 * fwnode_pcs_get_from_provider() - Retrieve a PCS from a specific provider
 * @provider: PCS provider to use
 * @fwnode: firmware node
 * @index: index fwnode PCS handle in firmware node
 *
 * Get a PCS from the firmware node at index specifically provided by
 * passed PCS provider.
 *
 * Unlike fwnode_pcs_get(), this function does not search the global list of
 * PCS providers. The caller must provide the provider responsible for the
 * referenced PCS.
 *
 * Returns: a pointer to the phylink_pcs or a negative error pointer. Can
 * return -ENODEV if the PCS is not present in global providers list (either
 * due to driver still needs to be probed or it failed to probe/removed) or
 * can return -EINVAL if the PCS provider doesn't expose any PCS for the fwnode.
 */
struct phylink_pcs *fwnode_pcs_get_from_provider(struct fwnode_pcs_provider *provider,
						 const struct fwnode_handle *fwnode,
						 int index);

/**
 * fwnode_pcs_matches_provider() - Check whether a PCS belongs to a provider
 * @provider: PCS provider to check
 * @fwnode: firmware node containing the PCS references
 * @pl_pcs: PCS to check
 *
 * Parse the PCS references from the "pcs-handle" property of a firmware node
 * and use provider to determine whether pl_pcs is one of the PCS provided by
 * provider.
 *
 * This function is intended to be used when handling provider removal
 * notifications, where the provider is already known and its PCS need to be
 * identified among the PCS associated with a phylink instance.
 *
 * Returns: true if pl_pcs is provided by provider and referenced by fwnode,
 * false otherwise.
 */
bool fwnode_pcs_matches_provider(struct fwnode_pcs_provider *provider,
				 const struct fwnode_handle *fwnode,
				 struct phylink_pcs *pl_pcs);

/**
 * fwnode_phylink_pcs_count - Count PCS entries described in firmware node
 * @fwnode: firmware node
 *
 * Helper function to count the number of PCS entries referenced by the
 * "pcs-handle" property in a firmware node.
 *
 * Note that this function counts all PCS references in the firmware node,
 * regardless of whether the corresponding PCS devices are already probed.
 *
 * Returns: number of PCS entries described in the firmware node.
 */
unsigned int fwnode_phylink_pcs_count(struct fwnode_handle *fwnode);

/**
 * fwnode_phylink_pcs_parse - Parse available PCS from firmware node
 * @fwnode: firmware node
 * @available_pcs: pointer to preallocated array of PCS
 * @num_pcs: maximum number of PCS entries to scan
 *
 * Helper function that parses PCS references from the "pcs-handle"
 * property of a firmware node and fills @available_pcs with PCS that are
 * currently available up to @num_pcs.
 *
 * Only PCS that are currently available are stored in @available_pcs.
 * PCS that returns -ENODEV are skipped.
 *
 * Returns: number of PCS stored in @available_pcs, or negative error code.
 */
int fwnode_phylink_pcs_parse(struct fwnode_handle *fwnode,
			     struct phylink_pcs **available_pcs,
			     unsigned int num_pcs);
#else
static inline int register_fwnode_pcs_notifier(struct notifier_block *nb)
{
	return -EOPNOTSUPP;
}

static inline int unregister_fwnode_pcs_notifier(struct notifier_block *nb)
{
	return -EOPNOTSUPP;
}

static inline struct phylink_pcs *fwnode_pcs_get(const struct fwnode_handle *fwnode,
						 unsigned int index)
{
	return ERR_PTR(-ENOENT);
}

static inline struct phylink_pcs *
fwnode_pcs_get_from_provider(struct fwnode_pcs_provider *provider,
			     const struct fwnode_handle *fwnode,
			     int index)
{
	return ERR_PTR(-ENOENT);
}

static inline bool fwnode_pcs_matches_provider(struct fwnode_pcs_provider *provider,
					       const struct fwnode_handle *fwnode,
					       struct phylink_pcs *pl_pcs)
{
	return false;
}

static inline unsigned int fwnode_phylink_pcs_count(struct fwnode_handle *fwnode)
{
	return 0;
}

static inline int fwnode_phylink_pcs_parse(struct fwnode_handle *fwnode,
					   struct phylink_pcs **available_pcs,
					   unsigned int num_pcs)
{
	return -EOPNOTSUPP;
}
#endif

#endif /* __LINUX_PCS_H */
