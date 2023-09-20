// SPDX-License-Identifier: GPL-2.0-or-later
/* MediaTek 10GE SerDes PHY driver
 *
 * Copyright (c) 2023 Daniel Golle <daniel@makrotopia.org>
 * based on mtk_usxgmii.c found in MediaTek's SDK released under GPL-2.0
 * Copyright (c) 2022 MediaTek Inc.
 * Author: Henry Yen <henry.yen@mediatek.com>
 */

#include <linux/printk.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/netdevice.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/io.h>
#include <linux/clk.h>
#include <linux/reset.h>
#include <linux/phy.h>
#include <linux/phy/phy.h>

struct mtk_pextp_phy {
	void __iomem		*base;
	struct device		*dev;
	struct reset_control	*reset;
	struct clk 		*clk;
	bool			ad_war;
};

static inline bool mtk_interface_mode_is_xgmii(phy_interface_t interface)
{
	switch (interface) {
	case PHY_INTERFACE_MODE_INTERNAL:
	case PHY_INTERFACE_MODE_USXGMII:
	case PHY_INTERFACE_MODE_10GBASER:
	case PHY_INTERFACE_MODE_5GBASER:
		return true;
	default:
		return false;
	}
}

static void mtk_pextp_setup(struct mtk_pextp_phy *pextp, phy_interface_t interface)
{
	bool is_10g = (interface == PHY_INTERFACE_MODE_10GBASER ||
		       interface == PHY_INTERFACE_MODE_USXGMII);
	bool is_2p5g = (interface == PHY_INTERFACE_MODE_2500BASEX);
	bool is_5g = (interface == PHY_INTERFACE_MODE_5GBASER);

	dev_dbg(pextp->dev, "setting up for mode %s\n", phy_modes(interface));

	/* Setup operation mode */
	if (is_10g)
		iowrite32(0x00C9071C, pextp->base + 0x9024);
	else
		iowrite32(0x00D9071C, pextp->base + 0x9024);

	if (is_5g)
		iowrite32(0xAAA5A5AA, pextp->base + 0x2020);
	else
		iowrite32(0xAA8585AA, pextp->base + 0x2020);

	if (is_2p5g || is_5g || is_10g) {
		iowrite32(0x0C020707, pextp->base + 0x2030);
		iowrite32(0x0E050F0F, pextp->base + 0x2034);
		iowrite32(0x00140032, pextp->base + 0x2040);
	} else {
		iowrite32(0x0C020207, pextp->base + 0x2030);
		iowrite32(0x0E05050F, pextp->base + 0x2034);
		iowrite32(0x00200032, pextp->base + 0x2040);
	}

	if (is_2p5g || is_10g)
		iowrite32(0x00C014AA, pextp->base + 0x50F0);
	else if (is_5g)
		iowrite32(0x00C018AA, pextp->base + 0x50F0);
	else
		iowrite32(0x00C014BA, pextp->base + 0x50F0);

	if (is_5g) {
		iowrite32(0x3777812B, pextp->base + 0x50E0);
		iowrite32(0x005C9CFF, pextp->base + 0x506C);
		iowrite32(0x9DFAFAFA, pextp->base + 0x5070);
		iowrite32(0x273F3F3F, pextp->base + 0x5074);
		iowrite32(0xA8883868, pextp->base + 0x5078);
		iowrite32(0x14661466, pextp->base + 0x507C);
	} else {
		iowrite32(0x3777C12B, pextp->base + 0x50E0);
		iowrite32(0x005F9CFF, pextp->base + 0x506C);
		iowrite32(0x9D9DFAFA, pextp->base + 0x5070);
		iowrite32(0x27273F3F, pextp->base + 0x5074);
		iowrite32(0xA7883C68, pextp->base + 0x5078);
		iowrite32(0x11661166, pextp->base + 0x507C);
	}

	if (is_2p5g || is_10g) {
		iowrite32(0x0E000AAF, pextp->base + 0x5080);
		iowrite32(0x08080D0D, pextp->base + 0x5084);
		iowrite32(0x02030909, pextp->base + 0x5088);
	} else if (is_5g) {
		iowrite32(0x0E001ABF, pextp->base + 0x5080);
		iowrite32(0x080B0D0D, pextp->base + 0x5084);
		iowrite32(0x02050909, pextp->base + 0x5088);
	} else {
		iowrite32(0x0E000EAF, pextp->base + 0x5080);
		iowrite32(0x08080E0D, pextp->base + 0x5084);
		iowrite32(0x02030B09, pextp->base + 0x5088);
	}

	if (is_5g) {
		iowrite32(0x0C000000, pextp->base + 0x50E4);
		iowrite32(0x04000000, pextp->base + 0x50E8);
	} else {
		iowrite32(0x0C0C0000, pextp->base + 0x50E4);
		iowrite32(0x04040000, pextp->base + 0x50E8);
	}

	if (is_2p5g || mtk_interface_mode_is_xgmii(interface))
		iowrite32(0x0F0F0C06, pextp->base + 0x50EC);
	else
		iowrite32(0x0F0F0606, pextp->base + 0x50EC);

	if (is_5g) {
		iowrite32(0x50808C8C, pextp->base + 0x50A8);
		iowrite32(0x18000000, pextp->base + 0x6004);
	} else {
		iowrite32(0x506E8C8C, pextp->base + 0x50A8);
		iowrite32(0x18190000, pextp->base + 0x6004);
	}

	if (is_10g)
		iowrite32(0x01423342, pextp->base + 0x00F8);
	else if (is_5g)
		iowrite32(0x00A132A1, pextp->base + 0x00F8);
	else if (is_2p5g)
		iowrite32(0x009C329C, pextp->base + 0x00F8);
	else
		iowrite32(0x00FA32FA, pextp->base + 0x00F8);

	/* Force SGDT_OUT off and select PCS */
	if (mtk_interface_mode_is_xgmii(interface))
		iowrite32(0x80201F20, pextp->base + 0x00F4);
	else
		iowrite32(0x80201F21, pextp->base + 0x00F4);

	/* Force GLB_CKDET_OUT */
	iowrite32(0x00050C00, pextp->base + 0x0030);

	/* Force AEQ on */
	iowrite32(0x02002800, pextp->base + 0x0070);
	ndelay(1020);

	/* Setup DA default value */
	iowrite32(0x00000020, pextp->base + 0x30B0);
	iowrite32(0x00008A01, pextp->base + 0x3028);
	iowrite32(0x0000A884, pextp->base + 0x302C);
	iowrite32(0x00083002, pextp->base + 0x3024);
	if (mtk_interface_mode_is_xgmii(interface)) {
		iowrite32(0x00022220, pextp->base + 0x3010);
		iowrite32(0x0F020A01, pextp->base + 0x5064);
		iowrite32(0x06100600, pextp->base + 0x50B4);
		if (interface == PHY_INTERFACE_MODE_USXGMII)
			iowrite32(0x40704000, pextp->base + 0x3048);
		else
			iowrite32(0x47684100, pextp->base + 0x3048);
	} else {
		iowrite32(0x00011110, pextp->base + 0x3010);
		iowrite32(0x40704000, pextp->base + 0x3048);
	}

	if (!mtk_interface_mode_is_xgmii(interface) && !is_2p5g)
		iowrite32(0x0000C000, pextp->base + 0x3064);

	if (interface == PHY_INTERFACE_MODE_USXGMII) {
		iowrite32(0xA8000000, pextp->base + 0x3050);
		iowrite32(0x000000AA, pextp->base + 0x3054);
	} else if (mtk_interface_mode_is_xgmii(interface)) {
		iowrite32(0x00000000, pextp->base + 0x3050);
		iowrite32(0x00000000, pextp->base + 0x3054);
	} else {
		iowrite32(0xA8000000, pextp->base + 0x3050);
		iowrite32(0x000000AA, pextp->base + 0x3054);
	}

	if (mtk_interface_mode_is_xgmii(interface))
		iowrite32(0x00000F00, pextp->base + 0x306C);
	else if (is_2p5g)
		iowrite32(0x22000F00, pextp->base + 0x306C);
	else
		iowrite32(0x20200F00, pextp->base + 0x306C);

	if (interface == PHY_INTERFACE_MODE_10GBASER && pextp->ad_war)
		iowrite32(0x0007B400, pextp->base + 0xA008);

	if (mtk_interface_mode_is_xgmii(interface))
		iowrite32(0x00040000, pextp->base + 0xA060);
	else
		iowrite32(0x00050000, pextp->base + 0xA060);

	if (is_10g)
		iowrite32(0x00000001, pextp->base + 0x90D0);
	else if (is_5g)
		iowrite32(0x00000003, pextp->base + 0x90D0);
	else if (is_2p5g)
		iowrite32(0x00000005, pextp->base + 0x90D0);
	else
		iowrite32(0x00000007, pextp->base + 0x90D0);

	/* Release reset */
	iowrite32(0x0200E800, pextp->base + 0x0070);
	usleep_range(150, 500);

	/* Switch to P0 */
	iowrite32(0x0200C111, pextp->base + 0x0070);
	ndelay(1020);
	iowrite32(0x0200C101, pextp->base + 0x0070);
	usleep_range(15, 50);

	if (mtk_interface_mode_is_xgmii(interface)) {
		/* Switch to Gen3 */
		iowrite32(0x0202C111, pextp->base + 0x0070);
	} else {
		/* Switch to Gen2 */
		iowrite32(0x0201C111, pextp->base + 0x0070);
	}
	ndelay(1020);
	if (mtk_interface_mode_is_xgmii(interface))
		iowrite32(0x0202C101, pextp->base + 0x0070);
	else
		iowrite32(0x0201C101, pextp->base + 0x0070);
	usleep_range(100, 500);
	iowrite32(0x00000030, pextp->base + 0x30B0);
	if (mtk_interface_mode_is_xgmii(interface))
		iowrite32(0x80201F00, pextp->base + 0x00F4);
	else
		iowrite32(0x80201F01, pextp->base + 0x00F4);

	iowrite32(0x30000000, pextp->base + 0x3040);
	usleep_range(400, 1000);
}

