// SPDX-License-Identifier: GPL-2.0-or-later

#include <linux/mutex.h>
#include <linux/property.h>
#include <linux/phylink.h>
#include <linux/pcs/pcs.h>
#include <linux/pcs/pcs-provider.h>

MODULE_DESCRIPTION("PCS library");
MODULE_AUTHOR("Christian Marangi <ansuelsmth@gmail.com>");
MODULE_LICENSE("GPL");

struct fwnode_pcs_provider {
	struct list_head link;

	struct fwnode_handle *fwnode;
	struct phylink_pcs *(*fwnode_xlate)(struct fwnode_reference_args *pcsspec,
					    void *data);

	void *data;
};

static LIST_HEAD(fwnode_pcs_providers);
static DEFINE_MUTEX(fwnode_pcs_mutex);
static BLOCKING_NOTIFIER_HEAD(fwnode_pcs_notify_list);

int register_fwnode_pcs_notifier(struct notifier_block *nb)
{
	return blocking_notifier_chain_register(&fwnode_pcs_notify_list, nb);
}
EXPORT_SYMBOL_GPL(register_fwnode_pcs_notifier);

int unregister_fwnode_pcs_notifier(struct notifier_block *nb)
{
	return blocking_notifier_chain_unregister(&fwnode_pcs_notify_list, nb);
}
EXPORT_SYMBOL_GPL(unregister_fwnode_pcs_notifier);

struct phylink_pcs *fwnode_pcs_simple_xlate(struct fwnode_reference_args *pcsspec,
					    void *data)
{
	return data;
}
EXPORT_SYMBOL_GPL(fwnode_pcs_simple_xlate);

struct fwnode_pcs_provider *
fwnode_pcs_add_provider(struct fwnode_handle *fwnode,
			struct phylink_pcs *(*fwnode_xlate)(struct fwnode_reference_args *pcsspec,
							    void *data),
			void *data)
{
	struct fwnode_pcs_provider *pp;

	if (!fwnode)
		return ERR_PTR(-EINVAL);

	pp = kzalloc_obj(*pp);
	if (!pp)
		return ERR_PTR(-ENOMEM);

	pp->fwnode = fwnode_handle_get(fwnode);
	pp->data = data;
	pp->fwnode_xlate = fwnode_xlate;

	mutex_lock(&fwnode_pcs_mutex);

	list_add(&pp->link, &fwnode_pcs_providers);
	fwnode_dev_initialized(fwnode, true);

	mutex_unlock(&fwnode_pcs_mutex);

	blocking_notifier_call_chain(&fwnode_pcs_notify_list,
				     FWNODE_PCS_PROVIDER_ADD,
				     pp);

	pr_debug("Added pcs provider from %pfwf\n", fwnode);

	return pp;
}
EXPORT_SYMBOL_GPL(fwnode_pcs_add_provider);

void fwnode_pcs_del_provider(struct fwnode_pcs_provider *pp)
{
	if (IS_ERR_OR_NULL(pp))
		return;

	mutex_lock(&fwnode_pcs_mutex);

	list_del(&pp->link);
	fwnode_dev_initialized(pp->fwnode, false);

	mutex_unlock(&fwnode_pcs_mutex);

	/* Signal phylink to release any PCS from this provider */
	blocking_notifier_call_chain(&fwnode_pcs_notify_list,
				     FWNODE_PCS_PROVIDER_DEL,
				     pp);

	fwnode_handle_put(pp->fwnode);
	kfree(pp);
}
EXPORT_SYMBOL_GPL(fwnode_pcs_del_provider);

static void devm_fwnode_pcs_del(struct device *dev, void *res)
{
	struct fwnode_pcs_provider *pp = *(struct fwnode_pcs_provider **)res;

	fwnode_pcs_del_provider(pp);
}

struct fwnode_pcs_provider *
devm_fwnode_pcs_add_provider(struct device *dev, struct fwnode_handle *fwnode,
			     struct phylink_pcs *(*fwnode_xlate)(struct fwnode_reference_args *pcsspec,
								 void *data),
			     void *data)
{
	struct fwnode_pcs_provider **ptr, *pp;

	ptr = devres_alloc(devm_fwnode_pcs_del, sizeof(*ptr), GFP_KERNEL);
	if (!ptr)
		return ERR_PTR(-ENOMEM);

	pp = fwnode_pcs_add_provider(fwnode, fwnode_xlate, data);

	if (!IS_ERR(pp)) {
		*ptr = pp;
		devres_add(dev, ptr);
	} else {
		devres_free(ptr);
	}

	return pp;
}
EXPORT_SYMBOL_GPL(devm_fwnode_pcs_add_provider);

static int fwnode_parse_pcsspec(const struct fwnode_handle *fwnode,
				int index, const char *name,
				struct fwnode_reference_args *out_args)
{
	if (!fwnode)
		return -EINVAL;

	if (name) {
		index = fwnode_property_match_string(fwnode, "pcs-names",
						     name);
		if (index < 0)
			return index;
	}

	return fwnode_property_get_reference_args(fwnode, "pcs-handle",
						  "#pcs-cells", 0, index,
						  out_args);
}

