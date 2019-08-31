/*
 * Driver for Mediatek MT7623 IR Receiver Controller
 *
 * Copyright (C) 2017 Sean Wang <sean.wang@mediatek.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#include <linux/clk.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/reset.h>
#include <media/rc-core.h>

#define MTK_IR_DEV "mtk-ir"

/* Register to enable PWM and IR */
#define MTK_CONFIG_HIGH_REG       0x0c
/* Enable IR pulse width detection */
#define MTK_PWM_EN		  BIT(13)
/* Enable IR hardware function */
#define MTK_IR_EN		  BIT(0)

/* Register to setting sample period */
#define MTK_CONFIG_LOW_REG        0x10
/* Field to set sample period */
#define CHK_PERIOD		  0xC00
#define MTK_CHK_PERIOD            (((CHK_PERIOD) << 8) & (GENMASK(20, 8)))
#define MTK_CHK_PERIOD_MASK	  (GENMASK(20, 8))

/* Register to clear state of state machine */
#define MTK_IRCLR_REG             0x20
/* Bit to restart IR receiving */
#define MTK_IRCLR		  BIT(0)

/* Register containing pulse width data */
#define MTK_CHKDATA_REG(i)        (0x88 + 4 * i)

/* Register to enable IR interrupt */
#define MTK_IRINT_EN_REG          0xcc
/* Bit to enable interrupt */
#define MTK_IRINT_EN		  BIT(0)

/* Register to ack IR interrupt */
#define MTK_IRINT_CLR_REG         0xd0
/* Bit to clear interrupt status */
#define MTK_IRINT_CLR		  BIT(0)

/* Number of registers to record the pulse width */
#define MTK_CHKDATA_SZ		  17
/* Required frequency */
#define MTK_IR_BASE_CLK		  273000000
/* Frequency after IR internal divider */
#define MTK_IR_CLK		  (MTK_IR_BASE_CLK / 4)
/* Sample period in ns */
#define MTK_IR_SAMPLE		  (((1000000000ul / MTK_IR_CLK) * CHK_PERIOD))
/* Indicate the end of IR data*/
#define MTK_IR_END(v)		  (v == 0xff)

/* struct mtk_ir -	This is the main datasructure for holding the state
 *			of the driver
 * @dev:		The device pointer
 * @ir_lock:		Make sure that synchronization between remove and ISR
 * @rc:			The rc instrance
 * @base:		The mapped register i/o base
 * @irq:		The IRQ that we are using
 * @clk:		The clock that we are using
 * @map_name:		The name for keymap lookup
 */
struct mtk_ir {
	struct device   *dev;
	/*Protect concurrency between driver removal and ISR*/
	spinlock_t      ir_lock;
	struct rc_dev   *rc;
	void __iomem    *base;
	int             irq;
	struct clk      *clk;
	const char      *map_name;
};

static void mtk_w32_mask(struct mtk_ir *ir, u32 val, u32 mask, unsigned int reg)
{
	u32 tmp;

	tmp = __raw_readl(ir->base + reg);
	tmp = (tmp & ~mask) | val;
	__raw_writel(tmp, ir->base + reg);
}

static void mtk_w32(struct mtk_ir *ir, u32 val, unsigned int reg)
{
	__raw_writel(val, ir->base + reg);
}

static u32 mtk_r32(struct mtk_ir *ir, unsigned int reg)
{
	return __raw_readl(ir->base + reg);
}

static inline void mtk_irq_disable(struct mtk_ir *ir, u32 mask)
{
	u32 val;

	val = mtk_r32(ir, MTK_IRINT_EN_REG);
	mtk_w32(ir, val & ~mask, MTK_IRINT_EN_REG);
}

static inline void mtk_irq_enable(struct mtk_ir *ir, u32 mask)
{
	u32 val;

	val = mtk_r32(ir, MTK_IRINT_EN_REG);
	mtk_w32(ir, val | mask, MTK_IRINT_EN_REG);
}

static irqreturn_t mtk_ir_irq(int irqno, void *dev_id)
{
	struct mtk_ir *ir = dev_id;
	u8  wid = 0;
	u32 i, j, val;
	
	DEFINE_IR_RAW_EVENT(rawir);

	spin_lock(&ir->ir_lock);

	mtk_irq_disable(ir, MTK_IRINT_EN);

	/* Reset decoder state machine */
	ir_raw_event_reset(ir->rc);

	/* First message must be pulse */
	rawir.pulse = false;

	/* Handle pulse and space until end of message */
	for (i = 0 ; i < MTK_CHKDATA_SZ ; i++) {
		val = mtk_r32(ir, MTK_CHKDATA_REG(i));
		dev_dbg(ir->dev, "@reg%d=0x%08x\n", i, val);

		for (j = 0 ; j < 4 ; j++) {
			wid = (val & (0xff << j * 8)) >> j * 8;
			rawir.pulse = !rawir.pulse;
			rawir.duration = wid * (MTK_IR_SAMPLE + 1);
			ir_raw_event_store_with_filter(ir->rc, &rawir);

			if (MTK_IR_END(wid))
				goto end_msg;
		}
	}
end_msg:
	/* Restart the next receive */
	mtk_w32_mask(ir, 0x1, MTK_IRCLR, MTK_IRCLR_REG);

	ir_raw_event_set_idle(ir->rc, true);
	ir_raw_event_handle(ir->rc);

	/* Clear interrupt status */
	mtk_w32_mask(ir, 0x1, MTK_IRINT_CLR, MTK_IRINT_CLR_REG);

	/* Enable interrupt */
	mtk_irq_enable(ir, MTK_IRINT_EN);

	spin_unlock(&ir->ir_lock);

	return IRQ_HANDLED;
}

