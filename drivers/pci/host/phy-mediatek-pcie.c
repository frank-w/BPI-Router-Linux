/*
 * Copyright (c) 2016 MediaTek Inc.
 * Author: Liang-Yen Wang <liang-yen.wang@mediatek.com>
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

#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/clk.h>
#include <linux/phy/phy.h>
#include <linux/regmap.h>
#include <linux/mfd/syscon.h>

struct mtk_pcie_phy {
	struct device *dev;
	struct phy	*phy;
	void __iomem *phy_base;
};

struct phy *of_pcie_phy_xlate(struct device *dev, struct of_phandle_args
	*args)
{
	struct mtk_pcie_phy *mtk_phy = dev_get_drvdata(dev);

	if (IS_ERR(mtk_phy->phy)) {
		dev_err(dev, "failed to get phy device\n");
		return ERR_PTR(-ENODEV);
	}

	return mtk_phy->phy;
}
static int mtk_pcie_phy_init(struct phy *phy)
{
	struct mtk_pcie_phy *pcie_phy;

	pcie_phy = phy_get_drvdata(phy);

	return 0;
}

static int mtk_pcie_phy_power_on(struct phy *phy)
{
	struct mtk_pcie_phy *pcie_phy;

	pcie_phy = phy_get_drvdata(phy);

	return 0;
}

static int mtk_pcie_phy_power_off(struct phy *phy)
{
	struct mtk_pcie_phy *pcie_phy;

	pcie_phy = phy_get_drvdata(phy);

	return 0;
}

static struct phy_ops mtk_pcie_phy_ops = {
	.init		= mtk_pcie_phy_init,
	.power_on	= mtk_pcie_phy_power_on,
	.power_off	= mtk_pcie_phy_power_off,
	.owner		= THIS_MODULE,
};

static const struct of_device_id mtk_pcie_phy_of_match[];

static int mtk_pcie_phy_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	const struct of_device_id *match;
	struct mtk_pcie_phy *mtk_phy;
	struct phy *phy;
	struct phy_provider *phy_provider;
	struct resource *phy_base;

	mtk_phy = devm_kzalloc(dev, sizeof(*mtk_phy), GFP_KERNEL);
	if (!mtk_phy)
		return -ENOMEM;

	match = of_match_device(mtk_pcie_phy_of_match, &pdev->dev);
	if (!match)
		return -ENODEV;

	mtk_phy->dev = dev;
	phy_base = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	mtk_phy->phy_base = devm_ioremap_resource(&pdev->dev, phy_base);

	if (IS_ERR(mtk_phy->phy_base)) {
		dev_err(dev, "failed to get phy base\n");
		return PTR_ERR(mtk_phy->phy_base);
	}
	platform_set_drvdata(pdev, mtk_phy);
	phy = devm_phy_create(dev, NULL, &mtk_pcie_phy_ops);
	if (IS_ERR(phy)) {
		dev_err(dev, "failed to create phy device\n");
		return PTR_ERR(phy);
	}
	mtk_phy->phy = phy;
	phy_set_drvdata(phy, mtk_phy);
	phy_provider = devm_of_phy_provider_register(dev, of_pcie_phy_xlate);
	if (IS_ERR(phy_provider)) {
		dev_err(dev, "failed to register phy provider\n");
		return PTR_ERR(phy_provider);
	}

	return 0;
}

static const struct of_device_id mtk_pcie_phy_of_match[] = {
	{ .compatible = "mediatek,pcie-phy", },
	{},
};
MODULE_DEVICE_TABLE(of, mtk_pcie_phy_of_match);

static struct platform_driver mtk_pcie_phy_driver = {
	.probe	= mtk_pcie_phy_probe,
	.driver = {
		.name	= "mtk-pcie-phy",
		.of_match_table	= mtk_pcie_phy_of_match,
	}
};

static int __init pcie_phy_init(void)
{
	return platform_driver_probe(&mtk_pcie_phy_driver, mtk_pcie_phy_probe);
}

subsys_initcall(pcie_phy_init);

MODULE_AUTHOR("Liang-Yen Wang <liang-yen.wang@mediatek.com>");
MODULE_DESCRIPTION("Mediatek PCIe phy driver");
MODULE_LICENSE("GPL v2");