static struct phylink_pcs *
__fwnode_pcs_get_from_pcsspec_provider(struct fwnode_reference_args *pcsspec,
				       struct fwnode_pcs_provider *provider)
{
	if (provider->fwnode != pcsspec->fwnode)
		return ERR_PTR(-EINVAL);

	return provider->fwnode_xlate(pcsspec, provider->data);
}

static struct phylink_pcs *
fwnode_pcs_get_from_pcsspec(struct fwnode_reference_args *pcsspec)
{
	struct fwnode_pcs_provider *provider;
	struct phylink_pcs *pcs = NULL;

	if (!pcsspec)
		return ERR_PTR(-EINVAL);

	mutex_lock(&fwnode_pcs_mutex);
	list_for_each_entry(provider, &fwnode_pcs_providers, link) {
		pcs = __fwnode_pcs_get_from_pcsspec_provider(pcsspec, provider);
		if (!IS_ERR(pcs))
			break;
	}
	mutex_unlock(&fwnode_pcs_mutex);

	return !IS_ERR_OR_NULL(pcs) ? pcs : ERR_PTR(-ENODEV);
}

static struct phylink_pcs *__fwnode_pcs_get(const struct fwnode_handle *fwnode,
					    unsigned int index, const char *con_id)
{
	struct fwnode_reference_args pcsspec;
	struct phylink_pcs *pcs;
	int ret;

	ret = fwnode_parse_pcsspec(fwnode, index, con_id, &pcsspec);
	if (ret)
		return ERR_PTR(ret);

	pcs = fwnode_pcs_get_from_pcsspec(&pcsspec);
	fwnode_handle_put(pcsspec.fwnode);

	return pcs;
}

struct phylink_pcs *fwnode_pcs_get(const struct fwnode_handle *fwnode, unsigned int index)
{
	return __fwnode_pcs_get(fwnode, index, NULL);
}
EXPORT_SYMBOL_GPL(fwnode_pcs_get);

struct phylink_pcs *fwnode_pcs_get_from_provider(struct fwnode_pcs_provider *provider,
						 const struct fwnode_handle *fwnode,
						 int index)
{
	struct fwnode_reference_args pcsspec;
	struct phylink_pcs *pcs;
	int ret;

	ret = fwnode_parse_pcsspec(fwnode, index, NULL, &pcsspec);
	if (ret)
		return ERR_PTR(ret);

	pcs = __fwnode_pcs_get_from_pcsspec_provider(&pcsspec, provider);
	fwnode_handle_put(pcsspec.fwnode);
	return pcs;
}
EXPORT_SYMBOL_GPL(fwnode_pcs_get_from_provider);

bool fwnode_pcs_matches_provider(struct fwnode_pcs_provider *provider,
				 const struct fwnode_handle *fwnode,
				 struct phylink_pcs *pl_pcs)
{
	struct fwnode_reference_args pcsspec;
	int index = 0;
	int ret;

	while (true) {
		struct phylink_pcs *pcs;

		ret = fwnode_parse_pcsspec(fwnode, index, NULL, &pcsspec);
		if (ret)
			return false;

		pcs = __fwnode_pcs_get_from_pcsspec_provider(&pcsspec, provider);
		if (!IS_ERR(pcs) && pcs == pl_pcs) {
			fwnode_handle_put(pcsspec.fwnode);
			return true;
		}

		fwnode_handle_put(pcsspec.fwnode);
		index++;
	}

	return false;
}
EXPORT_SYMBOL_GPL(fwnode_pcs_matches_provider);

unsigned int fwnode_phylink_pcs_count(struct fwnode_handle *fwnode)
{
	struct fwnode_reference_args out_args;
	int index = 0;
	int ret;

	while (true) {
		ret = fwnode_property_get_reference_args(fwnode, "pcs-handle",
							 "#pcs-cells", 0, index,
							 &out_args);
		/* We expect to reach an -ENOENT error while counting */
		if (ret)
			break;

		fwnode_handle_put(out_args.fwnode);
		index++;
	}

	return index;
}
EXPORT_SYMBOL_GPL(fwnode_phylink_pcs_count);

int fwnode_phylink_pcs_parse(struct fwnode_handle *fwnode,
			     struct phylink_pcs **available_pcs,
			     unsigned int num_pcs)
{
	unsigned int i, found = 0;

	if (!available_pcs)
		return -EINVAL;

	if (!fwnode_property_present(fwnode, "pcs-handle"))
		return -ENODEV;

	for (i = 0; i < num_pcs; i++) {
		struct phylink_pcs *pcs;

		pcs = fwnode_pcs_get(fwnode, i);
		if (IS_ERR(pcs)) {
			/* Exit early if no PCS remain.*/
			if (PTR_ERR(pcs) == -ENOENT)
				break;

			/*
			 * Ignore -ENODEV error for PCS that still
			 * needs to probe.
			 */
			if (PTR_ERR(pcs) == -ENODEV)
				continue;

			return PTR_ERR(pcs);
		}

		available_pcs[found] = pcs;
		found++;
	}

	return found;
}
EXPORT_SYMBOL_GPL(fwnode_phylink_pcs_parse);