static int mtk_pextp_set_mode(struct phy *phy, enum phy_mode mode, int submode)
{
	struct mtk_pextp_phy *priv = phy_get_drvdata(phy);

	if (mode != PHY_MODE_ETHERNET)
		return -EINVAL;

	switch (submode) {
	case PHY_INTERFACE_MODE_1000BASEX:
	case PHY_INTERFACE_MODE_2500BASEX:
	case PHY_INTERFACE_MODE_SGMII:
	case PHY_INTERFACE_MODE_5GBASER:
	case PHY_INTERFACE_MODE_10GBASER:
	case PHY_INTERFACE_MODE_USXGMII:
		mtk_pextp_setup(priv, submode);
		return 0;
	default:
		return -EINVAL;
	}
}

static int mtk_pextp_reset(struct phy *phy)
{
	struct mtk_pextp_phy *priv = phy_get_drvdata(phy);

	reset_control_assert(priv->reset);
	usleep_range(100, 500);
	reset_control_deassert(priv->reset);
	mdelay(10);

	return 0;
}

static int mtk_pextp_power_on(struct phy *phy)
{
	struct mtk_pextp_phy *priv = phy_get_drvdata(phy);

	return clk_prepare_enable(priv->clk);
}

static int mtk_pextp_power_off(struct phy *phy)
{
	struct mtk_pextp_phy *priv = phy_get_drvdata(phy);

	clk_disable_unprepare(priv->clk);

	return 0;
}

