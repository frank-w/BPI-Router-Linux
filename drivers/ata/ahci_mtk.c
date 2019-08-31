/*
 * Mediatek AHCI SATA driver.
 *
 * Author: Long Cheng <long.cheng@mediatek.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pm.h>
#include <linux/device.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/libata.h>
#include <linux/ahci_platform.h>
#include <linux/pci_ids.h>
#include <linux/of_address.h>
#include "ahci.h"

#define DRV_NAME "ahci-mtk"
#define SATA_COMPATIBLE "mediatek,mt7622-sata"

#define MTK_DIAGNR 0xB4
#define MTK_PxTFD 0x120
#define MTK_PxSIG 0x124
#define MTK_PxSSTS 0x128

#define	BIT_IE_PSE (1 << 1)
#define	BIT_IE_DHRE (1 << 0)

#define SET_REG_BIT(addr, bit, mask, val) \
	(*((volatile u32 *)(addr)) = \
	(*((volatile u32 *)(addr)) & ~((mask) << (bit))) | ((val) << (bit)))

/* others */
#define GCSR_PHYDONE_DC 0xDC
#define PHYDONE_PHYDONE_MASK 0x00000001

/* sata phy */
#define REG_SATAPHY_OFFSET_14	0x14
#define REG_SATAPHY_OFFSET_1C	0x1c
#define REG_SATAPHY_OFFSET_24	0x24
#define REG_SATAPHY_OFFSET_40	0x40
#define REG_SATAPHY_OFFSET_4C	0x4c
#define REG_SATAPHY_OFFSET_54	0x54
#define REG_SATAPHY_OFFSET_58	0x58
#define REG_SATAPHY_OFFSET_60	0x60
#define REG_SATAPHY_OFFSET_6C	0x6c
#define REG_SATAPHY_OFFSET_78	0x78
#define REG_SATAPHY_OFFSET_A0	0xa0
#define REG_SATAPHY_OFFSET_D8	0xd8
#define REG_SATAPHY_OFFSET_DC	0xdc

/* sata switch function */
#define REG_SATASOC_OFFSET_14	0x14
#define REG_SATASOC_OFFSET_34	0x34

unsigned int sata_efuse_val;

/* for PxFB */
static void *fis_mem;
static dma_addr_t mem_dma;
static size_t dma_sz = 256;

static struct MTK_SATA_ADDR {
	void __iomem *mac;
	void __iomem *pbase;
	void __iomem *switchsata; /* 7622, need setting sata function in SOC chip */
} mtk_sata_addr;

static const struct ata_port_info ahci_port_info = {
	.flags		= AHCI_FLAG_COMMON,
	.pio_mask	= ATA_PIO4,
	.udma_mask	= ATA_UDMA6,
	.port_ops	= &ahci_platform_ops,
};

static struct scsi_host_template ahci_platform_sht = {
	AHCI_SHT(DRV_NAME),
};

static int host_init(struct device *dev, void __iomem *mmio)
{
	int count = 2000;

	if (!mmio) {
		dev_err(dev, "[mtahci]:%s fail @line%d\n", __func__, __LINE__);
		return -1;
	}

	dev_info(dev, "[mtahci] init: waiting for PHY done...\n");
	while ((!(PHYDONE_PHYDONE_MASK & readl(mmio + GCSR_PHYDONE_DC))) && (count-- != 0))
		udelay(1000);

	if (count > 0) {
		dev_info(dev, "[mtahci] init: PHY is done[0x%x]!\n", readl(mmio + GCSR_PHYDONE_DC));
	} else {
		dev_err(dev, "[mtahci] init error: waiting for PHY done timeout!\n");
		return -1;
	}

	dev_info(dev, "[mtahci] init: deassert top soft reset!\n");

	return 0;
}

