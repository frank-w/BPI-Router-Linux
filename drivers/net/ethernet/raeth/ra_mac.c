/* Copyright  2016 MediaTek Inc.
 * Author: Nelson Chang <nelson.chang@mediatek.com>
 * Author: Carlos Huang <carlos.huang@mediatek.com>
 * Author: Harry Huang <harry.huang@mediatek.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#include "raether.h"

void enable_auto_negotiate(struct END_DEVICE *ei_local)
{
	u32 reg_value;

	/* FIXME: we don't know how to deal with PHY end addr */
	reg_value = sys_reg_read(ESW_PHY_POLLING);
	reg_value |= (1 << 31);
	reg_value &= ~(0x1f);
	reg_value &= ~(0x1f << 8);

	if (ei_local->architecture & (GE2_RGMII_AN | GE2_SGMII_AN)) {
		/* setup PHY address for auto polling (Start Addr). */
		reg_value |= ((CONFIG_MAC_TO_GIGAPHY_MODE_ADDR2 - 1) & 0x1f);
		/* setup PHY address for auto polling (End Addr). */
		reg_value |= (CONFIG_MAC_TO_GIGAPHY_MODE_ADDR2 << 8);
	} else if (ei_local->architecture & (GE1_RGMII_AN | GE1_SGMII_AN)) {
		/* setup PHY address for auto polling (Start Addr). */
		reg_value |= (CONFIG_MAC_TO_GIGAPHY_MODE_ADDR << 0);
		/* setup PHY address for auto polling (End Addr). */
		reg_value |= ((CONFIG_MAC_TO_GIGAPHY_MODE_ADDR + 1) << 8);
	}

	sys_reg_write(ESW_PHY_POLLING, reg_value);
}

void ra2880stop(struct END_DEVICE *ei_local)
{
	unsigned int reg_value;

	pr_info("ra2880stop()...");

	reg_value = sys_reg_read(DMA_GLO_CFG);
	reg_value &= ~(TX_WB_DDONE | RX_DMA_EN | TX_DMA_EN);
	sys_reg_write(DMA_GLO_CFG, reg_value);

	pr_info("Done\n");
}

void set_mac_address(unsigned char p[6])
{
	unsigned long reg_value;

	reg_value = (p[0] << 8) | (p[1]);
	sys_reg_write(GDMA1_MAC_ADRH, reg_value);

	reg_value = (p[2] << 24) | (p[3] << 16) | (p[4] << 8) | p[5];
	sys_reg_write(GDMA1_MAC_ADRL, reg_value);

	pr_info("GMAC1_MAC_ADRH -- : 0x%08x\n", sys_reg_read(GDMA1_MAC_ADRH));
	pr_info("GMAC1_MAC_ADRL -- : 0x%08x\n", sys_reg_read(GDMA1_MAC_ADRL));
}

void set_mac2_address(unsigned char p[6])
{
	unsigned long reg_value;

	reg_value = (p[0] << 8) | (p[1]);
	sys_reg_write(GDMA2_MAC_ADRH, reg_value);

	reg_value = (p[2] << 24) | (p[3] << 16) | (p[4] << 8) | p[5];
	sys_reg_write(GDMA2_MAC_ADRL, reg_value);

	pr_info("GDMA2_MAC_ADRH -- : 0x%08x\n", sys_reg_read(GDMA2_MAC_ADRH));
	pr_info("GDMA2_MAC_ADRL -- : 0x%08x\n", sys_reg_read(GDMA2_MAC_ADRL));
}

static int getnext(const char *src, int separator, char *dest)
{
	char *c;
	int len;

	if (!src || !dest)
		return -1;

	c = strchr(src, separator);
	if (!c) {
		strcpy(dest, src);
		return -1;
	}
	len = c - src;
	strncpy(dest, src, len);
	dest[len] = '\0';
	return len + 1;
}

int str_to_ip(unsigned int *ip, const char *str)
{
	int len;
	const char *ptr = str;
	char buf[128];
	unsigned char c[4];
	int i;
	int ret;

	for (i = 0; i < 3; ++i) {
		len = getnext(ptr, '.', buf);
		if (len == -1)
			return 1;	/* parse error */

		ret = kstrtoul(buf, 10, (unsigned long *)&c[i]);
		if (ret)
			return ret;

		ptr += len;
	}
	ret = kstrtoul(ptr, 0, (unsigned long *)&c[3]);
	if (ret)
		return ret;

	*ip = (c[0] << 24) + (c[1] << 16) + (c[2] << 8) + c[3];

	return 0;
}
