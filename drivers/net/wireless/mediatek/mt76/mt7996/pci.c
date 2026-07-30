// SPDX-License-Identifier: BSD-3-Clause-Clear
/*
 * Copyright (C) 2022 MediaTek Inc.
 */

#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>

#include "mt7996.h"
#include "mac.h"
#include "../trace.h"

static LIST_HEAD(hif_list);
static DEFINE_SPINLOCK(hif_lock);
static u32 hif_idx;

static const struct pci_device_id mt7996_pci_device_table[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_MEDIATEK, MT7996_DEVICE_ID) },
	{ PCI_DEVICE(PCI_VENDOR_ID_MEDIATEK, MT7992_DEVICE_ID) },
	{ PCI_DEVICE(PCI_VENDOR_ID_MEDIATEK, MT7990_DEVICE_ID) },
	{ },
};

static const struct pci_device_id mt7996_hif_device_table[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_MEDIATEK, MT7996_DEVICE_ID_2) },
	{ PCI_DEVICE(PCI_VENDOR_ID_MEDIATEK, MT7992_DEVICE_ID_2) },
	{ PCI_DEVICE(PCI_VENDOR_ID_MEDIATEK, MT7990_DEVICE_ID_2) },
	{ },
};

static struct mt7996_hif *mt7996_pci_get_hif2(u32 idx)
{
	struct mt7996_hif *hif;
	u32 val;

	spin_lock_bh(&hif_lock);

	list_for_each_entry(hif, &hif_list, list) {
		val = readl(hif->regs + MT_PCIE_RECOG_ID);
		val &= MT_PCIE_RECOG_ID_MASK;
		if (val != idx)
			continue;

		get_device(hif->dev);
		goto out;
	}
	hif = NULL;

out:
	spin_unlock_bh(&hif_lock);

	return hif;
}

static void mt7996_put_hif2(struct mt7996_hif *hif)
{
	if (!hif)
		return;

	put_device(hif->dev);
}

static struct mt7996_hif *mt7996_pci_init_hif2(struct pci_dev *pdev)
{
	hif_idx++;

	if (!pci_get_device(PCI_VENDOR_ID_MEDIATEK, MT7996_DEVICE_ID_2, NULL) &&
	    !pci_get_device(PCI_VENDOR_ID_MEDIATEK, MT7992_DEVICE_ID_2, NULL) &&
	    !pci_get_device(PCI_VENDOR_ID_MEDIATEK, MT7990_DEVICE_ID_2, NULL))
		return NULL;

	writel(hif_idx | MT_PCIE_RECOG_ID_SEM,
	       pcim_iomap_table(pdev)[0] + MT_PCIE_RECOG_ID);

	return mt7996_pci_get_hif2(hif_idx);
}

static int mt7996_pci_hif2_probe(struct pci_dev *pdev)
{
	struct mt7996_hif *hif;

	hif = devm_kzalloc(&pdev->dev, sizeof(*hif), GFP_KERNEL);
	if (!hif)
		return -ENOMEM;

	hif->dev = &pdev->dev;
	hif->regs = pcim_iomap_table(pdev)[0];
	hif->irq = pdev->irq;
	pcie_bandwidth_available(pdev, NULL, &hif->speed, &hif->width);
	spin_lock_bh(&hif_lock);
	list_add(&hif->list, &hif_list);
	spin_unlock_bh(&hif_lock);
	pci_set_drvdata(pdev, hif);

	return 0;
}

