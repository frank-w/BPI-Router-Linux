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

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/platform_device.h>
#include <linux/of_device.h>
#include <linux/of_irq.h>
#include <linux/of_gpio.h>
#include <linux/of_mdio.h>
#include <linux/clk.h>
#include <linux/mfd/syscon.h>
#include <linux/regulator/consumer.h>
#include <linux/regmap.h>
#include <linux/reset.h>
#include <linux/mii.h>
#include <linux/interrupt.h>
#include <linux/netdevice.h>
#include <linux/dma-mapping.h>
#include <linux/phy.h>
#include <linux/ethtool.h>
#include <linux/version.h>
#include <linux/atomic.h>

#include "mtk_eth_soc.h"
#include "gsw_mt7620.h"

#define ETHSYS_CLKCFG0			0x2c
#define ETHSYS_TRGMII_CLK_SEL362_5	BIT(11)

static u32 (*_cb_mtk_mdio_read)(struct mtk_eth *, int, int);
static u32 (*_cb_mtk_mdio_write)(struct mtk_eth *, u32, u32, u32);
static void (*_cb_mtk_w32)(struct mtk_eth *, u32, unsigned);
static u32 (*_cb_mtk_r32)(struct mtk_eth *, unsigned);

static inline u32 cb_mtk_mdio_read(struct mtk_eth *eth, int phy_addr, int phy_reg)
{
	return !_cb_mtk_mdio_read ? -1 :
	       _cb_mtk_mdio_read(eth, phy_addr, phy_reg);
}

static inline u32 cb_mtk_mdio_write(struct mtk_eth *eth, u32 phy_addr,
				    u32 phy_reg, u32 write_data)
{
	return !_cb_mtk_mdio_write ? -1 :
	       _cb_mtk_mdio_write(eth, phy_addr, phy_reg, write_data);
}

static inline void cb_mtk_w32(struct mtk_eth *eth, u32 val, unsigned reg)
{
	if (_cb_mtk_w32)
		_cb_mtk_w32(eth, val, reg);
}

static inline u32 cb_mtk_r32(struct mtk_eth *eth, unsigned reg)
{
	return !_cb_mtk_r32 ? -1 :
	       _cb_mtk_r32(eth, reg);
}

void mt7530_mdio_w32(struct mt7620_gsw *gsw, u32 reg, u32 val)
{
	cb_mtk_mdio_write(gsw->eth, 0x1f, 0x1f, (reg >> 6) & 0x3ff);
	cb_mtk_mdio_write(gsw->eth, 0x1f, (reg >> 2) & 0xf,  val & 0xffff);
	cb_mtk_mdio_write(gsw->eth, 0x1f, 0x10, val >> 16);
}
EXPORT_SYMBOL(mt7530_mdio_w32);

u32 mt7530_mdio_r32(struct mt7620_gsw *gsw, u32 reg)
{
	u16 high, low;

	cb_mtk_mdio_write(gsw->eth, 0x1f, 0x1f, (reg >> 6) & 0x3ff);
	low = cb_mtk_mdio_read(gsw->eth, 0x1f, (reg >> 2) & 0xf);
	high = cb_mtk_mdio_read(gsw->eth, 0x1f, 0x10);

	return (high << 16) | (low & 0xffff);
}
EXPORT_SYMBOL(mt7530_mdio_r32);

void mt7530_mdio_m32(struct mt7620_gsw *gsw, u32 mask, u32 set, u32 reg)
{
	u32 val = mt7530_mdio_r32(gsw, reg);

	val &= mask;
	val |= set;
	mt7530_mdio_w32(gsw, reg, val);
}

void mtk_switch_w32(struct mt7620_gsw *gsw, u32 val, unsigned reg)
{
	cb_mtk_w32(gsw->eth, val, reg + 0x10000);
}
EXPORT_SYMBOL(mtk_switch_w32);

u32 mtk_switch_r32(struct mt7620_gsw *gsw, unsigned reg)
{
	return cb_mtk_r32(gsw->eth, reg + 0x10000);
}
EXPORT_SYMBOL(mtk_switch_r32);

void mtk_switch_m32(struct mt7620_gsw *gsw, u32 mask, u32 set, unsigned reg)
{
	u32 val = mtk_switch_r32(gsw, reg);

	val &= mask;
	val |= set;

	mtk_switch_w32(gsw, val, reg);
}

