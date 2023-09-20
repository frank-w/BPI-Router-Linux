// SPDX-License-Identifier: GPL-2.0
#include <linux/mm.h>
#include <linux/execmem.h>

static struct execmem_params execmem_params __ro_after_init = {
	.ranges = {
		[EXECMEM_DEFAULT] = {
#ifdef CONFIG_SPARC64
			.start = MODULES_VADDR,
			.end = MODULES_END,
#else
			.start = VMALLOC_START,
			.end = VMALLOC_END,
#endif
			.alignment = 1,
		},
	},
};

struct execmem_params __init *execmem_arch_params(void)
{
	execmem_params.ranges[EXECMEM_DEFAULT].pgprot = PAGE_KERNEL;

	return &execmem_params;
}