static int mt7996_pci_probe(struct pci_dev *pdev,
			    const struct pci_device_id *id)
{
	struct pci_dev *hif2_dev;
	struct mt7996_hif *hif2;
	struct mt7996_dev *dev;
	int irq, hif2_irq, ret;
	struct mt76_dev *mdev;

	ret = pcim_enable_device(pdev);
	if (ret)
		return ret;

	ret = pcim_iomap_regions(pdev, BIT(0), pci_name(pdev));
	if (ret)
		return ret;

	pci_set_master(pdev);

	ret = dma_set_mask(&pdev->dev, DMA_BIT_MASK(36));
	if (ret)
		return ret;

	ret = dma_set_coherent_mask(&pdev->dev, DMA_BIT_MASK(32));
	if (ret)
		return ret;

	mt76_pci_disable_aspm(pdev);

	if (id->device == MT7996_DEVICE_ID_2 ||
	    id->device == MT7992_DEVICE_ID_2 ||
	    id->device == MT7990_DEVICE_ID_2)
		return mt7996_pci_hif2_probe(pdev);

	dev = mt7996_mmio_probe(&pdev->dev, pcim_iomap_table(pdev)[0],
				id->device);
	if (IS_ERR(dev))
		return PTR_ERR(dev);

	mdev = &dev->mt76;
	mt7996_wfsys_reset(dev);
	hif2 = mt7996_pci_init_hif2(pdev);
	dev->hif2 = hif2;
	if (hif2)
		hif2->mt7996 = dev;

	/* Optional reset line for slot_reset() recovery; absent on stock DT. */
	dev->reset_gpio = devm_gpiod_get_optional(&pdev->dev, "reset",
						  GPIOD_OUT_LOW);
	if (IS_ERR(dev->reset_gpio))
		dev->reset_gpio = NULL;

	mt76_npu_init(mdev, pci_resource_start(pdev, 0),
		      pci_domain_nr(pdev->bus) ? 3 : 2);

	ret = mt7996_mmio_wed_init(dev, pdev, false, &irq);
	if (ret < 0)
		goto free_wed_or_irq_vector;

	if (!ret) {
		ret = pci_alloc_irq_vectors(pdev, 1, 1, PCI_IRQ_ALL_TYPES);
		if (ret < 0)
			goto free_device;

		irq = pdev->irq;
	}

	ret = devm_request_irq(mdev->dev, irq, mt7996_irq_handler,
			       IRQF_SHARED, KBUILD_MODNAME, dev);
	if (ret)
		goto free_wed_or_irq_vector;

	mt76_wr(dev, MT_INT_MASK_CSR, 0);
	/* master switch of PCIe interrupt enable */
	mt76_wr(dev, MT_PCIE_MAC_INT_ENABLE, 0xff);

	if (hif2) {
		hif2_dev = container_of(hif2->dev, struct pci_dev, dev);

		ret = mt7996_mmio_wed_init(dev, hif2_dev, true, &hif2_irq);
		if (ret < 0)
			goto free_hif2_wed_irq_vector;

		if (!ret) {
			ret = pci_alloc_irq_vectors(hif2_dev, 1, 1,
						    PCI_IRQ_ALL_TYPES);
			if (ret < 0)
				goto free_hif2;

			dev->hif2->irq = hif2_dev->irq;
			hif2_irq = dev->hif2->irq;
		}

		ret = devm_request_irq(mdev->dev, hif2_irq, mt7996_irq_handler,
				       IRQF_SHARED, KBUILD_MODNAME "-hif",
				       dev);
		if (ret)
			goto free_hif2_wed_irq_vector;

		mt76_wr(dev, MT_INT1_MASK_CSR, 0);
		/* master switch of PCIe tnterrupt enable */
		mt76_wr(dev, MT_PCIE1_MAC_INT_ENABLE, 0xff);
	}

	ret = mt7996_register_device(dev);
	if (ret)
		goto free_hif2_irq;

	/* Snapshot config space so slot_reset() can restore BARs and the
	 * command register after a PCIe bus reset clobbers them.
	 */
	pci_save_state(pdev);

	return 0;

free_hif2_irq:
	if (dev->hif2)
		devm_free_irq(mdev->dev, hif2_irq, dev);
free_hif2_wed_irq_vector:
	if (dev->hif2) {
		if (mtk_wed_device_active(&dev->mt76.mmio.wed_hif2))
			mtk_wed_device_detach(&dev->mt76.mmio.wed_hif2);
		else
			pci_free_irq_vectors(hif2_dev);
	}
free_hif2:
	if (dev->hif2)
		put_device(dev->hif2->dev);
	devm_free_irq(mdev->dev, irq, dev);
free_wed_or_irq_vector:
	if (mtk_wed_device_active(&dev->mt76.mmio.wed))
		mtk_wed_device_detach(&dev->mt76.mmio.wed);
	else
		pci_free_irq_vectors(pdev);
free_device:
	mt76_free_device(&dev->mt76);

	return ret;
}