int mt7623_gsw_config(struct mtk_eth *eth)
{
	struct mt7620_gsw *gsw = (struct mt7620_gsw *)eth->sw_priv;

	if (gsw->port_map == LLLLL) {
		pr_info("%s> LLLLL\n", __func__);
		mt7530_mdio_w32(gsw, 0x2004, 0xff0000);
		mt7530_mdio_w32(gsw, 0x2104, 0xff0000);
		mt7530_mdio_w32(gsw, 0x2204, 0xff0000);
		mt7530_mdio_w32(gsw, 0x2304, 0xff0000);
		mt7530_mdio_w32(gsw, 0x2404, 0xff0000);
		mt7530_mdio_w32(gsw, 0x2504, 0xff0000);
		mt7530_mdio_w32(gsw, 0x2604, 0xff0000);
		mt7530_mdio_w32(gsw, 0x2704, 0xff0000);

		mt7530_mdio_w32(gsw, 0x2010, 0x810000c0);
		mt7530_mdio_w32(gsw, 0x2110, 0x810000c0);
		mt7530_mdio_w32(gsw, 0x2210, 0x810000c0);
		mt7530_mdio_w32(gsw, 0x2310, 0x810000c0);
		mt7530_mdio_w32(gsw, 0x2410, 0x810000c0);
		mt7530_mdio_w32(gsw, 0x2510, 0x810000c0);
		mt7530_mdio_w32(gsw, 0x2610, 0x810000c0);
		mt7530_mdio_w32(gsw, 0x2710, 0x810000c0);

		mt7530_mdio_w32(gsw, 0x80, 0x8002);
		mt7530_mdio_w32(gsw, 0x94, 0x0);
		mt7530_mdio_w32(gsw, 0x90, 0x80001000);
		mt7530_mdio_w32(gsw, 0x94, 0x0);
		mt7530_mdio_w32(gsw, 0x90, 0x80001001);
		mt7530_mdio_w32(gsw, 0x94, 0x0);
		mt7530_mdio_w32(gsw, 0x90, 0x80001002);
		mt7530_mdio_w32(gsw, 0x94, 0x0);
		mt7530_mdio_w32(gsw, 0x90, 0x80001003);
		mt7530_mdio_w32(gsw, 0x94, 0x0);
		mt7530_mdio_w32(gsw, 0x90, 0x80001004);
		mt7530_mdio_w32(gsw, 0x94, 0x0);
		mt7530_mdio_w32(gsw, 0x90, 0x80001005);
		mt7530_mdio_w32(gsw, 0x94, 0x0);
		mt7530_mdio_w32(gsw, 0x90, 0x80001006);
		mt7530_mdio_w32(gsw, 0x94, 0x0);
		mt7530_mdio_w32(gsw, 0x90, 0x80001007);
		mt7530_mdio_w32(gsw, 0x94, 0x0);
		mt7530_mdio_w32(gsw, 0x90, 0x80001008);
		mt7530_mdio_w32(gsw, 0x94, 0x0);
		mt7530_mdio_w32(gsw, 0x90, 0x80001009);
		mt7530_mdio_w32(gsw, 0x94, 0x0);
		mt7530_mdio_w32(gsw, 0x90, 0x8000100a);
		mt7530_mdio_w32(gsw, 0x94, 0x0);
		mt7530_mdio_w32(gsw, 0x90, 0x8000100b);
		mt7530_mdio_w32(gsw, 0x94, 0x0);
		mt7530_mdio_w32(gsw, 0x90, 0x8000100c);
		mt7530_mdio_w32(gsw, 0x94, 0x0);
		mt7530_mdio_w32(gsw, 0x90, 0x8000100c);
		mt7530_mdio_w32(gsw, 0x94, 0x0);
		mt7530_mdio_w32(gsw, 0x90, 0x8000100c);
		mt7530_mdio_w32(gsw, 0x94, 0x0);
		mt7530_mdio_w32(gsw, 0x90, 0x8000100d);
		mt7530_mdio_w32(gsw, 0x94, 0x0);
		mt7530_mdio_w32(gsw, 0x90, 0x8000100e);
		mt7530_mdio_w32(gsw, 0x94, 0x0);
		mt7530_mdio_w32(gsw, 0x90, 0x8000100f);
		pr_info("%s<\n", __func__);
	}  else if (gsw->port_map == WLLLL) {
		pr_info("%s> WLLLL\n", __func__);
		/* Set Port Security Mode and untagged */
		mt7530_mdio_w32(gsw, 0x2004, 0xff0003);
		mt7530_mdio_w32(gsw, 0x2104, 0xff0003);
		mt7530_mdio_w32(gsw, 0x2204, 0xff0003);
		mt7530_mdio_w32(gsw, 0x2304, 0xff0003);
		mt7530_mdio_w32(gsw, 0x2404, 0xff0003);
		mt7530_mdio_w32(gsw, 0x2504, 0xff0003);
		mt7530_mdio_w32(gsw, 0x2604, 0xff0003);

		/* Set as Transparent Port */
		mt7530_mdio_w32(gsw, 0x2010, 0x810000c0);
		mt7530_mdio_w32(gsw, 0x2110, 0x810000c0);
		mt7530_mdio_w32(gsw, 0x2210, 0x810000c0);
		mt7530_mdio_w32(gsw, 0x2310, 0x810000c0);
		mt7530_mdio_w32(gsw, 0x2410, 0x810000c0);
		mt7530_mdio_w32(gsw, 0x2510, 0x810000c0);
		mt7530_mdio_w32(gsw, 0x2610, 0x810000c0);

		/* Set PVID=1 to LAN, PVID=2 to WAN */
		mt7530_mdio_w32(gsw, 0x2014, 0x10002);
		mt7530_mdio_w32(gsw, 0x2114, 0x10001);
		mt7530_mdio_w32(gsw, 0x2214, 0x10001);
		mt7530_mdio_w32(gsw, 0x2314, 0x10001);
		mt7530_mdio_w32(gsw, 0x2414, 0x10001);
		mt7530_mdio_w32(gsw, 0x2514, 0x10002);
		mt7530_mdio_w32(gsw, 0x2614, 0x10001);

		/* switch vlan set 0 1 0111101 */
		mt7530_mdio_w32(gsw, 0x94, 0x405e0001);
		mt7530_mdio_w32(gsw, 0x90, 0x80001001);

		/* switch vlan set 0 2 1000010 */
		mt7530_mdio_w32(gsw, 0x94, 0x40210001);
		mt7530_mdio_w32(gsw, 0x90, 0x80001002);

		/* switch clear */
		mt7530_mdio_w32(gsw, 0x80, 0x8002);
	} else if (gsw->port_map == LLLLW) {
		pr_info("%s> LLLLW\n", __func__);
		/* Set Port Security Mode and untagged */
		mt7530_mdio_w32(gsw, 0x2004, 0xff0003);
		mt7530_mdio_w32(gsw, 0x2104, 0xff0003);
		mt7530_mdio_w32(gsw, 0x2204, 0xff0003);
		mt7530_mdio_w32(gsw, 0x2304, 0xff0003);
		mt7530_mdio_w32(gsw, 0x2404, 0xff0003);
		mt7530_mdio_w32(gsw, 0x2504, 0xff0003);
		mt7530_mdio_w32(gsw, 0x2604, 0xff0003);

		/* Set as Transparent Port */
		mt7530_mdio_w32(gsw, 0x2010, 0x810000c0);
		mt7530_mdio_w32(gsw, 0x2110, 0x810000c0);
		mt7530_mdio_w32(gsw, 0x2210, 0x810000c0);
		mt7530_mdio_w32(gsw, 0x2310, 0x810000c0);
		mt7530_mdio_w32(gsw, 0x2410, 0x810000c0);
		mt7530_mdio_w32(gsw, 0x2510, 0x810000c0);
		mt7530_mdio_w32(gsw, 0x2610, 0x810000c0);

		/* Set PVID=1 to LAN, PVID=2 to WAN */
		mt7530_mdio_w32(gsw, 0x2014, 0x10001);
		mt7530_mdio_w32(gsw, 0x2114, 0x10001);
		mt7530_mdio_w32(gsw, 0x2214, 0x10001);
		mt7530_mdio_w32(gsw, 0x2314, 0x10001);
		mt7530_mdio_w32(gsw, 0x2414, 0x10002);
		mt7530_mdio_w32(gsw, 0x2514, 0x10002);
		mt7530_mdio_w32(gsw, 0x2614, 0x10001);

		/* switch vlan set 0 1 1111001 */
		mt7530_mdio_w32(gsw, 0x94, 0x404f0001);
		mt7530_mdio_w32(gsw, 0x90, 0x80001001);

		/* switch vlan set 0 2 0000110 */
		mt7530_mdio_w32(gsw, 0x94, 0x40300001);
		mt7530_mdio_w32(gsw, 0x90, 0x80001002);

		/* switch clear */
		mt7530_mdio_w32(gsw, 0x80, 0x8002);
	}

	return 0;
}
EXPORT_SYMBOL(mt7623_gsw_config);

static irqreturn_t gsw_interrupt_mt7623(int irq, void *_eth)
{
	struct mtk_eth *eth = (struct mtk_eth *)_eth;
	struct mt7620_gsw *gsw = (struct mt7620_gsw *)eth->sw_priv;
	u32 reg, i;

	reg = mt7530_mdio_r32(gsw, 0x700c);

	for (i = 0; i < 5; i++)
		if (reg & BIT(i)) {
			unsigned int link;

			link = mt7530_mdio_r32(gsw,
					       0x3008 + (i * 0x100)) & 0x1;

			if (link)
				dev_info(gsw->dev,
					 "port %d link up\n", i);
			else
				dev_info(gsw->dev,
					 "port %d link down\n", i);
		}

	/*mt7620_handle_carrier(eth);*/
	mt7530_mdio_w32(gsw, 0x700c, 0x1f);

	return IRQ_HANDLED;
}

static void wait_loop(struct mt7620_gsw *gsw)
{
	int i;
	int read_data;

	for (i = 0; i < 320; i = i + 1)
		read_data = mtk_switch_r32(gsw, 0x610);
}