static int hba_init(struct device *dev, void __iomem *mmio)
{
	unsigned int regvalue = 0;
	int count = 500;

	if (!mmio) {
		dev_err(dev, "[mtahci]:%s fail @line%d\n", __func__, __LINE__);
		return -1;
	}

	/* Set Host Hwinit registers */
	if (host_init(dev, mmio) < 0) {
		dev_err(dev, "[mtahci] init: host init failed!\n");
		return -1;
	}

	udelay(1000);

	/* CAP */
	regvalue = readl(mmio+HOST_CAP);
	regvalue &= (~(HOST_CAP_SSS | HOST_CAP_MPS | HOST_CAP_SXS));
	writel(regvalue, mmio+HOST_CAP);

	/* PI */
	regvalue = 0x00000001;
	writel(regvalue, mmio+HOST_PORTS_IMPL);

	dev_info(dev, "[mtahci]:before GHC\n");
	dev_info(dev, "[mtahci]:*0x%08lx = 0x%08x\n", (unsigned long)(mmio + 0xb4), readl(mmio + MTK_DIAGNR));
	dev_info(dev, "[mtahci]:*0x%08lx = 0x%08x\n", (unsigned long)(mmio + 0xb4), readl(mmio + MTK_PxTFD));
	dev_info(dev, "[mtahci]:*0x%08lx = 0x%08x\n", (unsigned long)(mmio + 0xb4), readl(mmio + MTK_PxSIG));
	dev_info(dev, "[mtahci]:*0x%08lx = 0x%08x\n", (unsigned long)(mmio + 0xb4), readl(mmio + MTK_PxSSTS));

	/* TODO: TESTR need Do?  */

	/* TODO: PxVS1 need Do?  */

	/* GHC  */
	regvalue = 0x80000001;
	writel(regvalue, mmio+HOST_CTL);

	while ((readl(mmio + HOST_CTL) == HOST_RESET) && (count > 0)) {
		count--;
		udelay(1000);
	}

	return 0;
}

static int port_init(struct device *dev, void __iomem *mmio, unsigned int port_no)
{
	unsigned int count = 2000;
	unsigned int regvalue = 0;

	/* PxSCTL */
	regvalue = readl(mmio + 0x100 + (port_no * 0x80) + PORT_SCR_CTL);
	regvalue &= ~0x0F;
	writel(regvalue, (mmio + 0x100 + (port_no * 0x80) + PORT_SCR_CTL));

	fis_mem = dmam_alloc_coherent(dev, dma_sz, &mem_dma, GFP_KERNEL);
	if (!fis_mem)
		return -ENOMEM;
	memset(fis_mem, 0, dma_sz);

	/* PxFB  */
	writel(mem_dma, (mmio + 0x100 + (port_no * 0x80) + PORT_FIS_ADDR));

	/* PxCMD  */
	regvalue = readl(mmio + 0x100 + (port_no * 0x80) + PORT_CMD);
	regvalue |= PORT_CMD_FIS_RX;
	writel(regvalue, (mmio + 0x100 + (port_no * 0x80) + PORT_CMD));

	dev_info(dev, "[mtahci]:after GHC\n");
	dev_info(dev, "[mtahci]:*0x%08lx = 0x%08x\n", (unsigned long)(mmio + 0xb4), readl(mmio+0xb4));
	dev_info(dev, "[mtahci]:*0x%08lx = 0x%08x\n", (unsigned long)(mmio + 0x120), readl(mmio+0x120));
	dev_info(dev, "[mtahci]:*0x%08lx = 0x%08x\n", (unsigned long)(mmio + 0x124), readl(mmio+0x124));
	dev_info(dev, "[mtahci]:*0x%08lx = 0x%08x\n", (unsigned long)(mmio + 0x128), readl(mmio+0x128));

	dev_info(dev, "[mtahci] init: waiting for ssts!\n");
	do {
		regvalue = readl(mmio + 0x100 + (port_no * 0x80) + PORT_SCR_STAT);
		udelay(1000);
	} while (((regvalue & 0xF) != 3) && (count-- != 0));

	if (count > 0) {
		dev_info(dev, "[mtahci] init: link ready!\n");
	} else {
		dev_err(dev, "[mtahci] init error: link timeout!\n");
		return -1;
	}

	/* clear serr  */
	writel(0xFFFFFFFF, (mmio + 0x100 + (port_no * 0x80) + PORT_SCR_ERR));
	/* clear port intr  */
	writel(0xFFFFFFFF, (mmio + 0x100 + (port_no * 0x80) + PORT_IRQ_STAT));

	regvalue = (BIT_IE_PSE | BIT_IE_DHRE);
	writel(regvalue, (mmio + 0x100 + (port_no * 0x80) + PORT_IRQ_MASK));

	/* clear global intr  */
	writel(0xFFFFFFFF, (mmio + 0x100 + 0x08));

	regvalue = readl(mmio + 0x100 + (port_no * 0x80) + PORT_CMD);
	regvalue |= PORT_CMD_START;
	writel(regvalue, (mmio + 0x100 + (port_no * 0x80) + PORT_CMD));

	return 0;
}

