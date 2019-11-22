// SPDX-License-Identifier: ISC
/* Copyright (C) 2019 MediaTek Inc.
 *
 * Author: Ryder Lee <ryder.lee@mediatek.com>
 */

#include "mt7622.h"

static int mt7622_read_temperature(struct seq_file *s, void *data)
{
	struct mt7622_dev *dev = dev_get_drvdata(s->private);
	int temp;

	/* cpu */
	temp = mt7622_mcu_get_temperature(dev, 0);
	seq_printf(s, "Temperature: %d\n", temp);

	return 0;
}

int mt7622_init_debugfs(struct mt7622_dev *dev)
{
	struct dentry *dir;

	dir = mt76_register_debugfs(&dev->mt76);
	if (!dir)
		return -ENOMEM;

	debugfs_create_devm_seqfile(dev->mt76.dev, "temperature", dir,
				mt7622_read_temperature);

	return 0;
}