static void trgmii_calibration_7623(struct mt7620_gsw *gsw)
{
	unsigned int tap_a[5] = { 0, 0, 0, 0, 0 };	/* minmum delay for all correct */
	unsigned int tap_b[5] = { 0, 0, 0, 0, 0 };	/* maximum delay for all correct */
	unsigned int final_tap[5];
	unsigned int rxc_step_size;
	unsigned int rxd_step_size;
	unsigned int read_data;
	unsigned int tmp;
	unsigned int rd_wd;
	int i;
	unsigned int err_cnt[5];
	unsigned int init_toggle_data;
	unsigned int err_flag[5];
	unsigned int err_total_flag;
	unsigned int training_word;
	unsigned int rd_tap;
	u32 val;

	u32 TRGMII_7623_base;
	u32 TRGMII_7623_RD_0;
	u32 TRGMII_RCK_CTRL;

	TRGMII_7623_base = 0x300;	/* 0xFB110300 */
	TRGMII_7623_RD_0 = TRGMII_7623_base + 0x10;
	TRGMII_RCK_CTRL = TRGMII_7623_base;
	rxd_step_size = 0x1;
	rxc_step_size = 0x4;
	init_toggle_data = 0x00000055;
	training_word = 0x000000AC;

	pr_debug("Calibration begin ........");
	/* RX clock gating in MT7623 */
	mtk_switch_m32(gsw, 0x3fffffff, 0, TRGMII_7623_base + 0x04);

	/* Assert RX  reset in MT7623 */
	mtk_switch_m32(gsw, ~0, 0x80000000, TRGMII_7623_base + 0x00);

	/* Set TX OE edge in  MT7623 */
	mtk_switch_m32(gsw, ~0, 0x00002000, TRGMII_7623_base + 0x78);

	/* Disable RX clock gating in MT7623 */
	mtk_switch_m32(gsw, ~0, 0xC0000000, TRGMII_7623_base + 0x04);

	/* Release RX reset in MT7623 */
	mtk_switch_m32(gsw, 0x7fffffff, 0, TRGMII_7623_base);

	for (i = 0; i < 5; i++)
		mtk_switch_m32(gsw, 0, 0x80000000, TRGMII_7623_RD_0 + i * 8);

	pr_debug("Enable Training Mode in MT7530\n");
	read_data = mt7530_mdio_r32(gsw, 0x7A40);
	read_data |= 0xC0000000;
	mt7530_mdio_w32(gsw, 0x7A40, read_data);	/* Enable Training Mode in MT7530 */
	err_total_flag = 0;
	pr_debug("Adjust RXC delay in MT7623\n");
	read_data = 0x0;
	while (err_total_flag == 0 && read_data != 0x68) {
		pr_debug("2nd Enable EDGE CHK in MT7623\n");
		/* Enable EDGE CHK in MT7623 */
		for (i = 0; i < 5; i++)
			mtk_switch_m32(gsw, 0x4fffffff, 0x40000000, TRGMII_7623_RD_0 + i * 8);

		wait_loop(gsw);
		err_total_flag = 1;
		for (i = 0; i < 5; i++) {
			err_cnt[i] =
			    mtk_switch_r32(gsw, TRGMII_7623_RD_0 + i * 8) >> 8;
			err_cnt[i] &= 0x0000000f;
			rd_wd = mtk_switch_r32(gsw, TRGMII_7623_RD_0 + i * 8) >> 16;
			rd_wd &= 0x000000ff;
			val = mtk_switch_r32(gsw, TRGMII_7623_RD_0 + i * 8);
			pr_debug("ERR_CNT = %d, RD_WD =%x, TRGMII_7623_RD_0=%x\n",
				 err_cnt[i], rd_wd, val);
			if (err_cnt[i] != 0)
				err_flag[i] = 1;
			else if (rd_wd != 0x55)
				err_flag[i] = 1;
			else
				err_flag[i] = 0;
			err_total_flag = err_flag[i] & err_total_flag;
		}

		pr_debug("2nd Disable EDGE CHK in MT7623\n");
		/* Disable EDGE CHK in MT7623 */
		for (i = 0; i < 5; i++)
			mtk_switch_m32(gsw, 0x4fffffff, 0x40000000, TRGMII_7623_RD_0 + i * 8);
		wait_loop(gsw);
		pr_debug("2nd Disable EDGE CHK in MT7623\n");
		/* Adjust RXC delay */
		/* RX clock gating in MT7623 */
		mtk_switch_m32(gsw, 0x3fffffff, 0, TRGMII_7623_base + 0x04);
		read_data = mtk_switch_r32(gsw, TRGMII_7623_base);
		if (err_total_flag == 0) {
			tmp = (read_data & 0x0000007f) + rxc_step_size;
			pr_debug(" RXC delay = %d\n", tmp);
			read_data >>= 8;
			read_data &= 0xffffff80;
			read_data |= tmp;
			read_data <<= 8;
			read_data &= 0xffffff80;
			read_data |= tmp;
			mtk_switch_w32(gsw, read_data, TRGMII_7623_base);
		} else {
			tmp = (read_data & 0x0000007f) + 16;
			pr_debug(" RXC delay = %d\n", tmp);
			read_data >>= 8;
			read_data &= 0xffffff80;
			read_data |= tmp;
			read_data <<= 8;
			read_data &= 0xffffff80;
			read_data |= tmp;
			mtk_switch_w32(gsw, read_data, TRGMII_7623_base);
		}
		read_data &= 0x000000ff;

		/* Disable RX clock gating in MT7623 */
		mtk_switch_m32(gsw, ~0, 0xC0000000, TRGMII_7623_base + 0x04);
		for (i = 0; i < 5; i++)
			mtk_switch_m32(gsw, ~0, 0x80000000, TRGMII_7623_RD_0 + i * 8);
	}

	/* Read RD_WD MT7623 */
	for (i = 0; i < 5; i++) {
		rd_tap = 0;
		while (err_flag[i] != 0 && rd_tap != 128) {
			/* Enable EDGE CHK in MT7623 */
			mtk_switch_m32(gsw, 0x4fffffff, 0x40000000, TRGMII_7623_RD_0 + i * 8);
			wait_loop(gsw);

			read_data = mtk_switch_r32(gsw, TRGMII_7623_RD_0 + i * 8);
			err_cnt[i] = (read_data >> 8) & 0x0000000f;	/* Read MT7623 Errcnt */
			rd_wd = (read_data >> 16) & 0x000000ff;
			if (err_cnt[i] != 0 || rd_wd != 0x55)
				err_flag[i] = 1;
			else
				err_flag[i] = 0;

			/* Disable EDGE CHK in MT7623 */
			mtk_switch_m32(gsw, 0x4fffffff, 0x40000000, TRGMII_7623_RD_0 + i * 8);
			wait_loop(gsw);
			if (err_flag[i] != 0) {
				rd_tap = (read_data & 0x0000007f) + rxd_step_size;	/* Add RXD delay in MT7623 */
				read_data = (read_data & 0xffffff80) | rd_tap;
				mtk_switch_w32(gsw, read_data,
					       TRGMII_7623_RD_0 + i * 8);
				tap_a[i] = rd_tap;
			} else {
				rd_tap = (read_data & 0x0000007f) + 48;
				read_data = (read_data & 0xffffff80) | rd_tap;
				mtk_switch_w32(gsw, read_data,
					       TRGMII_7623_RD_0 + i * 8);
			}
		}
		pr_debug("MT7623 %dth bit  Tap_a = %d\n", i, tap_a[i]);
	}
	/* pr_err("Last While Loop\n"); */
	for (i = 0; i < 5; i++) {
		while ((err_flag[i] == 0) && (rd_tap != 128)) {
			read_data = mtk_switch_r32(gsw, TRGMII_7623_RD_0 + i * 8);
			rd_tap = (read_data & 0x0000007f) + rxd_step_size;	/* Add RXD delay in MT7623 */
			read_data = (read_data & 0xffffff80) | rd_tap;
			mtk_switch_w32(gsw, read_data, TRGMII_7623_RD_0 + i * 8);
			/* Enable EDGE CHK in MT7623 */
			val =
			    mtk_switch_r32(gsw, TRGMII_7623_RD_0 + i * 8) | 0x40000000;
			val &= 0x4fffffff;
			mtk_switch_w32(gsw, val, TRGMII_7623_RD_0 + i * 8);
			wait_loop(gsw);
			read_data = mtk_switch_r32(gsw, TRGMII_7623_RD_0 + i * 8);
			err_cnt[i] = (read_data >> 8) & 0x0000000f;	/* Read MT7623 Errcnt */
			rd_wd = (read_data >> 16) & 0x000000ff;
			if (err_cnt[i] != 0 || rd_wd != 0x55)
				err_flag[i] = 1;
			else
				err_flag[i] = 0;

			/* Disable EDGE CHK in MT7623 */
			mtk_switch_m32(gsw, 0x4fffffff, 0x40000000, TRGMII_7623_RD_0 + i * 8);
			wait_loop(gsw);
		}

		tap_b[i] = rd_tap;	/* -rxd_step_size; */
		pr_debug("MT7623 %dth bit  Tap_b = %d\n", i, tap_b[i]);
		final_tap[i] = (tap_a[i] + tap_b[i]) / 2;	/* Calculate RXD delay = (TAP_A + TAP_B)/2 */
		read_data = (read_data & 0xffffff80) | final_tap[i];
		mtk_switch_w32(gsw, read_data, TRGMII_7623_RD_0 + i * 8);
	}

	read_data = mt7530_mdio_r32(gsw, 0x7A40);
	read_data &= 0x3fffffff;
	mt7530_mdio_w32(gsw, 0x7A40, read_data);
}

