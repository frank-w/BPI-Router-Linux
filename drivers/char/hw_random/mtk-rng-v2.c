// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Driver for Mediatek Hardware Random Number Generator (v2/SMCC)
 *
 * Copyright (C) 2023 Daniel Golle <daniel@makrotopia.org>
 * based on patch from Mingming Su <Mingming.Su@mediatek.com>
 */
#define MTK_RNG_DEV KBUILD_MODNAME

#include <linux/arm-smccc.h>
#include <linux/err.h>
#include <linux/hw_random.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/soc/mediatek/mtk_sip_svc.h>

#define MTK_SIP_KERNEL_GET_RND		MTK_SIP_SMC_CMD(0x550)

static int mtk_rng_v2_read(struct hwrng *rng, void *buf, size_t max, bool wait)
{
	struct arm_smccc_res res;
	int retval = 0;

	while (max >= sizeof(u32)) {
		arm_smccc_smc(MTK_SIP_KERNEL_GET_RND, 0, 0, 0, 0, 0, 0, 0,
			      &res);
		if (res.a0)
			break;

		*(u32 *)buf = res.a1;
		retval += sizeof(u32);
		buf += sizeof(u32);
		max -= sizeof(u32);
	}

	return retval || !wait ? retval : -EIO;
}

static int mtk_rng_v2_probe(struct platform_device *pdev)
{
	struct hwrng *trng;

	trng = devm_kzalloc(&pdev->dev, sizeof(*trng), GFP_KERNEL);
	if (!trng)
		return -ENOMEM;

	trng->name = pdev->name;
	trng->read = mtk_rng_v2_read;
	trng->quality = 900;

	return devm_hwrng_register(&pdev->dev, trng);
}

static const struct of_device_id mtk_rng_v2_match[] = {
	{ .compatible = "mediatek,mt7981-rng" },
	{},
};
MODULE_DEVICE_TABLE(of, mtk_rng_v2_match);

static struct platform_driver mtk_rng_v2_driver = {
	.probe          = mtk_rng_v2_probe,
	.driver = {
		.name = KBUILD_MODNAME,
		.of_match_table = mtk_rng_v2_match,
	},
};
module_platform_driver(mtk_rng_v2_driver);

MODULE_DESCRIPTION("Mediatek Random Number Generator Driver (v2/SMC)");
MODULE_AUTHOR("Daniel Golle <daniel@makrotopia.org>");
MODULE_LICENSE("GPL");