static const struct phy_ops mtk_pextp_ops = {
	.power_on	= mtk_pextp_power_on,
	.power_off	= mtk_pextp_power_off,
	.set_mode	= mtk_pextp_set_mode,
	.reset		= mtk_pextp_reset,
	.owner		= THIS_MODULE,
};

static int mtk_pextp_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct phy_provider *phy_provider;
	struct mtk_pextp_phy *priv;
	struct phy *phy;

	if (!np)
		return -ENODEV;

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->base = devm_of_iomap(&pdev->dev, np, 0, NULL);
	if (!priv->base)
		return -EIO;

	priv->dev = &pdev->dev;
	priv->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(priv->clk))
		return PTR_ERR(priv->clk);

	priv->reset = devm_reset_control_get_exclusive(&pdev->dev, NULL);
	if (IS_ERR(priv->reset))
		return PTR_ERR(priv->reset);

	priv->ad_war = of_property_read_bool(np, "mediatek,usxgmii-performance-errata");

	phy = devm_phy_create(&pdev->dev, NULL, &mtk_pextp_ops);
	if (IS_ERR(phy))
		return PTR_ERR(phy);

	phy_set_drvdata(phy, priv);

	phy_provider = devm_of_phy_provider_register(&pdev->dev, of_phy_simple_xlate);

	return PTR_ERR_OR_ZERO(phy_provider);
}

static const struct of_device_id mtk_pextp_match[] = {
	{ .compatible = "mediatek,mt7988-xfi-pextp", },
	{ }
};
MODULE_DEVICE_TABLE(of, mtk_pextp_match);

static struct platform_driver mtk_pextp_driver = {
	.probe = mtk_pextp_probe,
	.driver = {
		.name = "mtk-pextp",
		.of_match_table = mtk_pextp_match,
	},
};
module_platform_driver(mtk_pextp_driver);

MODULE_DESCRIPTION("MediaTek pextp SerDes PHY driver");
MODULE_AUTHOR("Daniel Golle <daniel@makrotopia.org>");
MODULE_LICENSE("GPL v2");