static void trgmii_calibration_7530(struct mt7620_gsw *gsw)
{
	unsigned int tap_a[5] = { 0, 0, 0, 0, 0 };
	unsigned int tap_b[5] = { 0, 0, 0, 0, 0 };
	unsigned int final_tap[5];
	unsigned int rxc_step_size;
	unsigned int rxd_step_size;
	unsigned int read_data;
//	unsigned int tmp = 0;
	int i;
	unsigned int err_cnt[5];
	unsigned int rd_wd;
	unsigned int init_toggle_data;
	unsigned int err_flag[5];
//	unsigned int err_total_flag;
	unsigned int training_word;
	unsigned int rd_tap;

	u32 TRGMII_7623_base;
	u32 TRGMII_7530_RD_0;
	u32 TRGMII_RCK_CTRL;
	u32 TRGMII_7530_base;
	u32 TRGMII_7530_TX_base;
	u32 val;

	TRGMII_7623_base = 0x300;
	TRGMII_7530_base = 0x7A00;
	TRGMII_7530_RD_0 = TRGMII_7530_base + 0x10;
	TRGMII_RCK_CTRL = TRGMII_7623_base;
	rxd_step_size = 0x1;
	rxc_step_size = 0x8;
	init_toggle_data = 0x00000055;
	training_word = 0x000000AC;

	TRGMII_7530_TX_base = TRGMII_7530_base + 0x50;

	/* pr_err("Calibration begin ........\n"); */
	val = mtk_switch_r32(gsw, TRGMII_7623_base + 0x40) | 0x80000000;
	mtk_switch_w32(gsw, val, TRGMII_7623_base + 0x40);
	read_data = mt7530_mdio_r32(gsw, 0x7a10);
	/* pr_err("TRGMII_7530_RD_0 is %x\n", read_data); */

	read_data = mt7530_mdio_r32(gsw, TRGMII_7530_base + 0x04);
	read_data &= 0x3fffffff;
	mt7530_mdio_w32(gsw, TRGMII_7530_base + 0x04, read_data);	/* RX clock gating in MT7530 */

	read_data = mt7530_mdio_r32(gsw, TRGMII_7530_base + 0x78);
	read_data |= 0x00002000;
	mt7530_mdio_w32(gsw, TRGMII_7530_base + 0x78, read_data);	/* Set TX OE edge in  MT7530 */

	read_data = mt7530_mdio_r32(gsw, TRGMII_7530_base);
	read_data |= 0x80000000;
	mt7530_mdio_w32(gsw, TRGMII_7530_base, read_data);	/* Assert RX  reset in MT7530 */

	read_data = mt7530_mdio_r32(gsw, TRGMII_7530_base);
	read_data &= 0x7fffffff;
	mt7530_mdio_w32(gsw, TRGMII_7530_base, read_data);	/* Release RX reset in MT7530 */

	read_data = mt7530_mdio_r32(gsw, TRGMII_7530_base + 0x04);
	read_data |= 0xC0000000;
	mt7530_mdio_w32(gsw, TRGMII_7530_base + 0x04, read_data);	/* Disable RX clock gating in MT7530 */

	/* pr_err("Enable Training Mode in MT7623\n"); */
	/*Enable Training Mode in MT7623 */
	val = mtk_switch_r32(gsw, TRGMII_7623_base + 0x40) | 0x80000000;
	mtk_switch_w32(gsw, val, TRGMII_7623_base + 0x40);
	if (gsw->trgmii_force == 2000) {
		val = mtk_switch_r32(gsw, TRGMII_7623_base + 0x40) | 0xC0000000;
		mtk_switch_w32(gsw, val, TRGMII_7623_base + 0x40);
	} else {
		val = mtk_switch_r32(gsw, TRGMII_7623_base + 0x40) | 0x80000000;
		mtk_switch_w32(gsw, val, TRGMII_7623_base + 0x40);
	}
#if 0
	val = mtk_switch_r32(gsw, TRGMII_7623_base + 0x078) & 0xfffff0ff;
	mtk_switch_w32(gsw, val, TRGMII_7623_base + 0x078);
	val = mtk_switch_r32(gsw, TRGMII_7623_base + 0x50) & 0xfffff0ff;
	mtk_switch_w32(gsw, val, TRGMII_7623_base + 0x50);
	val = mtk_switch_r32(gsw, TRGMII_7623_base + 0x58) & 0xfffff0ff;
	mtk_switch_w32(gsw, val, TRGMII_7623_base + 0x58);
	val = mtk_switch_r32(gsw, TRGMII_7623_base + 0x60) & 0xfffff0ff;
	mtk_switch_w32(gsw, val, TRGMII_7623_base + 0x60);
	val = mtk_switch_r32(gsw, TRGMII_7623_base + 0x68) & 0xfffff0ff;
	mtk_switch_w32(gsw, val, TRGMII_7623_base + 0x68);
	val = mtk_switch_r32(gsw, TRGMII_7623_base + 0x70) & 0xfffff0ff;
	mtk_switch_w32(gsw, val, TRGMII_7623_base + 0x70);
	val = mtk_switch_r32(gsw, TRGMII_7623_base + 0x78) & 0x00000800;
	mtk_switch_w32(gsw, val, TRGMII_7623_base + 0x78);
	err_total_flag = 0;
	/* pr_err("Adjust RXC delay in MT7530\n"); */
	read_data = 0x0;
	while (err_total_flag == 0 && (read_data != 0x68)) {
		/* pr_err("2nd Enable EDGE CHK in MT7530\n"); */
		/* Enable EDGE CHK in MT7530 */
		for (i = 0; i < 5; i++) {
			read_data =
			    mt7530_mdio_r32(gsw, TRGMII_7530_RD_0 + i * 8);
			read_data |= 0x40000000;
			read_data &= 0x4fffffff;
			mt7530_mdio_w32(gsw, TRGMII_7530_RD_0 + i * 8,
					read_data);
			wait_loop(gsw);
			/* pr_err("2nd Disable EDGE CHK in MT7530\n"); */
			err_cnt[i] =
			    mt7530_mdio_r32(gsw, TRGMII_7530_RD_0 + i * 8);
			/* pr_err("***** MT7530 %dth bit ERR_CNT =%x\n",i, err_cnt[i]); */
			/* pr_err("MT7530 %dth bit ERR_CNT =%x\n",i, err_cnt[i]); */
			err_cnt[i] >>= 8;
			err_cnt[i] &= 0x0000ff0f;
			rd_wd = err_cnt[i] >> 8;
			rd_wd &= 0x000000ff;
			err_cnt[i] &= 0x0000000f;
			/* read_data = mt7530_mdio_r32(gsw,0x7a10,&read_data); */
			if (err_cnt[i] != 0)
				err_flag[i] = 1;
			else if (rd_wd != 0x55)
				err_flag[i] = 1;
			else
				err_flag[i] = 0;
			if (i == 0)
				err_total_flag = err_flag[i];
			else
				err_total_flag = err_flag[i] & err_total_flag;
			/* Disable EDGE CHK in MT7530 */
			read_data =
			    mt7530_mdio_r32(gsw, TRGMII_7530_RD_0 + i * 8);
			read_data |= 0x40000000;
			read_data &= 0x4fffffff;
			mt7530_mdio_w32(gsw, TRGMII_7530_RD_0 + i * 8,
					read_data);
			wait_loop(gsw);
		}
		/*Adjust RXC delay */
		if (err_total_flag == 0) {
			read_data = mt7530_mdio_r32(gsw, TRGMII_7530_base);
			read_data |= 0x80000000;
			mt7530_mdio_w32(gsw, TRGMII_7530_base, read_data);	/* Assert RX  reset in MT7530 */

			read_data =
			    mt7530_mdio_r32(gsw, TRGMII_7530_base + 0x04);
			read_data &= 0x3fffffff;
			mt7530_mdio_w32(gsw, TRGMII_7530_base + 0x04, read_data);	/* RX clock gating in MT7530 */

			read_data = mt7530_mdio_r32(gsw, TRGMII_7530_base);
			tmp = read_data;
			tmp &= 0x0000007f;
			tmp += rxc_step_size;
			/* pr_err("Current rxc delay = %d\n", tmp); */
			read_data &= 0xffffff80;
			read_data |= tmp;
			mt7530_mdio_w32(gsw, TRGMII_7530_base, read_data);
			read_data = mt7530_mdio_r32(gsw, TRGMII_7530_base);
			/* pr_err("Current RXC delay = %x\n", read_data); */

			read_data = mt7530_mdio_r32(gsw, TRGMII_7530_base);
			read_data &= 0x7fffffff;
			mt7530_mdio_w32(gsw, TRGMII_7530_base, read_data);
			/* Release RX reset in MT7530 */

			read_data =
			    mt7530_mdio_r32(gsw, TRGMII_7530_base + 0x04);
			read_data |= 0xc0000000;
			mt7530_mdio_w32(gsw, TRGMII_7530_base + 0x04, read_data);
			/* Disable RX clock gating in MT7530 */
			pr_debug("####### MT7530 RXC delay is %d\n", tmp);
		}
		read_data = tmp;
	}
	pr_debug("Finish RXC Adjustment while loop\n");
#endif
	/* pr_err("Read RD_WD MT7530\n"); */
	/* Read RD_WD MT7530 */
	for (i = 0; i < 5; i++) {
		rd_tap = 0;
		err_flag[i] = 1;
		while (err_flag[i] != 0 && rd_tap != 128) {
			/* Enable EDGE CHK in MT7530 */
			read_data =
			    mt7530_mdio_r32(gsw, TRGMII_7530_RD_0 + i * 8);
			read_data |= 0x40000000;
			read_data &= 0x4fffffff;
			mt7530_mdio_w32(gsw, TRGMII_7530_RD_0 + i * 8,
					read_data);
			wait_loop(gsw);
			err_cnt[i] = (read_data >> 8) & 0x0000000f;
			rd_wd = (read_data >> 16) & 0x000000ff;
			if (err_cnt[i] != 0 || rd_wd != 0x55)
				err_flag[i] = 1;
			else
				err_flag[i] = 0;

			if (err_flag[i] != 0) {
				rd_tap = (read_data & 0x0000007f) + rxd_step_size;	/* Add RXD delay in MT7530 */
				read_data = (read_data & 0xffffff80) | rd_tap;
				mt7530_mdio_w32(gsw, TRGMII_7530_RD_0 + i * 8,
						read_data);
				tap_a[i] = rd_tap;
			} else {
				tap_a[i] = (read_data & 0x0000007f);	/* Record the min delay TAP_A */
				rd_tap = tap_a[i] + 0x4;
				read_data = (read_data & 0xffffff80) | rd_tap;
				mt7530_mdio_w32(gsw, TRGMII_7530_RD_0 + i * 8,
						read_data);
			}

			/* Disable EDGE CHK in MT7530 */
			read_data =
			    mt7530_mdio_r32(gsw, TRGMII_7530_RD_0 + i * 8);
			read_data |= 0x40000000;
			read_data &= 0x4fffffff;
			mt7530_mdio_w32(gsw, TRGMII_7530_RD_0 + i * 8,
					read_data);
			wait_loop(gsw);
		}
		pr_err("MT7530 %dth bit  Tap_a = %d\n", i, tap_a[i]);
	}

	/* pr_err("Last While Loop\n"); */
	for (i = 0; i < 5; i++) {
		rd_tap = 0;
		while (err_flag[i] == 0 && (rd_tap != 128)) {
			/* Enable EDGE CHK in MT7530 */
			read_data = mt7530_mdio_r32(gsw, TRGMII_7530_RD_0 + i * 8);
			read_data |= 0x40000000;
			read_data &= 0x4fffffff;
			mt7530_mdio_w32(gsw, TRGMII_7530_RD_0 + i * 8,
					read_data);
			wait_loop(gsw);
			err_cnt[i] = (read_data >> 8) & 0x0000000f;
			rd_wd = (read_data >> 16) & 0x000000ff;
			if (err_cnt[i] != 0 || rd_wd != 0x55)
				err_flag[i] = 1;
			else
				err_flag[i] = 0;

			if (err_flag[i] == 0 && (rd_tap != 128)) {
				/* Add RXD delay in MT7530 */
				rd_tap = (read_data & 0x0000007f) + rxd_step_size;
				read_data = (read_data & 0xffffff80) | rd_tap;
				mt7530_mdio_w32(gsw, TRGMII_7530_RD_0 + i * 8,
						read_data);
			}
			/* Disable EDGE CHK in MT7530 */
			read_data =
			    mt7530_mdio_r32(gsw, TRGMII_7530_RD_0 + i * 8);
			read_data |= 0x40000000;
			read_data &= 0x4fffffff;
			mt7530_mdio_w32(gsw, TRGMII_7530_RD_0 + i * 8,
					read_data);
			wait_loop(gsw);
		}
		tap_b[i] = rd_tap;	/* - rxd_step_size; */
		pr_err("MT7530 %dth bit  Tap_b = %d\n", i, tap_b[i]);
		/* Calculate RXD delay = (TAP_A + TAP_B)/2 */
		final_tap[i] = (tap_a[i] + tap_b[i]) / 2;
		/* pr_err("########****** MT7530 %dth bit Final Tap = %d\n", i, final_tap[i]); */

		read_data = (read_data & 0xffffff80) | final_tap[i];
		mt7530_mdio_w32(gsw, TRGMII_7530_RD_0 + i * 8, read_data);
	}

	if (gsw->trgmii_force == 2000)
		mtk_switch_m32(gsw, 0x7fffffff, 0, TRGMII_7623_base + 0x40);
	else
		mtk_switch_m32(gsw, 0x3fffffff, 0, TRGMII_7623_base + 0x40);
}