static void sata_switch_function_in_soc_chip(void __iomem *switchsata)
{
	if (!switchsata)
		return;

	/* 14[31]=1'b1 */
	SET_REG_BIT(switchsata + REG_SATASOC_OFFSET_14, 31, 0x01, 0x01);
	udelay(2000);

	/* 34[15]=1'b1 */
	SET_REG_BIT(switchsata + REG_SATASOC_OFFSET_34, 15, 0x01, 0x01);
	udelay(2000);

	/* 34[13:11]=3'b110 */
	SET_REG_BIT(switchsata + REG_SATASOC_OFFSET_34, 11, 0x07, 0x06);
	udelay(2000);
	/* 34[13:11]=3'b100 */
	SET_REG_BIT(switchsata + REG_SATASOC_OFFSET_34, 11, 0x07, 0x04);
	udelay(2000);
	/* 34[13:11]=3'b000 */
	SET_REG_BIT(switchsata + REG_SATASOC_OFFSET_34, 11, 0x07, 0x00);
	udelay(2000);

	/* 34[15]=1'b0 */
	SET_REG_BIT(switchsata + REG_SATASOC_OFFSET_34, 15, 0x01, 0x00);
}

static void sata_phy_cdr_charge_adjt(void __iomem *pbase)
{
	return;
	if (!pbase)
		return;

	/* 60[4:0]=5'b00110 */
	SET_REG_BIT(pbase + REG_SATAPHY_OFFSET_60, 0, 0x1f, 0x06);
	/* 63[4:0]=5'b11010 */
	SET_REG_BIT(pbase + REG_SATAPHY_OFFSET_60, 24, 0x1f, 0x1a);
	/* da[4:0]=5'b11000 */
	SET_REG_BIT(pbase + REG_SATAPHY_OFFSET_D8, 16, 0x1f, 0x18);
	/* dd[4:0]=5'b00110	RG_SSUSB_CDR_BIRLTD1_GEN1 */
	SET_REG_BIT(pbase + REG_SATAPHY_OFFSET_DC, 8, 0x1f, 0x06);
	/* 5a[7:4]=4'b1100	RG_SSUSB_CDR_BICLTD1_GEN1 */
	SET_REG_BIT(pbase + REG_SATAPHY_OFFSET_58, 20, 0x0f, 0x0c);
	/* 59[2:0]=3'b111 */
	SET_REG_BIT(pbase + REG_SATAPHY_OFFSET_58, 8, 0x07, 0x07);
	/* 1d[3:0]=4'b1000 */
	SET_REG_BIT(pbase + REG_SATAPHY_OFFSET_1C, 8, 0x0f, 0x08);
	/* 1e[7:4]=4'b0010 */
	SET_REG_BIT(pbase + REG_SATAPHY_OFFSET_1C, 20, 0x0f, 0x02);

	/* 24[5:4]=4'b10	RG_LOCK_COUNT_SEL */
	SET_REG_BIT(pbase + REG_SATAPHY_OFFSET_24, 4, 0x03, 0x02);
	/* 40[5:0]=5'b10010	RG_T2_MIN */
	SET_REG_BIT(pbase + REG_SATAPHY_OFFSET_40, 0, 0x1f, 0x12);
	/* 40[7:5]=3'b100	RG_TG_MIN */
	SET_REG_BIT(pbase + REG_SATAPHY_OFFSET_40, 5, 0x07, 0x04);
	/* 40[15:8]=6'b110001	RG_T2_MAX */
	SET_REG_BIT(pbase + REG_SATAPHY_OFFSET_40, 8, 0x3f, 0x31);
	/* 40[20:16]=5'b01110	RG_TG_MAX */
	SET_REG_BIT(pbase + REG_SATAPHY_OFFSET_40, 16, 0x1f, 0x0e);

	/* 4c[13:8]=6'b100000	RG_SSUSB_IDRV_0DB_GEN1   IDRV=0x20   TX output swing setting */
	SET_REG_BIT(pbase + REG_SATAPHY_OFFSET_4C, 8, 0x3f, 0x20);
	/* 6c[11:8]=5'b0011	RG_SSUSB_EQ_DLEQ_LFI_GEN1  LEQ=3 */
	SET_REG_BIT(pbase + REG_SATAPHY_OFFSET_6C, 8, 0x0f, 0x03);
}

