// SPDX-License-Identifier: ISC
/* Copyright (C) 2019 MediaTek Inc.
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>

#include "mt7622.h"
#include "mac.h"

static void
mt7622_rx_poll_complete(struct mt76_dev *mdev, enum mt76_rxq_id q)
{
	struct mt7622_dev *dev = container_of(mdev, struct mt7622_dev, mt76);

	mt7622_irq_enable(dev, MT_INT_RX_DONE(q));
}

static irqreturn_t mt7622_irq_handler(int irq, void *dev_instance)
{
	struct mt7622_dev *dev = dev_instance;
	u32 intr;

	intr = mt76_rr(dev, MT_INT_SOURCE_CSR(dev));
	mt76_wr(dev, MT_INT_SOURCE_CSR(dev), intr);

	if (!test_bit(MT76_STATE_INITIALIZED, &dev->mt76.state))
		return IRQ_NONE;

	intr &= dev->mt76.mmio.irqmask;

	if (intr & MT_INT_TX_DONE_ALL) {
		mt7622_irq_disable(dev, MT_INT_TX_DONE_ALL);
		napi_schedule(&dev->mt76.tx_napi);
	}

	if (intr & MT_INT_RX_DONE(0)) {
		mt7622_irq_disable(dev, MT_INT_RX_DONE(0));
		napi_schedule(&dev->mt76.napi[0]);
	}

	if (intr & MT_INT_RX_DONE(1)) {
		mt7622_irq_disable(dev, MT_INT_RX_DONE(1));
		napi_schedule(&dev->mt76.napi[1]);
	}

	return IRQ_HANDLED;
}

static int mt76_wmac_probe(struct platform_device *pdev)
{
	static const struct mt76_driver_ops drv_ops = {
		/* txwi_size = txd size + txp size */
		.txwi_size = MT_TXD_SIZE + sizeof(struct mt7622_txp),
		.drv_flags = MT_DRV_TXWI_NO_FREE,
		.tx_prepare_skb = mt7622_tx_prepare_skb,
		.tx_complete_skb = mt7622_tx_complete_skb,
		.rx_skb = mt7622_queue_rx_skb,
		.rx_poll_complete = mt7622_rx_poll_complete,
		.sta_ps = mt7622_sta_ps,
		.sta_add = mt7622_sta_add,
		.sta_assoc = mt7622_sta_assoc,
		.sta_remove = mt7622_sta_remove,
		.update_survey = mt7622_update_channel,
	};
	struct resource *res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	struct mt7622_dev *dev;
	void __iomem *mem_base;
	struct mt76_dev *mdev;
	int irq;
	int ret;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_err(&pdev->dev, "Failed to get device IRQ\n");
		return irq;
	}

	mem_base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(mem_base)) {
		dev_err(&pdev->dev, "Failed to get memory resource\n");
		return PTR_ERR(mem_base);
	}

	mdev = mt76_alloc_device(&pdev->dev, sizeof(*dev), &mt7622_ops,
				 &drv_ops);
	if (!mdev)
		return -ENOMEM;

	dev = container_of(mdev, struct mt7622_dev, mt76);
	mt76_mmio_init(mdev, mem_base);
	dev->regs = mt7622_mmio_regs_base;

	mdev->rev = (mt76_rr(dev, MT_HW_CHIPID(dev)) << 16) |
		(mt76_rr(dev, MT_HW_REV(dev)) & 0xff);
	dev_info(mdev->dev, "ASIC revision: %04x\n", mdev->rev);

	ret = devm_request_irq(mdev->dev, irq, mt7622_irq_handler,
			       IRQF_SHARED, KBUILD_MODNAME, dev);
	if (ret)
		goto error;

	ret = mt7622_register_device(dev);
	if (ret)
		goto error;

	return 0;
error:
	ieee80211_free_hw(mt76_hw(dev));
	return ret;
}

static int mt76_wmac_remove(struct platform_device *pdev)
{
	struct mt76_dev *mdev = platform_get_drvdata(pdev);
	struct mt7622_dev *dev = container_of(mdev, struct mt7622_dev, mt76);

	mt7622_unregister_device(dev);

	return 0;
}

static const struct of_device_id of_wmac_match[] = {
	{ .compatible = "mediatek,mt7622-wmac" },
	{},
};

struct platform_driver mt76_wmac_driver = {
	.probe		= mt76_wmac_probe,
	.remove		= mt76_wmac_remove,
	.driver = {
		.name = "mt7622_wmac",
		.of_match_table = of_wmac_match,
	},
};
module_platform_driver(mt76_wmac_driver);

MODULE_DEVICE_TABLE(of, of_wmac_match);
MODULE_FIRMWARE(MT7622_FIRMWARE_N9);
MODULE_FIRMWARE(MT7622_ROM_PATCH);
MODULE_LICENSE("Dual BSD/GPL");