static void mt7530_trgmii_clock_setting(struct mt7620_gsw *gsw, u32 xtal_mode)
{
	u32 regv;

	/* TRGMII Clock */
	cb_mtk_mdio_write(gsw->eth, 0, 13, 0x1f);
	cb_mtk_mdio_write(gsw->eth, 0, 14, 0x410);
	cb_mtk_mdio_write(gsw->eth, 0, 13, 0x401f);
	cb_mtk_mdio_write(gsw->eth, 0, 14, 0x1);
	cb_mtk_mdio_write(gsw->eth, 0, 13, 0x1f);
	cb_mtk_mdio_write(gsw->eth, 0, 14, 0x404);
	cb_mtk_mdio_write(gsw->eth, 0, 13, 0x401f);

	if (xtal_mode == 1) {
		/* 25MHz */
		if (gsw->trgmii_force == 2600)
			/* 325MHz */
			cb_mtk_mdio_write(gsw->eth, 0, 14, 0x1a00);
		else if (gsw->trgmii_force == 2000)
			/* 250MHz */
			cb_mtk_mdio_write(gsw->eth, 0, 14, 0x1400);
	} else if (xtal_mode == 2) {
		/* 40MHz */
		if (gsw->trgmii_force == 2600)
			/* 325MHz */
			cb_mtk_mdio_write(gsw->eth, 0, 14, 0x1040);
		else if (gsw->trgmii_force == 2000)
			/* 250MHz */
			cb_mtk_mdio_write(gsw->eth, 0, 14, 0x0c80);
	}
	cb_mtk_mdio_write(gsw->eth, 0, 13, 0x1f);
	cb_mtk_mdio_write(gsw->eth, 0, 14, 0x405);
	cb_mtk_mdio_write(gsw->eth, 0, 13, 0x401f);
	cb_mtk_mdio_write(gsw->eth, 0, 14, 0x0);
	cb_mtk_mdio_write(gsw->eth, 0, 13, 0x1f);
	cb_mtk_mdio_write(gsw->eth, 0, 14, 0x409);
	cb_mtk_mdio_write(gsw->eth, 0, 13, 0x401f);
	if (xtal_mode == 1)
		/* 25MHz */
		cb_mtk_mdio_write(gsw->eth, 0, 14, 0x0057);
	else
		/* 40MHz */
		cb_mtk_mdio_write(gsw->eth, 0, 14, 0x0087);
	cb_mtk_mdio_write(gsw->eth, 0, 13, 0x1f);
	cb_mtk_mdio_write(gsw->eth, 0, 14, 0x40a);
	cb_mtk_mdio_write(gsw->eth, 0, 13, 0x401f);
	if (xtal_mode == 1)
		/* 25MHz */
		cb_mtk_mdio_write(gsw->eth, 0, 14, 0x0057);
	else
		/* 40MHz */
		cb_mtk_mdio_write(gsw->eth, 0, 14, 0x0087);

	cb_mtk_mdio_write(gsw->eth, 0, 13, 0x1f);
	cb_mtk_mdio_write(gsw->eth, 0, 14, 0x403);
	cb_mtk_mdio_write(gsw->eth, 0, 13, 0x401f);
	cb_mtk_mdio_write(gsw->eth, 0, 14, 0x1800);

	cb_mtk_mdio_write(gsw->eth, 0, 13, 0x1f);
	cb_mtk_mdio_write(gsw->eth, 0, 14, 0x403);
	cb_mtk_mdio_write(gsw->eth, 0, 13, 0x401f);
	cb_mtk_mdio_write(gsw->eth, 0, 14, 0x1c00);

	cb_mtk_mdio_write(gsw->eth, 0, 13, 0x1f);
	cb_mtk_mdio_write(gsw->eth, 0, 14, 0x401);
	cb_mtk_mdio_write(gsw->eth, 0, 13, 0x401f);
	cb_mtk_mdio_write(gsw->eth, 0, 14, 0xc020);

	cb_mtk_mdio_write(gsw->eth, 0, 13, 0x1f);
	cb_mtk_mdio_write(gsw->eth, 0, 14, 0x406);
	cb_mtk_mdio_write(gsw->eth, 0, 13, 0x401f);
	cb_mtk_mdio_write(gsw->eth, 0, 14, 0xa030);

	cb_mtk_mdio_write(gsw->eth, 0, 13, 0x1f);
	cb_mtk_mdio_write(gsw->eth, 0, 14, 0x406);
	cb_mtk_mdio_write(gsw->eth, 0, 13, 0x401f);
	cb_mtk_mdio_write(gsw->eth, 0, 14, 0xa038);

	usleep_range(120, 140);		/* for MT7623 bring up test */

	cb_mtk_mdio_write(gsw->eth, 0, 13, 0x1f);
	cb_mtk_mdio_write(gsw->eth, 0, 14, 0x410);
	cb_mtk_mdio_write(gsw->eth, 0, 13, 0x401f);
	cb_mtk_mdio_write(gsw->eth, 0, 14, 0x3);

	regv = mt7530_mdio_r32(gsw, 0x7830);
	regv &= 0xFFFFFFFC;
//	regv |= 0x00000001;
	mt7530_mdio_w32(gsw, 0x7830, regv);

	regv = mt7530_mdio_r32(gsw, 0x7a40);
	regv &= ~(0x1 << 30);
	regv &= ~(0x1 << 28);
	mt7530_mdio_w32(gsw, 0x7a40, regv);

	mt7530_mdio_w32(gsw, 0x7a78, 0x55);
	usleep_range(100, 125);		/* for mt7623 bring up test */

	mtk_switch_m32(gsw, 0x7fffffff, 0, 0x300);

	trgmii_calibration_7623(gsw);
	trgmii_calibration_7530(gsw);

	mtk_switch_m32(gsw, ~0, 0x80000000, 0x300);
	mtk_switch_m32(gsw, 0x7fffffff, 0, 0x300);

	/*MT7530 RXC reset */
	regv = mt7530_mdio_r32(gsw, 0x7a00);
	regv |= (0x1 << 31);
	mt7530_mdio_w32(gsw, 0x7a00, regv);
	mdelay(1);
	regv &= ~(0x1 << 31);
	mt7530_mdio_w32(gsw, 0x7a00, regv);
	mdelay(100);
}