static int sata_phy_txrx_init_efuse(void __iomem *pbase, unsigned int *sata_efuse)
{
	unsigned int sataphy_iext_intr_ctrl = 0;
	unsigned int sataphy_tx_imp_sel = 0;
	unsigned int sataphy_rx_imp_sel = 0;

	return 0;

	if (!pbase)
		return -1;

	if (sata_efuse == NULL)
		return -1;

	/* printk("###mt_ahci: sata efuse: 0x%08x\n", *sata_efuse); */
	sataphy_iext_intr_ctrl = *sata_efuse & 0x3f;
	sataphy_tx_imp_sel = (*sata_efuse & 0x1f00) >> 8;
	sataphy_rx_imp_sel = (*sata_efuse & 0x1f0000) >> 16;

	if ((sataphy_iext_intr_ctrl == 0) || (sataphy_iext_intr_ctrl == 0x3f)) {
		sataphy_iext_intr_ctrl = 0x28;
		/* printk("###mt_ahci: SATAPHY iext_intr_ctrl use default value!!!\n"); */
	}
	if ((sataphy_tx_imp_sel == 0) || (sataphy_tx_imp_sel == 0x1f)) {
		sataphy_tx_imp_sel = 0x0d;
		/* printk("###mt_ahci: SATAPHY tx_imp_sel use default value!!!\n"); */
	}
	if ((sataphy_rx_imp_sel == 0) || (sataphy_rx_imp_sel == 0x1f)) {
		sataphy_rx_imp_sel = 0x12;
		/* printk("###mt_ahci: SATAPHY rx_imp_sel use default value!!!\n"); */
	}

	SET_REG_BIT(pbase + REG_SATAPHY_OFFSET_A0, 26, 0x01, 1);
	SET_REG_BIT(pbase + REG_SATAPHY_OFFSET_A0, 10, 0x3F, sataphy_iext_intr_ctrl);

	SET_REG_BIT(pbase + REG_SATAPHY_OFFSET_14, 20, 0x01, 1);
	SET_REG_BIT(pbase + REG_SATAPHY_OFFSET_54, 7, 0x1F, sataphy_tx_imp_sel);

	SET_REG_BIT(pbase + REG_SATAPHY_OFFSET_1C, 2, 0x01, 1);
	SET_REG_BIT(pbase + REG_SATAPHY_OFFSET_78, 4, 0x1F, sataphy_rx_imp_sel);

	/* 4c[13:8]=6'b100000	RG_SSUSB_IDRV_0DB_GEN1 / IDRV=0x20 / TX output swing setting */
	SET_REG_BIT(pbase + REG_SATAPHY_OFFSET_4C, 8, 0x3f, 0x20);
	/* 6c[11:8]=5'b0011	RG_SSUSB_EQ_DLEQ_LFI_GEN1 / LEQ=3 */
	SET_REG_BIT(pbase + REG_SATAPHY_OFFSET_6C, 8, 0x0f, 0x03);

	/* printk("###mt_ahci: SATAPHY iext_intr_ctrl & tx/rx_imp_sel init OK!\n"); */
	return 0;
}

