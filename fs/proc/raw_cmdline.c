// SPDX-License-Identifier: GPL-2.0
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <asm/setup.h>
#include "internal.h"

static int raw_cmdline_proc_show(struct seq_file *m, void *v)
{
	seq_puts(m, boot_command_line);
	seq_putc(m, '\n');
	return 0;
}

static int __init proc_raw_cmdline_init(void)
{
	struct proc_dir_entry *pde;

	pde = proc_create_single("raw_cmdline", 0, NULL, raw_cmdline_proc_show);
	pde_make_permanent(pde);
	pde->size = strnlen(boot_command_line, COMMAND_LINE_SIZE) + 1;
	return 0;
}
fs_initcall(proc_raw_cmdline_init);