static void mt7530_hw_deinit(struct mtk_eth *eth, struct mt7620_gsw *gsw, struct platform_device *pdev)
{
	int ret;

	ret = regulator_disable(gsw->supply);
	if (ret)
		dev_err(&pdev->dev, "Failed to disable mt7530 power: %d\n", ret);
	
	if (gsw->mcm) {
		ret = regulator_disable(gsw->b3v);
		if (ret)
			dev_err(&pdev->dev, "Failed to disable b3v: %d\n", ret);
	}
}

static void mt7530_hw_preinit(struct mtk_eth *eth, struct mt7620_gsw *gsw, struct platform_device *pdev)
{
	int ret;

	if (!gsw->mcm && pinctrl_select_state(gsw->pins, gsw->ps_reset))
		dev_err(&pdev->dev, "failed to select gsw_reset state\n");

	regulator_set_voltage(gsw->supply, 1000000, 1000000);
	ret = regulator_enable(gsw->supply);
	if (ret)
		dev_err(&pdev->dev, "Failed to enable mt7530 power: %d\n", ret);

	if (gsw->mcm) {
		regulator_set_voltage(gsw->b3v, 3300000, 3300000);
		ret = regulator_enable(gsw->b3v);
		if (ret)
			dev_err(&pdev->dev, "Failed to enable b3v: %d\n", ret);
	}

	if (!gsw->mcm) {
		ret = devm_gpio_request(&pdev->dev, gsw->reset_pin, "mediatek,reset-pin");
		if (ret)
			dev_err(&pdev->dev, "fail to devm_gpio_request\n");

		gpio_direction_output(gsw->reset_pin, 0);
		usleep_range(1000, 1100);
		gpio_set_value(gsw->reset_pin, 1);
		mdelay(100);
		devm_gpio_free(&pdev->dev, gsw->reset_pin);
	}
}

