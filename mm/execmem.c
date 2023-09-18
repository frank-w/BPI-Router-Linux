// SPDX-License-Identifier: GPL-2.0

#include <linux/mm.h>
#include <linux/vmalloc.h>
#include <linux/execmem.h>
#include <linux/moduleloader.h>

static struct execmem_params execmem_params;

static void *execmem_alloc(size_t size, struct execmem_range *range)
{
	unsigned long start = range->start;
	unsigned long end = range->end;
	unsigned int align = range->alignment;
	pgprot_t pgprot = range->pgprot;

	return __vmalloc_node_range(size, align, start, end,
				   GFP_KERNEL, pgprot, VM_FLUSH_RESET_PERMS,
				   NUMA_NO_NODE, __builtin_return_address(0));
}

void *execmem_text_alloc(enum execmem_type type, size_t size)
{
	if (!execmem_params.ranges[type].start)
		return module_alloc(size);

	return execmem_alloc(size, &execmem_params.ranges[type]);
}

void execmem_free(void *ptr)
{
	/*
	 * This memory may be RO, and freeing RO memory in an interrupt is not
	 * supported by vmalloc.
	 */
	WARN_ON(in_interrupt());
	vfree(ptr);
}

struct execmem_params * __weak execmem_arch_params(void)
{
	return NULL;
}

static bool execmem_validate_params(struct execmem_params *p)
{
	struct execmem_range *r = &p->ranges[EXECMEM_DEFAULT];

	if (!r->alignment || !r->start || !r->end || !pgprot_val(r->pgprot)) {
		pr_crit("Invalid parameters for execmem allocator, module loading will fail");
		return false;
	}

	return true;
}

static void execmem_init_missing(struct execmem_params *p)
{
	struct execmem_range *default_range = &p->ranges[EXECMEM_DEFAULT];

	for (int i = EXECMEM_DEFAULT + 1; i < EXECMEM_TYPE_MAX; i++) {
		struct execmem_range *r = &p->ranges[i];

		if (!r->start) {
			r->pgprot = default_range->pgprot;
			r->alignment = default_range->alignment;
			r->start = default_range->start;
			r->end = default_range->end;
		}
	}
}

void __init execmem_init(void)
{
	struct execmem_params *p = execmem_arch_params();

	if (!p)
		return;

	if (!execmem_validate_params(p))
		return;

	execmem_init_missing(p);

	execmem_params = *p;
}
