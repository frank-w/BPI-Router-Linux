/*
 * mtd-rw - Make all MTD partitions writeable.
 *
 * Copyright (C) 2016 Joseph C. Lehner <joseph.c.lehner@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.
 * If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mtd/mtd.h>
#include <linux/err.h>

/*
 * This module is intended for embedded devices where the mtd partition
 * layout may be hard-coded in the firmware. If, for some reason, you
 * DO have to write to a read-only partition (which is often a bad idea),
 * this module is the way to go.
 *
 * The module is currently limited to the first 64 partitions, but this
 * should suffice for most purposes.
 *
 * Inspired by dougg3@electronics.stackexchange.com:
 * https://electronics.stackexchange.com/a/116133/97342
 */

#ifndef MODULE
#error "Must be compiled as a module."
#endif

#define MOD_WARNING KERN_WARNING "mtd-rw: "
#define MOD_INFO KERN_INFO "mtd-rw: "
#define MOD_ERR KERN_ERR "mtd-rw: "

#define MTD_MAX (8 * sizeof(unlocked))

static uint64_t unlocked = 0;
static unsigned mtd_last = 0;

static bool i_want_a_brick = false;
module_param(i_want_a_brick, bool, S_IRUGO);
MODULE_PARM_DESC(i_want_a_brick, "Make all partitions writeable");

static int set_writeable(unsigned n, bool w)
{
	struct mtd_info *mtd = get_mtd_device(NULL, n);
	int err;

	if (IS_ERR(mtd)) {
		if (PTR_ERR(mtd) != -ENODEV || !w) {
			printk(MOD_ERR "mtd%d: error %ld\n", n, PTR_ERR(mtd));
		}
		return PTR_ERR(mtd);
	}

	err = -EEXIST;

	if (w && !(mtd->flags & MTD_WRITEABLE)) {
		printk(MOD_INFO "mtd%d: setting writeable flag\n", n);
		mtd->flags |= MTD_WRITEABLE;
		err = 0;
	} else if (!w && (mtd->flags & MTD_WRITEABLE)) {
		printk(MOD_INFO "mtd%d: clearing writeable flag\n", n);
		mtd->flags &= ~MTD_WRITEABLE;
		err = 0;
	}

	put_mtd_device(mtd);
	return err;
}

static int __init mtd_rw_init(void)
{
	int i, err;

	if (!i_want_a_brick) {
		printk(MOD_ERR "must specify i_want_a_brick=1 to continue\n");
		return -EINVAL;
	}

	for (i = 0; i < MTD_MAX; ++i) {
		err = set_writeable(i, true);
		if (!err) {
			unlocked |= (1 << i);
		} else if (err == -ENODEV) {
			break;
		}
	}

	if (i == MTD_MAX) {
		printk(MOD_WARNING "partitions beyond mtd%d are ignored\n", i - 1);
	}

	if (!unlocked) {
		printk(MOD_INFO "no partitions to unlock\n");
		return -ENODEV;
	}

	mtd_last = i;

	return 0;
}

static void __exit mtd_rw_exit(void)
{
	unsigned i;

	for (i = 0; i < mtd_last; ++i) {
		if (unlocked & (1 << i)) {
			set_writeable(i, false);
		}
	}
}

module_init(mtd_rw_init);
module_exit(mtd_rw_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Joseph C. Lehner <joseph.c.lehner@gmail.com>");
MODULE_DESCRIPTION("Write-enabler for MTD partitions");
MODULE_VERSION("1");