static void mt7530_hw_init(struct mtk_eth *eth, struct mt7620_gsw *gsw, struct device_node *np)
{
	u32     i;
	u32     val;
	u32     xtal_mode;

	if ((!gsw->eth->mac[0] || !of_phy_is_fixed_link(gsw->eth->mac[0]->of_node)) &&
	    (!gsw->eth->mac[1] || !of_phy_is_fixed_link(gsw->eth->mac[1]->of_node))) {
		dev_info(gsw->dev, "no switch found, return\n");
		return;
	}

	if (gsw->mcm) {
		#define SYSC_REG_RSTCTRL      0x34
		regmap_update_bits(eth->ethsys, SYSC_REG_RSTCTRL,
				   RESET_MCM,
				   RESET_MCM);

		usleep_range(1000, 1100);
		regmap_update_bits(eth->ethsys, SYSC_REG_RSTCTRL,
				   RESET_MCM,
				   ~RESET_MCM);
		mdelay(100);

		regmap_update_bits(gsw->ethsys, ETHSYS_CLKCFG0,
				   ETHSYS_TRGMII_CLK_SEL362_5,
				   ETHSYS_TRGMII_CLK_SEL362_5);
	}

	/* reset the TRGMII core */
	mtk_switch_m32(gsw, ~0, 0 /*INTF_MODE_TRGMII*/, GSW_INTF_MODE);
	/* Assert MT7623 RXC reset */
	mtk_switch_m32(gsw, ~0, TRGMII_RCK_CTRL_RX_RST, GSW_TRGMII_RCK_CTRL);

	for (i = 0; i < 100; i++) {
		mdelay(10);
		if (mt7530_mdio_r32(gsw, MT7530_HWTRAP)) {
			pr_info("mt7530 reset completed!!\n");
			break;
		}

		if (i == 99) {
			pr_info("mt7530 reset timeout!!\n");
			return;
		}
	}
	/* turn off all PHYs */
	for (i = 0; i <= 4; i++) {
		val = cb_mtk_mdio_read(gsw->eth, i, 0x0);
		val |= BIT(11);
		cb_mtk_mdio_write(gsw->eth, i, 0x0, val);
	}

	/* reset the switch */
	mt7530_mdio_w32(gsw, MT7530_SYS_CTRL,
			SYS_CTRL_SW_RST | SYS_CTRL_REG_RST);

	usleep_range(100, 200);

	/* GE1, Force 1000M/FD, FC ON */
	mt7530_mdio_w32(gsw, MT7530_PMCR_P(6), PMCR_FIXED_LINK_FC);

	/* GE2, Force 1000M/FD, FC ON */
	mt7530_mdio_w32(gsw, MT7530_PMCR_P(5), PMCR_FIXED_LINK_FC);

	/* Enable Port 6, P5 as GMAC5, P5 disable */
	val = mt7530_mdio_r32(gsw, MT7530_MHWTRAP);
	if (gsw->eth->mac[0] &&
	    of_phy_is_fixed_link(gsw->eth->mac[0]->of_node)) {
		/* Enable Port 6 */
		val &= ~MHWTRAP_P6_DIS;
		dev_info(gsw->dev, "mac [0] is fixed link\n");
	} else {
		/* Disable Port 6 */
		val |= MHWTRAP_P6_DIS;
		dev_info(gsw->dev, "mac [0] is not fixed link\n");
	}
	if (gsw->eth->mac[1] &&
	    of_phy_is_fixed_link(gsw->eth->mac[1]->of_node)) {
		/* Enable Port 5 */
		if (gsw->port_map != LLLLL)
			val &= ~MHWTRAP_P5_DIS;
		else
			val |= MHWTRAP_P5_DIS;
		/* Port 5 as GMAC */
		val |= MHWTRAP_P5_MAC_SEL | MHWTRAP_P5_RGMII_MODE;
		dev_info(gsw->dev, "mac [1] is fixed link\n");
	} else {
		/* Disable Port 5 */
		val |= MHWTRAP_P5_DIS;
		/* Port 5 as GMAC */
		val |= MHWTRAP_P5_MAC_SEL | MHWTRAP_P5_RGMII_MODE;
		dev_info(gsw->dev, "mac [1] is not fixed link\n");
		mt7530_mdio_w32(gsw, MT7530_PMCR_P(5), 0x8000);
	}

	/* Set MT7530 phy direct access mode**/
	val &= ~MHWTRAP_PHY_ACCESS;
	/* manual override of HW-Trap */
	val |= MHWTRAP_MANUAL;
	mt7530_mdio_w32(gsw, MT7530_MHWTRAP, val);
	dev_info(gsw->dev, "Setting MHWTRAP to 0x%08x\n", val);

	val = mt7530_mdio_r32(gsw, 0x7800);
	val = (val >> 9) & 0x3;

val = 2;
	if (val == 0x3) {
		xtal_mode = 1;
		/* 25Mhz Xtal - do nothing */
	} else if (val == 0x2) {
		/* 40Mhz */
		xtal_mode = 2;

		/* disable MT7530 core clock */
		cb_mtk_mdio_write(gsw->eth, 0, 13, 0x1f);
		cb_mtk_mdio_write(gsw->eth, 0, 14, 0x410);
		cb_mtk_mdio_write(gsw->eth, 0, 13, 0x401f);
		cb_mtk_mdio_write(gsw->eth, 0, 14, 0x0);

		/* disable MT7530 PLL */
		cb_mtk_mdio_write(gsw->eth, 0, 13, 0x1f);
		cb_mtk_mdio_write(gsw->eth, 0, 14, 0x40d);
		cb_mtk_mdio_write(gsw->eth, 0, 13, 0x401f);
		cb_mtk_mdio_write(gsw->eth, 0, 14, 0x2020);

		/* for MT7530 core clock = 500Mhz */
		cb_mtk_mdio_write(gsw->eth, 0, 13, 0x1f);
		cb_mtk_mdio_write(gsw->eth, 0, 14, 0x40e);
		cb_mtk_mdio_write(gsw->eth, 0, 13, 0x401f);
		cb_mtk_mdio_write(gsw->eth, 0, 14, 0x119);

		/* enable MT7530 PLL */
		cb_mtk_mdio_write(gsw->eth, 0, 13, 0x1f);
		cb_mtk_mdio_write(gsw->eth, 0, 14, 0x40d);
		cb_mtk_mdio_write(gsw->eth, 0, 13, 0x401f);
		cb_mtk_mdio_write(gsw->eth, 0, 14, 0x2820);

		usleep_range(20, 120);

		/* enable MT7530 core clock */
		cb_mtk_mdio_write(gsw->eth, 0, 13, 0x1f);
		cb_mtk_mdio_write(gsw->eth, 0, 14, 0x410);
		cb_mtk_mdio_write(gsw->eth, 0, 13, 0x401f);
	} else {
		xtal_mode = 3;
		/* 20Mhz Xtal - TODO */
	}

	/* RGMII */
	cb_mtk_mdio_write(gsw->eth, 0, 14, 0x1);

	/* set MT7530 central align */
	val = mt7530_mdio_r32(gsw, 0x7830);
	val &= ~1;
	val |= 1 << 1;
	mt7530_mdio_w32(gsw, 0x7830, val);

	val = mt7530_mdio_r32(gsw, 0x7a40);
	val &= ~(1 << 30);
	mt7530_mdio_w32(gsw, 0x7a40, val);

	mt7530_mdio_w32(gsw, 0x7a78, 0x855);

	/* delay setting for 10/1000M */
	mt7530_mdio_w32(gsw, 0x7b00, 0x104);
	mt7530_mdio_w32(gsw, 0x7b04, 0x10);

	/* lower Tx Driving */
	mt7530_mdio_w32(gsw, 0x7a54, 0x88);
	mt7530_mdio_w32(gsw, 0x7a5c, 0x88);
	mt7530_mdio_w32(gsw, 0x7a64, 0x88);
	mt7530_mdio_w32(gsw, 0x7a6c, 0x88);
	mt7530_mdio_w32(gsw, 0x7a74, 0x88);
	mt7530_mdio_w32(gsw, 0x7a7c, 0x88);
	mt7530_mdio_w32(gsw, 0x7810, 0x11);

	/* Set MT7623/MT7683 TX Driving */
	mtk_switch_w32(gsw, 0x88, 0x354);
	mtk_switch_w32(gsw, 0x88, 0x35c);
	mtk_switch_w32(gsw, 0x88, 0x364);
	mtk_switch_w32(gsw, 0x88, 0x36c);
	mtk_switch_w32(gsw, 0x88, 0x374);
	mtk_switch_w32(gsw, 0x88, 0x37c);

	mt7530_trgmii_clock_setting(gsw, xtal_mode);

	/*LANWANPartition(gsw);*/

	/* disable EEE */
	for (i = 0; i <= 4; i++) {
		cb_mtk_mdio_write(gsw->eth, i, 13, 0x7);
		cb_mtk_mdio_write(gsw->eth, i, 14, 0x3C);
		cb_mtk_mdio_write(gsw->eth, i, 13, 0x4007);
		cb_mtk_mdio_write(gsw->eth, i, 14, 0x0);

		/* Increase SlvDPSready time */
		cb_mtk_mdio_write(gsw->eth, i, 31, 0x52b5);
		cb_mtk_mdio_write(gsw->eth, i, 16, 0xafae);
		cb_mtk_mdio_write(gsw->eth, i, 18, 0x2f);
		cb_mtk_mdio_write(gsw->eth, i, 16, 0x8fae);

		/* Incease post_update_timer */
		cb_mtk_mdio_write(gsw->eth, i, 31, 0x3);
		cb_mtk_mdio_write(gsw->eth, i, 17, 0x4b);

		/* Adjust 100_mse_threshold */
		cb_mtk_mdio_write(gsw->eth, i, 13, 0x1e);
		cb_mtk_mdio_write(gsw->eth, i, 14, 0x123);
		cb_mtk_mdio_write(gsw->eth, i, 13, 0x401e);
		cb_mtk_mdio_write(gsw->eth, i, 14, 0xffff);

		/* Disable mcc */
		cb_mtk_mdio_write(gsw->eth, i, 13, 0x1e);
		cb_mtk_mdio_write(gsw->eth, i, 14, 0xa6);
		cb_mtk_mdio_write(gsw->eth, i, 13, 0x401e);
		cb_mtk_mdio_write(gsw->eth, i, 14, 0x300);

		/* Disable HW auto downshift*/
		cb_mtk_mdio_write(gsw->eth, i, 31, 0x1);
		val = cb_mtk_mdio_read(gsw->eth, i, 0x14);
		val &= ~(1 << 4);
		cb_mtk_mdio_write(gsw->eth, i, 0x14, val);
	}

	/* turn on all PHYs */
	for (i = 0; i <= 4; i++) {
		val = cb_mtk_mdio_read(gsw->eth, i, 0);
		val &= ~BIT(11);
		cb_mtk_mdio_write(gsw->eth, i, 0, val);
	}

	/* enable irq */
	mt7530_mdio_m32(gsw, 0, TOP_SIG_CTRL_NORMAL, MT7530_TOP_SIG_CTRL);


{
	u32  offset, data;
	int i;
	struct mt7530_ranges {
		u32 start;
		u32 end;
	} ranges[] = {
		{0x0, 0xac},
		{0x1000, 0x10e0},
		{0x1100, 0x1140},
		{0x1200, 0x1240},
		{0x1300, 0x1340},
		{0x1400, 0x1440},
		{0x1500, 0x1540},
		{0x1600, 0x1640},
		{0x1800, 0x1848},
		{0x1900, 0x1948},
		{0x1a00, 0x1a48},
		{0x1b00, 0x1b48},
		{0x1c00, 0x1c48},
		{0x1d00, 0x1d48},
		{0x1e00, 0x1e48},
		{0x1f60, 0x1ffc},
		{0x2000, 0x212c},
		{0x2200, 0x222c},
		{0x2300, 0x232c},
		{0x2400, 0x242c},
		{0x2500, 0x252c},
		{0x2600, 0x262c},
		{0x3000, 0x3014},
		{0x30c0, 0x30f8},
		{0x3100, 0x3114},
		{0x3200, 0x3214},
		{0x3300, 0x3314},
		{0x3400, 0x3414},
		{0x3500, 0x3514},
		{0x3600, 0x3614},
		{0x4000, 0x40d4},
		{0x4100, 0x41d4},
		{0x4200, 0x42d4},
		{0x4300, 0x43d4},
		{0x4400, 0x44d4},
		{0x4500, 0x45d4},
		{0x4600, 0x46d4},
		{0x4f00, 0x461c},
		{0x7000, 0x7038},
		{0x7120, 0x7124},
		{0x7800, 0x7804},
		{0x7810, 0x7810},
		{0x7830, 0x7830},
		{0x7a00, 0x7a7c},
		{0x7b00, 0x7b04},
		{0x7e00, 0x7e04},
		{0x7ffc, 0x7ffc},
	};

	for (i = 0 ; i < ARRAY_SIZE(ranges) ; i++) {
		for (offset = ranges[i].start ; offset <= ranges[i].end ; offset += 4) {
			data =  mt7530_mdio_r32(gsw, offset);
			//dev_info(gsw->dev, "mt7530 switch reg=0x%08x, data=0x%08x\n",
		        //		   offset, data);
		}
	}
}
}