static int mtk_ahci_init(struct device *dev, struct ahci_host_priv *hpriv)
{
	struct device_node *node;
	unsigned int port_no = 0;
	int count = 500;
	int ret = 0;

	node = of_find_compatible_node(NULL, NULL, SATA_COMPATIBLE);
	if (node == NULL)
		dev_err(dev, "[mtahci] init: get node failed!\n");

	mtk_sata_addr.mac = hpriv->mmio;
	dev_info(dev, "[mtahci]:mac addr 0x%08lx\n", (unsigned long)mtk_sata_addr.mac);
	mtk_sata_addr.pbase = of_iomap(node, 1);
	dev_info(dev, "[mtahci]:phy addr 0x%08lx\n", (unsigned long)mtk_sata_addr.pbase);
	mtk_sata_addr.switchsata = of_iomap(node, 2);
	dev_info(dev, "[mtahci]:switchsata addr 0x%08lx\n", (unsigned long)mtk_sata_addr.switchsata);

#if 0
	/* just modify pll for debug. maybe remove it, later. */
	{
		void __iomem *pllpbase;

		pllpbase = of_iomap(node, 3);
		dev_info(dev, "[mtahci]:pllpbase addr 0x%08lx\n", (unsigned long)pllpbase);

		SET_REG_BIT(pllpbase + 0x224, 0, 0xffffffff, 0x80180000);
		udelay(2000);
	}
#endif

	/* phy init partiton start */
	/* switch sata function in soc chip */
	sata_switch_function_in_soc_chip(mtk_sata_addr.switchsata);

	sata_phy_cdr_charge_adjt(mtk_sata_addr.pbase);
	if (sata_phy_txrx_init_efuse(mtk_sata_addr.pbase, (unsigned int *)&sata_efuse_val)  < 0) {
		dev_err(dev, "[mtahci] init: get TX/RX parameter failed!\n");
		return -1;
	}
	/* phy init partiton end */

	/*init hba*/
	if (hba_init(dev, hpriv->mmio) < 0) {
		dev_err(dev, "[mtahci] init: hba init failed!\n");
		return -1;
	}

	/* Set Port 0 Hwinit registers */
	/*init port*/
	port_no = 0;
	if (port_init(dev, hpriv->mmio, port_no) < 0) {
		dev_err(dev, "[mtahci] init: port init failed!\n");
		return -1;
	}

	/* check sata mac status machine 500ms */
	while ((readl(hpriv->mmio + 0xb4) != 0x0100000A) && (count > 0)) {
		count--;
		udelay(1000);
	}

	if (count > 0) {
		ret = 1;
		dev_info(dev, "[ahci] receive good status OK! wait %dms\n", (500 - count));
	} else {
		dev_err(dev, "[ahci] receive good status timeout!\n");
		ret = -1;
	}

	dmam_free_coherent(dev, dma_sz, fis_mem, mem_dma);

	return 0;
}

static int ahci_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct ahci_host_priv *hpriv;
	int rc;

	hpriv = ahci_platform_get_resources(pdev);
	if (IS_ERR(hpriv)) {
		dev_err(dev, "[mtahci]%s get resource error @line %d\n", __func__, __LINE__);
		return PTR_ERR(hpriv);
	}

	rc = ahci_platform_enable_resources(hpriv);
	if (rc) {
		dev_err(dev, "[mtahci]%s enable resource error @line %d\n", __func__, __LINE__);
		return rc;
	}

	of_property_read_u32(dev->of_node,
			     "ports-implemented", &hpriv->force_port_map);

	if (of_device_is_compatible(dev->of_node, "hisilicon,hisi-ahci"))
		hpriv->flags |= AHCI_HFLAG_NO_FBS | AHCI_HFLAG_NO_NCQ;

	/* MTK SATA init */
	rc = mtk_ahci_init(dev, hpriv);
	if (rc) {
		dev_err(dev, "[mtahci]%s ahci init error @line %d\n", __func__, __LINE__);
		goto disable_resources;
	}

	rc = ahci_platform_init_host(pdev, hpriv, &ahci_port_info,
				     &ahci_platform_sht);
	if (rc) {
		dev_err(dev, "[mtahci]%s init host error @line %d\n", __func__, __LINE__);
		goto disable_resources;
	}

	return 0;
disable_resources:
	ahci_platform_disable_resources(hpriv);
	return rc;
}

static SIMPLE_DEV_PM_OPS(ahci_pm_ops, ahci_platform_suspend,
			 ahci_platform_resume);

static const struct of_device_id ahci_of_match[] = {
	{ .compatible = SATA_COMPATIBLE, },
	{},
};
MODULE_DEVICE_TABLE(of, ahci_of_match);

static struct platform_driver ahci_driver = {
	.probe = ahci_probe,
	.remove = ata_platform_remove_one,
	.driver = {
		.name = DRV_NAME,
		.of_match_table = ahci_of_match,
		.pm = &ahci_pm_ops,
	},
};
module_platform_driver(ahci_driver);

MODULE_DESCRIPTION("MediaTek MTK AHCI SATA Driver");
MODULE_AUTHOR("Long Cheng (Long) <long.cheng@mediatek.com>");
MODULE_LICENSE("GPL v2");