static int mtk_ir_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *dn = dev->of_node;
	struct resource *res;
	struct mtk_ir *ir;
	u32 val;
	int ret = 0;

	ir = devm_kzalloc(dev, sizeof(struct mtk_ir), GFP_KERNEL);
	if (!ir)
		return -ENOMEM;

	spin_lock_init(&ir->ir_lock);

	ir->dev = dev;

	if (!of_device_is_compatible(dn, "mediatek,mt7623-ir"))
		return -ENODEV;

	ir->clk = devm_clk_get(dev, "clk");
	if (IS_ERR(ir->clk)) {
		dev_err(dev, "failed to get a ir clock.\n");
		return PTR_ERR(ir->clk);
	}

	if (clk_prepare_enable(ir->clk)) {
		dev_err(dev, "try to enable ir_clk failed\n");
		ret = -EINVAL;
		goto exit_clkdisable_clk;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	ir->base = devm_ioremap_resource(dev, res);
	if (IS_ERR(ir->base)) {
		dev_err(dev, "failed to map registers\n");
		ret = PTR_ERR(ir->base);
		goto exit_clkdisable_clk;
	}

	ir->rc = rc_allocate_device();
	if (!ir->rc) {
		dev_err(dev, "failed to allocate device\n");
		ret = -ENOMEM;
		goto exit_clkdisable_clk;
	}

	ir->rc->priv = ir;
	ir->rc->input_name = MTK_IR_DEV;
	ir->rc->input_phys = "mtk-ir/input0";
	ir->rc->input_id.bustype = BUS_HOST;
	ir->rc->input_id.vendor = 0x0001;
	ir->rc->input_id.product = 0x0001;
	ir->rc->input_id.version = 0x0001;
	ir->map_name = of_get_property(dn, "linux,rc-map-name", NULL);
	ir->rc->map_name = ir->map_name ?: RC_MAP_EMPTY;
	ir->rc->dev.parent = dev;
	ir->rc->driver_type = RC_DRIVER_IR_RAW;
	ir->rc->driver_name = MTK_IR_DEV;
	ir->rc->allowed_protocols = RC_BIT_ALL;
	ir->rc->rx_resolution = MTK_IR_SAMPLE;

	ret = rc_register_device(ir->rc);
	if (ret) {
		dev_err(dev, "failed to register rc device\n");
		goto exit_free_dev;
	}

	platform_set_drvdata(pdev, ir);

	ir->irq = platform_get_irq(pdev, 0);
	if (ir->irq < 0) {
		dev_err(dev, "no irq resource\n");
		ret = ir->irq;
		goto exit_free_dev;
	}

	ret = devm_request_irq(dev, ir->irq, mtk_ir_irq, 0, MTK_IR_DEV, ir);
	if (ret) {
		dev_err(dev, "failed request irq\n");
		goto exit_free_dev;
	}

	mtk_irq_disable(ir, MTK_IRINT_EN);

	val = mtk_r32(ir, MTK_CONFIG_HIGH_REG);
	val |= MTK_PWM_EN | MTK_IR_EN;
	mtk_w32(ir, val, MTK_CONFIG_HIGH_REG);

	/* Setting sample period */
	mtk_w32_mask(ir, MTK_CHK_PERIOD, MTK_CHK_PERIOD_MASK,
		     MTK_CONFIG_LOW_REG);

	mtk_irq_enable(ir, MTK_IRINT_EN);

	dev_info(dev, "initialized MT7623 IR driver\n");
	return 0;

exit_free_dev:
	rc_free_device(ir->rc);
exit_clkdisable_clk:
	clk_disable_unprepare(ir->clk);

	return ret;
}

static int mtk_ir_remove(struct platform_device *pdev)
{
	unsigned long flags;

	struct mtk_ir *ir = platform_get_drvdata(pdev);

	spin_lock_irqsave(&ir->ir_lock, flags);

	mtk_irq_disable(ir, MTK_IRINT_EN);

	clk_disable_unprepare(ir->clk);

	spin_unlock_irqrestore(&ir->ir_lock, flags);

	rc_unregister_device(ir->rc);

	return 0;
}

static const struct of_device_id mtk_ir_match[] = {
	{ .compatible = "mediatek,mt7623-ir" },
	{},
};
MODULE_DEVICE_TABLE(of, mtk_ir_match);

static struct platform_driver mtk_ir_driver = {
	.probe          = mtk_ir_probe,
	.remove         = mtk_ir_remove,
	.driver = {
		.name = MTK_IR_DEV,
		.of_match_table = mtk_ir_match,
	},
};

module_platform_driver(mtk_ir_driver);

MODULE_DESCRIPTION("Mediatek MT7623 IR Receiver Controller Driver");
MODULE_AUTHOR("Sean Wang <sean.wang@mediatek.com>");
MODULE_LICENSE("GPL");
