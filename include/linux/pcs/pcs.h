/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef __LINUX_PCS_H
#define __LINUX_PCS_H

#include <linux/phylink.h>

#if IS_ENABLED(CONFIG_FWNODE_PCS)
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
static inline struct phylink_pcs *fwnode_pcs_get(const struct fwnode_handle *fwnode,
						 unsigned int index)
{
	return ERR_PTR(-ENOENT);
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
