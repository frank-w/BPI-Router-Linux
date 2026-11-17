/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef __LINUX_PCS_PROVIDER_H
#define __LINUX_PCS_PROVIDER_H

struct fwnode_pcs_provider;

/**
 * fwnode_pcs_simple_xlate - Simple xlate function to retrieve PCS
 * @pcsspec: reference arguments
 * @data: Context data (assumed assigned to the single PCS)
 *
 * Returns: the PCS pointed by data.
 */
struct phylink_pcs *fwnode_pcs_simple_xlate(struct fwnode_reference_args *pcsspec,
					    void *data);

/**
 * fwnode_pcs_add_provider - Registers a new PCS provider
 * @fwnode: Firmware node
 * @fwnode_xlate: xlate function to retrieve the PCS
 * @data: Context data
 *
 * Register and add a new PCS provider to the global providers list
 * for the firmware node. The relevant PCS from the PCS provider
 * is retrieved from the passed fwnode_xlate function.
 *
 * The xlate function MUST return a pointer to an existing, already-allocated
 * struct phylink_pcs and MUST NOT dynamically allocate a new PCS.
 * The same PCS object must be returned for a given firmware reference and args,
 * as PCS pointer identity is used to associate PCS objects with their provider.
 *
 * Returns: A pointer to the registered PCS provider on success, or
 * an ERR_PTR() encoded error code on failure.
 */
struct fwnode_pcs_provider *
fwnode_pcs_add_provider(struct fwnode_handle *fwnode,
			struct phylink_pcs *(*fwnode_xlate)(struct fwnode_reference_args *pcsspec,
							    void *data),
			void *data);

/**
 * fwnode_pcs_del_provider - Removes a PCS provider
 * @pp: PCS provider returned by fwnode_pcs_add_provider()
 */
void fwnode_pcs_del_provider(struct fwnode_pcs_provider *pp);

/**
 * devm_fwnode_pcs_add_provider - Registers a new PCS provider
 * @dev: Device of the PCS provider
 * @fwnode: Firmware node
 * @fwnode_xlate: xlate function to retrieve the PCS
 * @data: Context data
 *
 * Register and add a new PCS provider to the global providers list
 * for the firmware node. The relevant PCS from the PCS provider
 * is retrieved from the passed fwnode_xlate function. While at that, it
 * also associates the device with the PCS provider using devres.
 * On driver detach, release function is invoked on the devres data,
 * then, devres data is freed.
 *
 * Returns: A pointer to the registered PCS provider on success, or
 * an ERR_PTR() encoded error code on failure.
 */
struct fwnode_pcs_provider *
devm_fwnode_pcs_add_provider(struct device *dev, struct fwnode_handle *fwnode,
			     struct phylink_pcs *(*fwnode_xlate)(struct fwnode_reference_args *pcsspec,
								 void *data),
			     void *data);

#endif /* __LINUX_PCS_PROVIDER_H */