static void mt7996_hif_remove(struct pci_dev *pdev)
{
	struct mt7996_hif *hif = pci_get_drvdata(pdev);

	list_del(&hif->list);
}

static void mt7996_pci_remove(struct pci_dev *pdev)
{
	struct mt76_dev *mdev;
	struct mt7996_dev *dev;

	mdev = pci_get_drvdata(pdev);
	dev = container_of(mdev, struct mt7996_dev, mt76);
	mt7996_put_hif2(dev->hif2);
	mt7996_unregister_device(dev);
}

static void mt7996_pci_mark_removed(struct mt76_dev *mdev)
{
	/* Flag the device gone so every liveness check - including
	 * mt7996_dev_gone() guarding the reset worker - short-circuits before
	 * it can touch the now-unreachable MMIO window, and wake anyone blocked
	 * on an MCU response so it fails fast instead of stalling to timeout.
	 */
	set_bit(MT76_REMOVED, &mdev->phy.state);
	wake_up(&mdev->mcu.wait);
}

static pci_ers_result_t
mt7996_pci_error_detected(struct pci_dev *pdev, pci_channel_state_t state)
{
	struct mt76_dev *mdev = pci_get_drvdata(pdev);

	if (state == pci_channel_io_perm_failure)
		return PCI_ERS_RESULT_DISCONNECT;

	dev_err(&pdev->dev, "PCIe error detected (state=%u), requesting slot reset\n",
		state);

	mt7996_pci_mark_removed(mdev);

	/* Ask the PCIe core to perform a secondary bus reset (PERST#) and then
	 * call .slot_reset, where the chip is reinitialised. If recovery there
	 * fails, slot_reset returns DISCONNECT and we end up in the same safe
	 * state as before - WiFi down, the rest of the system alive.
	 */
	return PCI_ERS_RESULT_NEED_RESET;
}

static pci_ers_result_t
mt7996_hif_error_detected(struct pci_dev *pdev, pci_channel_state_t state)
{
	struct mt7996_hif *hif = pci_get_drvdata(pdev);
	struct mt7996_dev *dev = hif ? hif->mt7996 : NULL;

	if (state == pci_channel_io_perm_failure)
		return PCI_ERS_RESULT_DISCONNECT;

	dev_err(&pdev->dev, "PCIe error detected (state=%u) on hif2\n", state);

	/* hif2 is the second PCIe function of the same physical chip; a link
	 * loss here downs the whole device. Mark the owning mt76 device removed
	 * so its reset worker bails out too, then request the shared slot reset.
	 * If the primary function has not claimed this hif yet, there is nothing
	 * to reinitialise from here - just disconnect this function.
	 */
	if (!dev)
		return PCI_ERS_RESULT_DISCONNECT;

	mt7996_pci_mark_removed(&dev->mt76);

	return PCI_ERS_RESULT_NEED_RESET;
}

