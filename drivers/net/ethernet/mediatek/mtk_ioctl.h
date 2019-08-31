/*   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; version 2 of the License
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   Copyright (C) 2009-2016 John Crispin <blogic@openwrt.org>
 *   Copyright (C) 2009-2016 Felix Fietkau <nbd@openwrt.org>
 *   Copyright (C) 2013-2016 Michael Lee <igvtee@gmail.com>
 */

#ifndef _MTKETH_IOCTL_H
#define _MTKETH_IOCTL_H

#define MTKETH_MII_READ                  0x89F3
#define MTKETH_MII_WRITE                 0x89F4
#define MTKETH_ESW_REG_READ              0x89F1
#define MTKETH_ESW_REG_WRITE             0x89F2
#define MTKETH_MII_READ_CL45             0x89FC
#define MTKETH_MII_WRITE_CL45            0x89FD
#define REG_ESW_MAX                     0xFC

struct mtk_esw_reg {
	unsigned int off;
	unsigned int val;
};

struct mtk_mii_ioctl_data {
	unsigned int phy_id;
	unsigned int reg_num;
	unsigned int val_in;
	unsigned int val_out;
	unsigned int port_num;
	unsigned int dev_addr;
	unsigned int reg_addr;
};

int mtketh_debugfs_init(struct mtk_eth *eth);
void mtketh_debugfs_exit(struct mtk_eth *eth);
int mtk_do_priv_ioctl(struct net_device *dev, struct ifreq *ifr, int cmd);
#endif