static const struct of_device_id mediatek_gsw_match[] = {
	{ .compatible = "mediatek,mt7623-gsw" },
	{},
};
MODULE_DEVICE_TABLE(of, mediatek_gsw_match);

int mtk_gsw_sw_init(struct mtk_eth *eth, struct mtk_eth_cb *cb)
{
	struct device_node *np = eth->switch_np;
	struct platform_device *pdev = of_find_device_by_node(np);
	struct mt7620_gsw *gsw;

	if (!pdev)
		return -ENODEV;

	if (!of_device_is_compatible(np, mediatek_gsw_match->compatible))
		return -EINVAL;

	gsw = platform_get_drvdata(pdev);
	if (!gsw)
		return -ENODEV;
	gsw->eth = eth;
	eth->sw_priv = gsw;

	pr_debug("%s(%d): register mtk_eth_soc callback\n", __func__, __LINE__);
	_cb_mtk_mdio_read = cb->mtk_mdio_read;
	_cb_mtk_mdio_write = cb->mtk_mdio_write;
	_cb_mtk_w32 = cb->mtk_w32;
	_cb_mtk_r32 = cb->mtk_r32;

	if (request_threaded_irq(gsw->irq, gsw_interrupt_mt7623, NULL, 0,
				 "gsw", eth))
		pr_err("fail to request irq\n");

	return 0;
}
EXPORT_SYMBOL(mtk_gsw_sw_init);

int mtk_gsw_sw_deinit(struct mtk_eth *eth)
{
	struct mt7620_gsw *gsw = eth->sw_priv;

	free_irq(gsw->irq, eth);
	return 0;
}
EXPORT_SYMBOL(mtk_gsw_sw_deinit);

int mtk_gsw_hw_init(struct mtk_eth *eth, int enable)
{
	struct device_node *np = eth->switch_np;
	struct platform_device *pdev = of_find_device_by_node(np);
	struct mt7620_gsw *gsw;

	if (!pdev)
		return -ENODEV;

	if (!of_device_is_compatible(np, mediatek_gsw_match->compatible))
		return -EINVAL;

	gsw = platform_get_drvdata(pdev);
	if (!gsw)
		return -ENODEV;

	if (enable) {
		mt7530_hw_preinit(eth, gsw, pdev);
		mt7530_hw_init(eth, gsw, np);
		mt7530_mdio_w32(gsw, 0x7008, 0x1f);
	} else {
		mt7530_hw_deinit(eth, gsw, pdev);
	}

	return 0;
}
EXPORT_SYMBOL(mtk_gsw_hw_init);

static int mt7623_gsw_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct mt7620_gsw *gsw;
	int err;
	const char *pm;

	gsw = devm_kzalloc(&pdev->dev, sizeof(struct mt7620_gsw), GFP_KERNEL);
	if (!gsw)
		return -ENOMEM;

	gsw->dev = &pdev->dev;
	err = of_property_read_string(pdev->dev.of_node,
				      "mcm", &pm);
	if (!err && !strcasecmp(pm, "enable")) {
		gsw->mcm = true;
		pr_info("== MT7530 MCM ==\n");
	}

	gsw->trgmii_force = 2000;

	gsw->irq = irq_of_parse_and_map(np, 0);
	if (gsw->irq < 0)
		return -EINVAL;

	gsw->ethsys = syscon_regmap_lookup_by_phandle(np, "mediatek,ethsys");
	if (IS_ERR(gsw->ethsys))
		return PTR_ERR(gsw->ethsys);

	if (!gsw->mcm) {
		gsw->reset_pin = of_get_named_gpio(np, "mediatek,reset-pin", 0);

		if (gsw->reset_pin < 0)
			return -1;
		pr_debug("reset_pin_port= %d\n", gsw->reset_pin);

		gsw->pins = pinctrl_get(&pdev->dev);
			if (gsw->pins) {
				gsw->ps_reset =
					pinctrl_lookup_state(gsw->pins, "reset");

				if (IS_ERR(gsw->ps_reset)) {
					dev_dbg(&pdev->dev,
						"failed to lookup the gsw_reset state\n");
					return PTR_ERR(gsw->ps_reset);
				}
		} else {
			dev_dbg(&pdev->dev, "gsw get pinctrl fail\n");
			return PTR_ERR(gsw->pins);
		}
	}

	gsw->supply = devm_regulator_get(&pdev->dev, "mt7530");
	if (IS_ERR(gsw->supply))
		return PTR_ERR(gsw->supply);

	if (gsw->mcm) {
		gsw->b3v = devm_regulator_get(&pdev->dev, "b3v");
		if (IS_ERR(gsw->b3v))
			return PTR_ERR(gsw->b3v);
	}

	err = of_property_read_string(pdev->dev.of_node,
				      "mediatek,port_map", &pm);
	if (err || !strcasecmp(pm, "lllll"))
		gsw->port_map = LLLLL;
	else if (!strcasecmp(pm, "wllll"))
		gsw->port_map = WLLLL;
	else if (!strcasecmp(pm, "llllw"))
		gsw->port_map = LLLLW;
	platform_set_drvdata(pdev, gsw);

	return 0;
}

static int mt7623_gsw_remove(struct platform_device *pdev)
{
	platform_set_drvdata(pdev, NULL);

	return 0;
}

static struct platform_driver gsw_driver = {
	.probe = mt7623_gsw_probe,
	.remove = mt7623_gsw_remove,
	.driver = {
		.name = "mt7623-gsw",
		.owner = THIS_MODULE,
		.of_match_table = mediatek_gsw_match,
	},
};

module_platform_driver(gsw_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("John Crispin <blogic@openwrt.org>");
MODULE_DESCRIPTION("GBit switch driver for Mediatek MT7623 SoC");