static pci_ers_result_t mt7996_pci_slot_reset(struct pci_dev *pdev)
{
	struct mt76_dev *mdev = pci_get_drvdata(pdev);
	struct mt7996_dev *dev = container_of(mdev, struct mt7996_dev, mt76);

	dev_info(&pdev->dev, "PCIe slot reset, reinitialising device\n");

	/* The secondary bus reset clobbered config space; restore BARs and the
	 * command register before any MMIO, and re-enable bus mastering.
	 */
	pci_restore_state(pdev);
	pci_set_master(pdev);

	/* Optional hard power-cycle of the card via a board GPIO wired in
	 * parallel with the manual power switch. PERST# from the bus reset above
	 * may not clear an MCU wedged by a supply brownout; cycling the rail
	 * does. No-op when no reset-gpios is described in DT.
	 */
	if (dev->reset_gpio) {
		gpiod_set_value_cansleep(dev->reset_gpio, 1);
		msleep(20);
		gpiod_set_value_cansleep(dev->reset_gpio, 0);
		msleep(50);
	}

	/* Confirm the chip is actually back on the bus before promising the
	 * core a recovery; a dead read returns all-ones.
	 */
	if (mt76_rr(dev, MT_HW_REV) == 0xffffffff) {
		dev_err(&pdev->dev, "device unreachable after slot reset\n");
		return PCI_ERS_RESULT_DISCONNECT;
	}

	/* Config space is valid and the channel is online again, so the
	 * mt7996_dev_gone() guard must stop tripping for the reinit to run.
	 */
	clear_bit(MT76_REMOVED, &mdev->phy.state);

	/* Reload firmware and rebuild DMA/NAPI/token state through the existing
	 * full chip reset path (mt7996_mac_reset_work -> mt7996_mac_full_reset).
	 */
	dev->recovery.restart = true;
	queue_work(mdev->wq, &dev->reset_work);

	return PCI_ERS_RESULT_RECOVERED;
}

static pci_ers_result_t mt7996_hif_slot_reset(struct pci_dev *pdev)
{
	/* hif2 shares the chip with the primary function, which drives the
	 * actual reinit from its own slot_reset. Here we only need this
	 * function's own config space restored so the reinit can reach the
	 * hif2 register window.
	 */
	pci_restore_state(pdev);
	pci_set_master(pdev);

	return PCI_ERS_RESULT_RECOVERED;
}

static void mt7996_pci_io_resume(struct pci_dev *pdev)
{
	/* The chip reinit queued from slot_reset ends in ieee80211_restart_hw(),
	 * which restarts the queues, so nothing extra is required here.
	 */
	dev_info(&pdev->dev, "PCIe recovery complete\n");
}

static const struct pci_error_handlers mt7996_pci_err_handler = {
	.error_detected = mt7996_pci_error_detected,
	.slot_reset	= mt7996_pci_slot_reset,
	.resume		= mt7996_pci_io_resume,
};

static const struct pci_error_handlers mt7996_hif_err_handler = {
	.error_detected = mt7996_hif_error_detected,
	.slot_reset	= mt7996_hif_slot_reset,
	.resume		= mt7996_pci_io_resume,
};

struct pci_driver mt7996_hif_driver = {
	.name		= KBUILD_MODNAME "_hif",
	.id_table	= mt7996_hif_device_table,
	.probe		= mt7996_pci_probe,
	.remove		= mt7996_hif_remove,
	.err_handler	= &mt7996_hif_err_handler,
};

struct pci_driver mt7996_pci_driver = {
	.name		= KBUILD_MODNAME,
	.id_table	= mt7996_pci_device_table,
	.probe		= mt7996_pci_probe,
	.remove		= mt7996_pci_remove,
	.err_handler	= &mt7996_pci_err_handler,
};

MODULE_DEVICE_TABLE(pci, mt7996_pci_device_table);
MODULE_DEVICE_TABLE(pci, mt7996_hif_device_table);
MODULE_FIRMWARE(MT7996_FIRMWARE_WA);
MODULE_FIRMWARE(MT7996_FIRMWARE_WM);
MODULE_FIRMWARE(MT7996_FIRMWARE_DSP);
MODULE_FIRMWARE(MT7996_ROM_PATCH);
MODULE_FIRMWARE(MT7992_FIRMWARE_WA);
MODULE_FIRMWARE(MT7992_FIRMWARE_WM);
MODULE_FIRMWARE(MT7992_FIRMWARE_DSP);
MODULE_FIRMWARE(MT7992_ROM_PATCH);
MODULE_FIRMWARE(MT7990_FIRMWARE_WM);
MODULE_FIRMWARE(MT7990_ROM_PATCH);
